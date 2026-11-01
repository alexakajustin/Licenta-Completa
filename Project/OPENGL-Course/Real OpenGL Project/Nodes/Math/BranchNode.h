#pragma once

#include "Nodes/NodeGraph.h"

// Routes a mesh to True or False by a condition ("if it rains, the muddy terrain pipeline")
class BranchNode : public GraphNode
{
public:
	explicit BranchNode(NodeGraph& graph) : GraphNode(graph)
	{
		AddBool("defaultCondition", "Condition", &condition);
		AddInput(PinType::Mesh, "Input", "input");
		AddOutput(PinType::Mesh, "True", "true");
		AddOutput(PinType::Mesh, "False", "false");
	}

	std::vector<std::string> LegacyInputKeys() const override { return { "defaultCondition", "input" }; }

	void Execute(NodeContext&) override
	{
		const NodeValue& input = In("input");
		if (!input.Valid()) return;
		Out(condition ? 0 : 1, input);
		status.info = condition ? "-> True" : "-> False";
	}

private:
	bool condition = true;
};
