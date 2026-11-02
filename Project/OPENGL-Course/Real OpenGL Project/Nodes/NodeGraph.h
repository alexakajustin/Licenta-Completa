#pragma once

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "Nodes/NodeValue.h"

#include "External Libs/nlohmann/json.hpp"

using json = nlohmann::json;

class SceneManager;
class Texture;
class Material;
class GameObject;
class InstancedGroup;
class NodeGraph;
class GraphNode;

// =====================================================================
// Procedural node graph
//
//  GraphNode   a processing step: typed input/output pins and declared parameters. Parameters
//              give the node its UI and its save format for free, and every scalar parameter has
//              an inline input pin: anything (a Seed, a Random Value, Math...) can drive it.
//  NodeGraph   nodes + links. Execute() runs the nodes in dependency order; each one reads the
//              values on its inputs and writes its outputs. Nodes that change the scene (Mesh
//              Output, Spawn Instances, River, City...) do it through their NodeContext, which
//              records what each node created: what a node stops producing is removed after the
//              run, and everything it made goes when the node is deleted.
//  Saving      stable type ids, pins by key, parameters by key. Graphs saved by older versions
//              are converted on load (see GraphMigration.cpp).
// =====================================================================

struct Pin
{
	int id = 0;
	PinType type = PinType::None;
	std::string name;   // Label
	std::string key;    // Stable name in the save file
	bool multi = false; // An input taking any number of links (their values are combined)
	int param = -1;     // An inline pin driving params[param]

	// While the graph runs: an output's value, or what reached an input
	NodeValue value;
	std::vector<NodeValue> sources; // Every value reaching a multi input, in link order
	bool linked = false;

	// Editor: screen y of the parameter row an inline pin sits on this frame (< 0: none)
	float anchorY = -1.0f;
};

struct Link
{
	int id = 0;
	int startPinId = 0; // Output pin
	int endPinId = 0;   // Input pin

	Link() {}
	Link(int id, int start, int end) : id(id), startPinId(start), endPinId(end) {}
};

// A parameter bound to a member of its node
struct NodeParam
{
	enum class Kind { Float, Int, Bool, Vec2, Vec3, Enum, Seed, Objects };

	Kind kind = Kind::Float;
	std::string key;     // Save key (the same names older saves used)
	std::string label;
	std::string tooltip;
	void* target = nullptr;
	float speed = 0.1f;
	float minV = 0.0f, maxV = 0.0f; // min == max: unbounded
	std::string format;
	std::vector<std::string> options; // Enum
	bool single = false;               // Objects: one object (a picker) rather than a list
	std::function<bool()> visible;     // Shown (and driven) only when this says so
	std::string section;               // Heading drawn above the first parameter of a section
	std::string legacyKey;             // Key an older save used for it
	int pinId = 0;                     // Inline input pin (0: none)
	std::string drivenText;            // Value a link set during the last run (shown instead of the widget)

	NodeParam& Tip(const std::string& text) { tooltip = text; return *this; }
	NodeParam& ShowIf(std::function<bool()> f) { visible = std::move(f); return *this; }
	NodeParam& Format(const char* f) { format = f; return *this; }
	NodeParam& Section(const std::string& title) { section = title; return *this; }
	NodeParam& Legacy(const std::string& oldKey) { legacyKey = oldKey; return *this; }
	bool Visible() const { return !visible || visible(); }
};

// What a node sees while it executes
class NodeContext
{
public:
	NodeContext(SceneManager& scene, NodeGraph& graph, GraphNode& node, std::function<void(float, const std::string&)> progress)
		: scene(scene), graph(graph), node(node), progressFn(std::move(progress)) {}

	SceneManager& scene;
	NodeGraph& graph;
	GraphNode& node;

	void Progress(float percent, const std::string& message) const { if (progressFn) progressFn(percent, message); }

	// Scene writes. Everything a node creates is recorded as its own: when the node stops
	// producing it (renamed, fewer results) it is removed after the run, and all of it goes
	// when the node is deleted.
	GameObject* Object(const std::string& name, GameObject* parent = nullptr); // Find or create
	void Claim(const std::string& objectName);                                // Created another way (clones...)
	void AddGroup(InstancedGroup* group);                                     // Replaces a group of the same name
	void ClaimGroup(const std::string& groupName);

	bool OutputUsed(size_t index) const; // Something is linked to this output
	void Warn(const std::string& message);
	void Info(const std::string& message); // Shown under the node until the next run

private:
	std::function<void(float, const std::string&)> progressFn;
};

class GraphNode
{
public:
	int id = 0;
	std::string type;  // Registry id (stable, saved)
	std::string title; // Shown in the header
	std::vector<Pin> inputs;
	std::vector<Pin> outputs;
	std::vector<NodeParam> params;
	glm::vec2 editorPos = glm::vec2(0.0f);

	// Last run
	struct Status {
		bool ran = false;
		bool failed = false;
		std::string message; // Error or warning
		std::string info;    // Short result line ("4 000 points")
		double ms = 0.0;
	} status;

	// Scene objects and instanced groups this node created (saved: cleaned up across sessions)
	std::set<std::string> ownedObjects;
	std::set<std::string> ownedGroups;

