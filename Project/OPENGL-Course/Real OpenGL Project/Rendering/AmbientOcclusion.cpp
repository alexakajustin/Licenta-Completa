#include "Rendering/AmbientOcclusion.h"
#include "GraphicsSettings.h"

#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

void AmbientOcclusion::Init()
{
	aoShader.CreateFromFiles("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/gtao.frag");
	applyShader.CreateFromFiles("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/gtao_apply.frag");
}

void AmbientOcclusion::Shutdown()
{
	if (aoFBO) glDeleteFramebuffers(1, &aoFBO);
	if (aoTex) glDeleteTextures(1, &aoTex);
	aoFBO = aoTex = 0;
	aoWidth = aoHeight = 0;
}

void AmbientOcclusion::Render(GLuint viewportFBO, GLuint depthTexture, int width, int height, const glm::mat4& projection,
	const GraphicsSettings& gs, const std::function<void()>& renderQuad)
{
	if (width < 2 || height < 2 || aoShader.GetShaderID() == 0 || applyShader.GetShaderID() == 0) return;
	const int hw = (width + 1) / 2, hh = (height + 1) / 2;
	if (!aoTex || hw != aoWidth || hh != aoHeight) {
		if (!aoTex) { glGenTextures(1, &aoTex); glGenFramebuffers(1, &aoFBO); }
		glBindTexture(GL_TEXTURE_2D, aoTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, hw, hh, 0, GL_RG, GL_HALF_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, aoFBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, aoTex, 0);
		aoWidth = hw;
		aoHeight = hh;
	}

	GLint oldFbo = 0, oldViewport[4], oldActive = 0, oldSrc = 0, oldDst = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
	glGetIntegerv(GL_VIEWPORT, oldViewport);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
	glGetIntegerv(GL_BLEND_SRC_RGB, &oldSrc);
	glGetIntegerv(GL_BLEND_DST_RGB, &oldDst);
	GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
	GLboolean depthMask = GL_TRUE;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glm::mat4 invProjection = glm::inverse(projection);

	// 1. Occlusion at half resolution
	glDisable(GL_BLEND);
	glBindFramebuffer(GL_FRAMEBUFFER, aoFBO);
	glViewport(0, 0, hw, hh);
	aoShader.UseShader();
	GLuint id = aoShader.GetShaderID();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, depthTexture);
	glUniform1i(glGetUniformLocation(id, "depthTex"), 0);
	glUniformMatrix4fv(glGetUniformLocation(id, "invProjection"), 1, GL_FALSE, glm::value_ptr(invProjection));
	glUniform1f(glGetUniformLocation(id, "projScale"), projection[1][1] * (float)height * 0.5f);
	glUniform1f(glGetUniformLocation(id, "radius"), std::max(gs.aoRadius, 0.05f));
	glUniform1f(glGetUniformLocation(id, "falloff"), 0.615f);
	renderQuad();

	// 2. Depth-aware upsample, multiplied into the scene
	glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
	glViewport(0, 0, width, height);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ZERO, GL_SRC_COLOR);
	applyShader.UseShader();
	id = applyShader.GetShaderID();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, aoTex);
	glUniform1i(glGetUniformLocation(id, "aoTex"), 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, depthTexture);
	glUniform1i(glGetUniformLocation(id, "depthTex"), 1);
	glUniformMatrix4fv(glGetUniformLocation(id, "invProjection"), 1, GL_FALSE, glm::value_ptr(invProjection));
	glUniform1f(glGetUniformLocation(id, "strength"), std::clamp(gs.aoStrength, 0.0f, 1.0f));
	glUniform1f(glGetUniformLocation(id, "power"), 1.5f);
	renderQuad();

	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture((GLenum)oldActive);
	glBlendFunc((GLenum)oldSrc, (GLenum)oldDst);
	if (!blend) glDisable(GL_BLEND);
	if (depthTest) glEnable(GL_DEPTH_TEST);
	glDepthMask(depthMask);
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)oldFbo);
	glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
}
