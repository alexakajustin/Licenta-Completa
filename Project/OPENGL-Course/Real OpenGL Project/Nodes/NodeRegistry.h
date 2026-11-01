#pragma once

#include <functional>
#include <string>
#include <typeinfo>
#include <vector>

class GraphNode;
class NodeGraph;

// =====================================================================
// Every node type, in one place: the add-node menu, loading saved graphs and the editor's
// categories all come from here.
// =====================================================================
struct NodeTypeInfo
{
	std::string id;          // Stable type id, saved with the graph
	std::string name;        // Menu entry and node title
	std::string category;
	std::string description; // Menu tooltip
	std::vector<std::string> legacyTitles; // What graphs saved by older versions called it
	std::function<GraphNode*(NodeGraph&)> create;
	const std::type_info* cls = nullptr;
};

namespace NodeRegistry
{
	const std::vector<NodeTypeInfo>& Types();
	const NodeTypeInfo* Find(const std::string& id);
	const NodeTypeInfo* FindByLegacyTitle(const std::string& title);
	// A node of this type (built-in id, or "custom:<definition name>" for Node Builder nodes)
	GraphNode* Create(const std::string& id, NodeGraph& graph);
	std::string CategoryOf(const GraphNode* node);
	// Fills in the type id and title of a node built directly (new SceneInputNode(graph)...)
	void Identify(GraphNode* node);
}
