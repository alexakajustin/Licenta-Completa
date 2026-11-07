#pragma once

#include "Nodes/NodeGraph.h"
#include <string>

// Droplet-based hydraulic erosion (Beyer 2015, see Procedural/DropletErosion.h) with thermal
// erosion settling the slopes, over the heights of an incoming grid mesh
class HydraulicErosionNode : public GraphNode
{
public:
	explicit HydraulicErosionNode(NodeGraph& graph);
	void Execute(NodeContext& ctx) override;
	void LoadState(const json& j) override;

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
	void SetSimulationCells(int cells) { simulationCells = cells; }

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
	int simulationCells = 512;     ///< The erosion's grid (cells per side): a finer terrain is eroded at it, the change interpolated. 0 = the terrain's own.
	int seed;                      ///< Where the droplets fall (saves from before it existed: 42).

	/**
	 * @brief Helper to rebuild normal vectors across the processed grid resolution.
	 */
	void RecomputeNormals(MeshData& data, int resolutionX, int resolutionZ);
};
