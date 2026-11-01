#include "Nodes/NodeGraph.h"
#include "Nodes/NodeRegistry.h"
#include "Nodes/NodeRandom.h"
#include "Nodes/GraphMigration.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "Rendering/InstancedGroup.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>

// =====================================================================
// GraphNode
// =====================================================================

GraphNode::GraphNode(NodeGraph& graph) : owner(&graph)
{
	id = graph.NextNodeId();
}

Pin& GraphNode::AddInput(PinType type, const std::string& name, const std::string& key, bool multi)
{
	Pin p;
	p.id = owner->NextPinId();
	p.type = type;
	p.name = name;
	p.key = key;
	p.multi = multi;
	inputs.push_back(p);
	return inputs.back();
}

Pin& GraphNode::AddOutput(PinType type, const std::string& name, const std::string& key)
{
	Pin p;
	p.id = owner->NextPinId();
	p.type = type;
	p.name = name;
	p.key = key;
	outputs.push_back(p);
	return outputs.back();
}

NodeParam& GraphNode::AddParam(NodeParam p, PinType pinType)
{
	int index = (int)params.size();
	if (pinType != PinType::None && paramPins) {
		Pin& pin = AddInput(pinType, p.label, p.key, p.kind == NodeParam::Kind::Objects && !p.single);
		pin.param = index;
		p.pinId = pin.id;
	}
	params.push_back(std::move(p));
	return params.back();
}

NodeParam& GraphNode::AddFloat(const std::string& key, const std::string& label, float* value, float speed, float minV, float maxV, const char* format)
{
	NodeParam p;
	p.kind = NodeParam::Kind::Float;
	p.key = key; p.label = label; p.target = value;
	p.speed = speed; p.minV = minV; p.maxV = maxV;
	p.format = format ? format : "%.3f";
	return AddParam(std::move(p), PinType::Float);
}

NodeParam& GraphNode::AddInt(const std::string& key, const std::string& label, int* value, float speed, int minV, int maxV)
{
	NodeParam p;
	p.kind = NodeParam::Kind::Int;
	p.key = key; p.label = label; p.target = value;
	p.speed = speed; p.minV = (float)minV; p.maxV = (float)maxV;
	p.format = "%d";
	return AddParam(std::move(p), PinType::Int);
}

NodeParam& GraphNode::AddBool(const std::string& key, const std::string& label, bool* value)
{
	NodeParam p;
	p.kind = NodeParam::Kind::Bool;
	p.key = key; p.label = label; p.target = value;
	return AddParam(std::move(p), PinType::Bool);
}

NodeParam& GraphNode::AddVec2(const std::string& key, const std::string& label, glm::vec2* value, float speed)
{
	NodeParam p;
	p.kind = NodeParam::Kind::Vec2;
	p.key = key; p.label = label; p.target = value; p.speed = speed;
	p.format = "%.3f";
	return AddParam(std::move(p), PinType::Vec2);
}

NodeParam& GraphNode::AddVec3(const std::string& key, const std::string& label, glm::vec3* value, float speed)
{
	NodeParam p;
	p.kind = NodeParam::Kind::Vec3;
	p.key = key; p.label = label; p.target = value; p.speed = speed;
	p.format = "%.3f";
	return AddParam(std::move(p), PinType::Vec3);
}

NodeParam& GraphNode::AddEnum(const std::string& key, const std::string& label, int* value, std::vector<std::string> options)
{
	NodeParam p;
	p.kind = NodeParam::Kind::Enum;
	p.key = key; p.label = label; p.target = value;
	p.options = std::move(options);
	return AddParam(std::move(p), PinType::None);
}

NodeParam& GraphNode::AddSeed(const std::string& key, const std::string& label, int* value)
{
	NodeParam p;
	p.kind = NodeParam::Kind::Seed;
	p.key = key; p.label = label; p.target = value;
	p.tooltip = "Random seed: the same seed gives the same result, the dice rolls a new one.\n"
		"Linked (e.g. to a Seed node), the incoming seed is mixed with this node's id,\n"
		"so nodes sharing one seed still differ from each other.";
	return AddParam(std::move(p), PinType::Int);
}

