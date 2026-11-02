#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <functional>

#include "Rendering/Shader.h"

struct GraphicsSettings;

// The last step of a frame: the HDR scene (TAA result, or the plain frame) is exposed, tone
// mapped and sharpened into the viewport (post_tonemap.frag).
//
// Auto exposure stays on the GPU (no read-back, no stall): the image's centre-weighted log
// luminance on a 256 x 256 grid (post_luminance.frag), averaged by its mip chain; a 1 x 1 pass
// moves the adapted value toward it over time like an eye (post_adapt.frag).
class PostProcess
{
public:
	static constexpr int LUM_SIZE = 256;

	void Init();
	void Shutdown();

	// hdrSource: the TAA result, or 0 to use the viewport texture itself (copied first: the
	// result goes back into it). sharpness: CAS strength (0 = none).
	void Apply(GLuint hdrSource, GLuint viewportTexture, GLuint viewportFBO, int width, int height,
		const GraphicsSettings& gs, float deltaTime, float sharpness, const std::function<void()>& renderQuad);

	// Scene load, camera cut: the eye adapts at once to the next frame
	void Invalidate() { adaptValid = false; }
	// The HDR copy of the viewport made when there was no TAA image (0: none)
	GLuint GetScratch() const { return scratch; }

private:
	Shader luminanceShader, adaptShader, tonemapShader;
	GLuint lumTex = 0, lumFBO = 0;
	GLuint adaptTex[2] = { 0, 0 }, adaptFBO[2] = { 0, 0 };
	int adaptIndex = 0;
	bool adaptValid = false;
	GLuint scratch = 0;
	int scratchWidth = 0, scratchHeight = 0;
};
