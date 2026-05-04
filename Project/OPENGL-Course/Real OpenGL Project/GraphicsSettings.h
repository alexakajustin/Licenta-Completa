#ifndef GRAPHICS_SETTINGS_H
#define GRAPHICS_SETTINGS_H

#include <GL/glew.h>

struct GraphicsSettings {
	// SSAO
	bool ssaoEnabled = true;
	float ssaoRadius = 0.5f;
	float ssaoBias = 0.025f;
	float ssaoIntensity = 1.5f;
	int ssaoKernelSize = 64;   // 1-64
	int ssaoBlurSize = 4;      // 2, 4, 6, 8

	// Culling & LOD Distances (Base values, before multipliers)
	float lod0Distance = 200.0f;
	float lod1Distance = 400.0f;
	float lod2Distance = 800.0f;
	float renderDistance = 2000.0f; // Object cut-off (Trees/Grass)
	float shadowDistance = 200.0f;

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


	// Debug Tools
	bool debugLODColoring = false;
	bool debugShowBounds = false;
	bool debugFreezeCulling = false;
	bool showWireframe = false;
	bool enableOcclusionCulling = true;
	bool debugShowHiZ = false;
};

#endif
