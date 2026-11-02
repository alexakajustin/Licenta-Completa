#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>

#include "Rendering/Shader.h"

// Uniform block binding point of FrameBlock (frame.glsl; Shader::CompileProgram binds every program to it)
constexpr GLuint FRAME_UBO_BINDING = 7;

// Temporal anti-aliasing. Every frame the scene is drawn with its projection shifted by a
// sub-pixel offset (Halton 2,3, 8 positions), and the resolve blends it into a history of past
// frames: edges, alpha-tested leaves and thin needles get several samples per pixel over time
// for one full-screen pass.
//
// Where the history is: plants moving in the wind write their own motion (attachment 1 of the
// viewport, frame.glsl / shader.frag MotionVector, with last frame's wind); everything else is
// found from the depth buffer and the camera's motion. The current 3x3 neighbourhood bounds the
// history (variance clipping in YCoCg), so nothing ghosts; the history is read with a
// Catmull-Rom filter. The result stays in HDR: PostProcess exposes, tone maps and sharpens it.
//
// The dithered fades (LOD cross-fades, distance fade-outs) shift their 4x4 pattern every frame
// while TAA is on (frame.glsl): accumulated, the dither becomes a smooth blend.
class TemporalAA
{
public:
	void Init();
	void Shutdown();

	// Start of a frame, before the scene pass: returns the projection the scene draws with
	// (jittered when enabled), publishes FrameBlock (jitter, dither offset, this and last
	// frame's camera) and prepares the motion vector attachment of viewportFBO.
	glm::mat4 BeginFrame(const glm::mat4& projection, const glm::mat4& view, int width, int height,
		bool enabled, GLuint viewportFBO);

	// After everything drawn into the viewport: resolves the frame into the history and returns
	// that texture (HDR, anti-aliased). 0 when it could not run.
	GLuint Resolve(GLuint viewportTexture, GLuint depthTexture, int width, int height,
		const std::function<void()>& renderQuad);

	// Camera cut, scene load, resize: the next frame starts a new history
	void Invalidate() { historyValid = false; }

private:
	struct FrameBlock {
		glm::vec4 jitter;           // xy pixels, z dither offset, w 1 = on
		glm::mat4 viewProj;
		glm::mat4 prevViewProj;
	};
	void AllocateHistory(int width, int height);
	void AllocateVelocity(int width, int height, GLuint viewportFBO);
	void UploadFrameBlock(const FrameBlock& block);

	Shader resolveShader;
	GLuint history[2] = { 0, 0 }, historyFBO[2] = { 0, 0 };
	GLuint velocity = 0;           // RGBA16F: xy motion (uv), z view depth of the writer, -1 = none
	GLuint velocityFBO = 0;        // The viewport FBO it is attached to
	int velocityWidth = 0, velocityHeight = 0;
	GLuint ubo = 0;
	int historyWidth = 0, historyHeight = 0;
	int historyIndex = 0;
	bool historyValid = false;
	bool frameEnabled = false;
	glm::mat4 viewProj = glm::mat4(1.0f), prevViewProj = glm::mat4(1.0f);
	bool hasPrevViewProj = false;
	glm::mat4 historyViewProj = glm::mat4(1.0f); // Camera of the frame in the history
	uint32_t frameIndex = 0;
};
