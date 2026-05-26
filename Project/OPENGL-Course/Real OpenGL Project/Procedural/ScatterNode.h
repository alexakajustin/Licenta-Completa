#pragma once

#include "Nodes/NodeGraph.h"
#include "imgui.h"
#include "Procedural/PerlinNoiseGenerator.h"
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
/**
 * @class ScatterNode
 * @brief Scatters instances of an object mesh across a surface mesh using either merged meshes or scene GameObject instances.
 */
class ScatterNode : public GraphNode
{
public:
	/**
	 * @brief Constructor registers input and output pins.
	 * @param graph NodeGraph that owns this node.
	 */
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

	/**
	 * @brief Serializes node properties to JSON.
	 */
	json Serialize() const override;

	/**
	 * @brief Deserializes node properties from JSON.
	 */
	void Deserialize(const json& j) override;

	/**
	 * @brief Renders the editor UI panel for configuring counts, seed values, and alignment options.
	 */
	void RenderContent(SceneManager* scene) override;

	/**
	 * @brief Runs the scatter layout simulation, generating coordinates and applying transforms.
	 */
	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override;

	/**
	 * @brief Triggered when node is removed, cleans up generated game objects.
	 */
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

	/**
	 * @brief Synchronizes object tracking names during renaming events.
	 */
	void OnObjectRenamed(const std::string& oldName, const std::string& newName) override
	{
		// Update parent reference
		if (targetParentName == oldName) targetParentName = newName;

		// Update spawnedMap keys
		if (spawnedMap.count(oldName)) {
			spawnedMap[newName] = std::move(spawnedMap[oldName]);
			spawnedMap.erase(oldName);
		}

		// Update createdGroupNames (they embed the object name in the string)
		std::set<std::string> updatedNames;
		for (const auto& gn : createdGroupNames) {
			std::string updated = gn;
			size_t pos = updated.find(oldName);
			if (pos != std::string::npos)
				updated.replace(pos, oldName.size(), newName);
			updatedNames.insert(updated);
		}
		createdGroupNames = updatedNames;
	}
	
	// Legacy tracking support
	const std::vector<std::string>& GetSpawnedNames() const { return spawnedNames; }
	void SetSpawnedNames(const std::vector<std::string>& names) { spawnedNames = names; }

	// Setters for programmatic setup (templates)
	void SetSpawnAsObjects(bool value) { spawnAsObjects = value; }
	void SetTargetParent(int index, const std::string& name) { targetParentIndex = index; targetParentName = name; }

private:
	int count = 50; ///< Number of instances to scatter.
	float minScale = 0.8f; ///< Minimum scale jitter factor.
	float maxScale = 1.2f; ///< Maximum scale jitter factor.
	bool randomRotation = true; ///< If true, randomizes Y rotation.
	bool alignToNormal = true; ///< If true, aligns the up vector of the model with the surface normal.
	int seed = 42; ///< Random seed.

	// Spawning Settings
	bool spawnAsObjects = false;
	std::string targetParentName = "(none)";
	int targetParentIndex = -1;
	
	// Legacy tracking (single object)
	std::vector<std::string> spawnedNames; 
	
	// Advanced tracking (multiple objects)
	std::set<std::string> createdGroupNames;
	std::map<std::string, std::vector<std::string>> spawnedMap; // objectName -> list of instance names
	
	/**
	 * @struct DeletionVolume
	 * @brief Defines spherical exclusion volumes where spawned instances should be culled.
	 */
	struct DeletionVolume {
		glm::vec3 position; ///< Center of the exclusion sphere.
		float radius; ///< Radius of the exclusion sphere.
	};
	std::vector<DeletionVolume> deletionVolumes;

public:
	void AddDeletionVolume(const glm::vec3& pos, float radius) {
		deletionVolumes.push_back({ pos, radius });
	}
	void ClearDeletionVolumes() { deletionVolumes.clear(); }
	const std::vector<DeletionVolume>& GetDeletionVolumes() const { return deletionVolumes; }

private:
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
