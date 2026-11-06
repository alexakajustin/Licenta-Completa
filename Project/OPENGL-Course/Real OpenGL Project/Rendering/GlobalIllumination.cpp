#include "Rendering/GlobalIllumination.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "Rendering/InstancedGroup.h"
#include "Rendering/Material.h"
#include "Rendering/Texture.h"
#include "Rendering/Model.h"
#include "Rendering/Mesh.h"
#include "Rendering/TextureLayer.h"
#include "Lighting/DirectionalLight.h"
#include "Lighting/PointLight.h"
#include "Lighting/SpotLight.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

// =====================================================================
// Setup
// =====================================================================

GlobalIllumination::Config GlobalIllumination::MakeConfig(int quality)
{
	Config c;
	c.quality = quality;
	c.voxelSize = 0.5f;
	c.spacing[0] = 2.0f;
	c.spacing[1] = 8.0f;
	if (quality <= 0) {        // Low: integrated GPUs
		c.resolution = 64; c.levels = 3; c.probes = glm::ivec3(16, 8, 16); c.rays = 32; c.slices = 8;
	} else if (quality == 1) { // Medium
		c.resolution = 96; c.levels = 4; c.probes = glm::ivec3(24, 12, 24); c.rays = 64; c.slices = 4;
	} else {                   // High
		c.resolution = 128; c.levels = 4; c.probes = glm::ivec3(32, 16, 32); c.rays = 64; c.slices = 4;
	}
	return c;
}

