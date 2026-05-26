#pragma once

#include "Nodes/NodeGraph.h"
#include <string>

// Simulates fluid dynamics (hydraulic erosion) over the Y channels of the incoming MeshData stream.
/**
 * @class HydraulicErosionNode
 * @brief Simulates fluid dynamics (hydraulic erosion) over the height coordinates of an incoming MeshData grid.
 */
class HydraulicErosionNode : public GraphNode
{
public:
	/**
	 * @brief Constructor.
	 * @param graph NodeGraph that owns this node.
	 */
	HydraulicErosionNode(NodeGraph& graph);

	/**
	 * @brief Renders the editor UI panel for configuring simulation parameters.
	 * @param scene Pointer to active scene manager.
	 */
	void RenderContent(SceneManager* scene) override;

	/**
	 * @brief Executes the hydraulic erosion simulation in-place on the connected mesh data.
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

	void SetSteps(int s) { simulationSteps = s; }
	void SetRainRate(float r) { rainRate = r; }
	void SetKs(float k) { dissolvingConstant = k; }
	void SetKd(float k) { depositionConstant = k; }
	void SetMaxDelta(float m) { maxDelta = m; }
	void SetSedimentCapacity(float c) { sedimentCapacity = c; }
	void SetEvaporation(float e) { evaporationConstant = e; }
	void SetSmoothPasses(int p) { smoothPasses = p; }

private:
	int simulationSteps = 60; ///< Total simulation timesteps or iteration iterations.
	float rainRate = 0.03f; ///< Liquid rain deposition volume per iteration.
	float sedimentCapacity = 30.0f; ///< Maximum carrying capacity of soil per unit volume of water.
	float dissolvingConstant = 0.03f; ///< Dissolving constant (Ks) representing how easily soil dissolves.
	float depositionConstant = 0.02f; ///< Deposition constant (Kd) representing how easily sediment deposits.
	float evaporationConstant = 0.0002f; ///< Water evaporation rate per step.
	float maxDelta = 1.0f; ///< Limit constraint on maximum soil height modifications per step.
	int smoothPasses = 0; ///< Post-erosion smoothing passes.

	/**
	 * @brief Helper to rebuild normal vectors across the processed grid resolution.
	 */
	void RecomputeNormals(MeshData& data, int resolutionX, int resolutionZ);
};