NodeParam& GraphNode::AddObjects(const std::string& key, const std::string& label, std::vector<std::string>* names, bool single)
{
	NodeParam p;
	p.kind = NodeParam::Kind::Objects;
	p.key = key; p.label = label; p.target = names; p.single = single;
	return AddParam(std::move(p), PinType::Objects);
}

Pin* GraphNode::FindPin(int pinId)
{
	if (Pin* p = FindInputPin(pinId)) return p;
	return FindOutputPin(pinId);
}

Pin* GraphNode::FindInputPin(int pinId)
{
	for (auto& p : inputs) if (p.id == pinId) return &p;
	return nullptr;
}

Pin* GraphNode::FindOutputPin(int pinId)
{
	for (auto& p : outputs) if (p.id == pinId) return &p;
	return nullptr;
}

Pin* GraphNode::Input(const std::string& key)
{
	for (auto& p : inputs) if (p.key == key) return &p;
	return nullptr;
}

Pin* GraphNode::Output(const std::string& key)
{
	for (auto& p : outputs) if (p.key == key) return &p;
	return nullptr;
}

NodeParam* GraphNode::FindParam(const std::string& key)
{
	for (auto& p : params) if (p.key == key) return &p;
	return nullptr;
}

bool GraphNode::SetParamValue(const std::string& key, double value)
{
	NodeParam* p = FindParam(key);
	if (!p) return false;
	switch (p->kind) {
	case NodeParam::Kind::Float: *(float*)p->target = (float)value; return true;
	case NodeParam::Kind::Int:
	case NodeParam::Kind::Enum:
	case NodeParam::Kind::Seed:  *(int*)p->target = (int)value; return true;
	case NodeParam::Kind::Bool:  *(bool*)p->target = value != 0.0; return true;
	default: return false;
	}
}

bool GraphNode::SetParamObjects(const std::string& key, const std::vector<std::string>& names)
{
	NodeParam* p = FindParam(key);
	if (!p || p->kind != NodeParam::Kind::Objects) return false;
	auto& list = *(std::vector<std::string>*)p->target;
	list = names;
	if (p->single && list.size() > 1) list.resize(1);
	return true;
}

const NodeValue& GraphNode::In(const std::string& key) const
{
	static const NodeValue none;
	for (auto& p : inputs) if (p.key == key) return p.value;
	return none;
}

void GraphNode::OnRemove(SceneManager& scene)
{
	for (const auto& name : ownedGroups) scene.RemoveInstancedGroup(name);
	for (const auto& name : ownedObjects) scene.RemoveObject(name);
	ownedGroups.clear();
	ownedObjects.clear();
}

void GraphNode::OnObjectRenamed(const std::string& oldName, const std::string& newName)
{
	for (auto& p : params) {
		if (p.kind != NodeParam::Kind::Objects) continue;
		for (auto& n : *(std::vector<std::string>*)p.target)
			if (n == oldName) n = newName;
	}
	if (ownedObjects.erase(oldName)) ownedObjects.insert(newName);
}

std::vector<std::string> GraphNode::LegacyInputKeys() const
{
	std::vector<std::string> keys;
	for (auto& p : inputs) if (p.param < 0) keys.push_back(p.key);
	return keys;
}

std::vector<std::string> GraphNode::LegacyOutputKeys() const
{
	std::vector<std::string> keys;
	for (auto& p : outputs) keys.push_back(p.key);
	return keys;
}

