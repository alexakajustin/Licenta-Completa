#pragma once

#include "Nodes/NodeGraph.h"

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

	json Serialize() const override { return GraphNode::Serialize(); }
	void Deserialize(const json& j) override { GraphNode::Deserialize(j); }

	void RenderContent(SceneManager* scene) override
	{
		ImGui::Text("Combines two mesh inputs.");
	}

	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override
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

		PinData& prefSrc = (target == inputs[1].data.sourceObjectName) ? inputs[1].data : inputs[0].data;
		outputs[0].data.sourceMaterial = prefSrc.sourceMaterial;
		outputs[0].data.sourceTexture = prefSrc.sourceTexture;
		outputs[0].data.sourceNormalMap = prefSrc.sourceNormalMap;
		outputs[0].data.textureLayers = prefSrc.textureLayers;
	}
};
