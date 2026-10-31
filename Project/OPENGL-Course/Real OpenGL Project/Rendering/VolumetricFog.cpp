#include "Rendering/VolumetricFog.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <glm/gtc/type_ptr.hpp>

#include "Core/AssetManager.h"
#include "Core/ServiceLocator.h"
#include "GraphicsSettings.h"
#include "Lighting/DirectionalLight.h"
#include "Rendering/ShadowMap.h"

// Texture units used by the fog passes: above everything the renderer binds for lighting,
// layers (up to 33), clouds (40-41) and the water foam map (36-38).
static const int UNIT_HISTORY = 42;
static const int UNIT_SHADOW = 43;
static const int UNIT_NOISE = 44;
static const int UNIT_DEPTH = 45;
static const int UNIT_INTEGRATED = 46;
static const int NOISE_SIZE = 64;

// Radical inverse in base 2 (Halton sequence): well spread jitter, frame after frame
static float Halton2(unsigned int i)
{
	float f = 1.0f, r = 0.0f;
	while (i > 0) { f *= 0.5f; r += f * (float)(i & 1u); i >>= 1u; }
	return r;
}

static GLuint CreateVolume(const glm::ivec3& size)
{
	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_3D, tex);
	glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA16F, size.x, size.y, size.z);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	return tex;
}

void VolumetricFog::LightRadiance(const GraphicsSettings& gs, DirectionalLight& sun, glm::vec3& sunRadiance, glm::vec3& ambientRadiance)
{
	glm::vec3 toSun = -glm::normalize(*sun.GetDirectionPtr());
	glm::vec3 lightColour = *sun.GetColourPtr();
	// Sun below the horizon fades out (the light itself keeps shining on the terrain)
	float sunUp = glm::clamp(toSun.y * 10.0f + 0.5f, 0.0f, 1.0f);
	sunRadiance = lightColour * (*sun.GetDiffuseIntensityPtr()) * gs.fogSunIntensity * sunUp;
	ambientRadiance = lightColour * (*sun.GetAmbientIntensityPtr()) * gs.fogAmbientIntensity;
}

VolumetricFog::~VolumetricFog()
{
	ReleaseGrid();
	if (noiseTexture) glDeleteTextures(1, &noiseTexture);
	if (emptyVAO) glDeleteVertexArrays(1, &emptyVAO);
}

bool VolumetricFog::Init()
{
	initialised = true;
	auto am = ServiceLocator::GetAssetManager();
	injectShader.CreateComputeShader(am->GetShaderPath("fog_inject.glsl").c_str());
	integrateShader.CreateComputeShader(am->GetShaderPath("fog_integrate.glsl").c_str());
	applyShader.CreateFromFiles(am->GetShaderPath("fog_apply.vert").c_str(), am->GetShaderPath("fog_apply.frag").c_str());
	Shader noiseShader;
	noiseShader.CreateComputeShader(am->GetShaderPath("cloud_noise.glsl").c_str());
	if (!injectShader.GetShaderID() || !integrateShader.GetShaderID() || !applyShader.GetShaderID() || !noiseShader.GetShaderID()) {
		printf("[VolumetricFog] Shader creation failed, fog disabled\n");
		failed = true;
		return false;
	}

	// Tileable Perlin-Worley volume for the fog's wisps (same generator as the clouds)
	glGenTextures(1, &noiseTexture);
	glBindTexture(GL_TEXTURE_3D, noiseTexture);
	glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA8, NOISE_SIZE, NOISE_SIZE, NOISE_SIZE);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
	noiseShader.UseShader();
	glUniform1i(glGetUniformLocation(noiseShader.GetShaderID(), "mode"), 0);
	glUniform1i(glGetUniformLocation(noiseShader.GetShaderID(), "size"), NOISE_SIZE);
	glBindImageTexture(0, noiseTexture, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glDispatchCompute((NOISE_SIZE + 3) / 4, (NOISE_SIZE + 3) / 4, (NOISE_SIZE + 3) / 4);
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

	glGenVertexArrays(1, &emptyVAO);
	glBindTexture(GL_TEXTURE_3D, 0);
	glUseProgram(0);
	return true;
}

void VolumetricFog::AllocateGrid(int width, int height, int quality)
{
	ReleaseGrid();
	// Frostbite default: 8x8 pixel tiles, 64 depth slices
	int tile = quality <= 0 ? 16 : 8;
	int slices = quality >= 2 ? 128 : 64;
	gridSize = glm::ivec3(std::max(1, (width + tile - 1) / tile), std::max(1, (height + tile - 1) / tile), slices);
	injectVolume[0] = CreateVolume(gridSize);
	injectVolume[1] = CreateVolume(gridSize);
	integratedVolume = CreateVolume(gridSize);
	gridViewportW = width;
	gridViewportH = height;
	gridQuality = quality;
	historyValid = false;
}