json GraphNode::Serialize() const
{
	json j;
	j["id"] = id;
	j["type"] = type;
	j["title"] = title;
	j["editorPos"] = { editorPos.x, editorPos.y };

	json in = json::object(), out = json::object();
	for (auto& p : inputs) in[p.key] = p.id;
	for (auto& p : outputs) out[p.key] = p.id;
	j["in"] = in;
	j["out"] = out;

	// Parameters at the top level, under the keys older saves used
	for (auto& p : params) {
		switch (p.kind) {
		case NodeParam::Kind::Float: j[p.key] = *(float*)p.target; break;
		case NodeParam::Kind::Int:
		case NodeParam::Kind::Enum:
		case NodeParam::Kind::Seed:  j[p.key] = *(int*)p.target; break;
		case NodeParam::Kind::Bool:  j[p.key] = *(bool*)p.target; break;
		case NodeParam::Kind::Vec2: { auto& v = *(glm::vec2*)p.target; j[p.key] = { v.x, v.y }; break; }
		case NodeParam::Kind::Vec3: { auto& v = *(glm::vec3*)p.target; j[p.key] = { v.x, v.y, v.z }; break; }
		case NodeParam::Kind::Objects: {
			auto& names = *(std::vector<std::string>*)p.target;
			if (p.single) j[p.key] = names.empty() ? std::string("(none)") : names[0];
			else j[p.key] = names;
			break;
		}
		}
	}

	if (!ownedObjects.empty() || !ownedGroups.empty())
		j["owned"] = { { "objects", ownedObjects }, { "groups", ownedGroups } };

	SaveState(j);
	return j;
}

void GraphNode::LoadParams(const json& j)
{
	for (auto& p : params) {
		auto it = j.find(p.key);
		if ((it == j.end() || it->is_null()) && !p.legacyKey.empty()) it = j.find(p.legacyKey);
		if (it == j.end() || it->is_null()) continue;
		const json& v = *it;
		try {
			switch (p.kind) {
			case NodeParam::Kind::Float:
				if (v.is_number()) *(float*)p.target = v.get<float>();
				break;
			case NodeParam::Kind::Int:
			case NodeParam::Kind::Enum:
			case NodeParam::Kind::Seed:
				if (v.is_number_float()) *(int*)p.target = (int)v.get<double>();
				else if (v.is_number()) *(int*)p.target = v.get<int>();
				else if (v.is_boolean()) *(int*)p.target = v.get<bool>() ? 1 : 0;
				break;
			case NodeParam::Kind::Bool:
				if (v.is_boolean()) *(bool*)p.target = v.get<bool>();
				else if (v.is_number()) *(bool*)p.target = v.get<double>() != 0.0;
				break;
			case NodeParam::Kind::Vec2:
				if (v.is_array() && v.size() >= 2) *(glm::vec2*)p.target = glm::vec2(v[0].get<float>(), v[1].get<float>());
				break;
			case NodeParam::Kind::Vec3:
				if (v.is_array() && v.size() >= 3) *(glm::vec3*)p.target = glm::vec3(v[0].get<float>(), v[1].get<float>(), v[2].get<float>());
				break;
			case NodeParam::Kind::Objects: {
				auto& names = *(std::vector<std::string>*)p.target;
				names.clear();
				if (v.is_string()) {
					std::string s = v.get<std::string>();
					if (!s.empty() && s != "(none)") names.push_back(s);
				} else if (v.is_array()) {
					for (auto& e : v)
						if (e.is_string() && !e.get<std::string>().empty()) names.push_back(e.get<std::string>());
				}
				if (p.single && names.size() > 1) names.resize(1);
				break;
			}
			}
		}
		catch (const std::exception& e) {
			printf("[NodeGraph] '%s': could not read parameter '%s': %s\n", title.c_str(), p.key.c_str(), e.what());
		}
	}

	if (auto it = j.find("owned"); it != j.end() && it->is_object()) {
		ownedObjects.clear();
		ownedGroups.clear();
		if (it->contains("objects")) for (auto& n : (*it)["objects"]) ownedObjects.insert(n.get<std::string>());
		if (it->contains("groups")) for (auto& n : (*it)["groups"]) ownedGroups.insert(n.get<std::string>());
	}
}

void GraphNode::LoadPins(const json& j)
{
	auto apply = [](std::vector<Pin>& pins, const json& map) {
		if (!map.is_object()) return;
		for (auto& p : pins) {
			auto it = map.find(p.key);
			if (it != map.end() && it->is_number_integer()) p.id = it->get<int>();
		}
	};
	if (j.contains("in")) apply(inputs, j["in"]);
	if (j.contains("out")) apply(outputs, j["out"]);
}

