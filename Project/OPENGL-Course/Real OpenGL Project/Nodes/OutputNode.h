#pragma once

#include "Nodes/NodeGraph.h"
#include <string>
#include <vector>

// =====================================================================
// Mesh Output: writes a mesh into a scene object (by default the object it was read from)
// The object keeps its own transform: the rotation and scale baked into the mesh by Scene Object
// are taken back out before the upload.
// =====================================================================
class OutputNode : public GraphNode
{
public:
	explicit OutputNode(NodeGraph& graph);
	void Execute(NodeContext& ctx) override;

	void SetSameAsInput(bool value) { sameAsInput = value; }
	void SetTarget(const std::string& name) { target = { name }; }

private:
	bool sameAsInput = true;
	std::vector<std::string> target;
	bool updateMesh = true;
};