void GlobalIllumination::Init()
{
	voxelizeShader.CreateFromFiles("Assets/Shaders/gi_voxelize.vert", "Assets/Shaders/gi_voxelize.geom", "Assets/Shaders/gi_voxelize.frag");
	injectShader.CreateComputeShader("Assets/Shaders/gi_inject.glsl");
	probeShader.CreateComputeShader("Assets/Shaders/gi_probes.glsl");

	glGenBuffers(1, &ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	Block off{};
	glBufferData(GL_UNIFORM_BUFFER, sizeof(Block), &off, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	glBindBufferBase(GL_UNIFORM_BUFFER, GI_UBO_BINDING, ubo);

	glGenSamplers(1, &skySampler);
	glSamplerParameteri(skySampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glSamplerParameteri(skySampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glSamplerParameteri(skySampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glSamplerParameteri(skySampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glSamplerParameteri(skySampler, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
}

void GlobalIllumination::Release()
{
	if (voxelData) glDeleteTextures(1, &voxelData);
	if (voxelRadiance) glDeleteTextures(1, &voxelRadiance);
	if (voxelBricks) glDeleteTextures(1, &voxelBricks);
	voxelBricks = 0;
	if (voxelFBO) glDeleteFramebuffers(1, &voxelFBO);
	if (probeBuffer) glDeleteBuffers(1, &probeBuffer);
	voxelData = voxelRadiance = voxelFBO = probeBuffer = 0;
	levels.clear();
	config = Config();
}

void GlobalIllumination::Shutdown()
{
	Release();
	for (auto& set : timestamps) if (set[0]) glDeleteQueries(4, set);
	std::memset(timestamps, 0, sizeof(timestamps));
	if (ubo) glDeleteBuffers(1, &ubo);
	ubo = 0;
	if (skySampler) glDeleteSamplers(1, &skySampler);
	skySampler = 0;
}

void GlobalIllumination::Allocate(const Config& c)
{
	Release();
	config = c;
	const int R = c.resolution;

	glGenTextures(1, &voxelData);
	glBindTexture(GL_TEXTURE_3D, voxelData);
	glTexStorage3D(GL_TEXTURE_3D, 1, GL_R32UI, R, R, R * c.levels);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	const GLuint zero = 0;
	glClearTexImage(voxelData, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);

	glGenTextures(1, &voxelRadiance);
	glBindTexture(GL_TEXTURE_3D, voxelRadiance);
	glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA16F, R, R, R * c.levels);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	const float zeros[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glClearTexImage(voxelRadiance, 0, GL_RGBA, GL_FLOAT, zeros);

	glGenTextures(1, &voxelBricks);
	glBindTexture(GL_TEXTURE_3D, voxelBricks);
	glTexStorage3D(GL_TEXTURE_3D, 1, GL_R8UI, R / 4, R / 4, (R / 4) * c.levels);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	const GLubyte zero8 = 0;
	glClearTexImage(voxelBricks, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &zero8);
	glBindTexture(GL_TEXTURE_3D, 0);

	// An empty framebuffer of one pixel per voxel column: the voxelization writes images only
	GLint oldFbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
	glGenFramebuffers(1, &voxelFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, voxelFBO);
	glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_WIDTH, R);
	glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_HEIGHT, R);
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)oldFbo);

	// Probes (std430 GIProbe) of both cascades, then the sky probe; key 0 = not valid yet
	size_t probeCount = (size_t)c.probes.x * c.probes.y * c.probes.z * 2 + 1;
	glGenBuffers(1, &probeBuffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, probeBuffer);
	glBufferData(GL_SHADER_STORAGE_BUFFER, probeCount * GI_PROBE_BYTES, nullptr, GL_DYNAMIC_COPY);
	glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	levels.assign(c.levels, Level());
	dirtyCursor = injectCursor = 0;
	warmupFrames = WARMUP_FRAMES;
}

void GlobalIllumination::Invalidate()
{
	for (auto& l : levels) { l.valid = false; l.dirty = true; }
	if (probeBuffer) {
		const GLuint zero = 0;
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, probeBuffer);
		glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}
	sceneSignature = 0;
	warmupFrames = WARMUP_FRAMES;
}

int GlobalIllumination::GetValidLevels() const
{
	int n = 0;
	while (n < (int)levels.size() && levels[n].valid) n++;
	return n;
}

// A level follows the camera in steps of an eighth of its size (re-voxelized at each step)
glm::vec3 GlobalIllumination::DesiredLevelMin(int level, const glm::vec3& cameraPos) const
{
	float voxel = GetVoxelSize(level);
	float extent = voxel * config.resolution;
	float step = voxel * (config.resolution / 8);
	glm::vec3 center = glm::round(cameraPos / step) * step;
	return center - extent * 0.5f;
}

// What the voxels show: every object's place and size, the instanced groups' instances
uint64_t GlobalIllumination::SceneSignature(SceneManager& scene)
{
	uint64_t h = 1469598103934665603ull;
	auto Mix = [&](const void* data, size_t bytes) {
		const unsigned char* p = (const unsigned char*)data;
		for (size_t i = 0; i < bytes; i++) h = (h ^ p[i]) * 1099511628211ull;
	};
	for (auto* obj : scene.GetObjects()) {
		if (!obj || !obj->GetVisible()) continue;
		glm::vec3 bmin, bmax;
		obj->GetWorldBounds(bmin, bmax);
		Mix(&bmin, sizeof(bmin));
		Mix(&bmax, sizeof(bmax));
		Material* m = obj->GetMaterial();
		glm::vec3 c = m ? m->GetColorRGB() : glm::vec3(1.0f);
		Mix(&c, sizeof(c));
	}
	for (auto* g : scene.GetInstancedGroups()) {
		if (!g) continue;
		uint32_t n = g->GetTotalCount();
		Mix(&n, sizeof(n));
		if (!g->cpuInstances.empty()) {
			Mix(&g->cpuInstances.front(), sizeof(InstancedGroup::PackedInstance));
			Mix(&g->cpuInstances.back(), sizeof(InstancedGroup::PackedInstance));
		}
	}
	return h;
}

// =====================================================================
// Albedo of a surface without its shader: material colour x the average of its texture(s)
// =====================================================================

glm::vec3 GlobalIllumination::TextureAverage(Texture* texture)
{
	if (!texture || !texture->GetTextureID()) return glm::vec3(1.0f);
	GLuint id = texture->GetTextureID();
	auto it = textureAverages.find(id);
	if (it != textureAverages.end()) return it->second;

	glBindTexture(GL_TEXTURE_2D, id);
	GLint w = 0, h = 0;
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
	glm::vec3 average(1.0f);
	if (w > 0 && h > 0) {
		int level = (int)std::floor(std::log2((float)std::max(w, h)));
		GLint lw = 0, lh = 0;
		glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &lw);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &lh);
		if (lw <= 0 || lh <= 0) level = 0, lw = w, lh = h; // No mip chain: read level 0 (once)
		std::vector<float> px((size_t)lw * lh * 4);
		glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_FLOAT, px.data());
		glm::dvec3 sum(0.0);
		for (size_t i = 0; i < (size_t)lw * lh; i++) sum += glm::dvec3(px[i * 4], px[i * 4 + 1], px[i * 4 + 2]);
		average = glm::vec3(sum / (double)((size_t)lw * lh));
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	textureAverages[id] = average;
	return average;
}