// =====================================================================
// NodeContext
// =====================================================================

GameObject* NodeContext::Object(const std::string& name, GameObject* parent)
{
	GameObject* obj = scene.FindObject(name);
	if (!obj) {
		obj = new GameObject(name);
		scene.AddObject(obj);
	}
	if (parent && obj->GetParent() != parent) obj->SetParent(parent);
	node.ownedObjects.insert(name);
	return obj;
}

void NodeContext::Claim(const std::string& objectName)
{
	node.ownedObjects.insert(objectName);
}

void NodeContext::AddGroup(InstancedGroup* group)
{
	if (!group) return;
	scene.RemoveInstancedGroup(group->GetName());
	scene.AddInstancedGroup(group);
	node.ownedGroups.insert(group->GetName());
}

void NodeContext::ClaimGroup(const std::string& groupName)
{
	node.ownedGroups.insert(groupName);
}

bool NodeContext::OutputUsed(size_t index) const
{
	return index < node.outputs.size() && graph.IsPinLinked(node.outputs[index].id);
}

void NodeContext::Warn(const std::string& message)
{
	if (!node.status.message.empty()) node.status.message += "\n";
	node.status.message += message;
	printf("[%s] %s\n", node.title.c_str(), message.c_str());
}

void NodeContext::Info(const std::string& message)
{
	node.status.info = message;
}

// =====================================================================
// NodeGraph
// =====================================================================

NodeGraph::NodeGraph() : nextId(1) {}

NodeGraph::NodeGraph(std::shared_ptr<int> sharedNextId) : nextId(1), m_sharedNextId(sharedNextId) {}

NodeGraph::~NodeGraph()
{
	Clear();
}

void NodeGraph::AddNode(GraphNode* node)
{
	if (!node) return;
	NodeRegistry::Identify(node);
	nodes.push_back(node);
}

void NodeGraph::RemoveNode(int nodeId, SceneManager* scene)
{
	GraphNode* node = FindNode(nodeId);
	if (!node) return;
	if (scene) node->OnRemove(*scene);

	std::set<int> pinIds;
	for (auto& p : node->inputs) pinIds.insert(p.id);
	for (auto& p : node->outputs) pinIds.insert(p.id);
	links.erase(std::remove_if(links.begin(), links.end(), [&](const Link& l) {
		return pinIds.count(l.startPinId) || pinIds.count(l.endPinId);
	}), links.end());

	nodes.erase(std::remove(nodes.begin(), nodes.end(), node), nodes.end());
	delete node;
}

GraphNode* NodeGraph::FindNode(int nodeId)
{
	for (auto* n : nodes) if (n->id == nodeId) return n;
	return nullptr;
}

GraphNode* NodeGraph::FindNodeByPinId(int pinId)
{
	GraphNode* owner = nullptr;
	FindPin(pinId, &owner);
	return owner;
}

Pin* NodeGraph::FindPin(int pinId, GraphNode** ownerNode)
{
	for (auto* n : nodes) {
		if (Pin* p = n->FindPin(pinId)) {
			if (ownerNode) *ownerNode = n;
			return p;
		}
	}
	if (ownerNode) *ownerNode = nullptr;
	return nullptr;
}

bool NodeGraph::IsPinLinked(int pinId) const
{
	for (auto& l : links) if (l.startPinId == pinId || l.endPinId == pinId) return true;
	return false;
}

