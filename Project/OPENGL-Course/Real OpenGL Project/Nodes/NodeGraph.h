#pragma once

#include <vector>
#include <string>
#include <map>
#include <memory>
#include <glm/glm.hpp>
#include "Rendering/MeshData.h"

#include "External Libs/nlohmann/json.hpp"

using json = nlohmann::json;

class SceneManager;
class Texture;
class Material;

// ========== Pin ==========
/**
 * @struct Pin
 * @brief Represents an input or output data connection slot on a graph node.
 */
struct Pin
{
	int id; ///< Unique identifier of the pin.
	PinDataType dataType; ///< Data type constraint of the pin.
	std::string name; ///< String name label of the pin.
	PinData data;  ///< Data payload populated during graph execution.

	Pin() : id(0), dataType(PinDataType::None) {}
	Pin(int id, PinDataType type, const std::string& name)
		: id(id), dataType(type), name(name) {}
};

/**
 * @struct Link
 * @brief Represents a connection link transferring data from an output pin to an input pin.
 */
struct Link
{
	int id; ///< Unique identifier of the link.
	int startPinId;  ///< Output source pin identifier.
	int endPinId;    ///< Input destination pin identifier.

	Link() : id(0), startPinId(0), endPinId(0) {}
	Link(int id, int start, int end) : id(id), startPinId(start), endPinId(end) {}
};

/**
 * @class GraphNode
 * @brief Abstract base class representing a single processing node within the procedural pipeline.
 */
class GraphNode
{
public:
	int id; ///< Unique identifier of the node.
	std::string title; ///< Display title of the node.
	std::vector<Pin> inputs; ///< Array of input pins.
	std::vector<Pin> outputs; ///< Array of output pins.
	glm::vec2 editorPos = glm::vec2(0.0f); ///< Coordinates position in the ImNodes editor workspace.
	bool positionSet = false; ///< Flag indicating if editor position has been defined.

	GraphNode() : id(0) {}
	virtual ~GraphNode() = default;

	/**
	 * @brief Abstract method to draw customized ImGui node configuration parameters.
	 */
	virtual void RenderContent(SceneManager* scene) = 0;

	using NodeProgressCallback = std::function<void(float, const std::string&)>;
	
	/**
	 * @brief Abstract method to process input values and update output pin payloads.
	 */
	virtual void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) = 0;

	/**
	 * @brief Triggered when the node is deleted to perform resource cleanup.
	 */
	virtual void OnRemove(SceneManager& scene) {}

	/**
	 * @brief Triggered when scene objects are renamed to synchronize name references.
	 */
	virtual void OnObjectRenamed(const std::string& oldName, const std::string& newName) {}

	Pin* FindPin(int pinId);
	Pin* FindInputPin(int pinId);
	Pin* FindOutputPin(int pinId);

	virtual json Serialize() const;
	virtual void Deserialize(const json& j);
};

/**
 * @class NodeGraph
 * @brief Master controller containing lists of nodes and connections, managing evaluation flows.
 */
class NodeGraph
{
public:
	NodeGraph();
	explicit NodeGraph(std::shared_ptr<int> sharedNextId);
	~NodeGraph();

	/**
	 * @brief Appends a node to the graph memory tracking.
	 */
	void AddNode(GraphNode* node);

	/**
	 * @brief Removes a node and all its connected links.
	 */
	void RemoveNode(int nodeId, SceneManager* scene = nullptr);
	
	GraphNode* FindNode(int nodeId);
	GraphNode* FindNodeByPinId(int pinId);

	/**
	 * @brief Creates a connection link between an output and input pin.
	 */
	bool AddLink(int outputPinId, int inputPinId);
	void RemoveLink(int linkId);
	void RemoveLinkByPinId(int pinId);
	
	/**
	 * @brief Validates if two pins can be connected (datatype check + cycle detection).
	 */
	bool CanLink(int outputPinId, int inputPinId);

	/**
	 * @brief Evaluates the entire graph, sorting nodes topologically and executing them sequentially.
	 */
	void Execute(SceneManager& scene, Texture* defaultTex, Material* defaultMat, std::function<void(float, float, const std::string&)> progressCallback = nullptr);

	std::vector<GraphNode*>& GetNodes() { return nodes; }
	std::vector<Link>& GetLinks() { return links; }

	int NextLinkId() { return m_sharedNextId ? (*m_sharedNextId)++ : nextId++; }
	int NextNodeId() { return m_sharedNextId ? (*m_sharedNextId)++ : nextId++; }
	int NextPinId()  { return m_sharedNextId ? (*m_sharedNextId)++ : nextId++; }

	void SetSharedNextId(std::shared_ptr<int> shared) { m_sharedNextId = shared; }
	int GetNextIdValue() const { return m_sharedNextId ? *m_sharedNextId : nextId; }

	json Serialize() const;
	void Deserialize(const json& j, SceneManager& scene);

	bool IsObjectGenerated(const std::string& name) const;
	bool IsObjectMeshModified(const std::string& name) const;

	/**
	 * @brief Dispatches object rename notifications to all active nodes.
	 */
	void NotifyObjectRenamed(const std::string& oldName, const std::string& newName);

	/**
	 * @brief Clears all nodes, links, and cached states.
	 */
	void Clear();

private:
	std::vector<GraphNode*> nodes;
	std::vector<Link> links;
	int nextId;
	std::shared_ptr<int> m_sharedNextId;

	/**
	 * @brief Sorts nodes topologically to define a valid dependency execution order.
	 */
	std::vector<GraphNode*> TopologicalSort();

	/**
	 * @brief Copies output pin payloads into their target input pins across active links.
	 */
	void PropagateData();

	std::vector<std::string> generatedObjectNames;
};
