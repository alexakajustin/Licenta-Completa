#pragma once

#include "Nodes/NodeGraph.h"
#include "Nodes/CustomNodeDef.h"
#include "Nodes/Operation.h"
#include "Nodes/OperationRegistry.h"
#include <vector>
#include <string>

// =====================================================================
//  CustomNode — a node made in the Node Builder, backed by a CustomNodeDef
//
//  The interpreter: instantiates the definition's operations from the registry and runs them in
//  sequence on an OperationContext. Inputs fill the context (first mesh = the main mesh, the next
//  one the secondary mesh, numbers become variables named after their pins); outputs read it back.
// =====================================================================
class CustomNode : public GraphNode
{
public:
	CustomNode(NodeGraph& graph, const CustomNodeDef& def);
	~CustomNode() override;

	void RenderContent(SceneManager* scene) override;
	void Execute(NodeContext& ctx) override;
	void SaveState(json& j) const override;
	void LoadState(const json& j) override;

	const CustomNodeDef& GetDefinition() const { return definition; }

	// Rebuild operations from the definition (e.g., after editing in NodeBuilder)
	void RebuildOperations();

private:
	CustomNodeDef definition;
	std::vector<Operation*> operationInstances;
};