bool NodeGraph::CanLink(int outputPinId, int inputPinId)
{
	GraphNode* outNode = nullptr;
	GraphNode* inNode = nullptr;
	if (!FindPin(outputPinId, &outNode) || !FindPin(inputPinId, &inNode)) return false;
	if (outNode == inNode) return false;

	Pin* outPin = outNode->FindOutputPin(outputPinId);
	Pin* inPin = inNode->FindInputPin(inputPinId);
	if (!outPin || !inPin) return false;
	if (!PinTypesConvertible(outPin->type, inPin->type)) return false;

	// A single input takes one link; a multi input takes each output once
	for (auto& l : links) {
		if (l.endPinId != inputPinId) continue;
		if (!inPin->multi || l.startPinId == outputPinId) return false;
	}

	// No cycles: walking downstream from the input's node must not reach the output's node
	std::vector<GraphNode*> stack{ inNode };
	std::set<GraphNode*> seen;
	while (!stack.empty()) {
		GraphNode* n = stack.back();
		stack.pop_back();
		if (n == outNode) return false;
		if (!seen.insert(n).second) continue;
		for (auto& out : n->outputs)
			for (auto& l : links)
				if (l.startPinId == out.id)
					if (GraphNode* next = FindNodeByPinId(l.endPinId)) stack.push_back(next);
	}
	return true;
}

bool NodeGraph::AddLink(int outputPinId, int inputPinId)
{
	if (!CanLink(outputPinId, inputPinId)) return false;
	links.push_back(Link(NextLinkId(), outputPinId, inputPinId));
	return true;
}

void NodeGraph::RemoveLink(int linkId)
{
	links.erase(std::remove_if(links.begin(), links.end(), [linkId](const Link& l) { return l.id == linkId; }), links.end());
}

void NodeGraph::RemoveLinkByPinId(int pinId)
{
	links.erase(std::remove_if(links.begin(), links.end(), [pinId](const Link& l) {
		return l.startPinId == pinId || l.endPinId == pinId;
	}), links.end());
}

void NodeGraph::DropInvalidLinks()
{
	std::set<std::pair<int, int>> seen;
	links.erase(std::remove_if(links.begin(), links.end(), [&](const Link& l) {
		GraphNode* a = nullptr;
		GraphNode* b = nullptr;
		Pin* out = FindPin(l.startPinId, &a);
		Pin* in = FindPin(l.endPinId, &b);
		bool ok = out && in && a != b && a->FindOutputPin(l.startPinId) && b->FindInputPin(l.endPinId) &&
			PinTypesConvertible(out->type, in->type) && seen.insert({ l.startPinId, l.endPinId }).second;
		if (!ok) printf("[NodeGraph] Dropped an invalid link (%d -> %d)\n", l.startPinId, l.endPinId);
		return !ok;
	}), links.end());

	// A single input keeps its first link
	std::set<int> taken;
	links.erase(std::remove_if(links.begin(), links.end(), [&](const Link& l) {
		Pin* in = FindPin(l.endPinId);
		if (!in || in->multi) return false;
		return !taken.insert(l.endPinId).second;
	}), links.end());
}

std::vector<GraphNode*> NodeGraph::TopologicalSort(std::vector<GraphNode*>* skipped)
{
	std::map<int, GraphNode*> pinOwner;
	for (auto* n : nodes) {
		for (auto& p : n->inputs) pinOwner[p.id] = n;
		for (auto& p : n->outputs) pinOwner[p.id] = n;
	}
	std::map<GraphNode*, std::set<GraphNode*>> downstream;
	std::map<GraphNode*, int> inDegree;
	for (auto* n : nodes) inDegree[n] = 0;
	for (auto& l : links) {
		auto a = pinOwner.find(l.startPinId), b = pinOwner.find(l.endPinId);
		if (a == pinOwner.end() || b == pinOwner.end() || a->second == b->second) continue;
		if (downstream[a->second].insert(b->second).second) inDegree[b->second]++;
	}

	// Kahn, in the graph's own node order whenever there is a choice (deterministic runs)
	std::vector<GraphNode*> sorted;
	std::vector<GraphNode*> ready;
	for (auto* n : nodes) if (inDegree[n] == 0) ready.push_back(n);
	size_t head = 0;
	while (head < ready.size()) {
		GraphNode* n = ready[head++];
		sorted.push_back(n);
		for (auto* m : nodes) {
			if (!downstream[n].count(m)) continue;
			if (--inDegree[m] == 0) ready.push_back(m);
		}
	}
	if (skipped)
		for (auto* n : nodes)
			if (std::find(sorted.begin(), sorted.end(), n) == sorted.end()) skipped->push_back(n);
	return sorted;
}

