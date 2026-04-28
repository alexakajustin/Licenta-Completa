#pragma once

#include "NodeGraph.h"
#include <string>

class SceneManager;

/**
 * Targets an existing GameObject in the scene and overwrites its mesh
 * with the mesh data received from the input pin.
 */
class OutputNode : public GraphNode
{
public:
	OutputNode(NodeGraph& graph);

	void RenderContent(SceneManager* scene) override;
	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override;

	json Serialize() const override;
	void Deserialize(const json& j) override;

	// Helper for the graph execution to find where to push the mesh
	int GetTargetIndex() const { return targetIndex; }
	std::string GetTargetName() const { return targetName; }
	void SetTargetIndex(int index, const std::string& name) { targetIndex = index; targetName = name; }
	bool IsSameAsInput() const { return sameAsInput; }

	void SetSameAsInput(bool value) { sameAsInput = value; }

	void OnObjectRenamed(const std::string& oldName, const std::string& newName) override
	{
		if (targetName == oldName) targetName = newName;
	}

	bool ShouldUpdateMesh() const { return updateMesh; }

private:
	int targetIndex = -1;
	std::string targetName = "(none)";
	bool sameAsInput = false;
	bool updateMesh = true; // New: Toggle whether to bake the input mesh into the target object
};
