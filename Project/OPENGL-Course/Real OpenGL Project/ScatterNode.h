#pragma once

#include "NodeGraph.h"
#include "imgui.h"
#include "PerlinNoiseGenerator.h"
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
		Pin instancesOut(graph.NextPinId(), PinDataType::Mesh, "Instances Only");
		outputs.push_back(meshOut);
		outputs.push_back(instancesOut);
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

	// Persistence for spawned objects (handled by Output node now)
	std::vector<std::string> spawnedNames;
	TransformList lastTransforms; 

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
	bool IsAlignToNormal() const { return alignToNormal; }
	const std::vector<std::string>& GetSpawnedNames() const { return spawnedNames; }
	void SetSpawnedNames(const std::vector<std::string>& names) { spawnedNames = names; }
};