void NodeGraph::GatherInputs(GraphNode* node)
{
	for (auto& pin : node->inputs) {
		pin.value = NodeValue();
		pin.sources.clear();
		pin.linked = false;

		std::vector<const Link*> incoming;
		for (auto& l : links) if (l.endPinId == pin.id) incoming.push_back(&l);
		std::sort(incoming.begin(), incoming.end(), [](const Link* a, const Link* b) { return a->id < b->id; });
		for (const Link* l : incoming) {
			Pin* src = FindPin(l->startPinId);
			if (!src || !src->value.Valid()) continue;
			pin.sources.push_back(src->value.ConvertTo(pin.type));
		}
		pin.linked = !incoming.empty();
		if (pin.sources.empty()) continue;

		if (pin.sources.size() == 1) {
			pin.value = pin.sources[0];
		} else if (pin.type == PinType::Points) {
			TransformList all;
			size_t total = 0;
			for (auto& s : pin.sources) total += s.Points().size();
			all.reserve(total);
			for (auto& s : pin.sources) all.insert(all.end(), s.Points().begin(), s.Points().end());
			pin.value = NodeValue::FromPoints(std::move(all));
		} else if (pin.type == PinType::Objects) {
			ObjectList all;
			for (auto& s : pin.sources) all.insert(all.end(), s.Objects().begin(), s.Objects().end());
			pin.value = NodeValue::FromObjects(std::move(all));
		} else {
			pin.value = pin.sources[0]; // Nodes read every mesh from `sources`
		}
	}
}

namespace {
	// A parameter overridden by a link for one run, and its own value to put back afterwards
	struct DrivenParam {
		NodeParam* param = nullptr;
		float f = 0.0f;
		int i = 0;
		bool b = false;
		glm::vec2 v2 = glm::vec2(0.0f);
		glm::vec3 v3 = glm::vec3(0.0f);
		std::vector<std::string> names;
	};

	float ClampTo(const NodeParam& p, float v) { return p.maxV > p.minV ? std::clamp(v, p.minV, p.maxV) : v; }

	std::vector<DrivenParam> ApplyDrivenParams(GraphNode* node)
	{
		std::vector<DrivenParam> driven;
		for (auto& p : node->params) {
			p.drivenText.clear();
			if (!p.pinId) continue;
			Pin* pin = node->FindInputPin(p.pinId);
			if (!pin || !pin->linked || !pin->value.Valid()) continue;
			const NodeValue& v = pin->value;
			DrivenParam d;
			d.param = &p;
			switch (p.kind) {
			case NodeParam::Kind::Float:
				d.f = *(float*)p.target;
				*(float*)p.target = ClampTo(p, v.AsFloat());
				break;
			case NodeParam::Kind::Int:
				d.i = *(int*)p.target;
				*(int*)p.target = (int)ClampTo(p, (float)v.AsInt());
				break;
			case NodeParam::Kind::Seed:
				d.i = *(int*)p.target;
				*(int*)p.target = NodeRandom::ToSeed(NodeRandom::Mix((uint64_t)(uint32_t)v.AsInt(), (uint64_t)node->id));
				break;
			case NodeParam::Kind::Bool:
				d.b = *(bool*)p.target;
				*(bool*)p.target = v.AsBool();
				break;
			case NodeParam::Kind::Vec2:
				d.v2 = *(glm::vec2*)p.target;
				*(glm::vec2*)p.target = v.AsVec2();
				break;
			case NodeParam::Kind::Vec3:
				d.v3 = *(glm::vec3*)p.target;
				*(glm::vec3*)p.target = v.AsVec3();
				break;
			case NodeParam::Kind::Objects: {
				auto& names = *(std::vector<std::string>*)p.target;
				d.names = names;
				names.clear();
				for (auto& e : v.Objects()) names.push_back(e.name);
				if (p.single && names.size() > 1) names.resize(1);
				break;
			}
			default:
				continue;
			}
			// What the link set, for the node's UI
			switch (p.kind) {
			case NodeParam::Kind::Float: {
				char buf[64];
				snprintf(buf, sizeof(buf), p.format.empty() ? "%.3f" : p.format.c_str(), *(float*)p.target);
				p.drivenText = buf;
				break;
			}
			case NodeParam::Kind::Int:
			case NodeParam::Kind::Seed: p.drivenText = std::to_string(*(int*)p.target); break;
			case NodeParam::Kind::Bool: p.drivenText = *(bool*)p.target ? "on" : "off"; break;
			default:                    p.drivenText = v.Summary(); break;
			}
			driven.push_back(std::move(d));
		}
		return driven;
	}

