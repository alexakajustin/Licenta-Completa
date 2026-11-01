#include "Nodes/SceneObjectsNode.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace {
	std::string Lower(std::string s)
	{
		for (auto& c : s) c = (char)std::tolower((unsigned char)c);
		return s;
	}

	// The scene object dragged onto the last item, if any
	std::string Dropped(SceneManager* scene)
	{
		std::string name;
		if (!scene || !ImGui::BeginDragDropTarget()) return name;
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT_INDEX")) {
			int index = *(const int*)payload->Data;
			if (index >= 0 && index < (int)scene->GetObjects().size()) name = scene->GetObjects()[index]->GetName();
		}
		ImGui::EndDragDropTarget();
		return name;
	}
}

SceneObjectsNode::SceneObjectsNode(NodeGraph& graph) : GraphNode(graph)
{
	AddOutput(PinType::Objects, "Objects", "objects");
	paramPins = false;
	AddEnum("mode", "From", &mode, { "Pick", "Children of", "Name contains" });
	AddObjects("parent", "Parent", &parent, true).ShowIf([this] { return mode == 1; }).Tip("Every child of this object goes in the list");
}

void SceneObjectsNode::SaveState(json& j) const
{
	json list = json::array();
	for (const auto& e : entries) list.push_back({ { "name", e.name }, { "weight", e.weight }, { "label", e.label } });
	j["entries"] = list;
	j["nameFilter"] = nameFilter;
}

void SceneObjectsNode::LoadState(const json& j)
{
	entries.clear();
	if (j.contains("entries"))
		for (const auto& e : j["entries"])
			entries.push_back({ e.value("name", ""), e.value("weight", 1.0f), e.value("label", "") });
	nameFilter = j.value("nameFilter", "");
}

void SceneObjectsNode::OnObjectRenamed(const std::string& oldName, const std::string& newName)
{
	GraphNode::OnObjectRenamed(oldName, newName);
	for (auto& e : entries) if (e.name == oldName) e.name = newName;
}

void SceneObjectsNode::RenderContent(SceneManager* scene)
{
	DrawParams(scene);
	const float frameH = ImGui::GetFrameHeight();
	const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
	const float width = ImGui::CalcItemWidth();

	if (mode == 0) {
		int remove = -1;
		for (int i = 0; i < (int)entries.size(); i++) {
			Entry& e = entries[i];
			ImGui::PushID(i);
			ImGui::SetNextItemWidth(width);
			if (ImGui::BeginCombo("##obj", e.name.empty() ? "(none)" : e.name.c_str(), ImGuiComboFlags_HeightLarge)) {
				if (scene) {
					std::vector<GameObject*> all;
					scene->GetAllObjects(all);
					for (int k = 0; k < (int)all.size(); k++) {
						ImGui::PushID(k);
						if (ImGui::Selectable(all[k]->GetName().c_str(), all[k]->GetName() == e.name)) e.name = all[k]->GetName();
						ImGui::PopID();
					}
				}
				ImGui::EndCombo();
			}
			std::string dropped = Dropped(scene);
			if (!dropped.empty()) e.name = dropped;
			ImGui::SameLine(0.0f, spacing);
			ImGui::SetNextItemWidth(frameH * 2.2f);
			ImGui::DragFloat("##w", &e.weight, 0.05f, 0.0f, 100.0f, "%.2f");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Weight: how often this object is picked among the others");
			ImGui::SameLine(0.0f, spacing);
			char label[64];
			strncpy_s(label, sizeof(label), e.label.c_str(), _TRUNCATE);
			ImGui::SetNextItemWidth(frameH * 3.5f);
			if (ImGui::InputTextWithHint("##label", "label", label, sizeof(label))) e.label = label;
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Optional tag, e.g. the furniture category for Interior Gen (bed, sofa...)");
			ImGui::SameLine(0.0f, spacing);
			if (ImGui::Button("x", ImVec2(frameH, frameH))) remove = i;
			ImGui::PopID();
		}
		if (remove >= 0) entries.erase(entries.begin() + remove);

		ImGui::SetNextItemWidth(width);
		if (ImGui::BeginCombo("##add", "+ add object", ImGuiComboFlags_HeightLarge)) {
			if (scene) {
				std::vector<GameObject*> all;
				scene->GetAllObjects(all);
				for (int k = 0; k < (int)all.size(); k++) {
					ImGui::PushID(k);
					if (ImGui::Selectable(all[k]->GetName().c_str())) AddEntry(all[k]->GetName());
					ImGui::PopID();
				}
			}
			ImGui::EndCombo();
		}
		std::string dropped = Dropped(scene);
		if (!dropped.empty()) AddEntry(dropped);
		ImGui::SameLine(0.0f, spacing);
		ImGui::TextDisabled("(or drag objects here)");
	} else if (mode == 2) {
		char buf[128];
		strncpy_s(buf, sizeof(buf), nameFilter.c_str(), _TRUNCATE);
		ImGui::SetNextItemWidth(width);
		if (ImGui::InputTextWithHint("##filter", "part of the name", buf, sizeof(buf))) nameFilter = buf;
		ImGui::SameLine(0.0f, spacing);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Name contains");
	}

	if (mode != 0 && !lastResolved.empty()) {
		ImGui::TextDisabled("%d objects", (int)lastResolved.size());
		if (ImGui::IsItemHovered()) {
			std::string tip;
			for (size_t i = 0; i < lastResolved.size() && i < 40; i++) tip += lastResolved[i] + "\n";
			if (lastResolved.size() > 40) tip += "...";
			ImGui::SetTooltip("%s", tip.c_str());
		}
	}
}

void SceneObjectsNode::Execute(NodeContext& ctx)
{
	ObjectList list;
	auto push = [&](GameObject* obj, float weight, const std::string& label) {
		ObjectEntry e;
		e.name = obj->GetName();
		e.object = obj;
		e.weight = weight;
		e.label = label;
		list.push_back(e);
	};

	if (mode == 0) {
		for (const auto& e : entries) {
			if (e.name.empty()) continue;
			GameObject* obj = ctx.scene.FindObject(e.name);
			if (!obj) { ctx.Warn("'" + e.name + "' is not in the scene"); continue; }
			push(obj, e.weight, e.label);
		}
	} else if (mode == 1) {
		GameObject* p = parent.empty() ? nullptr : ctx.scene.FindObject(parent[0]);
		if (!p) { if (!parent.empty()) ctx.Warn("Parent '" + parent[0] + "' not found"); }
		else for (GameObject* child : p->GetChildren()) push(child, 1.0f, "");
	} else if (!nameFilter.empty()) {
		std::string needle = Lower(nameFilter);
		std::vector<GameObject*> all;
		ctx.scene.GetAllObjects(all);
		for (GameObject* obj : all)
			if (Lower(obj->GetName()).find(needle) != std::string::npos) push(obj, 1.0f, "");
	}

	lastResolved.clear();
	for (auto& e : list) lastResolved.push_back(e.name);
	ctx.Info(list.size() == 1 ? list[0].name : std::to_string(list.size()) + " objects");
	Out(0, NodeValue::FromObjects(std::move(list)));
}
