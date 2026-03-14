#pragma once

#include "NodeGraph.h"

// Merges two meshes into one.
class MergeMeshNode : public GraphNode
{
public:
	MergeMeshNode(NodeGraph& graph)
	{
		id = graph.NextNodeId();
		title = "Merge Mesh";

		// Inputs
		Pin a(graph.NextPinId(), PinDataType::Mesh, "Mesh A");
		Pin b(graph.NextPinId(), PinDataType::Mesh, "Mesh B");
		inputs.push_back(a);
		inputs.push_back(b);

		// Outputs
		Pin meshOut(graph.NextPinId(), PinDataType::Mesh, "Merged");
		outputs.push_back(meshOut);
	}

	void RenderContent(SceneManager* scene) override
	{
		ImGui::Text("Combines two mesh inputs.");
	}

	void Execute(SceneManager& scene) override
	{
		outputs[0].data.Clear();
		outputs[0].data.type = PinDataType::Mesh;

		MeshData& meshA = inputs[0].data.meshData;
		MeshData& meshB = inputs[1].data.meshData;

		MeshData result = meshA;
		result.Append(meshB);

		outputs[0].data.meshData = result;

		// Propagate source name: Prefer latest (B), fallback to first (A)
		std::string target = inputs[1].data.sourceObjectName;
		if (target == "(none)") target = inputs[0].data.sourceObjectName;
		outputs[0].data.sourceObjectName = target;
	}
};
