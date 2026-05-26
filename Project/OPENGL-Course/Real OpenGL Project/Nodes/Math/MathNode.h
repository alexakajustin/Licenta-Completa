#pragma once

#include "Nodes/NodeGraph.h"
#include "imgui.h"
#include <cmath>
#include <algorithm>

// =====================================================================
//  MathNode — Performs arithmetic on two Float inputs
// =====================================================================
class MathNode : public GraphNode
{
public:
	MathNode(NodeGraph& graph)
	{
		id = graph.NextNodeId();
		title = "Math";

		// Inputs
		Pin a(graph.NextPinId(), PinDataType::Float, "A");
		Pin b(graph.NextPinId(), PinDataType::Float, "B");
		inputs.push_back(a);
		inputs.push_back(b);

		// Output
		Pin out(graph.NextPinId(), PinDataType::Float, "Result");
		outputs.push_back(out);
	}

	json Serialize() const override
	{
		json j = GraphNode::Serialize();
		j["operation"] = operation;
		j["defaultA"] = defaultA;
		j["defaultB"] = defaultB;
		return j;
	}

	void Deserialize(const json& j) override
	{
		GraphNode::Deserialize(j);
		operation = j.value("operation", 0);
		defaultA = j.value("defaultA", 0.0f);
		defaultB = j.value("defaultB", 1.0f);
	}

	void RenderContent(SceneManager* scene) override
	{
		const char* ops[] = { "Add", "Subtract", "Multiply", "Divide", "Power", "Min", "Max", "Modulo", "Abs", "Negate", "Sqrt", "Sin", "Cos" };
		ImGui::Combo("##op", &operation, ops, IM_ARRAYSIZE(ops));

		ImGui::DragFloat("Default A", &defaultA, 0.1f);
		ImGui::DragFloat("Default B", &defaultB, 0.1f);
	}

	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override
	{
		// Use connected input if available, otherwise use defaults
		float a = (inputs[0].data.type == PinDataType::Float) ? inputs[0].data.floatValue : defaultA;
		float b = (inputs[1].data.type == PinDataType::Float) ? inputs[1].data.floatValue : defaultB;

		float result = 0.0f;
		switch (operation)
		{
		case 0:  result = a + b; break;                                          // Add
		case 1:  result = a - b; break;                                          // Subtract
		case 2:  result = a * b; break;                                          // Multiply
		case 3:  result = (b != 0.0f) ? a / b : 0.0f; break;                    // Divide
		case 4:  result = powf(a, b); break;                                     // Power
		case 5:  result = std::min(a, b); break;                                 // Min
		case 6:  result = std::max(a, b); break;                                 // Max
		case 7:  result = (b != 0.0f) ? fmodf(a, b) : 0.0f; break;              // Modulo
		case 8:  result = fabsf(a); break;                                       // Abs
		case 9:  result = -a; break;                                             // Negate
		case 10: result = (a >= 0.0f) ? sqrtf(a) : 0.0f; break;                 // Sqrt
		case 11: result = sinf(a); break;                                        // Sin
		case 12: result = cosf(a); break;                                        // Cos
		}

		outputs[0].data.Clear();
		outputs[0].data.type = PinDataType::Float;
		outputs[0].data.floatValue = result;
	}

private:
	int operation = 0; // Index into ops array
	float defaultA = 0.0f;
	float defaultB = 1.0f;
};
