#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <functional>

#include "Rendering/Shader.h"

struct GraphicsSettings;

// Screen-space ambient occlusion (GTAO, gtao.frag): contact shadows in creases, under rocks and
// logs, where the grass meets the ground. Computed at half resolution from the depth buffer
// (no normal buffer needed), two directions per pixel with noise that moves every frame so TAA
// accumulates it; a depth-aware upsample (gtao_apply.frag) multiplies it into the scene before
// the fog and god rays, so the air in front stays lit.
class AmbientOcclusion
{
public:
	void Init();
	void Shutdown();
	// projection: this frame's, without the TAA jitter
	void Render(GLuint viewportFBO, GLuint depthTexture, int width, int height, const glm::mat4& projection,
		const GraphicsSettings& gs, const std::function<void()>& renderQuad);

private:
	Shader aoShader, applyShader;
	GLuint aoTex = 0, aoFBO = 0;
	int aoWidth = 0, aoHeight = 0;
};
