#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "Rendering/Shader.h"

class DirectionalLight;
struct GraphicsSettings;

/**
 * @class VolumetricFog
 * @brief Froxel-based volumetric fog (Wronski, "Volumetric Fog", SIGGRAPH 2014; Hillaire,
 *        "Physically Based and Unified Volumetric Rendering in Frostbite", SIGGRAPH 2015).
 *
 * Per frame: inject medium + sun/ambient light (with cascaded shadows) into a camera-aligned
 * 3D grid, blend it with the reprojected previous frame (jittered depth, Halton), integrate
 * each column front to back, then composite over the scene using its depth buffer.
 */
class VolumetricFog
{
public:
	~VolumetricFog();

	/**
	 * @brief Renders the fog over the currently bound framebuffer (the scene must be in it).
	 *        sceneDepthTexture is the depth of that framebuffer; it is only read.
	 */
	void Render(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos,
		DirectionalLight& sun, GLuint sceneDepthTexture, int viewportWidth, int viewportHeight,
		const GraphicsSettings& gs, float time);

	/** @brief Drops the temporal history (camera cut, scene load). */
	void InvalidateHistory() { historyValid = false; }

	/**
	 * @brief Sun and sky light the fog scatters (also used by the analytic fog over water
	 *        reflections, so both match).
	 */
	static void LightRadiance(const GraphicsSettings& gs, DirectionalLight& sun, glm::vec3& sunRadiance, glm::vec3& ambientRadiance);

private:
	bool Init();
	void AllocateGrid(int width, int height, int quality);
	void ReleaseGrid();

	bool initialised = false;
	bool failed = false;

	Shader injectShader;
	Shader integrateShader;
	Shader applyShader;
	GLuint noiseTexture = 0;
	GLuint emptyVAO = 0;

	GLuint injectVolume[2] = { 0, 0 };   // Ping-pong: this frame / history
	GLuint integratedVolume = 0;
	glm::ivec3 gridSize = glm::ivec3(0);
	int gridViewportW = 0, gridViewportH = 0, gridQuality = -1;
	int current = 0;

	bool historyValid = false;
	glm::mat4 prevViewProj = glm::mat4(1.0f);
	glm::mat4 prevView = glm::mat4(1.0f);
	unsigned int frameIndex = 0;

	// Everything that shapes or lights the medium; when it changes the history is stale
	// (blending it in would make every settings tweak fade in over dozens of frames)
	struct MediumState {
		float density, baseHeight, heightFalloff, range, noiseStrength, noiseScale, anisotropy;
		glm::vec3 albedo, sunDir, sunRadiance, ambientRadiance;
	};
	MediumState lastMedium = {};

	glm::vec3 windOffset = glm::vec3(0.0f); // Integrated per frame, so changing the speed never jumps
	float lastTime = -1.0f;
};
