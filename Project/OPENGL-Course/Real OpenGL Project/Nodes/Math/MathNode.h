#pragma once

#include "Nodes/NodeGraph.h"
#include <algorithm>
#include <cmath>

// Arithmetic on two numbers (A and B are fields, or driven by links)
class MathNode : public GraphNode
{
public:
	explicit MathNode(NodeGraph& graph) : GraphNode(graph)
	{
		AddOutput(PinType::Float, "Result", "result");
		AddEnum("operation", "Operation", &operation,
			{ "Add", "Subtract", "Multiply", "Divide", "Power", "Min", "Max", "Modulo", "Abs", "Negate", "Sqrt", "Sin", "Cos" });
		AddFloat("defaultA", "A", &a, 0.1f);
		AddFloat("defaultB", "B", &b, 0.1f).ShowIf([this] { return operation < 8; });
	}

	std::vector<std::string> LegacyInputKeys() const override { return { "defaultA", "defaultB" }; }

	void Execute(NodeContext&) override
	{
		float r = 0.0f;
		switch (operation) {
		case 0:  r = a + b; break;
		case 1:  r = a - b; break;
		case 2:  r = a * b; break;
		case 3:  r = b != 0.0f ? a / b : 0.0f; break;
		case 4:  r = std::pow(a, b); break;
		case 5:  r = std::min(a, b); break;
		case 6:  r = std::max(a, b); break;
		case 7:  r = b != 0.0f ? std::fmod(a, b) : 0.0f; break;
		case 8:  r = std::fabs(a); break;
		case 9:  r = -a; break;
		case 10: r = a >= 0.0f ? std::sqrt(a) : 0.0f; break;
		case 11: r = std::sin(a); break;
		case 12: r = std::cos(a); break;
		}
		Out(0, NodeValue::Float(r));
		status.info = NodeValue::Float(r).Summary();
	}

private:
	int operation = 0;
	float a = 0.0f;
	float b = 1.0f;
};
