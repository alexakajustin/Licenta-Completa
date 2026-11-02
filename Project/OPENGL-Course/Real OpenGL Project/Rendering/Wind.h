#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

struct GraphicsSettings;

// =====================================================================
// The scene's wind (Graphics Settings -> Wind)
//
// One wind for everything, the way Unity's WindZone drives every tree and grass shader: its
// direction, speed, gusts and turbulence live in one uniform block (WindBlock, wind.glsl) that
// every program reads, written once per frame. Grass, tree trunks, branches and leaves, impostor
// cards, the lake's ripples, the fog wisps and the clouds all answer to it.
//
// The oscillation phases and the drift of the gust field are integrated here, frame by frame,
// so changing the speed or the direction never makes anything jump.
// =====================================================================

// Uniform block binding point of WindBlock (Shader::CompileProgram binds every program to it)
constexpr GLuint WIND_UBO_BINDING = 5;

class WindSystem
{
public:
	~WindSystem();

	// Once per frame, before anything is drawn: integrates the phases and uploads the block
	void Update(const GraphicsSettings* gs, float deltaTime);

	glm::vec2 GetDirection() const { return direction; } // World xz, unit: where the wind blows to

private:
	struct Block {
		glm::vec4 dirSpeed;  // xy direction, z speed (m/s), w 1 = wind on
		glm::vec4 gust;      // xy gust field drift (m), z gustiness, w turbulence
		glm::vec4 phase;     // trunks, branches, leaves, grass (radians)
		glm::vec4 response;  // tree sway, leaf flutter, grass bend, -
		glm::vec4 clouds;    // xy cloud drift (sky units)
		// Last frame's dirSpeed, gust, phase, response: where the plants were (motion vectors)
		glm::vec4 prevDirSpeed, prevGust, prevPhase, prevResponse;
	};
	Block last{};            // The block uploaded last frame
	bool hasLast = false;

	GLuint ubo = 0;
	glm::vec2 direction = glm::vec2(1.0f, 0.0f);
	double gustDrift[2] = { 0.0, 0.0 };
	double cloudDrift[2] = { 0.0, 0.0 };
	double phases[4] = { 0.0, 0.0, 0.0, 0.0 };
};
