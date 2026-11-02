#include "Rendering/CloudShadows.h"
#include "GraphicsSettings.h"

#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

void CloudShadowMap::Init()
{
	shader.CreateFromFiles("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/cloud_shadow_map.frag");

	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, SIZE / 2, SIZE, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr); // Two 4-bit texels per byte
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// Header + the map's bytes (std140: the uvec4 array right after the two vec4)
	glGenBuffers(1, &ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(Block) + ((SIZE * SIZE / 2 + 15) / 16) * 16, nullptr, GL_DYNAMIC_COPY);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	UploadHeader(Block{ glm::vec4(0.0f), glm::vec4(0.0f) }); // Off until the first update
}

void CloudShadowMap::UploadHeader(const Block& block)
{
	if (!ubo) return;
	glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Block), &block);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	glBindBufferBase(GL_UNIFORM_BUFFER, CLOUD_SHADOW_UBO_BINDING, ubo);
}

void CloudShadowMap::Update(const glm::vec3& cameraPos, const glm::vec3& toSun, const GraphicsSettings& gs,
	GLuint shapeNoise, GLuint detailNoise, float time, const std::function<void()>& renderQuad)
{
	// Full shadows until the sun is a few degrees above the horizon (a sunset still has them)
	float strength = gs.cloudShadowStrength * glm::smoothstep(0.0f, 0.06f, toSun.y);
	active = gs.cloudShadowsEnabled && gs.cloudsEnabled && gs.volumetricSkyEnabled &&
		gs.skyboxType == SkyboxType::Atmospheric && strength > 0.001f && shader.GetShaderID() != 0 && fbo != 0;
	if (!active) {
		UploadHeader(Block{ glm::vec4(0.0f), glm::vec4(0.0f) });
		return;
	}

	// The square follows the camera in whole texels, so a texel always covers the same ground
	// (no shimmering while moving)
	const float texel = AREA / SIZE;
	glm::vec2 origin = glm::floor((glm::vec2(cameraPos.x, cameraPos.z) - AREA * 0.5f) / texel) * texel;

	GLint oldFbo = 0, oldViewport[4];
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
	glGetIntegerv(GL_VIEWPORT, oldViewport);
	GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, SIZE / 2, SIZE);
	shader.UseShader();
	GLuint id = shader.GetShaderID();
	glUniform1f(glGetUniformLocation(id, "time"), time);
	glUniform1f(glGetUniformLocation(id, "cloudsDensity"), gs.cloudsDensity);
	glUniform1f(glGetUniformLocation(id, "cloudsSpeed"), gs.cloudsSpeed);
	glUniform1f(glGetUniformLocation(id, "cloudsSharpness"), gs.cloudsSharpness);
	glUniform3fv(glGetUniformLocation(id, "sunDir"), 1, glm::value_ptr(toSun));
	glUniform2fv(glGetUniformLocation(id, "areaOrigin"), 1, glm::value_ptr(origin));
	glUniform1f(glGetUniformLocation(id, "areaSize"), AREA);
	glUniform1f(glGetUniformLocation(id, "mapSize"), (float)SIZE);
	glActiveTexture(GL_TEXTURE0 + 40);
	glBindTexture(GL_TEXTURE_3D, shapeNoise);
	glUniform1i(glGetUniformLocation(id, "cloudShapeNoise"), 40);
	glActiveTexture(GL_TEXTURE0 + 41);
	glBindTexture(GL_TEXTURE_3D, detailNoise);
	glUniform1i(glGetUniformLocation(id, "cloudDetailNoise"), 41);
	glActiveTexture(GL_TEXTURE0);
	renderQuad();

	// Into the uniform block, GPU to GPU (a pixel pack buffer: no stall, no CPU copy)
	GLint oldPack = 4;
	glGetIntegerv(GL_PACK_ALIGNMENT, &oldPack);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, ubo);
	glReadPixels(0, 0, SIZE / 2, SIZE, GL_RED, GL_UNSIGNED_BYTE, (void*)sizeof(Block));
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, oldPack);

	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)oldFbo);
	glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
	if (depth) glEnable(GL_DEPTH_TEST);
	if (blend) glEnable(GL_BLEND);

	float sy = std::max(toSun.y, 0.01f);
	UploadHeader(Block{ glm::vec4(origin, AREA, 1.0f), glm::vec4(toSun.x / sy, toSun.z / sy, strength, 0.0f) });
}

void CloudShadowMap::Shutdown()
{
	if (fbo) glDeleteFramebuffers(1, &fbo);
	if (texture) glDeleteTextures(1, &texture);
	if (ubo) glDeleteBuffers(1, &ubo);
	fbo = texture = ubo = 0;
	active = false;
}
