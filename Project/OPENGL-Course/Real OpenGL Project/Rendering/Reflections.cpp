#include "Rendering/Reflections.h"
#include "GraphicsSettings.h"

#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

GLuint Reflections::target = 0;

void Reflections::Init()
{
	ssrShader.CreateFromFiles("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/ssr.frag");
}

void Reflections::Shutdown()
{
	target = 0;
	glDeleteTextures(3, textures);
	textures[0] = textures[1] = textures[2] = 0;
	texWidth = texHeight = 0;
	attachedFBO = 0;
}

void Reflections::BeginFrame(GLuint viewportFBO, int width, int height, bool enabled)
{
	frameActive = enabled && viewportFBO != 0 && width > 0 && height > 0 && ssrShader.GetShaderID() != 0;
	target = 0;
	if (!frameActive) return;

	if (!textures[0] || width != texWidth || height != texHeight) {
		if (!textures[0]) glGenTextures(3, textures);
		for (int k = 0; k < 3; k++) {
			glBindTexture(GL_TEXTURE_2D, textures[k]);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}
		glBindTexture(GL_TEXTURE_2D, 0);
		texWidth = width;
		texHeight = height;
		attachedFBO = 0; // Re-attach (same names, but be explicit after a resize)
	}
	if (attachedFBO != viewportFBO) {
		GLint oldFbo = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
		glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
		for (int k = 0; k < 3; k++)
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2 + k, GL_TEXTURE_2D, textures[k], 0);
		glDrawBuffer(GL_COLOR_ATTACHMENT0); // Only the reflective draws enable 2-4
		glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)oldFbo);
		attachedFBO = viewportFBO;
	}
	const float none[4] = { 0.0f, 0.0f, 0.0f, -1.0f };
	glClearTexImage(textures[0], 0, GL_RGBA, GL_FLOAT, none);
	target = viewportFBO;
}

void Reflections::Render(GLuint viewportFBO, GLuint depthTexture, int width, int height,
	const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos,
	GLuint prevColor, const glm::mat4& prevViewProj, const GraphicsSettings& gs,
	const std::function<void()>& renderQuad)
{
	target = 0; // The scene is drawn: no more deferred reflections this frame
	if (!frameActive || width != texWidth || height != texHeight) return;

	GLint oldFbo = 0, oldViewport[4], oldActive = 0, oldSrc = 0, oldDst = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
	glGetIntegerv(GL_VIEWPORT, oldViewport);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
	glGetIntegerv(GL_BLEND_SRC_RGB, &oldSrc);
	glGetIntegerv(GL_BLEND_DST_RGB, &oldDst);
	GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
	GLboolean depthMask = GL_TRUE;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);

	glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
	glViewport(0, 0, width, height);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE); // The reflection adds onto the lit surface

	ssrShader.UseShader();
	GLuint id = ssrShader.GetShaderID();
	GLuint inputs[5] = { depthTexture, textures[0], textures[1], textures[2], prevColor };
	const char* names[5] = { "depthTex", "normalTex", "weightTex", "probeTex", "prevColor" };
	for (int k = 0; k < 5; k++) {
		glActiveTexture(GL_TEXTURE0 + k);
		glBindTexture(GL_TEXTURE_2D, inputs[k]);
		glUniform1i(glGetUniformLocation(id, names[k]), k);
	}
	glUniform1i(glGetUniformLocation(id, "prevValid"), prevColor ? 1 : 0);
	glUniformMatrix4fv(glGetUniformLocation(id, "view"), 1, GL_FALSE, glm::value_ptr(view));
	glUniformMatrix4fv(glGetUniformLocation(id, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(glGetUniformLocation(id, "invViewProj"), 1, GL_FALSE, glm::value_ptr(glm::inverse(projection * view)));
	glUniformMatrix4fv(glGetUniformLocation(id, "prevViewProj"), 1, GL_FALSE, glm::value_ptr(prevViewProj));
	glUniform3fv(glGetUniformLocation(id, "cameraPos"), 1, glm::value_ptr(cameraPos));
	// Near plane from the projection (perspective: P[3][2] / (P[2][2] - 1))
	float nearPlane = projection[3][2] / (projection[2][2] - 1.0f);
	glUniform1f(glGetUniformLocation(id, "nearPlane"), std::max(nearPlane, 1e-3f));
	glUniform1f(glGetUniformLocation(id, "maxDistance"), std::max(gs.ssrMaxDistance, 1.0f));
	renderQuad();

	for (int k = 4; k >= 0; k--) { glActiveTexture(GL_TEXTURE0 + k); glBindTexture(GL_TEXTURE_2D, 0); }
	glActiveTexture((GLenum)oldActive);
	glBlendFunc((GLenum)oldSrc, (GLenum)oldDst);
	if (!blend) glDisable(GL_BLEND);
	if (depthTest) glEnable(GL_DEPTH_TEST);
	glDepthMask(depthMask);
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)oldFbo);
	glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
}