void VolumetricFog::ReleaseGrid()
{
	GLuint textures[] = { injectVolume[0], injectVolume[1], integratedVolume };
	for (GLuint t : textures) if (t) glDeleteTextures(1, &t);
	injectVolume[0] = injectVolume[1] = integratedVolume = 0;
}

void VolumetricFog::Render(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos,
	DirectionalLight& sun, GLuint sceneDepthTexture, int viewportWidth, int viewportHeight,
	const GraphicsSettings& gs, float time)
{
	if (failed || viewportWidth <= 0 || viewportHeight <= 0) return;
	if (!initialised && !Init()) return;
	int quality = std::clamp(gs.fogQuality, 0, 2);
	if (!injectVolume[0] || viewportWidth != gridViewportW || viewportHeight != gridViewportH || quality != gridQuality)
		AllocateGrid(viewportWidth, viewportHeight, quality);

	GLint prevProgram = 0, prevVAO = 0, prevActive = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);

	const glm::mat4 invProjection = glm::inverse(projection);
	const glm::mat4 invView = glm::inverse(view);
	const float range = std::max(gs.fogRange, 10.0f);
	const GLuint groupsX = (gridSize.x + 7) / 8, groupsY = (gridSize.y + 7) / 8;
	int history = 1 - current;

	glm::vec3 toSun = -glm::normalize(*sun.GetDirectionPtr());
	glm::vec3 sunRadiance, ambientRadiance;
	LightRadiance(gs, sun, sunRadiance, ambientRadiance);

	// A changed medium or light makes last frame's volume wrong, not just noisy: drop it so
	// the change shows up this frame instead of fading in
	MediumState medium = {};
	medium.density = std::max(gs.fogDensity, 0.0f);
	medium.baseHeight = gs.fogBaseHeight;
	medium.heightFalloff = std::max(gs.fogHeightFalloff, 0.0f);
	medium.range = range;
	medium.noiseStrength = std::clamp(gs.fogNoiseStrength, 0.0f, 1.0f);
	medium.noiseScale = std::max(gs.fogNoiseScale, 0.0001f);
	medium.anisotropy = std::clamp(gs.fogAnisotropy, -0.95f, 0.95f);
	medium.albedo = gs.fogAlbedo;
	medium.sunDir = toSun;
	medium.sunRadiance = sunRadiance;
	medium.ambientRadiance = ambientRadiance;
	if (std::memcmp(&medium, &lastMedium, sizeof(MediumState)) != 0) historyValid = false;
	lastMedium = medium;

	float dt = lastTime < 0.0f ? 0.0f : std::clamp(time - lastTime, 0.0f, 0.25f);
	lastTime = time;
	windOffset += glm::vec3(1.0f, 0.05f, 0.4f) * gs.fogWindSpeed * dt;

	// --- 1. Inject medium + light ---
	injectShader.UseShader();
	GLuint id = injectShader.GetShaderID();
	auto U = [&](const char* n) { return glGetUniformLocation(id, n); };
	glUniform3iv(U("gridSize"), 1, glm::value_ptr(gridSize));
	glUniform1f(U("fogRange"), range);
	// Jitter only pays off when frames are accumulated; a lone frame samples the slice centres
	glUniform1f(U("depthJitter"), historyValid ? Halton2((frameIndex % 16u) + 1u) : 0.5f);
	glUniformMatrix4fv(U("invProjection"), 1, GL_FALSE, glm::value_ptr(invProjection));
	glUniformMatrix4fv(U("invView"), 1, GL_FALSE, glm::value_ptr(invView));
	glUniformMatrix4fv(U("prevViewProj"), 1, GL_FALSE, glm::value_ptr(prevViewProj));
	glUniformMatrix4fv(U("prevView"), 1, GL_FALSE, glm::value_ptr(prevView));
	glUniform1i(U("historyValid"), historyValid ? 1 : 0);
	glUniform1f(U("temporalBlend"), std::clamp(gs.fogTemporalBlend, 0.05f, 1.0f));
	glUniform3fv(U("cameraPos"), 1, glm::value_ptr(cameraPos));
	glUniform1f(U("density"), medium.density);
	glUniform1f(U("baseHeight"), medium.baseHeight);
	glUniform1f(U("heightFalloff"), medium.heightFalloff);
	glUniform1f(U("noiseStrength"), medium.noiseStrength);
	glUniform1f(U("noiseScale"), medium.noiseScale);
	glUniform3fv(U("windOffset"), 1, glm::value_ptr(windOffset));
	glUniform3fv(U("albedo"), 1, glm::value_ptr(medium.albedo));
	glUniform3fv(U("sunDir"), 1, glm::value_ptr(toSun));
	glUniform3fv(U("sunRadiance"), 1, glm::value_ptr(sunRadiance));
	glUniform3fv(U("ambientRadiance"), 1, glm::value_ptr(ambientRadiance));
	glUniform1f(U("anisotropy"), medium.anisotropy);

	const auto& matrices = sun.GetCascadedLightMatrices();
	const auto& splits = sun.GetCascadeSplitDistances();
	int cascades = (int)std::min<size_t>(std::min(matrices.size(), splits.size()), 4);
	for (int i = 0; i < cascades; i++) {
		std::string m = "cascadeMatrices[" + std::to_string(i) + "]";
		std::string s = "cascadeSplits[" + std::to_string(i) + "]";
		glUniformMatrix4fv(U(m.c_str()), 1, GL_FALSE, glm::value_ptr(matrices[i]));
		glUniform1f(U(s.c_str()), splits[i]);
	}
	glUniform1i(U("cascadeCount"), sun.GetShadowMap() ? cascades : 0);

	glActiveTexture(GL_TEXTURE0 + UNIT_HISTORY);
	glBindTexture(GL_TEXTURE_3D, injectVolume[history]);
	glUniform1i(U("historyVolume"), UNIT_HISTORY);
	if (sun.GetShadowMap()) sun.GetShadowMap()->Read(GL_TEXTURE0 + UNIT_SHADOW);
	glUniform1i(U("directionalShadowMap"), UNIT_SHADOW);
	glActiveTexture(GL_TEXTURE0 + UNIT_NOISE);
	glBindTexture(GL_TEXTURE_3D, noiseTexture);
	glUniform1i(U("noiseVolume"), UNIT_NOISE);
	glBindImageTexture(0, injectVolume[current], 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glDispatchCompute(groupsX, groupsY, gridSize.z);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

	// --- 2. Integrate columns front to back ---
	integrateShader.UseShader();
	id = integrateShader.GetShaderID();
	glUniform3iv(U("gridSize"), 1, glm::value_ptr(gridSize));
	glUniform1f(U("fogRange"), range);
	glUniformMatrix4fv(U("invProjection"), 1, GL_FALSE, glm::value_ptr(invProjection));
	glBindImageTexture(0, injectVolume[current], 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
	glBindImageTexture(1, integratedVolume, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glDispatchCompute(groupsX, groupsY, 1);
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

	// --- 3. Composite: scene * transmittance + in-scattered light (alpha left untouched) ---
	GLboolean blendWas = glIsEnabled(GL_BLEND), depthWas = glIsEnabled(GL_DEPTH_TEST), cullWas = glIsEnabled(GL_CULL_FACE);
	GLboolean depthMaskWas = GL_TRUE;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWas);
	GLint srcRGB = GL_ONE, dstRGB = GL_ZERO, srcA = GL_ONE, dstA = GL_ZERO;
	glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB);
	glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
	glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
	glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);

	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_ONE, GL_SRC_ALPHA, GL_ZERO, GL_ONE);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	applyShader.UseShader();
	id = applyShader.GetShaderID();
	glActiveTexture(GL_TEXTURE0 + UNIT_DEPTH);
	glBindTexture(GL_TEXTURE_2D, sceneDepthTexture);
	glUniform1i(U("sceneDepth"), UNIT_DEPTH);
	glActiveTexture(GL_TEXTURE0 + UNIT_INTEGRATED);
	glBindTexture(GL_TEXTURE_3D, integratedVolume);
	glUniform1i(U("integratedVolume"), UNIT_INTEGRATED);
	glUniformMatrix4fv(U("projection"), 1, GL_FALSE, glm::value_ptr(projection));
	glUniform1f(U("fogRange"), range);
	glUniform1i(U("gridDepth"), gridSize.z);
	glBindVertexArray(emptyVAO);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	// Restore
	glBlendFuncSeparate(srcRGB, dstRGB, srcA, dstA);
	if (!blendWas) glDisable(GL_BLEND);
	if (depthWas) glEnable(GL_DEPTH_TEST);
	if (cullWas) glEnable(GL_CULL_FACE);
	glDepthMask(depthMaskWas);
	glBindVertexArray(prevVAO);
	glActiveTexture(prevActive);
	glUseProgram(prevProgram);

	prevViewProj = projection * view;
	prevView = view;
	historyValid = true;
	current = history;
	frameIndex++;
}
