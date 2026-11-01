#include "Nodes/GraphMigration.h"
#include "Nodes/NodeGraph.h"
#include "Nodes/NodeRegistry.h"
#include "Nodes/SceneInputNode.h"
#include "Procedural/ScatterNode.h"
#include "Procedural/SpawnInstancesNode.h"
#include "Procedural/InteriorGenNode.h"
#include "Nodes/SceneObjectsNode.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace {
	struct LegacyLink {
		int id;
		int oldStart, oldEnd;
		GraphNode* from = nullptr;
		GraphNode* to = nullptr;
		std::string fromKey, toKey; // What the old pins are now ("furniture:bed", "legacy:object"...)
	};

	std::string BaseKey(const std::string& key)
	{
		size_t colon = key.find(':');
		return colon == std::string::npos ? key : key.substr(0, colon);
	}
}

void GraphMigration::LoadLegacy(NodeGraph& g, const nlohmann::json& j, SceneManager& scene)
{
	(void)scene;
	std::map<int, std::pair<GraphNode*, std::string>> oldPins; // Old pin id -> node, legacy key
	std::map<int, int> remap;                                   // Old pin id -> new pin id

	if (j.contains("nodes")) {
		for (const auto& nj : j["nodes"]) {
			std::string title = nj.value("title", "");
			GraphNode* node = nullptr;
			if (const NodeTypeInfo* info = NodeRegistry::FindByLegacyTitle(title)) node = NodeRegistry::Create(info->id, g);
			else node = NodeRegistry::Create("custom:" + nj.value("definitionName", title), g);
			if (!node) {
				printf("[GraphMigration] Unknown node '%s' (id %d): skipped\n", title.c_str(), nj.value("id", 0));
				continue;
			}
			node->id = nj.value("id", node->id);
			if (nj.contains("editorPos") && nj["editorPos"].size() >= 2)
				node->editorPos = glm::vec2(nj["editorPos"][0].get<float>(), nj["editorPos"][1].get<float>());
			node->LoadParams(nj);
			node->LoadState(nj);

			auto mapPins = [&](const char* listKey, const std::vector<std::string>& keys, bool input) {
				if (!nj.contains(listKey)) return;
				const auto& ids = nj[listKey];
				for (size_t i = 0; i < ids.size(); i++) {
					int oldId = ids[i].get<int>();
					std::string key = i < keys.size() ? keys[i] : std::string();
					oldPins[oldId] = { node, key };
					Pin* pin = input ? node->Input(BaseKey(key)) : node->Output(BaseKey(key));
					if (pin && key.rfind("legacy:", 0) != 0) remap[oldId] = pin->id;
				}
			};
			mapPins("inputPins", node->LegacyInputKeys(), true);
			mapPins("outputPins", node->LegacyOutputKeys(), false);
			g.AddNode(node);
		}
	}

	std::vector<LegacyLink> legacyLinks;
	if (j.contains("links")) {
		for (const auto& lj : j["links"]) {
			LegacyLink l;
			l.id = lj.value("id", 0);
			l.oldStart = lj.value("start", 0);
			l.oldEnd = lj.value("end", 0);
			auto a = oldPins.find(l.oldStart), b = oldPins.find(l.oldEnd);
			if (a != oldPins.end()) { l.from = a->second.first; l.fromKey = a->second.second; }
			if (b != oldPins.end()) { l.to = b->second.first; l.toKey = b->second.second; }
			legacyLinks.push_back(l);
		}
	}

	auto link = [&](Pin* out, Pin* in) {
		if (!out || !in) return;
		for (auto& l : g.GetLinks()) if (l.startPinId == out->id && l.endPinId == in->id) return;
		g.GetLinks().push_back(Link(g.NextLinkId(), out->id, in->id));
	};

	struct FurnitureFeed { SceneInputNode* source; std::string category; };
	std::map<InteriorGenNode*, std::vector<FurnitureFeed>> furnitureFeeds;

	for (const auto& l : legacyLinks) {
		if (!l.from || !l.to) continue;

		// Interior Gen: a furniture pin per category (sorted out below)
		if (l.toKey.rfind("furniture:", 0) == 0) {
			auto* interior = dynamic_cast<InteriorGenNode*>(l.to);
			auto* source = dynamic_cast<SceneInputNode*>(l.from);
			if (interior && source) furnitureFeeds[interior].push_back({ source, l.toKey.substr(10) });
			continue;
		}
		// Scatter's object input: what it spawned (handled with the Spawn Instances below)
		if (l.toKey == "legacy:object") continue;

		auto a = remap.find(l.oldStart), b = remap.find(l.oldEnd);
		if (a == remap.end() || b == remap.end()) {
			printf("[GraphMigration] Link %d -> %d has no counterpart: dropped\n", l.oldStart, l.oldEnd);
			continue;
		}
		Pin* out = g.FindPin(a->second);
		Pin* in = g.FindPin(b->second);
		// A Scene Input's mesh into a pin that now takes objects: its object output instead
		if (out && in && in->type == PinType::Objects && out->type == PinType::Mesh)
			if (auto* source = dynamic_cast<SceneInputNode*>(l.from)) out = source->Output("object");
		link(out, in);
	}

	// Scatter spawned its instances itself, from the last filter of the chain hanging off its
	// points: end that chain in a Spawn Instances node
	std::vector<GraphNode*> nodesNow = g.GetNodes();
	for (GraphNode* node : nodesNow) {
		auto* scatter = dynamic_cast<ScatterNode*>(node);
		if (!scatter || !scatter->legacy.spawn) continue;

		SceneInputNode* objectSource = nullptr;
		for (const auto& l : legacyLinks)
			if (l.to == scatter && l.toKey == "legacy:object") objectSource = dynamic_cast<SceneInputNode*>(l.from);

		// The chain: Scatter -> filters (each one's Passed output) -> ...
		Pin* end = scatter->Output("points");
		GraphNode* endNode = scatter;
		for (int guard = 0; guard < 64; guard++) {
			GraphNode* next = nullptr;
			for (auto& l : g.GetLinks()) {
				if (l.startPinId != end->id) continue;
				GraphNode* down = g.FindNodeByPinId(l.endPinId);
				if (down && (down->type == "filter_points" || down->type == "object_filter")) { next = down; break; }
			}
			if (!next) break;
			end = next->Output("passed");
			endNode = next;
		}

		auto* spawn = static_cast<SpawnInstancesNode*>(NodeRegistry::Create("spawn_instances", g));
		spawn->editorPos = endNode->editorPos + glm::vec2(320.0f, 0.0f);
		spawn->SetAlignToNormal(scatter->legacy.alignToNormal);
		spawn->SetLegacyScale(true);
		std::vector<SpawnInstancesNode::DeletionVolume> volumes;
		for (const auto& v : scatter->legacy.deletionVolumes) volumes.push_back({ v.position, v.radius });
		spawn->SetDeletionVolumes(std::move(volumes));
		// What the Scatter made before is this node's now: replaced and cleaned up on its first run
		spawn->ownedGroups = scatter->legacy.groups;
		std::string objectName = objectSource ? objectSource->GetSelectedName() : std::string();
		if (!objectName.empty()) {
			spawn->ownedGroups.insert("Scatter_Instanced_" + std::to_string(scatter->id) + "_" + objectName);
			spawn->ownedObjects.insert("Scatter_Group_" + std::to_string(scatter->id) + "_" + objectName);
		}
		g.AddNode(spawn);
		link(end, spawn->Input("points"));
		if (objectSource) link(objectSource->Output("object"), spawn->Input("objects"));
		printf("[GraphMigration] Scatter %d: its instances now come from Spawn Instances %d\n", scatter->id, spawn->id);
	}

	// Interior Gen's furniture: the Scene Inputs that fed its category pins become one Scene
	// Objects list, each object labelled with its category (when they fed nothing else); otherwise
	// they link their objects into the Furniture list and the categories go in the node's table
	for (auto& [interior, feeds] : furnitureFeeds) {
		bool exclusive = true;
		std::set<std::string> names;
		for (const auto& f : feeds) {
			if (!names.insert(f.source->GetSelectedName()).second) exclusive = false;
			for (const auto& l : legacyLinks)
				if (l.from == f.source && !(l.to == interior && l.toKey.rfind("furniture:", 0) == 0)) exclusive = false;
		}
		if (exclusive) {
			auto* list = static_cast<SceneObjectsNode*>(NodeRegistry::Create("scene_objects", g));
			list->editorPos = feeds.front().source->editorPos;
			for (const auto& f : feeds) list->AddEntry(f.source->GetSelectedName(), 1.0f, f.category);
			g.AddNode(list);
			link(list->Output("objects"), interior->Input("furniture"));
			for (const auto& f : feeds) g.RemoveNode(f.source->id);
			printf("[GraphMigration] Interior Gen %d: %d furniture inputs -> one Scene Objects list\n", interior->id, (int)feeds.size());
		} else {
			for (const auto& f : feeds) {
				std::string name = f.source->GetSelectedName();
				if (name != "(none)") interior->SetFurnitureOverride(f.category, { name });
				link(f.source->Output("object"), interior->Input("furniture"));
			}
		}
	}

	auto linksFrom = [&](Pin* out) {
		std::vector<Link*> result;
		for (auto& l : g.GetLinks()) if (out && l.startPinId == out->id) result.push_back(&l);
		return result;
	};
	auto linksInto = [&](Pin* in) {
		std::vector<Link*> result;
		for (auto& l : g.GetLinks()) if (in && l.endPinId == in->id) result.push_back(&l);
		return result;
	};

	// Older graphs read the same object in many Scene Input nodes (the terrain once per scatter
	// layer): one node per object, feeding all of them
	{
		std::map<std::pair<std::string, bool>, SceneInputNode*> keepers;
		std::vector<int> duplicates;
		for (GraphNode* node : std::vector<GraphNode*>(g.GetNodes())) {
			auto* input = dynamic_cast<SceneInputNode*>(node);
			if (!input || input->GetSelectedName() == "(none)") continue;
			bool original = *(bool*)input->FindParam("forceOriginalPrimitive")->target;
			auto key = std::make_pair(input->GetSelectedName(), original);
			auto it = keepers.find(key);
			if (it == keepers.end()) { keepers[key] = input; continue; }
			for (const char* out : { "mesh", "object" })
				for (Link* l : linksFrom(input->Output(out))) l->startPinId = it->second->Output(out)->id;
			duplicates.push_back(input->id);
		}
		for (int id : duplicates) g.RemoveNode(id);
		if (!duplicates.empty()) printf("[GraphMigration] %d duplicate Scene Input nodes merged\n", (int)duplicates.size());
	}

	// Chained Object Filters avoiding one object each (the river, then the lake): one filter
	// avoiding both (the same points pass)
	for (bool merged = true; merged;) {
		merged = false;
		for (GraphNode* a : g.GetNodes()) {
			if (a->type != "object_filter" || !linksFrom(a->Output("rejected")).empty()) continue;
			auto out = linksFrom(a->Output("passed"));
			if (out.size() != 1) continue;
			GraphNode* b = g.FindNodeByPinId(out[0]->endPinId);
			if (!b || b == a || b->type != "object_filter" || b->Input("points")->id != out[0]->endPinId) continue;
			if (linksInto(b->Input("points")).size() != 1 || !linksFrom(b->Output("rejected")).empty()) continue;
			if (!linksInto(a->Input("targets")).empty() || !linksInto(b->Input("targets")).empty()) continue;
			auto same = [&](const char* key) { return a->Serialize()[key] == b->Serialize()[key]; };
			if (*(int*)a->FindParam("filterMode")->target != 0 || !same("filterMode") || !same("verticalMode") ||
				!same("bufferDistance") || !same("horizontalMargin")) continue;
			auto& targetsA = *(std::vector<std::string>*)a->FindParam("targets")->target;
			auto& targetsB = *(std::vector<std::string>*)b->FindParam("targets")->target;
			for (const auto& t : targetsB)
				if (std::find(targetsA.begin(), targetsA.end(), t) == targetsA.end()) targetsA.push_back(t);
			for (Link* l : linksFrom(b->Output("passed"))) l->startPinId = a->Output("passed")->id;
			printf("[GraphMigration] Object Filters %d + %d merged\n", a->id, b->id);
			g.RemoveNode(b->id);
			merged = true;
			break;
		}
	}
}
