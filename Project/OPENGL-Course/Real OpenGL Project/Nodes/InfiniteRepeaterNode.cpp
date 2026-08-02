#include "Nodes/InfiniteRepeaterNode.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "Scene/WorldStreamerComponent.h"
#include "imgui.h"

InfiniteRepeaterNode::InfiniteRepeaterNode(NodeGraph& graph)
	: targetName("(none)"), targetIndex(-1), referenceName("(none)"), referenceIndex(-1), radius(2)
{
	id = graph.NextNodeId();
	title = "Infinite Repeater";

	// No inputs/outputs. It's a pure configuration node.
}

void InfiniteRepeaterNode::RenderContent(SceneManager* scene)
{
	if (!scene) return;
	std::vector<GameObject*> allObjects;
	scene->GetAllObjects(allObjects);

	ImGui::DragInt("Radius", &radius, 1, 0, 50);

	bool valid = false;
	if (targetName != "(none)" && !targetName.empty())
	{
		GameObject* target = scene->FindObject(targetName);
		if (target) {
			valid = true;
			targetIndex = -1;
			for (int i = 0; i < (int)allObjects.size(); i++) {
				if (allObjects[i] == target) {
					targetIndex = i;
					break;
				}
			}
		}
	}

	if (!valid)
	{
		targetName = "(none)";
		targetIndex = -1;
	}

	if (ImGui::BeginCombo("Target Object", targetName.c_str()))
	{
		for (int i = 0; i < (int)allObjects.size(); i++)
		{
			bool isSelected = (targetName == allObjects[i]->GetName());
			if (ImGui::Selectable(allObjects[i]->GetName().c_str(), isSelected))
			{
				targetIndex = i;
				targetName = allObjects[i]->GetName();
			}
		}
		ImGui::EndCombo();
	}

	bool refValid = false;
	if (referenceName != "(none)" && !referenceName.empty())
	{
		GameObject* refTarget = scene->FindObject(referenceName);
		if (refTarget) {
			refValid = true;
			referenceIndex = -1;
			for (int i = 0; i < (int)allObjects.size(); i++) {
				if (allObjects[i] == refTarget) {
					referenceIndex = i;
					break;
				}
			}
		}
	}

	if (!refValid)
	{
		referenceName = "(none)";
		referenceIndex = -1;
	}

	if (ImGui::BeginCombo("Reference Object (Size)", referenceName.c_str()))
	{
		for (int i = 0; i < (int)allObjects.size(); i++)
		{
			bool isSelected = (referenceName == allObjects[i]->GetName());
			if (ImGui::Selectable(allObjects[i]->GetName().c_str(), isSelected))
			{
				referenceIndex = i;
				referenceName = allObjects[i]->GetName();
			}
		}
		ImGui::EndCombo();
	}
}

json InfiniteRepeaterNode::Serialize() const
{
	json j = GraphNode::Serialize();
	j["targetName"] = targetName;
	j["referenceName"] = referenceName;
	j["radius"] = radius;
	return j;
}

void InfiniteRepeaterNode::Deserialize(const json& j)
{
	GraphNode::Deserialize(j);
	targetName = j.value("targetName", "(none)");
	referenceName = j.value("referenceName", "(none)");
	radius = j.value("radius", 2);
}

void InfiniteRepeaterNode::Execute(SceneManager& scene, NodeProgressCallback progress)
{
	GameObject* target = nullptr;
	if (targetName != "(none)" && !targetName.empty()) {
		target = scene.FindObject(targetName);
	}
	
	if (!target) {
		target = scene.FindObject("WorldStreamerManager");
		if (!target) {
			target = new GameObject("WorldStreamerManager");
			scene.AddObject(target);
		}
	}

	GameObject* refTarget = nullptr;
	if (referenceName != "(none)" && !referenceName.empty())
		refTarget = scene.FindObject(referenceName);

	WorldStreamerComponent* comp = target->GetComponent<WorldStreamerComponent>();
	if (!comp)
	{
		comp = target->AddComponent<WorldStreamerComponent>(&scene, refTarget, radius);
	}
	else
	{
		comp->SetReferenceObject(refTarget);
		comp->SetRadius(radius);
	}
}
