#pragma once

#include "NodeGraph.h"
#include "imgui.h"

// =====================================================================
//  CompareNode — Compares two Float inputs, outputs Bool
// =====================================================================
class CompareNode : public GraphNode
{
public:
	CompareNode(NodeGraph& graph)
	{
		id = graph.NextNodeId();
		title = "Compare";

		// Inputs
		Pin a(graph.NextPinId(), PinDataType::Float, "A");
		Pin b(graph.NextPinId(), PinDataType::Float, "B");
		inputs.push_back(a);
		inputs.push_back(b);

		// Output
		Pin out(graph.NextPinId(), PinDataType::Bool, "Result");
		outputs.push_back(out);
	}

	json Serialize() const override
	{
		json j = GraphNode::Serialize();
		j["comparison"] = comparison;
		j["defaultA"] = defaultA;
		j["defaultB"] = defaultB;
		return j;
	}

	void Deserialize(const json& j) override
	{
		GraphNode::Deserialize(j);
		comparison = j.value("comparison", 0);
		defaultA = j.value("defaultA", 0.0f);
		defaultB = j.value("defaultB", 0.0f);
	}

	void RenderContent(SceneManager* scene) override
	{
		const char* ops[] = { "<", ">", "<=", ">=", "==", "!=" };
		ImGui::Combo("##cmp", &comparison, ops, IM_ARRAYSIZE(ops));

		ImGui::DragFloat("Default A", &defaultA, 0.1f);
		ImGui::DragFloat("Default B", &defaultB, 0.1f);
	}

	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override
	{
		float a = (inputs[0].data.type == PinDataType::Float) ? inputs[0].data.floatValue : defaultA;
		float b = (inputs[1].data.type == PinDataType::Float) ? inputs[1].data.floatValue : defaultB;

		bool result = false;
		switch (comparison)
		{
		case 0: result = (a < b);  break;
		case 1: result = (a > b);  break;
		case 2: result = (a <= b); break;
		case 3: result = (a >= b); break;
		case 4: result = (fabsf(a - b) < 0.0001f); break;
		case 5: result = (fabsf(a - b) >= 0.0001f); break;
		}

		outputs[0].data.Clear();
		outputs[0].data.type = PinDataType::Bool;
		outputs[0].data.boolValue = result;
	}

private:
	int comparison = 0; // Index: <, >, <=, >=, ==, !=
	float defaultA = 0.0f;
	float defaultB = 0.0f;
};
