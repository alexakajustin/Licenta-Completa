#include "Rendering/Wind.h"
#include "GraphicsSettings.h"
#include <algorithm>
#include <cmath>

WindSystem::~WindSystem()
{
	if (ubo) glDeleteBuffers(1, &ubo);
}

void WindSystem::Update(const GraphicsSettings* gs, float deltaTime)
{
	static const GraphicsSettings defaults;
	const GraphicsSettings& g = gs ? *gs : defaults;
	const double TAU = 6.283185307179586;
	// A hitch (loading, a breakpoint) must not fling everything forward
	double dt = std::clamp((double)deltaTime, 0.0, 0.1);

	float speed = std::max(g.windSpeed, 0.0f);
	float angle = g.windDirection * 3.14159265f / 180.0f;
	direction = glm::vec2(std::cos(angle), std::sin(angle));
	double s = speed / 10.0; // 10 m/s (fresh breeze) = 1

	// Gust fronts travel downwind a little slower than the mean wind
	gustDrift[0] += direction.x * speed * 0.8 * dt;
	gustDrift[1] += direction.y * speed * 0.8 * dt;
	// The clouds keep their own speed setting, in the wind's direction
	double cloudSpeed = std::max(g.cloudsSpeed, 0.0f) * 106.0;
	cloudDrift[0] += direction.x * cloudSpeed * dt;
	cloudDrift[1] += direction.y * cloudSpeed * dt;

	// Rhythms that quicken with the wind: trunks sway slowly, branches faster, leaves flutter,
	// grass flicks in between (wind.glsl scales the trunks and branches by each plant's size)
	phases[0] += TAU * (0.22 + 0.12 * s) * dt;
	phases[1] += TAU * (0.65 + 0.45 * s) * dt;
	phases[2] += TAU * (2.4 + 3.6 * s) * dt;
	phases[3] += TAU * (0.85 + 1.1 * s) * dt;
	// The leaf and grass phases are only used times multiples of 0.01 (wind.glsl): wrapping them
	// every 100 turns keeps them exact. Trunks and branches are scaled per plant and stay unwrapped
	// (slow enough for float precision over many hours).
	phases[2] = std::fmod(phases[2], TAU * 100.0);
	phases[3] = std::fmod(phases[3], TAU * 100.0);

	Block block;
	block.dirSpeed = glm::vec4(direction, speed, speed > 0.01f ? 1.0f : 0.0f);
	block.gust = glm::vec4((float)gustDrift[0], (float)gustDrift[1], std::clamp(g.windGustiness, 0.0f, 1.0f), std::clamp(g.windTurbulence, 0.0f, 1.0f));
	block.phase = glm::vec4((float)phases[0], (float)phases[1], (float)phases[2], (float)phases[3]);
	block.response = glm::vec4(std::max(g.windTreeSway, 0.0f), std::max(g.windLeafFlutter, 0.0f), std::max(g.windGrassBend, 0.0f), 0.0f);
	block.clouds = glm::vec4((float)cloudDrift[0], (float)cloudDrift[1], 0.0f, 0.0f);

	if (!ubo) {
		glGenBuffers(1, &ubo);
		glBindBuffer(GL_UNIFORM_BUFFER, ubo);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(Block), nullptr, GL_DYNAMIC_DRAW);
	} else {
		glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	}
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Block), &block);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	glBindBufferBase(GL_UNIFORM_BUFFER, WIND_UBO_BINDING, ubo);
}
