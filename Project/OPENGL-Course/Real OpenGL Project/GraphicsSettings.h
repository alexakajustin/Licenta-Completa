#ifndef GRAPHICS_SETTINGS_H
#define GRAPHICS_SETTINGS_H

#include <GL/glew.h>
#include <glm/glm.hpp>

enum class SkyboxType {
	Atmospheric = 0,
	Universe = 1
};

struct GraphicsSettings {
	// SSAO
	bool ssaoEnabled = true;
	float ssaoRadius = 0.5f;
	float ssaoBias = 0.025f;
	float ssaoIntensity = 1.5f;
	int ssaoKernelSize = 64;   // 1-64
	int ssaoBlurSize = 4;      // 2, 4, 6, 8

	// Culling & LOD Distances (Base values, before multipliers)
	float lod0Distance = 50.0f;    // Full detail within 50 units
	float lod1Distance = 150.0f;   // 50% detail at 50-150 units
	float lod2Distance = 400.0f;   // 25% detail at 150-400 units
	float renderDistance = 2000.0f; // Object cut-off
	float shadowDistance = 500.0f;
	int shadowCascades = 2;        // Number of shadow cascades (1-4, fewer = faster)

	// God Rays
	bool godraysEnabled = true;
	float godraysDecay = 0.95f;
	float godraysDensity = 1.0f;

	// Volumetric Sky & Clouds
	bool volumetricSkyEnabled = true;
	bool cloudsEnabled = true;
	float cloudsDensity = 0.5f;
	float cloudsSpeed = 0.05f;
	float cloudsSharpness = 0.3f;

	// Skybox Selection
	SkyboxType skyboxType = SkyboxType::Atmospheric;

	// Universe Settings
	float universeStarDensity = 0.5f;
	float universeStarBrightness = 1.0f;
	float universeNebulaIntensity = 0.5f;
	float universeSpeed = 0.01f;
	glm::vec3 universeNebulaColor1 = glm::vec3(0.5f, 0.2f, 0.8f);
	glm::vec3 universeNebulaColor2 = glm::vec3(0.1f, 0.5f, 0.9f);


	// Debug Tools
	bool debugLODColoring = false;
	bool debugShowBounds = false;
	bool debugFreezeCulling = false;
	bool showWireframe = false;
	bool enableOcclusionCulling = true;
	bool debugShowHiZ = false;
	bool debugShowCulling = false;
};

#endif