	void RestoreDrivenParams(std::vector<DrivenParam>& driven)
	{
		for (auto& d : driven) {
			NodeParam& p = *d.param;
			switch (p.kind) {
			case NodeParam::Kind::Float:   *(float*)p.target = d.f; break;
			case NodeParam::Kind::Int:
			case NodeParam::Kind::Seed:    *(int*)p.target = d.i; break;
			case NodeParam::Kind::Bool:    *(bool*)p.target = d.b; break;
			case NodeParam::Kind::Vec2:    *(glm::vec2*)p.target = d.v2; break;
			case NodeParam::Kind::Vec3:    *(glm::vec3*)p.target = d.v3; break;
			case NodeParam::Kind::Objects: *(std::vector<std::string>*)p.target = d.names; break;
			default: break;
			}
		}
	}
}

void NodeGraph::Execute(SceneManager& scene, Texture* defaultTex, Material* defaultMat, std::function<void(float, float, const std::string&)> progressCallback)
{
	(void)defaultTex;
	(void)defaultMat;
	if (nodes.empty()) return;

	// Nodes may delete and recreate the objects they generate: keep the components users added
	// to them across the run
	std::map<std::string, std::vector<std::unique_ptr<Component>>> componentBackup;
	for (auto* obj : scene.GetObjects())
		if (obj && obj->HasComponents()) componentBackup[obj->GetName()] = obj->DetachAllComponents();

	for (auto* n : nodes) {
		for (auto& p : n->inputs) { p.value = NodeValue(); p.sources.clear(); p.linked = false; }
		for (auto& p : n->outputs) p.value = NodeValue();
		n->status = GraphNode::Status();
	}

	std::vector<GraphNode*> skipped;
	auto sorted = TopologicalSort(&skipped);
	for (auto* n : skipped) {
		n->status.failed = true;
		n->status.message = "Part of a cycle: not executed";
	}

	const float count = (float)std::max<size_t>(sorted.size(), 1);
	for (size_t i = 0; i < sorted.size(); i++) {
		GraphNode* node = sorted[i];
		auto report = [&](float nodePct, const std::string& msg) {
			if (progressCallback) progressCallback(((float)i + nodePct / 100.0f) / count * 100.0f, nodePct, msg);
		};
		report(0.0f, "Computing Node: " + node->title);

		GatherInputs(node);
		auto driven = ApplyDrivenParams(node);

		std::set<std::string> previousObjects, previousGroups;
		previousObjects.swap(node->ownedObjects);
		previousGroups.swap(node->ownedGroups);

		NodeContext ctx(scene, *this, *node, report);
		auto t0 = std::chrono::steady_clock::now();
		bool ok = true;
		try {
			node->Execute(ctx);
		}
		catch (const std::exception& e) {
			ok = false;
			node->status.message = e.what();
		}
		catch (...) {
			ok = false;
			node->status.message = "Unknown error";
		}
		node->status.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		node->status.ran = true;
		node->status.failed = !ok;
		RestoreDrivenParams(driven);

		if (!ok) {
			printf("[NodeGraph] '%s' failed: %s\n", node->title.c_str(), node->status.message.c_str());
			for (auto& p : node->outputs) p.value = NodeValue();
			// A failed run removes nothing: everything made before stays the node's
			node->ownedObjects.insert(previousObjects.begin(), previousObjects.end());
			node->ownedGroups.insert(previousGroups.begin(), previousGroups.end());
			continue;
		}

		// What the node made last time and not this time is gone
		for (const auto& name : previousGroups)
			if (!node->ownedGroups.count(name)) scene.RemoveInstancedGroup(name);
		for (const auto& name : previousObjects)
			if (!node->ownedObjects.count(name)) scene.RemoveObject(name);

		if (node->status.info.empty()) {
			for (auto& p : node->outputs)
				if (p.value.Valid() && !IsScalarPin(p.value.type)) { node->status.info = p.value.Summary(); break; }
		}
	}

	// Values are transient: a forest's points or a terrain's vertices must not stay in memory
	if (!keepValues) {
		for (auto* n : nodes) {
			for (auto& p : n->inputs) { p.value = NodeValue(); p.sources.clear(); }
			for (auto& p : n->outputs) p.value = NodeValue();
		}
	}

	for (auto& [name, comps] : componentBackup)
		if (GameObject* obj = scene.FindObject(name)) obj->AttachComponents(comps);

	if (progressCallback) progressCallback(100.0f, 100.0f, "Done!");
}

