#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <cstdint>

#include "GraphicsSettings.h"
#include "Rendering/Shader.h"

class SceneManager;
class DirectionalLight;
class PointLight;
class SpotLight;
class Texture;
class GameObject;

// Uniform block binding point of GIBlock (gi_block.glsl; Shader::CompileProgram binds it) and the
// shader storage binding of the probes (gi.glsl)
constexpr GLuint GI_UBO_BINDING = 8;
constexpr GLuint GI_PROBE_SSBO_BINDING = 10;
constexpr size_t GI_PROBE_BYTES = 128; // std430 GIProbe (gi_block.glsl)

// Dynamic global illumination by software ray tracing: no ray tracing hardware, only compute
// shaders (OpenGL 4.3), so it runs on any GPU, integrated ones included (quality presets).
//
// 1. The scene, voxelized: a few nested voxel grids ("levels", each twice as coarse as the one
//    inside it) centred on the camera hold each surface's opacity, colour and normal
//    (gi_voxelize.*). A level is voxelized again when the camera has moved far enough for it to
//    re-centre, or when the scene changed (objects moved or were added), one level per frame.
// 2. The voxels are lit (gi_inject.glsl): sun with its shadow cascades and the cloud shadows,
//    point and spot lights with their cube shadow maps, plus the probes' light from last frame,
//    so light keeps bouncing.
// 3. Probes (gi_probes.glsl, DDGI by Majercik et al. 2019): two grids around the camera, fine and
//    coarse, trace rays through the voxels a slice at a time and keep the light that reaches
//    them for six directions (an ambient cube) and how far they see (leak prevention). One more
//    probe sees only the sky: the light beyond the grids.
// 4. The lit shaders replace the flat ambient terms (the sun's, the point / spot lights') with the
//    probes' light (gi.glsl, GIAmbient): shade under trees and indoors, light bounced off sunlit
//    ground, colours bleeding over.
class GlobalIllumination
{
public:
	void Init();
	void Shutdown();

	// Every frame after the shadow maps (the voxels' sunlight uses them) and before the scene.
	// skyCubemap: what rays that get out see (its colours shape the ambient light).
	void Update(SceneManager& scene, const glm::vec3& cameraPos, const glm::mat4& view,
		DirectionalLight& sun, PointLight* pointLights, unsigned int pointLightCount,
		SpotLight* spotLights, unsigned int spotLightCount,
		const GraphicsSettings& gs, GLuint cullShaderID, GLuint skyCubemap);

	// Scene loaded or rebuilt: voxelize everything again and forget the probes
	void Invalidate();

	// Inspection (tests, debugging)
	int GetResolution() const { return config.resolution; }
	int GetLevelCount() const { return config.levels; }
	int GetValidLevels() const;
	GLuint GetVoxelData() const { return voxelData; }
	GLuint GetVoxelRadiance() const { return voxelRadiance; }
	GLuint GetProbeBuffer() const { return probeBuffer; }
	glm::vec3 GetLevelMin(int level) const { return levels[level].min; }
	float GetVoxelSize(int level) const { return config.voxelSize * float(1 << level); }
	// GPU time of the last measured frame (ms): voxelization (with its lighting), re-lighting, probes
	float GetVoxelizeMs() const { return stageMs[0]; }
	float GetInjectMs() const { return stageMs[1]; }
	float GetProbesMs() const { return stageMs[2]; }

private:
	struct Config {
		int quality = -1;
		int resolution = 0;       // Voxels along each axis of a level
		int levels = 0;
		float voxelSize = 0.5f;   // Finest level (m)
		glm::ivec3 probes = glm::ivec3(0);
		float spacing[2] = { 2.0f, 8.0f };
		int rays = 64;            // Per probe update (<= 64)
		int slices = 4;           // A probe is updated every 'slices' frames
	};
	struct Level {
		glm::vec3 min = glm::vec3(0.0f);
		bool valid = false;
		bool dirty = true;
	};
	struct Block {
		glm::vec4 spacing;  // near, far, intensity, on
		glm::ivec4 base0, base1;
		glm::ivec4 counts;  // xyz, probes per cascade
		glm::ivec4 slot0, slot1; // Slots of the windows' first probes (base mod counts)
	};

	static Config MakeConfig(int quality);
	void Allocate(const Config& c);
	void Release();
	glm::vec3 DesiredLevelMin(int level, const glm::vec3& cameraPos) const;
	uint64_t SceneSignature(SceneManager& scene);
	void Voxelize(int level, SceneManager& scene, const glm::vec3& cameraPos, const GraphicsSettings& gs, GLuint cullShaderID);
	void Inject(int level, bool writeEmpty, const glm::vec3& cameraPos, const glm::mat4& view, DirectionalLight& sun,
		PointLight* pointLights, unsigned int pointLightCount, SpotLight* spotLights, unsigned int spotLightCount,
		const GraphicsSettings& gs);
	void UpdateProbes(const GraphicsSettings& gs, DirectionalLight& sun, GLuint skyCubemap);
	glm::vec3 TextureAverage(Texture* texture);
	glm::vec3 SurfaceAlbedo(GameObject* obj);

	Config config;
	Shader voxelizeShader, injectShader, probeShader;
	GLuint voxelData = 0;      // r32ui: opacity | rgb565 | octahedral normal, levels stacked along z
	GLuint voxelRadiance = 0;  // rgba16f: lit colour, opacity
	GLuint voxelBricks = 0;    // r8ui, a quarter of the size: 1 per 4^3 block holding anything
	GLuint voxelFBO = 0;       // No attachments: the voxelization only writes images
	GLuint probeBuffer = 0;
	GLuint ubo = 0;
	GLuint skySampler = 0;     // Trilinear: the probes read the sky's blurred mip levels
	std::vector<Level> levels;
	uint64_t sceneSignature = 0;
	int dirtyCursor = 0, injectCursor = 0;
	uint32_t frame = 0;
	// Frames left of the warm-up after a reset (scene loaded, GI turned on): every probe traced
	// and every level re-lit each frame, so the light settles (bounces included) in about a second
	int warmupFrames = 0;
	static constexpr int WARMUP_FRAMES = 60;
	bool enabledLastFrame = false;
	GraphicsSettings voxelSettings; // The culler's settings for the voxel levels (cheapest LOD, no impostors)
	std::unordered_map<GLuint, glm::vec3> textureAverages;
	// GPU timestamps around the stages, read a few frames later (no stall)
	GLuint timestamps[3][4] = {};
	bool timestampsIssued[3] = {};
	int timestampFrame = 0;
	float stageMs[3] = {};
	void ReadTimestamps();
};
