#pragma once

#include "NodeGraph.h"
#include "SceneManager.h"
#include "imgui.h"
#include <string>
#include <vector>

// =====================================================================
// BuildingGenNode — Procedural Building Generator
//
// Consumes a TransformList of plot positions (from CityGridNode) and
// generates procedural box buildings with randomized heights, textures,
// and roof styles.
//
// Inputs:
//   [0] Plots (TransformList) — building positions + sizes from CityGridNode
//
// Outputs:
//   (none — spawns GameObjects directly)
// =====================================================================

class BuildingGenNode : public GraphNode
{
public:
	BuildingGenNode(NodeGraph& graph);

	void RenderContent(SceneManager* scene) override;
	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override;

	json Serialize() const override;
	void Deserialize(const json& j) override;

private:
	// Generation parameters
	float minHeight = 5.0f;           // Minimum building height
	float maxHeight = 30.0f;          // Maximum building height
	float floorHeight = 3.0f;         // Height per floor (for texture tiling)
	float wallInset = 0.5f;           // Shrink from plot edge
	float roofOverhang = 0.2f;        // Roof extends beyond walls
	float roofThickness = 0.3f;       // Flat roof slab thickness
	int seed = 42;                    // Random seed

	// Mesh generation
	void BuildBoxMesh(MeshData& mesh, glm::vec3 center, glm::vec3 halfExtents, float texTilingU, float texTilingV);
	void BuildRoofMesh(MeshData& mesh, glm::vec3 center, glm::vec3 halfExtents);
};
