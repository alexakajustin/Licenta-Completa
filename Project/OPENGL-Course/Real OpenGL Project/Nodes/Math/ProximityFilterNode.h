#pragma once
#include "Nodes/NodeGraph.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "Rendering/InstancedGroup.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// =====================================================================
// Proximity Filter: keeps points by how close they are to other things - the way nature clusters
// what grows and falls around something else: branches, needles and pebbles near trunks and rocks,
// mushrooms at the foot of trees, flowers out in the clearings, nothing growing right against a
// wall. Put it after a dense Scatter.
//
// "Near" what: scene objects (their position) and, for an object that other graphs scattered as
// instances (a tree, a rock: Spawn Instances), every one of its instances, from any tab that ran
// before this one; plus any points linked into Near points.
//
// Each point is kept with a probability from its distance d to the nearest of them:
//   Near:      1 within Inner, falling to 0 at Radius (Falloff shapes the curve)
//   Away from: 0 within Inner, rising to 1 at Radius
// The draw is a hash of the point's position: the same points pass on every run.
// =====================================================================
class ProximityFilterNode : public GraphNode
{
public:
	explicit ProximityFilterNode(NodeGraph& graph) : GraphNode(graph)
	{
		AddInput(PinType::Points, "Points", "points");
		AddInput(PinType::Points, "Near points", "near");
		AddOutput(PinType::Points, "Passed", "passed");
		AddOutput(PinType::Points, "Rejected", "rejected");
		AddObjects("targets", "Near objects", &targets, false)
			.Tip("Objects to measure the distance to. An object scattered as instances by another graph (a tree, a rock) counts with every instance");
		AddEnum("mode", "Keep", &mode, { "Near them", "Away from them" });
		AddFloat("inner", "Inner", &inner, 0.1f, 0.0f, 500.0f, "%.1f m")
			.Tip("Near: all points kept within this distance. Away: none kept within it");
		AddFloat("radius", "Radius", &radius, 0.1f, 0.1f, 1000.0f, "%.1f m")
			.Tip("Near: no point kept beyond this distance. Away: all kept beyond it");
		AddFloat("falloff", "Falloff", &falloff, 0.05f, 0.1f, 8.0f, "%.2f")
			.Tip("Shape of the change between Inner and Radius: 1 linear, higher = concentrated closer to the objects");
		AddFloat("ignoreBelow", "Ignore within", &ignoreBelow, 0.05f, 0.0f, 50.0f, "%.2f m")
			.Tip("Points closer than this to an object are dropped (inside a trunk or a rock)");
	}

	std::vector<std::string> LegacyInputKeys() const override { return { "points", "near" }; }

	void Execute(NodeContext& ctx) override
	{
		const NodeValue& in = In("points");
		const TransformList& points = in.Points();
		if (points.empty()) return;

		// What the points are measured against (xz positions)
		std::vector<glm::vec2> sources;
		for (const auto& p : In("near").Points()) sources.push_back(glm::vec2(p.position.x, p.position.z));
		for (const auto& name : targets) {
			size_t before = sources.size();
			// Instances spawned from this object: groups "Scatter_Instanced_<node>_<object>[...]"
			for (InstancedGroup* g : ctx.scene.GetInstancedGroups()) {
				if (!g || !SpawnedFrom(g->GetName(), name)) continue;
				for (const auto& inst : g->cpuInstances) sources.push_back(glm::vec2(inst.positionAndScale.x, inst.positionAndScale.z));
			}
			if (sources.size() == before) {
				GameObject* obj = ctx.scene.FindObject(name);
				if (!obj) { ctx.Warn("Object '" + name + "' not found"); continue; }
				glm::vec3 p = glm::vec3(obj->GetWorldMatrix()[3]);
				sources.push_back(glm::vec2(p.x, p.z));
			}
		}
		if (sources.empty()) {
			ctx.Warn("Nothing to measure against: link Near points or pick Near objects");
			Out(0, mode == 1 ? in : NodeValue::FromPoints(TransformList()));
			Out(1, mode == 1 ? NodeValue::FromPoints(TransformList()) : in);
			return;
		}

		// Spatial hash of the sources, cells of the search radius
		const float reach = std::max(std::max(radius, inner), ignoreBelow) + 1e-3f;
		std::unordered_map<int64_t, std::vector<int>> grid;
		auto Key = [](int x, int z) { return ((int64_t)x << 32) ^ (int64_t)(uint32_t)z; };
		for (int i = 0; i < (int)sources.size(); i++)
			grid[Key((int)std::floor(sources[i].x / reach), (int)std::floor(sources[i].y / reach))].push_back(i);

		ctx.Progress(10.0f, "Measuring " + std::to_string(points.size()) + " points...");
		TransformList passed, rejected;
		passed.reserve(points.size());
		for (const auto& p : points) {
			glm::vec2 xz(p.position.x, p.position.z);
			int cx = (int)std::floor(xz.x / reach), cz = (int)std::floor(xz.y / reach);
			float best = 1e30f;
			for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
				auto it = grid.find(Key(cx + dx, cz + dz));
				if (it == grid.end()) continue;
				for (int i : it->second) {
					glm::vec2 d = sources[i] - xz;
					best = std::min(best, d.x * d.x + d.y * d.y);
				}
			}
			float dist = best < 1e29f ? std::sqrt(best) : 1e9f;
			float keep;
			if (dist < ignoreBelow) keep = 0.0f;
			else {
				float t = radius > inner ? glm::clamp((dist - inner) / (radius - inner), 0.0f, 1.0f) : (dist <= inner ? 0.0f : 1.0f);
				float nearness = std::pow(1.0f - t, std::max(falloff, 0.1f)); // 1 at Inner, 0 at Radius
				keep = mode == 0 ? nearness : 1.0f - nearness;
			}
			(Hash(p.position) < keep ? passed : rejected).push_back(p);
		}
		ctx.Info(std::to_string(passed.size()) + " passed, " + std::to_string(rejected.size()) + " rejected (" + std::to_string(sources.size()) + " objects)");
		Out(0, NodeValue::FromPoints(std::move(passed)));
		Out(1, NodeValue::FromPoints(std::move(rejected)));
	}

private:
	// "Scatter_Instanced_<id>_<object>" or "Scatter_Instanced_<id>_<object>_<part>"
	static bool SpawnedFrom(const std::string& group, const std::string& object)
	{
		const std::string prefix = "Scatter_Instanced_";
		if (group.rfind(prefix, 0) != 0) return false;
		size_t underscore = group.find('_', prefix.size());
		if (underscore == std::string::npos) return false;
		std::string rest = group.substr(underscore + 1);
		return rest == object || rest.rfind(object + "_", 0) == 0;
	}

	static float Hash(const glm::vec3& p)
	{
		uint32_t h = (uint32_t)(int32_t)std::floor(p.x * 37.0f) * 0x8DA6B343u ^ (uint32_t)(int32_t)std::floor(p.z * 37.0f) * 0xD8163841u
			^ (uint32_t)(int32_t)std::floor(p.y * 11.0f) * 0xCB1AB31Fu;
		h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
		return (float)(h & 0xFFFFFF) / 16777216.0f;
	}

	std::vector<std::string> targets;
	int mode = 0;            // 0: near them, 1: away from them
	float inner = 1.0f;
	float radius = 6.0f;
	float falloff = 1.5f;
	float ignoreBelow = 0.4f;
};
