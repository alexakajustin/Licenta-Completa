#pragma once

#include "Nodes/NodeGraph.h"
#include <cmath>

// Compares two numbers into a Bool (e.g. to drive a Branch)
class CompareNode : public GraphNode
{
public:
	explicit CompareNode(NodeGraph& graph) : GraphNode(graph)
	{
		AddOutput(PinType::Bool, "Result", "result");
		AddEnum("comparison", "Comparison", &comparison, { "A < B", "A > B", "A <= B", "A >= B", "A == B", "A != B" });
		AddFloat("defaultA", "A", &a, 0.1f);
		AddFloat("defaultB", "B", &b, 0.1f);
	}

	std::vector<std::string> LegacyInputKeys() const override { return { "defaultA", "defaultB" }; }

	void Execute(NodeContext&) override
	{
		bool r = false;
		switch (comparison) {
		case 0: r = a < b; break;
		case 1: r = a > b; break;
		case 2: r = a <= b; break;
		case 3: r = a >= b; break;
		case 4: r = std::fabs(a - b) < 0.0001f; break;
		case 5: r = std::fabs(a - b) >= 0.0001f; break;
		}
		Out(0, NodeValue::Bool(r));
		status.info = r ? "true" : "false";
	}

private:
	int comparison = 0;
	float a = 0.0f;
	float b = 0.0f;
};