json NodeGraph::Serialize() const
{
	json j;
	j["version"] = FORMAT_VERSION;
	j["nextId"] = GetNextIdValue();

	json nodesArray = json::array();
	for (auto* n : nodes) nodesArray.push_back(n->Serialize());
	j["nodes"] = nodesArray;

	json linksArray = json::array();
	for (const auto& l : links) linksArray.push_back({ { "id", l.id }, { "start", l.startPinId }, { "end", l.endPinId } });
	j["links"] = linksArray;
	return j;
}

void NodeGraph::Deserialize(const json& j, SceneManager& scene)
{
	Clear();
	int loadedNextId = j.value("nextId", 1);
	if (m_sharedNextId) {
		if (loadedNextId > *m_sharedNextId) *m_sharedNextId = loadedNextId;
	} else {
		nextId = std::max(nextId, loadedNextId);
	}

	if (j.value("version", 1) < 2) {
		GraphMigration::LoadLegacy(*this, j, scene);
		DropInvalidLinks();
	} else {
		if (j.contains("nodes")) {
			for (const auto& nj : j["nodes"]) {
				std::string type = nj.value("type", "");
				GraphNode* node = NodeRegistry::Create(type, *this);
				if (!node) {
					printf("[NodeGraph] Unknown node type '%s' (node %d): skipped\n", type.c_str(), nj.value("id", 0));
					continue;
				}
				node->id = nj.value("id", node->id);
				if (nj.contains("editorPos") && nj["editorPos"].size() >= 2)
					node->editorPos = glm::vec2(nj["editorPos"][0].get<float>(), nj["editorPos"][1].get<float>());
				node->LoadParams(nj);
				node->LoadPins(nj);
				node->LoadState(nj);
				AddNode(node);
			}
		}
		if (j.contains("links"))
			for (const auto& lj : j["links"])
				links.push_back(Link(lj.value("id", 0), lj.value("start", 0), lj.value("end", 0)));
		DropInvalidLinks();
	}

	// Ids handed out from now on must not collide with anything loaded
	int maxId = 0;
	for (auto* n : nodes) {
		maxId = std::max(maxId, n->id);
		for (auto& p : n->inputs) maxId = std::max(maxId, p.id);
		for (auto& p : n->outputs) maxId = std::max(maxId, p.id);
	}
	for (auto& l : links) maxId = std::max(maxId, l.id);
	if (m_sharedNextId) *m_sharedNextId = std::max(*m_sharedNextId, maxId + 1);
	else nextId = std::max(nextId, maxId + 1);
}

void NodeGraph::NotifyObjectRenamed(const std::string& oldName, const std::string& newName)
{
	for (auto* node : nodes) node->OnObjectRenamed(oldName, newName);
}

void NodeGraph::Clear()
{
	for (auto* n : nodes) delete n;
	nodes.clear();
	links.clear();
}
