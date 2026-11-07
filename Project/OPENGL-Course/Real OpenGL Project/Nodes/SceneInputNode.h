#pragma once

#include "Nodes/NodeGraph.h"
#include "Rendering/TextureLayer.h"
#include <string>
#include <vector>

class Texture;
class Material;

// An object's geometry and its children's, in the object's frame with its rotation and scale baked
// in (relative to its world position), plus its look. originalPrimitives: planes, cubes and
// spheres as fresh primitives rather than the mesh a graph last generated into them; a fresh plane
// has planeCells cells along each side.
MeshValue ReadObjectMesh(GameObject* obj, bool originalPrimitives, int planeCells = 512);

// =====================================================================
// Scene Object: one object of the scene, as geometry and as an object reference
//
//  Mesh    its geometry and its children's, rotation and scale baked in (computed only when the
//          output is linked)
//  Object  the object itself, for nodes that place or clone objects (Spawn Instances, Interior
//          Gen...) or test against them (Object Filter)
// If the object is deleted, the mesh is rebuilt from what was cached the last time it was read
// (primitive type or model file, transform, material).
// =====================================================================
class SceneInputNode : public GraphNode
{
public:
	explicit SceneInputNode(NodeGraph& graph);
	~SceneInputNode() override;

	void Execute(NodeContext& ctx) override;
	void SaveState(json& j) const override;
	void LoadState(const json& j) override;
	std::vector<std::string> LegacyOutputKeys() const override { return { "mesh" }; }

	std::string GetSelectedName() const { return selected.empty() ? std::string("(none)") : selected[0]; }
	void SetSelection(int index, const std::string& name) { (void)index; selected = { name }; }
	void SetForceOriginalPrimitive(bool force) { forceOriginalPrimitive = force; }

private:
	std::vector<std::string> selected;
	bool forceOriginalPrimitive = true; // Planes/cubes/spheres as fresh primitives, not their last generated mesh
	int planeCells = 512;               // A fresh plane's cells along each side (a terrain's grid)

	// Fallback cache (the object may be deleted later)
	std::string cachedPrimitiveType;
	std::string cachedModelPath;
	glm::vec3 cachedPosition = glm::vec3(0.0f);
	glm::vec3 cachedRotation = glm::vec3(0.0f);
	glm::vec3 cachedScale = glm::vec3(1.0f);
	std::string cachedTexturePath;
	std::string cachedNormalMapPath;
	std::vector<TextureLayer> cachedTextureLayers;
	bool hasCachedMaterial = false;
	float cachedMatSpecular = 0.5f;
	float cachedMatShininess = 32.0f;
	glm::vec3 cachedMatColor = glm::vec3(1.0f);
	glm::vec2 cachedMatTiling = glm::vec2(1.0f);
	glm::vec2 cachedMatOffset = glm::vec2(0.0f);

	Texture* fallbackTexture = nullptr;
	Texture* fallbackNormalMap = nullptr;
	Material* fallbackMaterial = nullptr;
	std::vector<Texture*> fallbackLayersTextures;
	std::vector<Texture*> fallbackLayersNormals;

	void CleanupFallbacks();
	MeshValue RebuildFromCache();
};
