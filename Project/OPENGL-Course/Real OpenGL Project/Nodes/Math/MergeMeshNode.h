#pragma once

#include "Nodes/NodeGraph.h"

// Joins any number of meshes into one (link as many as needed into Meshes)
class MergeMeshNode : public GraphNode
{
public:
	explicit MergeMeshNode(NodeGraph& graph) : GraphNode(graph)
	{
		AddInput(PinType::Mesh, "Meshes", "meshes", true);
		AddOutput(PinType::Mesh, "Merged", "mesh");
	}

	std::vector<std::string> LegacyInputKeys() const override { return { "meshes", "meshes" }; }

	void Execute(NodeContext&) override
	{
		const auto& sources = inputs[0].sources;
		if (sources.empty()) return;
		if (sources.size() == 1) { Out(0, sources[0]); return; }

		// The first mesh's frame; the others are moved into it
		MeshValue result = sources[0].mesh;
		MeshData merged = sources[0].mesh.Data();
		for (size_t i = 1; i < sources.size(); i++) {
			const MeshValue& m = sources[i].mesh;
			if (m.Empty()) continue;
			glm::vec3 offset = m.origin - result.origin;
			size_t start = merged.vertices.size();
			merged.Append(m.Data());
			if (offset != glm::vec3(0.0f))
				for (size_t v = start; v < merged.vertices.size(); v += 14) {
					merged.vertices[v] += offset.x;
					merged.vertices[v + 1] += offset.y;
					merged.vertices[v + 2] += offset.z;
				}
			// The latest mesh read from an object decides the look (as Merge always did)
			if (!m.sourceObject.empty()) { result.sourceObject = m.sourceObject; result.look = m.look; }
		}
		result.Set(std::move(merged));
		Out(0, NodeValue::FromMesh(std::move(result)));
	}
};
