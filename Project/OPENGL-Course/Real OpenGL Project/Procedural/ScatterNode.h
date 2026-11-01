#pragma once

#include "Nodes/NodeGraph.h"
#include <set>
#include <string>
#include <vector>

// =====================================================================
// Scatter: random points on a surface
//
// Outputs points only (position, random rotation and scale, surface normal); what is placed on
// them is decided downstream (filters, then Spawn Instances or Copy to Points). Points are spread
// evenly over the surface area, and each one comes from its own hash of (seed, index): the same
// seed gives the same points on any machine, however many threads computed them.
// =====================================================================
class ScatterNode : public GraphNode
{
public:
	explicit ScatterNode(NodeGraph& graph);
	void Execute(NodeContext& ctx) override;
	void RenderContent(SceneManager* scene) override;
	void LoadState(const json& j) override;
	void SaveState(json& j) const override;

	// Older saves: inputs (Surface, Object mesh), outputs (Combined, Instances Only)
	std::vector<std::string> LegacyInputKeys() const override { return { "surface", "legacy:object" }; }
	std::vector<std::string> LegacyOutputKeys() const override { return { "surface", "points" }; }

	// What graphs saved before Spawn Instances existed asked Scatter to spawn (read by the migration)
	struct DeletionVolume { glm::vec3 position; float radius; };
	struct Legacy {
		bool spawn = false;
		bool alignToNormal = true;
		std::vector<DeletionVolume> deletionVolumes;
		std::set<std::string> groups;
	} legacy;

	void SetLegacyRandom(bool on) { legacyRandom = on; }

private:
	int count = 1000;
	float minScale = 0.8f;
	float maxScale = 1.2f;
	bool randomRotation = true;
	int seed;
	// Saves from before the counter-based random: the old per-thread generators, so the layout of
	// existing scenes stays exactly as it was (it depends on the machine's thread count)
	bool legacyRandom = false;

	void ScatterLegacy(const MeshValue& surface, TransformList& points, int total);
	void ScatterByArea(const MeshValue& surface, TransformList& points, int total, NodeContext& ctx);
};
