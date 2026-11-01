#pragma once

#include "Nodes/NodeGraph.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "Rendering/MeshData.h"
#include <algorithm>
#include <string>
#include <thread>
#include <vector>

// =====================================================================
// Object Filter: keeps points away from (or only on) other scene objects, e.g. no trees in the
// river or the lake. Takes any number of objects: a point counts as "on" them when it is over
// (or near, by the margin) any of their meshes.
// =====================================================================
class ObjectIntersectionFilterNode : public GraphNode
{
public:
	explicit ObjectIntersectionFilterNode(NodeGraph& graph) : GraphNode(graph)
	{
		AddInput(PinType::Points, "Points", "points");
		AddOutput(PinType::Points, "Passed", "passed");
		AddOutput(PinType::Points, "Rejected", "rejected");

		AddObjects("targets", "Objects", &targets, false).Legacy("targetName")
			.Tip("The objects to test against (drag them here from the hierarchy, or link Scene Objects)");
		AddEnum("filterMode", "Mode", &filterMode, { "Avoid", "Only on" });
		AddEnum("verticalMode", "Vertical rule", &verticalMode, { "Any (over or under)", "Above it", "Below it" });
		AddFloat("bufferDistance", "Vertical buffer", &bufferDistance, 0.01f, -10.0f, 10.0f, "%.3f")
			.Tip("Height offset for the Above/Below rules (positive = stricter)");
		AddFloat("horizontalMargin", "Margin", &horizontalMargin, 0.5f, 0.0f, 500.0f, "%.1f m")
			.Tip("Extra distance around the objects, in world units");
	}

	std::vector<std::string> LegacyInputKeys() const override { return { "points" }; }

	void Execute(NodeContext& ctx) override
	{
		const NodeValue& in = In("points");
		const TransformList& points = in.Points();
		if (points.empty()) return;

		struct Target { const MeshData* mesh; glm::mat4 model, invModel; float localRadius; };
		std::vector<Target> list;
		for (const auto& name : targets) {
			GameObject* obj = ctx.scene.FindObject(name);
			if (!obj) { ctx.Warn("Object '" + name + "' not found"); continue; }
			const MeshData& mesh = obj->GetCPUMeshData();
			if (mesh.vertices.empty()) { ctx.Warn("'" + name + "' has no mesh data"); continue; }
			Target t;
			t.mesh = &mesh;
			t.model = obj->GetWorldMatrix();
			t.invModel = glm::inverse(t.model);
			glm::vec3 scale = obj->GetTransform().GetScale();
			t.localRadius = scale.x > 0.001f ? horizontalMargin / scale.x : 0.0f;
			mesh.GetHeightAt(0.0f, 0.0f); // Build the height cache before the threads read it
			list.push_back(t);
		}
		if (list.empty()) {
			Out(0, in);
			Out(1, NodeValue::FromPoints(TransformList()));
			return;
		}

		ctx.Progress(10.0f, "Filtering " + std::to_string(points.size()) + " points...");
		unsigned threadCount = std::max(1u, std::min(std::thread::hardware_concurrency(), (unsigned)(points.size() / 2048 + 1)));
		std::vector<std::thread> threads;
		size_t perThread = (points.size() + threadCount - 1) / threadCount;
		std::vector<char> keep(points.size(), 0);
		for (unsigned t = 0; t < threadCount; t++) {
			size_t start = t * perThread, end = std::min(points.size(), start + perThread);
			if (start >= end) break;
			threads.emplace_back([&, start, end]() {
				for (size_t j = start; j < end; j++) {
					const TransformData& p = points[j];
					bool on = false;
					for (const Target& tg : list) {
						glm::vec4 local = tg.invModel * glm::vec4(p.position, 1.0f);
						float h = tg.mesh->GetHeightAt(local.x, local.z, tg.localRadius);
						if (h <= -1e9f) continue;
						float worldH = (tg.model * glm::vec4(local.x, h, local.z, 1.0f)).y;
						float dist = p.position.y - worldH;
						if (verticalMode == 0) on = true;
						else if (verticalMode == 1) on = dist >= bufferDistance;
						else on = dist <= -bufferDistance;
						if (on) break;
					}
					keep[j] = (filterMode == 0) ? !on : on;
				}
			});
		}
		for (auto& th : threads) th.join();

		TransformList passed, rejected;
		passed.reserve(points.size());
		for (size_t j = 0; j < points.size(); j++) (keep[j] ? passed : rejected).push_back(points[j]);
		ctx.Info(std::to_string(passed.size()) + " passed, " + std::to_string(rejected.size()) + " rejected");
		Out(0, NodeValue::FromPoints(std::move(passed)));
		Out(1, NodeValue::FromPoints(std::move(rejected)));
	}

private:
	std::vector<std::string> targets;
	int filterMode = 0;   // 0: avoid, 1: only on
	int verticalMode = 0; // 0: any, 1: above, 2: below
	float bufferDistance = 0.1f;
	float horizontalMargin = 0.0f;
};
