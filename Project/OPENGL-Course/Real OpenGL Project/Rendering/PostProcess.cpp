#include "Rendering/PostProcess.h"
#include "GraphicsSettings.h"

#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

void PostProcess::Init()
{
	luminanceShader.CreateFromFiles("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/post_luminance.frag");
	adaptShader.CreateFromFiles("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/post_adapt.frag");
	tonemapShader.CreateFromFiles("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/post_tonemap.frag");

	int levels = 1 + (int)std::floor(std::log2((float)LUM_SIZE));
	glGenTextures(1, &lumTex);
	glBindTexture(GL_TEXTURE_2D, lumTex);
	glTexStorage2D(GL_TEXTURE_2D, levels, GL_RG32F, LUM_SIZE, LUM_SIZE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glGenFramebuffers(1, &lumFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, lumFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, lumTex, 0);

	glGenTextures(2, adaptTex);
	glGenFramebuffers(2, adaptFBO);
	for (int k = 0; k < 2; k++) {
		glBindTexture(GL_TEXTURE_2D, adaptTex[k]);
		glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32F, 1, 1);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		const float zero = 0.0f;
		glClearTexImage(adaptTex[k], 0, GL_RED, GL_FLOAT, &zero);
		glBindFramebuffer(GL_FRAMEBUFFER, adaptFBO[k]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, adaptTex[k], 0);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PostProcess::Shutdown()
{
	if (lumFBO) glDeleteFramebuffers(1, &lumFBO);
	if (lumTex) glDeleteTextures(1, &lumTex);
	glDeleteFramebuffers(2, adaptFBO);
	glDeleteTextures(2, adaptTex);
	if (scratch) glDeleteTextures(1, &scratch);
	lumFBO = lumTex = scratch = 0;
	adaptFBO[0] = adaptFBO[1] = adaptTex[0] = adaptTex[1] = 0;
	scratchWidth = scratchHeight = 0;
}

void PostProcess::Apply(GLuint hdrSource, GLuint viewportTexture, GLuint viewportFBO, int width, int height,
	const GraphicsSettings& gs, float deltaTime, float sharpness, const std::function<void()>& renderQuad)
{
	if (width < 1 || height < 1 || tonemapShader.GetShaderID() == 0) return;

	GLint oldFbo = 0, oldViewport[4], oldActive = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
	glGetIntegerv(GL_VIEWPORT, oldViewport);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
	GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	// The source must not be the texture the result goes into
	GLuint source = hdrSource;
	if (!source) {
		if (!scratch || scratchWidth != width || scratchHeight != height) {
			if (!scratch) glGenTextures(1, &scratch);
			glBindTexture(GL_TEXTURE_2D, scratch);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			scratchWidth = width;
			scratchHeight = height;
		}
		glCopyImageSubData(viewportTexture, GL_TEXTURE_2D, 0, 0, 0, 0, scratch, GL_TEXTURE_2D, 0, 0, 0, 0, width, height, 1);
		source = scratch;
	}

	// --- Auto exposure: measure, then adapt ---
	bool autoExposure = gs.autoExposure && luminanceShader.GetShaderID() && adaptShader.GetShaderID();
	if (autoExposure) {
		glBindFramebuffer(GL_FRAMEBUFFER, lumFBO);
		glViewport(0, 0, LUM_SIZE, LUM_SIZE);
		luminanceShader.UseShader();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, source);
		glUniform1i(glGetUniformLocation(luminanceShader.GetShaderID(), "source"), 0);
		renderQuad();
		glBindTexture(GL_TEXTURE_2D, lumTex);
		glGenerateMipmap(GL_TEXTURE_2D);

		int write = 1 - adaptIndex;
		glBindFramebuffer(GL_FRAMEBUFFER, adaptFBO[write]);
		glViewport(0, 0, 1, 1);
		adaptShader.UseShader();
		GLuint id = adaptShader.GetShaderID();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, lumTex);
		glUniform1i(glGetUniformLocation(id, "luminance"), 0);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, adaptTex[adaptIndex]);
		glUniform1i(glGetUniformLocation(id, "previous"), 1);
		glUniform1f(glGetUniformLocation(id, "lastLevel"), std::floor(std::log2((float)LUM_SIZE)));
		glUniform1f(glGetUniformLocation(id, "deltaTime"), std::clamp(deltaTime, 0.0f, 0.25f));
		glUniform1f(glGetUniformLocation(id, "speed"), std::max(gs.exposureAdaptSpeed, 0.01f));
		glUniform1i(glGetUniformLocation(id, "reset"), adaptValid ? 0 : 1);
		renderQuad();
		adaptIndex = write;
		adaptValid = true;
	} else {
		adaptValid = false;
	}

	// --- Exposure, tone mapping and sharpening into the viewport ---
	glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
	glViewport(0, 0, width, height);
	tonemapShader.UseShader();
	GLuint id = tonemapShader.GetShaderID();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, source);
	glUniform1i(glGetUniformLocation(id, "source"), 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, adaptTex[adaptIndex]);
	glUniform1i(glGetUniformLocation(id, "adapted"), 1);
	glUniform1i(glGetUniformLocation(id, "autoExposure"), autoExposure ? 1 : 0);
	glUniform1f(glGetUniformLocation(id, "exposureKey"), std::max(gs.exposureKey, 0.01f));
	glUniform1f(glGetUniformLocation(id, "exposureStrength"), std::clamp(gs.exposureStrength, 0.0f, 1.0f));
	glUniform1f(glGetUniformLocation(id, "exposureRange"), std::max(gs.exposureRange, 0.0f));
	glUniform1f(glGetUniformLocation(id, "exposureBias"), gs.exposureCompensation);
	glUniform1i(glGetUniformLocation(id, "tonemapper"), std::clamp(gs.tonemapper, 0, 2));
	glUniform1f(glGetUniformLocation(id, "sharpness"), std::clamp(sharpness, 0.0f, 1.0f));
	renderQuad();

	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture((GLenum)oldActive);
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)oldFbo);
	glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
	if (depthTest) glEnable(GL_DEPTH_TEST);
	if (blend) glEnable(GL_BLEND);
}
