#pragma once

#include "Nodes/NodeGraph.h"
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

// Copies a mesh onto every point and merges the copies into one mesh (static props baked into a
// single draw; for thousands of instances prefer Spawn Instances)
class ForEachNode : public GraphNode
{
public:
	explicit ForEachNode(NodeGraph& graph) : GraphNode(graph)
	{
		AddInput(PinType::Points, "Points", "points");
		AddInput(PinType::Mesh, "Mesh", "mesh");
		AddOutput(PinType::Mesh, "Mesh", "mesh");
		AddBool("alignToNormal", "Align to Normal", &alignToNormal).Tip("Tilt each copy to the surface normal of its point");
		AddInt("maxInstances", "Max Copies", &maxInstances, 100.0f, 1, 1000000);
	}

	void Execute(NodeContext& ctx) override
	{
		const TransformList& points = In("points").Points();
		const MeshValue& source = In("mesh").mesh;
		if (points.empty() || source.Empty()) return;

		const MeshData& objectMesh = source.Data();
		int count = std::min((int)points.size(), maxInstances);
		MeshData combined;
		combined.vertices.reserve(objectMesh.vertices.size() * (size_t)count);
		combined.indices.reserve(objectMesh.indices.size() * (size_t)count);
		for (int i = 0; i < count; i++) {
			const TransformData& t = points[i];
			glm::mat4 model = glm::translate(glm::mat4(1.0f), t.position);
			if (alignToNormal && glm::length(t.normal) > 0.001f) {
				glm::vec3 up(0, 1, 0), n = glm::normalize(t.normal);
				if (std::abs(glm::dot(up, n)) < 0.999f)
					model = glm::rotate(model, std::acos(glm::clamp(glm::dot(up, n), -1.0f, 1.0f)), glm::normalize(glm::cross(up, n)));
			}
			model = glm::rotate(model, glm::radians(t.rotation.y), glm::vec3(0, 1, 0));
			model = glm::rotate(model, glm::radians(t.rotation.x), glm::vec3(1, 0, 0));
			model = glm::rotate(model, glm::radians(t.rotation.z), glm::vec3(0, 0, 1));
			model = glm::scale(model, t.scale);
			MeshData copy = objectMesh;
			copy.TransformBy(model);
			combined.Append(copy);
			if (i % 1000 == 0) ctx.Progress(100.0f * i / count, "Copying " + std::to_string(i) + "/" + std::to_string(count));
		}

		// Points are in world space: so is the result
		MeshValue result;
		result.Set(std::move(combined));
		result.look = source.look;
		result.sourceObject = source.sourceObject;
		Out(0, NodeValue::FromMesh(std::move(result)));
		ctx.Info(std::to_string(count) + " copies");
	}

private:
	bool alignToNormal = true;
	int maxInstances = 10000;
};
