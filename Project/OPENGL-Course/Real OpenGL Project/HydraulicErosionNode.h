#pragma once

#include "NodeGraph.h"
#include <string>

// Simulates fluid dynamics (hydraulic erosion) over the Y channels of the incoming MeshData stream.
class HydraulicErosionNode : public GraphNode
{
public:
	HydraulicErosionNode(NodeGraph& graph);

	void RenderContent(SceneManager* scene) override;
	void Execute(SceneManager& scene) override;

	json Serialize() const override;
	void Deserialize(const json& j) override;

	void SetSteps(int s) { simulationSteps = s; }
	void SetRainRate(float r) { rainRate = r; }
	void SetKs(float k) { dissolvingConstant = k; }
	void SetKd(float k) { depositionConstant = k; }
	void SetMaxDelta(float m) { maxDelta = m; }

private:
	int simulationSteps = 10;
	float rainRate = 0.01f;
	float sedimentCapacity = 25.0f;
	float dissolvingConstant = 0.012f;
	float depositionConstant = 0.012f;
	float evaporationConstant = 0.000055f;
	float maxDelta = 0.2f;

	// Internal helper
	void RecomputeNormals(MeshData& data, int resolutionX, int resolutionZ);
};
