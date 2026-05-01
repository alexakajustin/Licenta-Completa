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

private:
	// Parameters
	int springCount = 5;
	int maxSteps = 500;
	float baseDepth = 4.0f;
	float baseWidth = 5.0f;
	float lakeSize = 4.0f;
	float waterOffset = 0.5f;
	int smoothPasses = 4;

	// Internal helper for normal recomputation
	void RecomputeNormals(MeshData& data, int gridRes);
};
