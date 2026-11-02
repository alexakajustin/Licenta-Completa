#include "Rendering/TemporalAA.h"
#include "Rendering/InstancedGroup.h"

#include <glm/gtc/type_ptr.hpp>
#include <cmath>

namespace {
	// Radical inverse in `base`: the Halton sequence (well spread sub-pixel positions)
	float Halton(uint32_t index, uint32_t base)
	{
		float f = 1.0f, r = 0.0f;
		while (index > 0) {
			f /= (float)base;
			r += f * (float)(index % base);
			index /= base;
		}
		return r;
	}

	const float NO_VELOCITY[4] = { 0.0f, 0.0f, -1.0f, 0.0f }; // z < 0: nothing wrote a motion vector
}

void TemporalAA::Init()
{
	resolveShader.CreateFromFiles("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/taa_resolve.frag");

	glGenBuffers(1, &ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(FrameBlock), nullptr, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	UploadFrameBlock(FrameBlock{ glm::vec4(0.0f), glm::mat4(1.0f), glm::mat4(1.0f) });
}

void TemporalAA::Shutdown()
{
	InstancedGroup::SetVelocityTarget(0);
	glDeleteFramebuffers(2, historyFBO);
	glDeleteTextures(2, history);
	if (velocity) glDeleteTextures(1, &velocity);
	if (ubo) glDeleteBuffers(1, &ubo);
	history[0] = history[1] = historyFBO[0] = historyFBO[1] = ubo = velocity = 0;
	historyWidth = historyHeight = velocityWidth = velocityHeight = 0;
	historyValid = false;
}

void TemporalAA::UploadFrameBlock(const FrameBlock& block)
{
	if (!ubo) return;
	glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(FrameBlock), &block);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	glBindBufferBase(GL_UNIFORM_BUFFER, FRAME_UBO_BINDING, ubo);
}

void TemporalAA::AllocateVelocity(int width, int height, GLuint viewportFBO)
{
	if (!velocity) glGenTextures(1, &velocity);
	if (width != velocityWidth || height != velocityHeight) {
		glBindTexture(GL_TEXTURE_2D, velocity);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
		velocityWidth = width;
		velocityHeight = height;
	}
	if (velocityFBO != viewportFBO) {
		// Attachment 1; the FBO keeps drawing into attachment 0 only, the plants enable both
		GLint oldFbo = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
		glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, velocity, 0);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)oldFbo);
		velocityFBO = viewportFBO;
	}
	glClearTexImage(velocity, 0, GL_RGBA, GL_FLOAT, NO_VELOCITY);
}

glm::mat4 TemporalAA::BeginFrame(const glm::mat4& projection, const glm::mat4& view, int width, int height,
	bool enabled, GLuint viewportFBO)
{
	viewProj = projection * view;
	glm::mat4 prev = hasPrevViewProj ? prevViewProj : viewProj;
	frameEnabled = enabled && width > 0 && height > 0 && resolveShader.GetShaderID() != 0;
	if (!frameEnabled) {
		historyValid = false;
		InstancedGroup::SetVelocityTarget(0);
		UploadFrameBlock(FrameBlock{ glm::vec4(0.0f), viewProj, viewProj }); // Static dither patterns
		prevViewProj = viewProj;
		hasPrevViewProj = true;
		return projection;
	}

	frameIndex++;
	uint32_t i = (frameIndex % 8) + 1;
	glm::vec2 jitter(Halton(i, 2) - 0.5f, Halton(i, 3) - 0.5f); // Pixels
	// Dither offset: golden-ratio steps, so every pixel goes through all 16 levels evenly
	float ditherOffset = std::fmod((float)frameIndex * 0.6180339887f, 1.0f);
	UploadFrameBlock(FrameBlock{ glm::vec4(jitter, ditherOffset, 1.0f), viewProj, prev });

	if (viewportFBO) {
		AllocateVelocity(width, height, viewportFBO);
		InstancedGroup::SetVelocityTarget(viewportFBO);
	}

	// A sub-pixel shift of the whole image: translate clip space after the projection
	glm::mat4 jittered = projection;
	jittered[2][0] += jitter.x * 2.0f / (float)width;
	jittered[2][1] += jitter.y * 2.0f / (float)height;
	return jittered;
}

void TemporalAA::AllocateHistory(int width, int height)
{
	if (history[0] && width == historyWidth && height == historyHeight) return;
	if (!history[0]) {
		glGenTextures(2, history);
		glGenFramebuffers(2, historyFBO);
	}
	for (int k = 0; k < 2; k++) {
		glBindTexture(GL_TEXTURE_2D, history[k]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindFramebuffer(GL_FRAMEBUFFER, historyFBO[k]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, history[k], 0);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	historyWidth = width;
	historyHeight = height;
	historyValid = false;
}

GLuint TemporalAA::Resolve(GLuint viewportTexture, GLuint depthTexture, int width, int height,
	const std::function<void()>& renderQuad)
{
	// The camera of this frame is last frame's for the next one, whatever happens below
	prevViewProj = viewProj;
	hasPrevViewProj = true;
	InstancedGroup::SetVelocityTarget(0);
	if (!frameEnabled || width < 1 || height < 1) return 0;

	GLint oldFbo = 0, oldViewport[4], oldActive = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
	glGetIntegerv(GL_VIEWPORT, oldViewport);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
	GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	AllocateHistory(width, height);
	const int write = 1 - historyIndex;
	bool hasVelocity = velocity && velocityWidth == width && velocityHeight == height;

	glBindFramebuffer(GL_FRAMEBUFFER, historyFBO[write]);
	glViewport(0, 0, width, height);
	resolveShader.UseShader();
	GLuint id = resolveShader.GetShaderID();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, viewportTexture);
	glUniform1i(glGetUniformLocation(id, "currentColor"), 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, depthTexture);
	glUniform1i(glGetUniformLocation(id, "currentDepth"), 1);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, history[historyIndex]);
	glUniform1i(glGetUniformLocation(id, "historyColor"), 2);
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, hasVelocity ? velocity : 0);
	glUniform1i(glGetUniformLocation(id, "velocityTex"), 3);
	glUniform1i(glGetUniformLocation(id, "useVelocity"), hasVelocity ? 1 : 0);
	glUniformMatrix4fv(glGetUniformLocation(id, "invViewProj"), 1, GL_FALSE, glm::value_ptr(glm::inverse(viewProj)));
	glUniformMatrix4fv(glGetUniformLocation(id, "viewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
	glUniformMatrix4fv(glGetUniformLocation(id, "prevViewProj"), 1, GL_FALSE, glm::value_ptr(historyValid ? historyViewProj : viewProj));
	glUniform1i(glGetUniformLocation(id, "historyValid"), historyValid ? 1 : 0);
	renderQuad();

	historyIndex = write;
	historyValid = true;
	historyViewProj = viewProj;

	for (int u = 3; u >= 0; u--) { glActiveTexture(GL_TEXTURE0 + u); glBindTexture(GL_TEXTURE_2D, 0); }
	glActiveTexture((GLenum)oldActive);
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)oldFbo);
	glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
	if (depthTest) glEnable(GL_DEPTH_TEST);
	if (blend) glEnable(GL_BLEND);
	return history[write];
}
