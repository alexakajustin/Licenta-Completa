#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>

#include "Rendering/Shader.h"

class GameObject;
class InstancedGroup;
struct GraphicsSettings;

/**
 * @class WaterFoamMap
 * @brief Top-down foam map around the camera: foam where geometry crosses the water surface
 *        and the wakes the current carries downstream.
 *
 * The obstacle foam of river tools such as Waterways (obstacle mask -> distance falloff ->
 * smeared along the flow), rebuilt every frame on the GPU so placed, scattered or moved objects
 * and obstacles outside the view are always included:
 *  1. water surfaces seen from above -> height + flow direction per texel,
 *  2. objects seen from above, keeping only the parts crossing that surface -> obstacle mask,
 *  3. compute -> soft ring around obstacles, then smeared downstream along the flow.
 * The water shaders sample the result (waterFoamMap) and erode a lacy foam pattern
 * (waterFoamPattern, generated once) with it.
 *
 * Over the same window the water is simulated (water_sim.glsl): the current around everything
 * standing in it (banks and objects are walls), the surface's waves carried by it and bouncing
 * off them, and foam carried by the current. The water shaders read the current
 * (waterSimVelocity), the surface (waterSimWave) and the foam (waterSimFoam).
 */
class WaterFoamMap
{
public:
	~WaterFoamMap();

	/**
	 * @brief Rebuilds the map around the camera. Call once per frame (main view) before the
	 *        water is drawn; restores the framebuffer, viewport and render state it changes.
	 */
	void Update(const std::vector<GameObject*>& waterObjects, const std::vector<GameObject*>& obstacleObjects,
		const std::vector<GameObject*>& groundObjects,
		const std::vector<InstancedGroup*>& groups, GLuint cullShaderID,
		const glm::vec3& cameraPos, const GraphicsSettings* gs, float time);

	/** @brief Binds the foam map and pattern for a shader that uses them (no-op otherwise). */
	void Bind(GLuint shaderID) const;

	// Inspection (debug views, tests)
	bool IsValid() const { return valid; }
	glm::vec2 GetOrigin() const { return origin; }
	GLuint GetSurfaceTexture() const { return surfaceTex; }
	GLuint GetObstacleTexture() const { return obstacleTex; }
	GLuint GetFoamTexture() const { return foamTex; }
	GLuint GetSimVelocityTexture() const { return velTex[cur]; }
	GLuint GetSimWaveTexture() const { return waveTex[cur]; }
	GLuint GetSimFoamTexture() const { return simFoamTex[cur]; }
	GLuint GetSimDomainTexture() const { return domainTex; }
	static constexpr int GetResolution() { return RESOLUTION; }
	static constexpr float GetWindow() { return WINDOW; }

private:
	bool Init();
	void Release();
	void Simulate(float dt);

	static constexpr int RESOLUTION = 512;   // Texels per side
	static constexpr float WINDOW = 128.0f;  // World metres per side, centred on the camera

	bool initialised = false;
	bool failed = false;
	bool valid = false;                      // Map holds this frame's foam

	GLuint surfaceFBO = 0, surfaceTex = 0, surfaceDepth = 0;
	GLuint obstacleFBO = 0, obstacleTex = 0;
	GLuint proximityTex = 0, foamTex = 0;
	GLuint patternTex = 0;
	GLuint rippleTex = 0;                    // Tileable ripple slopes (water_ripple_normal.glsl)
	glm::vec2 origin = glm::vec2(0.0f);      // World xz of the window's corner

	Shader surfaceShader;
	Shader obstacleShader;
	Shader obstacleInstancedShader;
	Shader maskShader;
	Shader patternShader;
	Shader rippleShader;

	// Water simulation (water_sim.glsl): ground seen from above, the domain (water / walls,
	// the river's current, depth), and the fields, two of each (last frame's, this frame's)
	GLuint groundFBO = 0, groundTex = 0, groundDepth = 0;
	GLuint domainTex = 0, velTmpTex = 0, curlTex = 0, divTex = 0;
	GLuint velTex[2] = {}, pressureTex[2] = {}, waveTex[2] = {}, simFoamTex[2] = {};
	int cur = 0;                             // This frame's fields
	bool simHistory = false;                 // Last frame's fields hold a simulation
	float lastTime = -1.0f;
	glm::vec2 prevOrigin = glm::vec2(0.0f);
	Shader simShader;
};
