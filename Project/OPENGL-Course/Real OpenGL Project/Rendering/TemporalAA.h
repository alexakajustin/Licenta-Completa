#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <functional>
#include <cstdint>

#include "Rendering/Shader.h"

// Uniform block binding point of FrameBlock (frame.glsl; Shader::CompileProgram binds every program to it)
constexpr GLuint FRAME_UBO_BINDING = 7;

// Temporal anti-aliasing. Every frame the scene is drawn with its projection shifted by a
// sub-pixel offset (Halton 2,3, 8 positions), and the resolve blends it into a history of past
// frames reprojected with the depth buffer: edges, alpha-tested leaves and thin needles get
// several samples per pixel over time for one full-screen pass. The current 3x3 neighbourhood
// bounds the history (variance clipping in YCoCg), so moving things do not ghost; the history is
// read with a Catmull-Rom filter and the result goes through AMD's contrast-adaptive sharpening,
// so the image stays crisp.
//
// The dithered fades (LOD cross-fades, distance fade-outs) shift their 4x4 pattern every frame
// while TAA is on (frame.glsl): accumulated, the dither becomes a smooth blend.
class TemporalAA
{
public:
	void Init();
	void Shutdown();

	// Start of a frame: returns the projection the scene pass draws with (jittered when enabled)
	// and publishes the frame's jitter and dither offset (FrameBlock)
	glm::mat4 BeginFrame(const glm::mat4& projection, int width, int height, bool enabled);

	// After everything drawn into the viewport (scene, sky, fog, god rays): resolves the frame
	// into the history and writes the anti-aliased, sharpened image back into the viewport.
	// projection / view: this frame's, without the jitter.
	void Resolve(GLuint viewportFBO, GLuint viewportTexture, GLuint depthTexture, int width, int height,
		const glm::mat4& projection, const glm::mat4& view, float sharpness, const std::function<void()>& renderQuad);

	// Camera cut, scene load, resize: the next frame starts a new history
	void Invalidate() { historyValid = false; }

private:
	void AllocateHistory(int width, int height);
	void UploadFrameBlock(const glm::vec4& value);

	Shader resolveShader, sharpenShader;
	GLuint history[2] = { 0, 0 }, historyFBO[2] = { 0, 0 };
	GLuint ubo = 0;
	int historyWidth = 0, historyHeight = 0;
	int historyIndex = 0;
	bool historyValid = false;
	glm::mat4 prevViewProj = glm::mat4(1.0f);
	uint32_t frameIndex = 0;
};