glm::vec3 GlobalIllumination::SurfaceAlbedo(GameObject* obj)
{
	Material* m = obj->GetMaterial();
	glm::vec3 colour = m ? m->GetColorRGB() : glm::vec3(1.0f);
	const auto& layers = obj->GetTextureLayers();
	if (layers.empty()) return colour;
	// Layered surfaces (terrain): the layers' average colours, by their opacity
	glm::vec3 sum(0.0f);
	float weight = 0.0f;
	for (const auto& layer : layers) {
		if (!layer.texture) continue;
		float w = std::max(layer.opacity, 0.05f);
		sum += TextureAverage(layer.texture) * w;
		weight += w;
	}
	return weight > 0.0f ? colour * (sum / weight) : colour;
}

// =====================================================================
// 1. Voxelization of one level
// =====================================================================

void GlobalIllumination::Voxelize(int l, SceneManager& scene, const glm::vec3& cameraPos, const GraphicsSettings& gs, GLuint cullShaderID)
{
	const int R = config.resolution;
	const float voxel = GetVoxelSize(l);
	const float extent = voxel * R;
	const glm::vec3 boxMin = levels[l].min, boxMax = boxMin + glm::vec3(extent);

	const GLuint zero = 0;
	const GLubyte zero8 = 0;
	glClearTexSubImage(voxelData, 0, 0, 0, l * R, R, R, R, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
	glClearTexSubImage(voxelBricks, 0, 0, 0, l * (R / 4), R / 4, R / 4, R / 4, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &zero8);
	glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	// GL state of the caller
	GLint oldFbo = 0, oldViewport[4], oldProgram = 0, oldActive = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
	glGetIntegerv(GL_VIEWPORT, oldViewport);
	glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
	GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE), blend = glIsEnabled(GL_BLEND);
	GLboolean depthMask = GL_TRUE, colorMask[4];
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
	glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);

	glBindFramebuffer(GL_FRAMEBUFFER, voxelFBO);
	glViewport(0, 0, R, R);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDepthMask(GL_FALSE);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	voxelizeShader.UseShader();
	GLuint id = voxelizeShader.GetShaderID();
	auto U = [&](const char* n) { return glGetUniformLocation(id, n); };
	glUniform3fv(U("levelMin"), 1, glm::value_ptr(boxMin));
	glUniform1f(U("levelSize"), extent);
	glUniform1f(U("voxelSize"), voxel);
	glUniform1i(U("resolution"), R);
	glUniform1i(U("levelIndex"), l);
	glUniform1f(U("albedoLod"), 5.0f);
	glUniform1i(U("theTexture"), 0);
	glUniform1i(U("voxelTexture"), 5);
	GLint modelLoc = U("model"), albedoLoc = U("albedoColor"), useDiffuseLoc = U("useDiffuseTexture"),
		useVoxelTexLoc = U("useVoxelTexture"), instancedLoc = U("instanced"), tilingLoc = U("material.tiling"), offsetLoc = U("material.offset");
	glBindImageTexture(0, voxelData, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);

	// Scene objects (models: every mesh with its own texture)
	glUniform1i(instancedLoc, 0);
	glUniform2f(tilingLoc, 1.0f, 1.0f);
	glUniform2f(offsetLoc, 0.0f, 0.0f);
	for (auto* obj : scene.GetObjects()) {
		if (!obj || !obj->GetVisible()) continue;
		Mesh* mesh = obj->GetMesh();
		Model* model = obj->GetModel();
		if (!mesh && !model) continue;
		Material* mat = obj->GetMaterial();
		if (mat && mat->GetAlpha() < 0.5f) continue; // Glass, transparent decals
		if (mat && mat->GetShader()) {
			const std::string& v = mat->GetShader()->GetVertexPath();
			if (v.find("water.vert") != std::string::npos || v.find("river.vert") != std::string::npos) continue;
		}
		glm::vec3 bmin, bmax;
		obj->GetWorldBounds(bmin, bmax);
		if (bmax.x < boxMin.x || bmax.y < boxMin.y || bmax.z < boxMin.z ||
			bmin.x > boxMax.x || bmin.y > boxMax.y || bmin.z > boxMax.z) continue;

		glm::mat4 world = obj->GetWorldMatrix();
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(world));
		glUniform3fv(albedoLoc, 1, glm::value_ptr(SurfaceAlbedo(obj)));
		glUniform1i(useVoxelTexLoc, 0);
		if (model) {
			model->RenderModel((GLuint)-1, (GLuint)useDiffuseLoc, (GLuint)-1, (GLuint)U("theTexture"));
		} else {
			Texture* tex = obj->GetTextureLayers().empty() ? obj->GetTexture() : nullptr;
			if (tex && tex->GetTextureID()) { tex->UseTexture(); glUniform1i(useDiffuseLoc, 1); }
			else glUniform1i(useDiffuseLoc, 0);
			mesh->RenderMesh();
		}
	}

	// Instanced groups (trees, rocks, logs) with their cheapest mesh; not the detail foliage (grass)
	if (cullShaderID) {
		voxelSettings = gs;
		voxelSettings.impostors = false;
		voxelSettings.lodEnabled[0] = false;
		voxelSettings.lodEnabled[1] = false;
		voxelSettings.lodEnabled[2] = true;
		voxelSettings.lodDistance[2] = 1.0e9f;
		voxelSettings.renderDistance = 1.0e9f;

		glm::vec3 center = boxMin + glm::vec3(extent * 0.5f);
		float h = extent * 0.5f;
		glm::mat4 boxView = glm::lookAt(center + glm::vec3(0.0f, extent, 0.0f), center, glm::vec3(0.0f, 0.0f, -1.0f));
		glm::mat4 boxProj = glm::ortho(-h, h, -h, h, extent - h, extent + h);
		InstancedGroup::CullPass pass;
		pass.viewProj = boxProj * boxView;
		pass.cameraPos = cameraPos;
		pass.settings = &voxelSettings;
		pass.projScale = 1.0f;
		pass.maxDrawDistance = 1.0e9f;
		pass.sizeScaledDistance = false;
		pass.minScreenSize = 0.0f;
		pass.minCasterSize = voxel * 0.5f; // Instances much smaller than a voxel would not show
		pass.detailMaxDistance = 0.0f;
		pass.impostors = false;
		pass.orthographic = true;

		for (auto* group : scene.GetInstancedGroups()) {
			if (!group || group->GetTotalCount() == 0 || group->IsDetailFoliage() || !group->GetMesh()) continue;
			voxelizeShader.UseShader();
			glUniform1i(instancedLoc, 1);
			Material* gm = group->GetMaterial();
			glm::vec3 colour = gm ? gm->GetColorRGB() : glm::vec3(1.0f);
			Texture* tex = group->GetTexture() ? group->GetTexture()
				: (!group->GetTextureLayers().empty() ? group->GetTextureLayers()[0].texture : nullptr);
			if (tex && tex->GetTextureID()) {
				tex->UseTextureOnUnit(GL_TEXTURE5);
				glUniform1i(useVoxelTexLoc, 1);
			} else {
				glUniform1i(useVoxelTexLoc, 0);
			}
			glUniform3fv(albedoLoc, 1, glm::value_ptr(colour));
			glBindImageTexture(0, voxelData, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
			group->CullAndDrawShadow(cullShaderID, voxelizeShader, pass, pass.viewProj, 0.0f);
			glDisable(GL_CULL_FACE); // It turns culling back on
		}
	}

	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

	glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
	glDepthMask(depthMask);
	if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	glActiveTexture((GLenum)oldActive);
	glUseProgram((GLuint)oldProgram);
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)oldFbo);
	glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
}

