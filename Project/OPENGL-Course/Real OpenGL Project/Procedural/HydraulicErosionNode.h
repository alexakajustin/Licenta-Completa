#pragma once

#include "Nodes/NodeGraph.h"
#include <string>

// Simulates fluid dynamics (hydraulic erosion) over the Y channels of the incoming MeshData stream.
/**
 * @class HydraulicErosionNode
 * @brief Droplet-based hydraulic erosion (Beyer 2015, see Procedural/DropletErosion.h) over the
 *        height coordinates of an incoming MeshData grid.
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

	void SetDropletsPerCell(float d) { dropletsPerCell = d; }
	void SetErodeSpeed(float e) { erodeSpeed = e; }
	void SetDepositSpeed(float d) { depositSpeed = d; }
	void SetSedimentCapacity(float c) { sedimentCapacity = c; }
	void SetEvaporation(float e) { evaporation = e; }
	void SetBrushRadius(int r) { brushRadius = r; }
	void SetInertia(float i) { inertia = i; }
	void SetSmoothPasses(int p) { smoothPasses = p; }
	void SetTalusAngle(float a) { talusAngle = a; }
	void SetThermalIterations(int i) { thermalIterations = i; }
	void SetEdgeFade(float cells) { edgeFadeCells = cells; }

private:
	float dropletsPerCell = 8.0f;  ///< Droplets simulated per heightmap cell (scales with resolution).
	float erodeSpeed = 0.4f;       ///< Fraction of the free sediment capacity eroded per step.
	float depositSpeed = 0.3f;     ///< Fraction of the excess sediment deposited per step.
	float sedimentCapacity = 6.0f; ///< Sediment carried per unit of slope * speed * water.
	float evaporation = 0.01f;     ///< Water lost per step.
	int brushRadius = 3;           ///< Erosion brush radius in cells (wider = smoother gullies).
	float inertia = 0.05f;         ///< How much droplets keep their direction instead of following the slope.
	int smoothPasses = 0;          ///< Post-erosion smoothing passes.
	float talusAngle = 35.0f;      ///< Thermal erosion: steepest stable slope (deg); steeper material slides down.
	int thermalIterations = 20;    ///< Thermal relaxation per erosion round (x3 before, x1 after). 0 = off.
	float edgeFadeCells = 40.0f;   ///< Erosion fades out over this many cells at the map border.

	/**
	 * @brief Helper to rebuild normal vectors across the processed grid resolution.
	 */
	void RecomputeNormals(MeshData& data, int resolutionX, int resolutionZ);
};
