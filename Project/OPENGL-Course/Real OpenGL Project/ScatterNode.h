#pragma once

#include "NodeGraph.h"
#include "imgui.h"
#include <cmath>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Scatters instances of an object mesh across a surface mesh.
// Inputs: Surface (Mesh), Object (Mesh)
// Outputs: Combined (Mesh) — all instances merged into one mesh
class ScatterNode : public GraphNode
{
public:
	ScatterNode(NodeGraph& graph)
	{
		id = graph.NextNodeId();
		title = "Scatter";

		// Inputs
		Pin surfaceIn(graph.NextPinId(), PinDataType::Mesh, "Surface");
		Pin objectIn(graph.NextPinId(), PinDataType::Mesh, "Object");
		inputs.push_back(surfaceIn);
		inputs.push_back(objectIn);

		// Outputs
		Pin meshOut(graph.NextPinId(), PinDataType::Mesh, "Combined");
		outputs.push_back(meshOut);
	}

	void RenderContent(SceneManager* scene) override;
	void Execute(SceneManager& scene) override;

private:
	int count = 50;
	float minScale = 0.8f;
	float maxScale = 1.2f;
	bool randomRotation = true;
	bool alignToNormal = true;
	int seed = 42;

	// In-place modification mode
	bool spawnAsObjects = false;
	std::string targetParentName = "(none)";
	int targetParentIndex = -1;

	// Persistence for spawned objects (to cleanup/replace)
	std::vector<std::string> spawnedNames;
	TransformList lastTransforms; // Internal storage for spawning, not exposed as pin

	// Random float in [min, max]
	float RandRange(float min, float max);

	// Pick a random point on a triangle mesh surface
	// Returns position and face normal
	void RandomPointOnMesh(const MeshData& mesh, glm::vec3& outPos, glm::vec3& outNormal);

	// Transform an object mesh by position, rotation, scale and merge into output
	void MergeTransformed(const MeshData& objectMesh, const glm::vec3& pos,
		const glm::vec3& rotation, const glm::vec3& scale,
		const glm::vec3& surfaceNormal, MeshData& output);

public:
	bool IsSpawnMode() const { return spawnAsObjects; }
	bool IsAlignToNormal() const { return alignToNormal; }
	int GetParentIndex() const { return targetParentIndex; }
	const std::vector<std::string>& GetSpawnedNames() const { return spawnedNames; }
	void SetSpawnedNames(const std::vector<std::string>& names) { spawnedNames = names; }
	const TransformList& GetLastTransforms() const { return lastTransforms; }
};