// =====================================================================
// 2. Lighting the voxels of one level
// =====================================================================

void GlobalIllumination::Inject(int l, bool writeEmpty, const glm::vec3& cameraPos, const glm::mat4& view,
	DirectionalLight& sun, PointLight* pointLights, unsigned int pointLightCount,
	SpotLight* spotLights, unsigned int spotLightCount, const GraphicsSettings& gs)
{
	const int R = config.resolution;
	injectShader.UseShader();
	GLuint id = injectShader.GetShaderID();
	auto U = [&](const std::string& n) { return glGetUniformLocation(id, n.c_str()); };

	glBindImageTexture(0, voxelData, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
	glBindImageTexture(1, voxelRadiance, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glBindImageTexture(2, voxelBricks, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R8UI);
	glUniform1i(U("levelIndex"), l);
	glUniform1i(U("resolution"), R);
	glUniform3fv(U("levelMin"), 1, glm::value_ptr(levels[l].min));
	glUniform1f(U("voxelSize"), GetVoxelSize(l));
	glUniform1i(U("writeEmpty"), writeEmpty ? 1 : 0);

	glm::vec3 colour = *sun.GetColourPtr();
	glm::vec3 toSun = -glm::normalize(*sun.GetDirectionPtr());
	glUniform3fv(U("sunDir"), 1, glm::value_ptr(toSun));
	glUniform3fv(U("sunRadiance"), 1, glm::value_ptr(colour * *sun.GetDiffuseIntensityPtr()));
	glUniform3fv(U("ambientColour"), 1, glm::value_ptr(colour * *sun.GetAmbientIntensityPtr()));
	glUniform1f(U("bounce"), std::clamp(gs.giBounce, 0.0f, 1.0f));

	// The sun's shadow cascades (as the volumetric fog reads them)
	const auto& matrices = sun.GetCascadedLightMatrices();
	const auto& splits = sun.GetCascadeSplitDistances();
	const auto& ranges = sun.GetCascadeDepthRanges();
	int cascades = sun.GetShadowMap() ? (int)std::min<size_t>(std::min(std::min(matrices.size(), splits.size()), ranges.size()), 4) : 0;
	for (int i = 0; i < cascades; i++) {
		std::string k = "[" + std::to_string(i) + "]";
		glUniformMatrix4fv(U("cascadeMatrices" + k), 1, GL_FALSE, glm::value_ptr(matrices[i]));
		glUniform1f(U("cascadeSplits" + k), splits[i]);
		glUniform1f(U("cascadeDepthRanges" + k), std::max(ranges[i], 1.0f));
	}
	glUniform1i(U("cascadeCount"), cascades);
	if (sun.GetShadowMap()) sun.GetShadowMap()->Read(GL_TEXTURE3);
	glUniform1i(U("directionalShadowMap"), 3);
	glm::mat4 invView = glm::inverse(view);
	glUniform3fv(U("cameraPos"), 1, glm::value_ptr(cameraPos));
	glm::vec3 forward = -glm::normalize(glm::vec3(invView[2]));
	glUniform3fv(U("cameraForward"), 1, glm::value_ptr(forward));

	int points = (int)std::min(pointLightCount, 3u), spots = (int)std::min(spotLightCount, 3u);
	glUniform1i(U("pointCount"), points);
	for (int i = 0; i < points; i++) {
		std::string k = "[" + std::to_string(i) + "]";
		PointLight& p = pointLights[i];
		glUniform3fv(U("pointPos" + k), 1, glm::value_ptr(*p.GetPositionPtr()));
		glUniform3fv(U("pointRadiance" + k), 1, glm::value_ptr(*p.GetColourPtr() * *p.GetDiffuseIntensityPtr()));
		glUniform3f(U("pointAtten" + k), *p.GetConstantPtr(), *p.GetLinearPtr(), *p.GetExponentPtr());
	}
	glUniform1i(U("spotCount"), spots);
	for (int i = 0; i < spots; i++) {
		std::string k = "[" + std::to_string(i) + "]";
		SpotLight& s = spotLights[i];
		glUniform3fv(U("spotPos" + k), 1, glm::value_ptr(*s.GetPositionPtr()));
		glUniform3fv(U("spotDir" + k), 1, glm::value_ptr(*s.GetDirectionPtr()));
		glUniform3fv(U("spotRadiance" + k), 1, glm::value_ptr(*s.GetColourPtr() * *s.GetDiffuseIntensityPtr()));
		glUniform3f(U("spotAtten" + k), *s.GetConstantPtr(), *s.GetLinearPtr(), *s.GetExponentPtr());
		glUniform1f(U("spotEdge" + k), std::cos(glm::radians(*s.GetEdgePtr())));
	}

	// The lights' cube shadow maps (units 4-9); a light without one is unshadowed
	auto BindOmni = [&](const char* name, int unit, PointLight* light, int i) {
		std::string k = "[" + std::to_string(i) + "]";
		ShadowMap* map = light ? light->GetShadowMap() : nullptr;
		bool shadowed = map && map->GetTextureID();
		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_CUBE_MAP, shadowed ? map->GetTextureID() : 0);
		glUniform1i(U(std::string(name) + "Shadow" + k), unit);
		glUniform1f(U(std::string(name) + "Far" + k), shadowed ? light->GetFarPlane() : 0.0f);
	};
	for (int i = 0; i < 3; i++) {
		BindOmni("point", 4 + i, i < points ? &pointLights[i] : nullptr, i);
		BindOmni("spot", 7 + i, i < spots ? &spotLights[i] : nullptr, i);
	}
	glActiveTexture(GL_TEXTURE0);

	int groups = R / 4;
	glDispatchCompute(groups, groups, groups);
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

// =====================================================================
// 3. Probes: a slice traces rays through the voxels
// =====================================================================

void GlobalIllumination::UpdateProbes(const GraphicsSettings& gs, DirectionalLight& sun, GLuint skyCubemap)
{
	int validLevels = GetValidLevels();
	if (validLevels == 0) return;
	probeShader.UseShader();
	GLuint id = probeShader.GetShaderID();
	auto U = [&](const std::string& n) { return glGetUniformLocation(id, n.c_str()); };

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_3D, voxelRadiance);
	glUniform1i(U("voxelRadiance"), 0);
	// The sky through its blurred mip levels (about 8x8 texels a face), when it has them
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_CUBE_MAP, skyCubemap);
	GLint faceSize = 0, mipSize = 0;
	glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_WIDTH, &faceSize);
	glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 1, GL_TEXTURE_WIDTH, &mipSize);
	bool skyMips = mipSize > 0 && faceSize > 0;
	glBindSampler(1, skyMips ? skySampler : 0);
	glUniform1f(U("skyLod"), skyMips ? std::max(std::log2((float)faceSize / 8.0f), 0.0f) : 0.0f);
	glUniform1i(U("skyCube"), 1);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_3D, voxelBricks);
	glUniform1i(U("voxelBricks"), 2);
	glActiveTexture(GL_TEXTURE0);

	glUniform1i(U("resolution"), config.resolution);
	glUniform1i(U("levelCount"), validLevels);
	for (int l = 0; l < validLevels; l++)
		glUniform4f(U("levelMinSize[" + std::to_string(l) + "]"), levels[l].min.x, levels[l].min.y, levels[l].min.z, GetVoxelSize(l));
	glm::vec3 ambient = *sun.GetColourPtr() * *sun.GetAmbientIntensityPtr();
	glUniform3fv(U("ambientColour"), 1, glm::value_ptr(ambient));
	glUniform1f(U("skyColour"), std::clamp(gs.giSkyColour, 0.0f, 1.0f));

	// A new random rotation of the ray set every frame
	auto Hash = [](uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; };
	float u1 = (Hash(frame * 3 + 1) & 0xFFFFFF) / 16777216.0f, u2 = (Hash(frame * 3 + 2) & 0xFFFFFF) / 16777216.0f,
		u3 = (Hash(frame * 3 + 3) & 0xFFFFFF) / 16777216.0f;
	// Uniform random quaternion (Shoemake)
	glm::quat q(std::sqrt(u1) * std::cos(6.2831853f * u3), std::sqrt(1.0f - u1) * std::sin(6.2831853f * u2),
		std::sqrt(1.0f - u1) * std::cos(6.2831853f * u2), std::sqrt(u1) * std::sin(6.2831853f * u3));
	glm::mat3 rotation = glm::mat3_cast(glm::normalize(q));
	glUniformMatrix3fv(U("rayRotation"), 1, GL_FALSE, glm::value_ptr(rotation));
	glUniform1i(U("rayCount"), std::clamp(config.rays, 1, 64));
	int slices = warmupFrames > 0 ? 1 : config.slices;
	glUniform1i(U("frameSlice"), (int)(frame % (uint32_t)slices));
	glUniform1i(U("sliceCount"), slices);
	glUniform1f(U("hysteresis"), 0.9f);
	glUniform1f(U("maxRayDistance"), GetVoxelSize(validLevels - 1) * config.resolution * 0.5f);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GI_PROBE_SSBO_BINDING, probeBuffer);
	int probeCount = config.probes.x * config.probes.y * config.probes.z * 2;
	glDispatchCompute(probeCount + 1, 1, 1); // + the sky probe
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	glBindSampler(1, 0);
}

