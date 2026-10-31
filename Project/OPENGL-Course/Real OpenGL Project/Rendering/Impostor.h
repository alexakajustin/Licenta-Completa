#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "Rendering/Shader.h"

class InstancedGroup;
class Mesh;
struct GraphicsSettings;

// =====================================================================
// Octahedral impostors for distant trees
//
// A tree's last LOD mesh loses most of its leaf cards. Instead, the full LOD0 (every group of the
// tree: bark, leaves) is baked once from 8 x 8 directions over the upper hemisphere into an
// atlas, and from the LOD2 distance on each tree is drawn as one camera-facing card that blends
// the three baked views closest to the view direction (Brucks, "Octahedral Impostors", 2018;
// Unreal's Impostor Baker, Fortnite). The atlas stores albedo + coverage, normals + depth and
// leaf/bark, so the card is lit live (sunset colour, cascaded shadows, crown occlusion) like the meshes.
//
//   - Two triangles instead of thousands per tree, and they keep the whole crown.
//   - The shadow cascades draw the same cards facing the light.
//   - Coverage is dithered instead of alpha-tested: a far crown keeps its gaps like the meshes.
// =====================================================================

struct ImpostorAtlas
{
	GLuint albedo = 0;          // rgb albedo, a coverage (mips: average coverage)
	GLuint data = 0;            // rgb mesh-space normal * 0.5 + 0.5, a depth toward the viewer
	GLuint material = 0;        // r: 1 leaves, 0 bark (each keeps its own lighting model)
	int frames = 8;             // Views per side of the hemi-octahedral grid
	int resolution = 0;         // Atlas size (frames * view size)
	glm::vec3 center = glm::vec3(0.0f); // Bounding sphere of the tree (mesh space)
	float radius = 1.0f;        // Half-size of a baked view (mesh space)
	float foliageRadius = 0.0f; // Leaves mesh radius: crown occlusion scale (shader.frag)
	glm::vec3 boundsMin = glm::vec3(-1.0f), boundsMax = glm::vec3(1.0f); // Box of the tree: the card covers its outline
	Mesh* quad = nullptr;       // Card: corners in [-1, 1]^2
	Shader* shader = nullptr;   // Camera passes (impostor.vert + impostor.frag)
	Shader* shadowShader = nullptr; // Directional shadow cascades (impostor.vert + impostor_shadow.frag)

	// Atlas, frame layout and view of a pass (orthographic: every ray along viewDir, toward the viewer).
	// pixelScale: screen pixels of one unit of radius at distance 1 (0 = always blend three views)
	void Bind(GLuint program, const glm::mat4& viewProj, const glm::vec3& viewPos,
		const glm::vec3& viewDir, bool orthographic, float pixelScale) const;
};

class ImpostorSystem
{
public:
	~ImpostorSystem();

	// Every frame before the shadow passes: finds the trees among the instanced groups (groups of
	// one scatter sharing their instances), bakes the atlases still missing (one per frame) and
	// tells every group whether and how it takes part. Frees the atlases of trees that are gone.
	void Update(std::vector<InstancedGroup*>& groups, const GraphicsSettings* gs);

	// Camera-pass shader: the scene binds its lights and shadows like a material shader's
	Shader* GetShader() { return initialized ? &shader : nullptr; }

	void Release();

private:
	bool Init();
	ImpostorAtlas* Bake(const std::vector<InstancedGroup*>& members, const glm::vec3& boundsMin,
		const glm::vec3& boundsMax, int resolution);

	std::map<std::string, ImpostorAtlas*> atlases; // By what the bake depends on (meshes, textures, size)
	std::set<std::string> failed;                  // Bakes that produced nothing: not retried
	Shader shader, shadowShader, bakeShader, resolveShader;
	Mesh* quad = nullptr;
	bool initialized = false;
	bool initFailed = false;
};
