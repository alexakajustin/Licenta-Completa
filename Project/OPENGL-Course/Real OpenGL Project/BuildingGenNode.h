#pragma once

#include "NodeGraph.h"
#include "SceneManager.h"
#include "imgui.h"
#include <string>
#include <vector>

// =====================================================================
// BuildingGenNode — Procedural Building Generator (3DWorld-inspired)
//
// Generates multi-part buildings with varied shapes:
//   - Base section (full footprint)
//   - Optional upper section (narrower, offset)
//   - Peaked gable roof (residential) or flat roof (commercial)
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
	float minHeight = 5.0f;
	float maxHeight = 25.0f;
	float floorHeight = 3.0f;
	float wallInset = 0.5f;
	float roofOverhang = 0.3f;
	float upperSectionProb = 0.5f;    // Probability of a second section
	float upperSectionScale = 0.65f;  // How much smaller the upper section is
	int seed = 42;

	// Mesh helpers — uses engine cube primitive
	MeshData MakeCubePart(glm::vec3 center, glm::vec3 halfExtents);
	void AddPeakedRoof(MeshData& mesh, glm::vec3 center, float halfW, float halfD, float peakH, bool dimX);
	void AddFlatRoof(MeshData& mesh, glm::vec3 base, float halfW, float halfD, float thickness);
};
