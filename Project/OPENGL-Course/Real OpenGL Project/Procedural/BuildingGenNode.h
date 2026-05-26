#pragma once

#include "Nodes/NodeGraph.h"
#include "Scene/SceneManager.h"
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

/**
 * @class BuildingGenNode
 * @brief Procedural building generator node which generates multi-part buildings with customizable roofs, height variations, and floor configurations.
 */
class BuildingGenNode : public GraphNode
{
public:
	/**
	 * @brief Constructor.
	 * @param graph NodeGraph that owns this node.
	 */
	BuildingGenNode(NodeGraph& graph);

	/**
	 * @brief Renders the editor UI panel for configuring building dimensions, section probabilities, and random seeds.
	 * @param scene Pointer to active scene manager.
	 */
	void RenderContent(SceneManager* scene) override;

	/**
	 * @brief Executes the building placement generator, creating GameObject instances directly within the active scene.
	 * @param scene Active scene manager.
	 * @param progress Callback for updating execution progress.
	 */
	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override;

	/**
	 * @brief Serializes node properties to JSON.
	 * @return JSON object.
	 */
	json Serialize() const override;

	/**
	 * @brief Deserializes node properties from JSON.
	 * @param j JSON object.
	 */
	void Deserialize(const json& j) override;

private:
	// Generation parameters
	float minHeight = 5.0f; ///< Minimum overall height of buildings.
	float maxHeight = 25.0f; ///< Maximum overall height of buildings.
	float floorHeight = 3.0f; ///< Height of a single building floor.
	float wallInset = 0.5f; ///< Wall offset inset margin.
	float roofOverhang = 0.3f; ///< Overhang distance for roofs.
	float upperSectionProb = 0.5f;    ///< Probability of generating an upper offset section.
	float upperSectionScale = 0.65f;  ///< Scale factor of the upper section relative to the base.
	int seed = 42; ///< Seed for procedural random determinations.

	// Mesh helpers — uses engine cube primitive
	
	/**
	 * @brief Generates vertices for a single cube section.
	 */
	MeshData MakeCubePart(glm::vec3 center, glm::vec3 halfExtents);

	/**
	 * @brief Generates peaked gable roof geometry.
	 */
	void AddPeakedRoof(MeshData& mesh, glm::vec3 center, float halfW, float halfD, float peakH, bool dimX);

	/**
	 * @brief Generates flat roof geometry with optional thickness.
	 */
	void AddFlatRoof(MeshData& mesh, glm::vec3 base, float halfW, float halfD, float thickness);
};
