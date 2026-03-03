#pragma once

#include <vector>
#include <string>
#include <map>
#include <glm/glm.hpp>
#include "MeshData.h"

class SceneManager;
class Texture;
class Material;

// ========== Pin ==========
struct Pin
{
	int id;
	PinDataType dataType;
	std::string name;
	PinData data;  // Filled during execution

	Pin() : id(0), dataType(PinDataType::None) {}
	Pin(int id, PinDataType type, const std::string& name)
		: id(id), dataType(type), name(name) {}
};

// ========== Link ==========
struct Link
{
	int id;
	int startPinId;  // Output pin
	int endPinId;    // Input pin

	Link() : id(0), startPinId(0), endPinId(0) {}
	Link(int id, int start, int end) : id(id), startPinId(start), endPinId(end) {}
};

// ========== Base Node ==========
class GraphNode
{
public:
	int id;
	std::string title;
	std::vector<Pin> inputs;
	std::vector<Pin> outputs;
	glm::vec2 editorPos = glm::vec2(0.0f); // Position in node editor
	bool positionSet = false;

	GraphNode() : id(0) {}
	virtual ~GraphNode() = default;

	// Render ImGui controls inside the node body
	virtual void RenderContent(SceneManager* scene) = 0;

	// Process: read input pin data, compute, write output pin data
	virtual void Execute() = 0;

	// Find a pin by ID
	Pin* FindPin(int pinId);
	Pin* FindInputPin(int pinId);
	Pin* FindOutputPin(int pinId);
};

// ========== Node Graph ==========
class NodeGraph
{
public:
	NodeGraph();
	~NodeGraph();

	// Node management
	void AddNode(GraphNode* node);
	void RemoveNode(int nodeId);
	GraphNode* FindNode(int nodeId);
	GraphNode* FindNodeByPinId(int pinId);

	// Link management
	bool AddLink(int outputPinId, int inputPinId);
	void RemoveLink(int linkId);
	bool CanLink(int outputPinId, int inputPinId);

	// Execution
	void Execute(SceneManager& scene, Texture* defaultTex, Material* defaultMat);

	// Accessors
	std::vector<GraphNode*>& GetNodes() { return nodes; }
	std::vector<Link>& GetLinks() { return links; }

	// ID generation (unique across session)
	int NextNodeId() { return nextId++; }
	int NextPinId() { return nextId++; }
	int NextLinkId() { return nextId++; }

	void Clear();

private:
	std::vector<GraphNode*> nodes;
	std::vector<Link> links;
	int nextId;

	// Topological sort for execution order
	std::vector<GraphNode*> TopologicalSort();

	// Propagate data along links (copy output pin data to connected input pins)
	void PropagateData();

	// Track generated objects for cleanup
	std::vector<std::string> generatedObjectNames;
};
