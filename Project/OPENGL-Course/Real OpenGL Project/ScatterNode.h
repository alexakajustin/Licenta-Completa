#pragma once

#include "NodeGraph.h"
#include "imgui.h"
#include "PerlinNoiseGenerator.h"
#include <cmath>
#include <cstdlib>
#include <set>
#include <map>

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

	json Serialize() const override;
	void Deserialize(const json& j) override;

	void RenderContent(SceneManager* scene) override;
	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override;
	void OnRemove(SceneManager& scene) override;

	// Multi-Object tracking
	void AddCreatedGroupName(const std::string& name) { createdGroupNames.insert(name); }
	const std::set<std::string>& GetCreatedGroupNames() const { return createdGroupNames; }
	void SetCreatedGroupNames(const std::set<std::string>& names) { createdGroupNames = names; }
	
	std::map<std::string, std::vector<std::string>>& GetSpawnedMap() { return spawnedMap; }

	bool IsAlignToNormal() const { return alignToNormal; }
	bool IsSpawnMode() const { return spawnAsObjects; }
	int GetParentIndex() const { return targetParentIndex; }
	std::string GetParentName() const { return targetParentName; }
	
	// Legacy tracking support
	const std::vector<std::string>& GetSpawnedNames() const { return spawnedNames; }
	void SetSpawnedNames(const std::vector<std::string>& names) { spawnedNames = names; }

	// Setters for programmatic setup (templates)
	void SetSpawnAsObjects(bool value) { spawnAsObjects = value; }
	void SetTargetParent(int index, const std::string& name) { targetParentIndex = index; targetParentName = name; }

private:
	int count = 50;
	float minScale = 0.8f;
	float maxScale = 1.2f;
	bool randomRotation = true;
	bool alignToNormal = true;
	int seed = 42;

	// Spawning Settings
	bool spawnAsObjects = false;
	std::string targetParentName = "(none)";
	int targetParentIndex = -1;
	
	// Legacy tracking (single object)
	std::vector<std::string> spawnedNames; 
	
	// Advanced tracking (multiple objects)
	std::set<std::string> createdGroupNames;
	std::map<std::string, std::vector<std::string>> spawnedMap; // objectName -> list of instance names
	
	// Random float in [min, max]
	float RandRange(float min, float max);

	// Pick a random point on a triangle mesh surface
	// Returns position and face normal
	void RandomPointOnMesh(const MeshData& mesh, glm::vec3& outPos, glm::vec3& outNormal);

	// Transform an object mesh by position, rotation, scale and merge into output
	void MergeTransformed(const MeshData& objectMesh, const glm::vec3& pos,
		const glm::vec3& rotation, const glm::vec3& scale,
		const glm::vec3& surfaceNormal, MeshData& output);
};
