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
	void Execute(SceneManager& scene) override;

	// Helper for the graph execution to find where to push the mesh
	int GetTargetIndex() const { return targetIndex; }
	std::string GetTargetName() const { return targetName; }
	bool IsSameAsInput() const { return sameAsInput; }

	void SetSpawnAsObjects(bool value) { spawnAsObjects = value; }
	void SetSameAsInput(bool value) { sameAsInput = value; }

	bool IsSpawnMode() const { return spawnAsObjects; }
	int GetParentIndex() const { return targetParentIndex; }
	std::string GetParentName() const { return targetParentName; }
	const std::vector<std::string>& GetSpawnedNames() const { return spawnedNames; }
	void SetSpawnedNames(const std::vector<std::string>& names) { spawnedNames = names; }

private:
	int targetIndex = -1;
	std::string targetName = "(none)";
	bool sameAsInput = false;

	// Modular Instance Spawning
	bool spawnAsObjects = false;
	std::string targetParentName = "(none)";
	int targetParentIndex = -1;
	std::vector<std::string> spawnedNames;
};
