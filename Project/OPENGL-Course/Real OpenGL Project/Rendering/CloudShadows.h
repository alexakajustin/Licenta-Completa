#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <functional>

#include "Rendering/Shader.h"

struct GraphicsSettings;

// Uniform block binding point of CloudShadowBlock (Shader::CompileProgram binds every program to it)
constexpr GLuint CLOUD_SHADOW_UBO_BINDING = 6;

// Shadows of the volumetric clouds on the scene. Once per frame a small map (a square of
// ground around the camera) is filled with the sun light that crosses the cloud layer there,
// using the clouds' own density (cloud_common.glsl), so the shadows sit under the drawn clouds
// and drift with them. Every lit shader samples it through cloud_shadow.glsl. The map is copied
// on the GPU into the uniform block itself (4 bits per texel): the main shader has no texture
// unit left for it. The block stays bound to its own binding point, so no draw sets anything.
class CloudShadowMap
{
public:
	static constexpr int SIZE = 180;        // cloud_shadow.glsl: CLOUD_SHADOW_SIZE (SIZE^2 nibbles fit the 16 KB block)
	static constexpr float AREA = 5000.0f;  // Metres covered by the map (texel ~ 28 m: a cloud's outline shows)

	void Init();
	// toSun: unit direction toward the sun. renderQuad draws a fullscreen quad.
	void Update(const glm::vec3& cameraPos, const glm::vec3& toSun, const GraphicsSettings& gs,
		GLuint shapeNoise, GLuint detailNoise, float time, const std::function<void()>& renderQuad);
	void Shutdown();

	GLuint GetTexture() const { return texture; }
	bool IsActive() const { return active; }

private:
	struct Block {
		glm::vec4 area; // xy: corner (world xz), z: size, w: 1 = on
		glm::vec4 sun;  // xy: toSun.xz / toSun.y, z: strength
	};
	void UploadHeader(const Block& block);

	Shader shader;
	GLuint fbo = 0, texture = 0, ubo = 0;
	bool active = false;
};