	explicit GraphNode(NodeGraph& graph);
	virtual ~GraphNode() = default;
	GraphNode(const GraphNode&) = delete;
	GraphNode& operator=(const GraphNode&) = delete;

	// Read the input values (In), write the outputs (Out). Throw to report an error.
	virtual void Execute(NodeContext& ctx) = 0;

	// Widgets inside the node. Default: the parameters.
	virtual void RenderContent(SceneManager* scene) { DrawParams(scene); }

	virtual void OnRemove(SceneManager& scene); // Default: removes what the node created
	virtual void OnObjectRenamed(const std::string& oldName, const std::string& newName); // Default: Objects parameters follow

	// State beyond the parameters
	virtual void SaveState(json& j) const { (void)j; }
	virtual void LoadState(const json& j) { (void)j; }

	// Graphs saved before pins had keys: what each saved pin (by position) is now
	virtual std::vector<std::string> LegacyInputKeys() const;
	virtual std::vector<std::string> LegacyOutputKeys() const;

	json Serialize() const;
	void LoadParams(const json& j);
	void LoadPins(const json& j); // Pin ids by key (current format)

	Pin* FindPin(int pinId);
	Pin* FindInputPin(int pinId);
	Pin* FindOutputPin(int pinId);
	Pin* Input(const std::string& key);
	Pin* Output(const std::string& key);
	NodeParam* FindParam(const std::string& key);
	// Sets a number/bool/enum/seed parameter by key (templates, tests); false if there is none
	bool SetParamValue(const std::string& key, double value);
	bool SetParamObjects(const std::string& key, const std::vector<std::string>& names);

	// Values while executing
	const NodeValue& In(size_t index) const { return inputs[index].value; }
	const NodeValue& In(const std::string& key) const;
	bool Linked(size_t index) const { return inputs[index].linked; }
	void Out(size_t index, NodeValue value) { outputs[index].value = std::move(value); }

	// The automatic parameter UI (also what RenderContent draws by default)
	void DrawParams(SceneManager* scene);

protected:
	NodeGraph* owner = nullptr;
	bool paramPins = true; // Parameters declared while false get no inline pin (a constant's own value)

	Pin& AddInput(PinType type, const std::string& name, const std::string& key, bool multi = false);
	Pin& AddOutput(PinType type, const std::string& name, const std::string& key);

	NodeParam& AddFloat(const std::string& key, const std::string& label, float* value, float speed, float minV = 0.0f, float maxV = 0.0f, const char* format = "%.3f");
	NodeParam& AddInt(const std::string& key, const std::string& label, int* value, float speed, int minV = 0, int maxV = 0);
	NodeParam& AddBool(const std::string& key, const std::string& label, bool* value);
	NodeParam& AddVec2(const std::string& key, const std::string& label, glm::vec2* value, float speed);
	NodeParam& AddVec3(const std::string& key, const std::string& label, glm::vec3* value, float speed);
	NodeParam& AddEnum(const std::string& key, const std::string& label, int* value, std::vector<std::string> options);
	// A random seed: a dice button rolls a new one; linked, it is mixed with the node's id so nodes
	// sharing one Seed node still differ
	NodeParam& AddSeed(const std::string& key, const std::string& label, int* value);
	NodeParam& AddObjects(const std::string& key, const std::string& label, std::vector<std::string>* names, bool single);

	// One parameter row; true when it changed
	bool DrawParam(NodeParam& p, SceneManager* scene);

private:
	NodeParam& AddParam(NodeParam p, PinType pinType);
};

class NodeGraph
{
public:
	NodeGraph();
	explicit NodeGraph(std::shared_ptr<int> sharedNextId);
	~NodeGraph();

	void AddNode(GraphNode* node);
	// With a scene, the node's scene objects are removed with it
	void RemoveNode(int nodeId, SceneManager* scene = nullptr);

	GraphNode* FindNode(int nodeId);
	GraphNode* FindNodeByPinId(int pinId);
	Pin* FindPin(int pinId, GraphNode** ownerNode = nullptr);

	// Output -> input. Types must convert, a single input takes one link, no cycles.
	bool AddLink(int outputPinId, int inputPinId);
	bool CanLink(int outputPinId, int inputPinId);
	void RemoveLink(int linkId);
	void RemoveLinkByPinId(int pinId);
	bool IsPinLinked(int pinId) const;

	// Runs every node in dependency order
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

	void NotifyObjectRenamed(const std::string& oldName, const std::string& newName);
	void Clear();

	static constexpr int FORMAT_VERSION = 2;

	// Unique for the whole session (undo steps find their graph by it, not by address)
	uint64_t GetUid() const { return uid; }

	// Values are dropped after a run (they can be huge); tests and tools may keep them to inspect
	bool keepValues = false;

private:
	uint64_t uid;
	std::vector<GraphNode*> nodes;
	std::vector<Link> links;
	int nextId;
	std::shared_ptr<int> m_sharedNextId;

	std::vector<GraphNode*> TopologicalSort(std::vector<GraphNode*>* skipped = nullptr);
	void GatherInputs(GraphNode* node);
	void DropInvalidLinks();

	friend struct GraphMigration;
};
