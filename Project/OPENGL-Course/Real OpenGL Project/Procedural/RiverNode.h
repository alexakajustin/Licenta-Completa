#pragma once

#include "Nodes/NodeGraph.h"
#include <glm/glm.hpp>
#include <vector>

// Carves automatic, natural river systems into terrain.
// Input: Mesh, Output: Mesh
/**
 * @class RiverNode
 * @brief Carves automatic, natural river systems into terrain based on height gradients and water flow simulations.
 */
class RiverNode : public GraphNode
{
public:
	/**
	 * @brief Constructor.
	 * @param graph NodeGraph that owns this node.
	 */
	RiverNode(NodeGraph& graph);

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

	/**
	 * @brief Renders the editor UI panel for configuring river paths, spring counts, widths, and depths.
	 * @param scene Pointer to active scene manager.
	 */
	void RenderContent(SceneManager* scene) override;

	/**
	 * @brief Executes the river carving algorithm, editing the height coordinates of the terrain.
	 * @param scene Active scene manager.
	 * @param progress Callback for updating execution progress.
	 */
	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override;

	void SetBaseDepth(float d) { baseDepth = d; }
	void SetBaseWidth(float w) { baseWidth = w; }
	void SetSmoothPasses(int p) { smoothPasses = p; }

private:
	// Parameters
	int springCount = 5; ///< Number of random water springs spawned on the terrain.
	int maxSteps = 500; ///< Maximum step distance for a river stream path.
	float baseDepth = 0.08f; ///< Base carving depth of the river.
	float baseWidth = 15.0f; ///< Base carving width of the river.
	float riverWaterOffset = -0.005f; ///< Vertical offset constraint for river water meshes.
	float lakeWaterOffset = -0.005f; ///< Vertical offset constraint for lake water meshes.
	int smoothPasses = 8; ///< Post-carving height smoothing passes.
	float lakeVolumeMultiplier = 500.0f; ///< Volume scale factor for flat lake regions.

	/**
	 * @brief Recomputes vertex normal vectors after river beds are carved.
	 */
	void RecomputeNormals(MeshData& data, int gridRes);
};
