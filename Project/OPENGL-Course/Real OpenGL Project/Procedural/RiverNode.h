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
	explicit RiverNode(NodeGraph& graph);
	// Carves the rivers and lakes into the terrain; their water surfaces become scene objects
	void Execute(NodeContext& ctx) override;

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
	float lakeDepth = 4.0f; ///< Lake depth in meters away from the shore (bed is dug out; extent follows the terrain).

	/**
	 * @brief Recomputes vertex normal vectors after river beds are carved.
	 */
	void RecomputeNormals(MeshData& data, int gridRes);
};
