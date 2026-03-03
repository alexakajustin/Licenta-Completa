#pragma once

#include "NodeGraph.h"
#include <string>

class SceneManager;

/**
 * Targets an existing GameObject in the scene and overwrites its mesh
 * with the mesh data received from the input pin.
 */
class MeshUpdateNode : public GraphNode
{
public:
	MeshUpdateNode(NodeGraph& graph);

	void RenderContent(SceneManager* scene) override;
	void Execute() override;

	// Helper for the graph execution to find where to push the mesh
	int GetTargetIndex() const { return targetIndex; }
	std::string GetTargetName() const { return targetName; }

private:
	int targetIndex = -1;
	std::string targetName = "(none)";
};
