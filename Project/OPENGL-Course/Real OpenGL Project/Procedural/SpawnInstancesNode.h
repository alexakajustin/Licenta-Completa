#pragma once

#include "Nodes/NodeGraph.h"
#include "Rendering/InstancedGroup.h"
#include <map>
#include <string>
#include <vector>

class Mesh;
class Model;

// =====================================================================
// Spawn Instances: places scene objects on points, drawn as GPU instanced groups
//
// Takes the points that reach it (any number of point sets) and the objects to place (any
// number: each point picks one by weight, the same one every run). Multi-mesh models (trees:
// trunk, leaves, LOD0/1/2 meshes) become one instanced group per part with its LODs, single
// meshes one group. Instances erased in the viewport are remembered here as deletion volumes.
// =====================================================================
class SpawnInstancesNode : public GraphNode
{
public:
	explicit SpawnInstancesNode(NodeGraph& graph);
	void Execute(NodeContext& ctx) override;
	void RenderContent(SceneManager* scene) override;
	void SaveState(json& j) const override;
	void LoadState(const json& j) override;

	struct DeletionVolume { glm::vec3 position; float radius; };
	void AddDeletionVolume(const glm::vec3& position, float radius) { deletionVolumes.push_back({ position, radius }); }
	void SetDeletionVolumes(std::vector<DeletionVolume> volumes) { deletionVolumes = std::move(volumes); }
	void SetAlignToNormal(bool on) { alignToNormal = on; }
	void SetLegacyScale(bool on) { legacyScale = on; }

	// Group and placeholder names of this node's instances of an object
	std::string GroupName(const std::string& objectName) const { return "Scatter_Instanced_" + std::to_string(id) + "_" + objectName; }

private:
	bool alignToNormal = false;
	int seed;
	// Saves from before the overhaul instanced single-mesh objects at their scale squared: kept for
	// those scenes (so they look the same) until switched off
	bool legacyScale = false;
	std::vector<DeletionVolume> deletionVolumes;

	struct MeshCache { std::map<const void*, Mesh*> meshes; Mesh* Get(const MeshData& data); };

	void ApplyDeletionVolumes(TransformList& points) const;
	int BuildGroups(NodeContext& ctx, GameObject* source, const std::string& objectName,
		const std::vector<InstancedGroup::PackedInstance>& instances, MeshCache& cache);
};
