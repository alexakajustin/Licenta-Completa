#pragma once

#include "NodeGraph.h"
#include <glm/glm.hpp>
#include <vector>

// Carves automatic, natural river systems into terrain.
// Input: Mesh, Output: Mesh
class RiverNode : public GraphNode
{
public:
	RiverNode(NodeGraph& graph);

	json Serialize() const override;
	void Deserialize(const json& j) override;

	void RenderContent(SceneManager* scene) override;
	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override;

	void SetBaseDepth(float d) { baseDepth = d; }
	void SetBaseWidth(float w) { baseWidth = w; }
	void SetSmoothPasses(int p) { smoothPasses = p; }

private:
	// Parameters
	int springCount = 5;
	int maxSteps = 500;
	float baseDepth = 0.08f;
	float baseWidth = 15.0f;
	float waterOffset = -0.005f; // Negative offset to hide jagged mesh edges inside the terrain bank
	int smoothPasses = 8;
	float lakeVolumeMultiplier = 500.0f;


	// Internal helper for normal recomputation
	void RecomputeNormals(MeshData& data, int gridRes);
};
