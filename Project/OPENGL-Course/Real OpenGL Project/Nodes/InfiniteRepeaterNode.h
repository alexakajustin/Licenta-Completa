#pragma once

#include "Nodes/NodeGraph.h"
#include <string>
#include <vector>

// Attaches a WorldStreamerComponent to a target object: its grid streams infinitely around the
// camera (a configuration node: no pins)
class InfiniteRepeaterNode : public GraphNode
{
public:
	explicit InfiniteRepeaterNode(NodeGraph& graph);
	void Execute(NodeContext& ctx) override;

private:
	std::vector<std::string> target;
	std::vector<std::string> reference;
	int radius = 2;
};