// =====================================================================
// Frame
// =====================================================================

// The oldest set of timestamps (issued three frames ago), if the GPU is done with it
void GlobalIllumination::ReadTimestamps()
{
	int oldest = timestampFrame;
	if (!timestampsIssued[oldest]) return;
	GLint available = 0;
	glGetQueryObjectiv(timestamps[oldest][3], GL_QUERY_RESULT_AVAILABLE, &available);
	if (!available) return;
	GLuint64 t[4];
	for (int i = 0; i < 4; i++) glGetQueryObjectui64v(timestamps[oldest][i], GL_QUERY_RESULT, &t[i]);
	for (int i = 0; i < 3; i++) stageMs[i] = (float)((t[i + 1] - t[i]) / 1.0e6);
	timestampsIssued[oldest] = false;
}

void GlobalIllumination::Update(SceneManager& scene, const glm::vec3& cameraPos, const glm::mat4& view,
	DirectionalLight& sun, PointLight* pointLights, unsigned int pointLightCount,
	SpotLight* spotLights, unsigned int spotLightCount,
	const GraphicsSettings& gs, GLuint cullShaderID, GLuint skyCubemap)
{
	bool ready = voxelizeShader.GetShaderID() && injectShader.GetShaderID() && probeShader.GetShaderID() && ubo;
	bool enabled = gs.giEnabled && ready;
	if (!enabled) {
		if (enabledLastFrame || ready) {
			Block off{};
			glBindBuffer(GL_UNIFORM_BUFFER, ubo);
			glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Block), &off);
			glBindBuffer(GL_UNIFORM_BUFFER, 0);
		}
		if (ubo) glBindBufferBase(GL_UNIFORM_BUFFER, GI_UBO_BINDING, ubo);
		enabledLastFrame = false;
		return;
	}
	int quality = std::clamp(gs.giQuality, 0, 2);
	if (config.quality != quality) Allocate(MakeConfig(quality));
	if (!enabledLastFrame) Invalidate(); // Turned back on: nothing kept is current
	enabledLastFrame = true;
	frame++;
	ReadTimestamps();
	int ts = timestampFrame;
	if (!timestamps[ts][0]) glGenQueries(4, timestamps[ts]);
	glQueryCounter(timestamps[ts][0], GL_TIMESTAMP);

	// Probe grids around the camera; the block every shader reads
	Block block;
	block.spacing = glm::vec4(config.spacing[0], config.spacing[1], std::max(gs.giIntensity, 0.0f), 1.0f);
	glm::ivec3 half = config.probes / 2;
	block.base0 = glm::ivec4(glm::ivec3(glm::floor(cameraPos / config.spacing[0])) - half, 0);
	block.base1 = glm::ivec4(glm::ivec3(glm::floor(cameraPos / config.spacing[1])) - half, 0);
	block.counts = glm::ivec4(config.probes, config.probes.x * config.probes.y * config.probes.z);
	auto Slot = [&](const glm::ivec4& b) {
		glm::ivec3 m = glm::ivec3(b) % config.probes;
		return glm::ivec4((m + config.probes) % config.probes, 0);
	};
	block.slot0 = Slot(block.base0);
	block.slot1 = Slot(block.base1);
	glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Block), &block);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	glBindBufferBase(GL_UNIFORM_BUFFER, GI_UBO_BINDING, ubo);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GI_PROBE_SSBO_BINDING, probeBuffer);

	// The scene changed: every level shows an older state now
	uint64_t signature = SceneSignature(scene);
	if (signature != sceneSignature) {
		sceneSignature = signature;
		for (auto& l : levels) l.dirty = true;
	}

	// One level voxelized per frame: the finest one that has to follow the camera, else one
	// that shows an older scene
	int target = -1;
	for (int l = 0; l < config.levels && target < 0; l++)
		if (!levels[l].valid || DesiredLevelMin(l, cameraPos) != levels[l].min) target = l;
	for (int k = 0; k < config.levels && target < 0; k++) {
		int l = (dirtyCursor + k) % config.levels;
		if (levels[l].dirty) { target = l; dirtyCursor = l + 1; }
	}
	if (target >= 0) {
		levels[target].min = DesiredLevelMin(target, cameraPos);
		Voxelize(target, scene, cameraPos, gs, cullShaderID);
		levels[target].valid = true;
		levels[target].dirty = false;
		Inject(target, true, cameraPos, view, sun, pointLights, pointLightCount, spotLights, spotLightCount, gs);
	}
	glQueryCounter(timestamps[ts][1], GL_TIMESTAMP);
	// And one more level re-lit (moving sun and lights, light bouncing further)
	// (all of them while warming up)
	int valid = GetValidLevels();
	int relit = warmupFrames > 0 ? valid : std::min(valid, 1);
	for (int k = 0; k < relit; k++) {
		int l = injectCursor++ % valid;
		if (l != target) Inject(l, false, cameraPos, view, sun, pointLights, pointLightCount, spotLights, spotLightCount, gs);
	}

	glQueryCounter(timestamps[ts][2], GL_TIMESTAMP);
	UpdateProbes(gs, sun, skyCubemap);
	glQueryCounter(timestamps[ts][3], GL_TIMESTAMP);
	timestampsIssued[ts] = true;
	timestampFrame = (timestampFrame + 1) % 3;
	if (warmupFrames > 0 && GetValidLevels() == config.levels) warmupFrames--;
	glUseProgram(0);
}
