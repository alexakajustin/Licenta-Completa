#ifndef GRAPHICS_SETTINGS_H
#define GRAPHICS_SETTINGS_H

#include <GL/glew.h>
#include <glm/glm.hpp>

enum class SkyboxType {
	Atmospheric = 0,
	Universe = 1
};

struct GraphicsSettings {
	// Level of detail by distance to the camera (Rendering/LODPolicy.h): LOD0, LOD1, LOD2 each end
	// at their distance, then the impostor (trees). A stage switched off is skipped: the next one
	// starts where the previous ends (LOD2 off: LOD1, then the impostor). The last stage in use
	// reaches the render distance.
	bool lodEnabled[3] = { true, true, false };
	float lodDistance[3] = { 100.0f, 200.0f, 400.0f }; // Where LOD0 / LOD1 / LOD2 end (m)
	bool lodCrossfade = true;       // Dithered transitions between LODs instead of pops
	bool taaEnabled = true;         // Temporal anti-aliasing (Rendering/TemporalAA.h)
	float taaSharpness = 0.5f;      // Contrast-adaptive sharpening of the TAA result (0-1)
	float renderDistance = 2000.0f; // Object cut-off (big objects are drawn up to 10x further)
	float detailDistance = 150.0f;  // Grass / detail foliage thins out to a quarter by this distance
	float detailDensity = 1.0f;     // Fraction of the detail foliage drawn at all
	bool impostors = true;          // Last stage: trees as one card baked from the full tree (Impostor.h)
	int impostorResolution = 2048;  // Atlas size per tree type (8 x 8 views)

	// Wind (Rendering/Wind.h): grass, plants, trees, impostors, the lake, the fog and the clouds
	float windSpeed = 4.0f;         // m/s (Beaufort: 4 = gentle breeze, 10 = fresh, 17 = gale)
	float windDirection = 30.0f;    // Degrees in the xz plane the wind blows TOWARD (0 = +X, 90 = +Z)
	float windGustiness = 0.5f;     // 0 = steady, 1 = strong gusts sweeping across the land
	float windTurbulence = 0.4f;    // Small, fast swirls on top of the gusts
	float windTreeSway = 1.0f;      // Response multipliers: trunks and branches
	float windLeafFlutter = 1.0f;   //                       leaves
	float windGrassBend = 1.0f;     //                       grass

	// Water (water.frag, river.frag)
	float waterDetail = 1.5f;       // How far the fine ripples / foam bubbles reach (1 = until they would shimmer)

	// Shadows (cascaded shadow maps)
	float shadowDistance = 150.0f;
	int shadowCascades = 4;           // 1-4: more cascades = sharper shadows near the camera
	int shadowResolution = 2048;      // Per cascade
	float shadowCascadeSplit = 0.75f; // 0 = even splits, 1 = logarithmic (sharper near the camera)
	int shadowLodBias = 1;            // Each further cascade may use one cheaper LOD (0 = the camera's LOD)
	float detailShadowDistance = 25.0f; // Grass casts shadows only this close

	// God Rays
	bool godraysEnabled = true;
	float godraysDecay = 0.95f;
	float godraysDensity = 1.0f;

	// Volumetric Fog (froxel grid, sun-lit with cascaded shadows)
	bool volumetricFogEnabled = true;
	int fogQuality = 1;                 // 0 Low (16 px tiles, 64 slices), 1 Medium (8 px, 64), 2 High (8 px, 128)
	float fogDensity = 0.01f;           // Extinction per metre at/below the base height
	float fogBaseHeight = 272.0f;       // World height below which the fog is at full density (Ultimate_Scene lake level)
	float fogHeightFalloff = 0.04f;     // Exponential density falloff per metre above the base height
	float fogRange = 350.0f;            // View distance covered by the froxel grid
	float fogAnisotropy = 0.4f;         // Henyey-Greenstein g: forward scattering = glow and shafts toward the sun
	glm::vec3 fogAlbedo = glm::vec3(0.92f, 0.94f, 1.0f);
	float fogSunIntensity = 0.25f;      // Scales the sun light scattered by the fog
	float fogAmbientIntensity = 0.7f;   // Scales the sky light scattered by the fog
	float fogNoiseStrength = 0.6f;      // 0 = uniform fog, 1 = patchy wisps
	float fogNoiseScale = 0.025f;       // Noise repeats per metre
	float fogWindSpeed = 1.2f;          // Metres per second the wisps drift
	float fogTemporalBlend = 0.1f;      // Weight of the new frame (lower = smoother, more ghosting)

	// Volumetric Sky & Clouds
	bool volumetricSkyEnabled = true;
	bool cloudsEnabled = true;
	float cloudsDensity = 0.5f;
	float cloudsSpeed = 0.05f;
	float cloudsSharpness = 0.3f;
	bool cloudShadowsEnabled = true;   // The clouds shade the ground (Rendering/CloudShadows.h)
	float cloudShadowStrength = 0.9f;  // How much sun a thick cloud takes away

	// Day/Night transition colors
	glm::vec3 zenithDay = glm::vec3(0.05f, 0.15f, 0.5f);
	glm::vec3 horizonDay = glm::vec3(0.3f, 0.5f, 0.8f);
	glm::vec3 zenithSunset = glm::vec3(0.02f, 0.02f, 0.1f);
	glm::vec3 horizonSunset = glm::vec3(0.6f, 0.2f, 0.05f);
	glm::vec3 zenithNight = glm::vec3(0.005f, 0.005f, 0.01f);
	glm::vec3 horizonNight = glm::vec3(0.01f, 0.01f, 0.02f);

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
	bool enableOcclusionCulling = false; // Disabled: counterproductive with static batching
	bool debugShowHiZ = false;
	bool debugShowCulling = false;
};

#endif
