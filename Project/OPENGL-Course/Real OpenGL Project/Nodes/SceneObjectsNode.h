#pragma once

#include "Nodes/NodeGraph.h"
#include <string>
#include <vector>

// =====================================================================
// Scene Objects: a list of scene objects, for any node taking Objects (Spawn Instances picks
// among them by weight, Interior Gen sorts them into furniture categories by label or name,
// Object Filter tests against all of them)
//
//  Pick           objects chosen one by one (or dragged in from the hierarchy), each with a weight
//                 and an optional label
//  Children of    every child of a parent object (a folder of tree variants, of furniture...)
//  Name contains  every object whose name contains a text
// =====================================================================
class SceneObjectsNode : public GraphNode
{
public:
	explicit SceneObjectsNode(NodeGraph& graph);
	void Execute(NodeContext& ctx) override;
	void RenderContent(SceneManager* scene) override;
	void SaveState(json& j) const override;
	void LoadState(const json& j) override;

	struct Entry { std::string name; float weight = 1.0f; std::string label; };
	void AddEntry(const std::string& name, float weight = 1.0f, const std::string& label = "") { entries.push_back({ name, weight, label }); }
	void OnObjectRenamed(const std::string& oldName, const std::string& newName) override;

private:
	int mode = 0;
	std::vector<Entry> entries;
	std::vector<std::string> parent;
	std::string nameFilter;
	std::vector<std::string> lastResolved;
};
