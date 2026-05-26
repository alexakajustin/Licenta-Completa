#pragma once

#include "Nodes/NodeGraph.h"
#include <string>

// Simulates fluid dynamics (hydraulic erosion) over the Y channels of the incoming MeshData stream.
class HydraulicErosionNode : public GraphNode
{
public:
	HydraulicErosionNode(NodeGraph& graph);

	void RenderContent(SceneManager* scene) override;
	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override;

	json Serialize() const override;
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
	int simulationSteps = 60;
	float rainRate = 0.03f;
	float sedimentCapacity = 30.0f;
	float dissolvingConstant = 0.03f;
	float depositionConstant = 0.02f;
	float evaporationConstant = 0.0002f;
	float maxDelta = 1.0f;
	int smoothPasses = 0; // Post-erosion smoothing passes

	// Internal helper
	void RecomputeNormals(MeshData& data, int resolutionX, int resolutionZ);
};
