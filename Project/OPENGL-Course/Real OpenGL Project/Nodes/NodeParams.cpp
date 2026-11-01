// The automatic parameter UI of graph nodes: one row per parameter, its inline input pin anchored
// on the row, a dice for seeds, object pickers that also take objects dragged from the hierarchy.
#include "Nodes/NodeGraph.h"
#include "Nodes/NodeRandom.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "imgui.h"

#include <algorithm>
#include <cstdio>

namespace {
	// A small die drawn with the draw list (no icon font needed); true when clicked
	bool DiceButton(const char* id, float size)
	{
		ImVec2 p = ImGui::GetCursorScreenPos();
		bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
		bool hovered = ImGui::IsItemHovered();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		float pad = size * 0.16f;
		ImVec2 a(p.x + pad, p.y + pad), b(p.x + size - pad, p.y + size - pad);
		ImU32 face = hovered ? IM_COL32(236, 228, 255, 255) : IM_COL32(200, 194, 222, 255);
		dl->AddRectFilled(a, b, face, size * 0.16f);
		ImU32 pip = IM_COL32(40, 32, 64, 255);
		float r = std::max(size * 0.065f, 1.0f);
		float w = b.x - a.x;
		auto at = [&](float u, float v) { dl->AddCircleFilled(ImVec2(a.x + w * u, a.y + w * v), r, pip, 8); };
		at(0.27f, 0.27f); at(0.73f, 0.27f); at(0.5f, 0.5f); at(0.27f, 0.73f); at(0.73f, 0.73f);
		if (hovered) ImGui::SetTooltip("Roll a new random seed");
		return clicked;
	}

	// Objects dragged in from the hierarchy onto the last item
	std::string DroppedObject(SceneManager* scene)
	{
		std::string name;
		if (!scene || !ImGui::BeginDragDropTarget()) return name;
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT_INDEX")) {
			int index = *(const int*)payload->Data;
			auto& objects = scene->GetObjects();
			if (index >= 0 && index < (int)objects.size()) name = objects[index]->GetName();
		}
		ImGui::EndDragDropTarget();
		return name;
	}

	// A combo of every scene object; true when one was picked
	bool ObjectCombo(const char* id, const std::string& current, SceneManager* scene, std::string& picked, float width)
	{
		bool changed = false;
		ImGui::SetNextItemWidth(width);
		if (ImGui::BeginCombo(id, current.empty() ? "(none)" : current.c_str(), ImGuiComboFlags_HeightLarge)) {
			if (scene) {
				std::vector<GameObject*> all;
				scene->GetAllObjects(all);
				for (int i = 0; i < (int)all.size(); i++) {
					ImGui::PushID(i);
					const std::string& name = all[i]->GetName();
					if (ImGui::Selectable(name.c_str(), name == current)) { picked = name; changed = true; }
					ImGui::PopID();
				}
			}
			ImGui::EndCombo();
		}
		std::string dropped = DroppedObject(scene);
		if (!dropped.empty()) { picked = dropped; changed = true; }
		return changed;
	}
}

void GraphNode::DrawParams(SceneManager* scene)
{
	std::string lastSection;
	for (auto& p : params) {
		if (!p.Visible()) continue;
		if (!p.section.empty() && p.section != lastSection) {
			ImGui::SeparatorText(p.section.c_str());
			lastSection = p.section;
		}
		DrawParam(p, scene);
	}
}

