#pragma once

#include "Nodes/NodeGraph.h"
#include "imgui.h"

// =====================================================================
//  BranchNode — Routes mesh data based on a Bool condition
// =====================================================================
//
//  If Condition is true:  True Out = Input, False Out = empty
//  If Condition is false: True Out = empty, False Out = Input
//
//  Use case: "If raining, use mud terrain pipeline, else use dry pipeline"
//
class BranchNode : public GraphNode
{
public:
	BranchNode(NodeGraph& graph)
	{
		id = graph.NextNodeId();
		title = "Branch";

		// Inputs
		Pin condIn(graph.NextPinId(), PinDataType::Bool, "Condition");
		Pin dataIn(graph.NextPinId(), PinDataType::Mesh, "Input");
		inputs.push_back(condIn);
		inputs.push_back(dataIn);

		// Outputs
		Pin trueOut(graph.NextPinId(), PinDataType::Mesh, "True");
		Pin falseOut(graph.NextPinId(), PinDataType::Mesh, "False");
		outputs.push_back(trueOut);
		outputs.push_back(falseOut);
	}

	json Serialize() const override
	{
		json j = GraphNode::Serialize();
		j["defaultCondition"] = defaultCondition;
		return j;
	}

	void Deserialize(const json& j) override
	{
		GraphNode::Deserialize(j);
		defaultCondition = j.value("defaultCondition", true);
	}

	void RenderContent(SceneManager* scene) override
	{
		ImGui::Checkbox("Default", &defaultCondition);
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(if no Condition input)");
	}

	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override
	{
		outputs[0].data.Clear();
		outputs[1].data.Clear();

		bool condition = (inputs[0].data.type == PinDataType::Bool)
			? inputs[0].data.boolValue
			: defaultCondition;

		if (condition)
		{
			// Route to True output
			outputs[0].data = inputs[1].data;
			outputs[0].data.type = PinDataType::Mesh;
		}
		else
		{
			// Route to False output
			outputs[1].data = inputs[1].data;
			outputs[1].data.type = PinDataType::Mesh;
		}
	}

private:
	bool defaultCondition = true;
};
