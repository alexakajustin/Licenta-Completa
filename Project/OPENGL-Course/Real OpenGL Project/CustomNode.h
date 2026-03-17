#pragma once

#include "NodeGraph.h"
#include "CustomNodeDef.h"
#include "Operation.h"
#include "OperationRegistry.h"
#include <vector>
#include <string>

class SceneManager;
class Texture;
class Material;

// =====================================================================
//  CustomNode — a runtime GraphNode backed by a CustomNodeDef
// =====================================================================
//
//  This is the interpreter: it instantiates operations from the registry
//  based on the definition, and executes them sequentially through an
//  OperationContext when the graph is evaluated.
//
//  Input pins are mapped to the OperationContext at the start of Execute().
//  Operations modify the context in sequence.
//  Output pins are read from the OperationContext at the end.
//
class CustomNode : public GraphNode
{
public:
	CustomNode(NodeGraph& graph, const CustomNodeDef& def);
	~CustomNode();

	// GraphNode interface
	void RenderContent(SceneManager* scene) override;
	void Execute(SceneManager& scene) override;

	// Access the definition (for serialization, display, etc.)
	const CustomNodeDef& GetDefinition() const { return definition; }

	// Rebuild operations from the definition (e.g., after editing in NodeBuilder)
	void RebuildOperations();

	// Check if this node is a custom node (for graph serialization)
	bool IsCustomNode() const { return true; }

private:
	CustomNodeDef definition;
	std::vector<Operation*> operationInstances;

	// Map input pin data into the OperationContext
	void MapInputsToContext(OperationContext& ctx);

	// Map OperationContext results back to output pins
	void MapContextToOutputs(OperationContext& ctx);
};