bool GraphNode::DrawParam(NodeParam& p, SceneManager* scene)
{
	ImGui::PushID(p.key.c_str());
	const float frameH = ImGui::GetFrameHeight();
	const float width = ImGui::CalcItemWidth();

	// The inline pin sits on this row
	Pin* pin = p.pinId ? FindInputPin(p.pinId) : nullptr;
	if (pin) pin->anchorY = ImGui::GetCursorScreenPos().y + frameH * 0.5f;

	bool changed = false;
	if (pin && owner && owner->IsPinLinked(pin->id)) {
		// Driven by a link: the value comes from the graph
		ImGui::AlignTextToFramePadding();
		if (p.drivenText.empty()) ImGui::TextDisabled("%s  (linked)", p.label.c_str());
		else ImGui::TextDisabled("%s = %s  (linked)", p.label.c_str(), p.drivenText.c_str());
		if (!p.tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.tooltip.c_str());
		ImGui::PopID();
		return false;
	}

	const char* fmt = p.format.empty() ? nullptr : p.format.c_str();
	const bool bounded = p.maxV > p.minV;
	switch (p.kind) {
	case NodeParam::Kind::Float: {
		float* v = (float*)p.target;
		ImGui::SetNextItemWidth(width);
		changed = ImGui::DragFloat("##v", v, p.speed, p.minV, p.maxV, fmt ? fmt : "%.3f", bounded ? ImGuiSliderFlags_AlwaysClamp : 0);
		break;
	}
	case NodeParam::Kind::Int: {
		int* v = (int*)p.target;
		ImGui::SetNextItemWidth(width);
		changed = ImGui::DragInt("##v", v, p.speed, (int)p.minV, (int)p.maxV, "%d", bounded ? ImGuiSliderFlags_AlwaysClamp : 0);
		break;
	}
	case NodeParam::Kind::Seed: {
		int* v = (int*)p.target;
		ImGui::SetNextItemWidth(std::max(width - frameH - ImGui::GetStyle().ItemInnerSpacing.x, frameH * 2.0f));
		changed = ImGui::DragInt("##v", v, 1.0f, 0, NodeRandom::MAX_SEED - 1, "%d", ImGuiSliderFlags_AlwaysClamp);
		if (!p.tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.tooltip.c_str());
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
		if (DiceButton("##dice", frameH)) { *v = NodeRandom::TrueRandomSeed(); changed = true; }
		break;
	}
	case NodeParam::Kind::Bool:
		changed = ImGui::Checkbox("##v", (bool*)p.target);
		break;
	case NodeParam::Kind::Vec2:
		ImGui::SetNextItemWidth(width * 1.4f);
		changed = ImGui::DragFloat2("##v", &((glm::vec2*)p.target)->x, p.speed, 0.0f, 0.0f, fmt ? fmt : "%.3f");
		break;
	case NodeParam::Kind::Vec3:
		ImGui::SetNextItemWidth(width * 1.8f);
		changed = ImGui::DragFloat3("##v", &((glm::vec3*)p.target)->x, p.speed, 0.0f, 0.0f, fmt ? fmt : "%.3f");
		break;
	case NodeParam::Kind::Enum: {
		int* v = (int*)p.target;
		const char* current = (*v >= 0 && *v < (int)p.options.size()) ? p.options[*v].c_str() : "?";
		ImGui::SetNextItemWidth(width);
		if (ImGui::BeginCombo("##v", current)) {
			for (int i = 0; i < (int)p.options.size(); i++)
				if (ImGui::Selectable(p.options[i].c_str(), i == *v)) { *v = i; changed = true; }
			ImGui::EndCombo();
		}
		break;
	}
	case NodeParam::Kind::Objects: {
		auto& names = *(std::vector<std::string>*)p.target;
		if (p.single) {
			std::string picked;
			if (ObjectCombo("##v", names.empty() ? std::string() : names[0], scene, picked, width)) {
				names = { picked };
				changed = true;
			}
		} else {
			// The list, one row each, then a picker that appends
			ImGui::BeginGroup();
			int remove = -1;
			for (int i = 0; i < (int)names.size(); i++) {
				ImGui::PushID(i);
				std::string picked;
				if (ObjectCombo("##e", names[i], scene, picked, width - frameH - ImGui::GetStyle().ItemInnerSpacing.x)) { names[i] = picked; changed = true; }
				ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
				if (ImGui::Button("x", ImVec2(frameH, frameH))) remove = i;
				ImGui::PopID();
			}
			if (remove >= 0) { names.erase(names.begin() + remove); changed = true; }
			std::string added;
			if (ObjectCombo("##add", names.empty() ? "(add objects)" : "+ add", scene, added, width) && !added.empty()) {
				if (std::find(names.begin(), names.end(), added) == names.end()) names.push_back(added);
				changed = true;
			}
			ImGui::EndGroup();
		}
		break;
	}
	}

	if (!p.tooltip.empty() && p.kind != NodeParam::Kind::Seed && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.tooltip.c_str());

	ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(p.label.c_str());
	if (!p.tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.tooltip.c_str());

	ImGui::PopID();
	return changed;
}
