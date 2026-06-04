#include "Nodes/OutputNode.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "Rendering/Mesh.h"
#include "imgui.h"

OutputNode::OutputNode(NodeGraph& graph)
	: sameAsInput(true) // Default to true for convenience
{
	id = graph.NextNodeId();
	title = "Output";

	// One input: Mesh
	Pin meshIn(graph.NextPinId(), PinDataType::Mesh, "Mesh");
	inputs.push_back(meshIn);
}

void OutputNode::RenderContent(SceneManager* scene)
{
	if (!scene) return;
	std::vector<GameObject*> allObjects;
	scene->GetAllObjects(allObjects);

	ImGui::Checkbox("Same As Input", &sameAsInput);
	ImGui::SameLine();
	ImGui::Checkbox("Update Target Mesh", &updateMesh);

	if (!sameAsInput)
	{
		// RE-VALIDATE: If reference is lost or name shifted, try to find
		bool valid = false;
		if (targetName != "(none)" && !targetName.empty())
		{
			GameObject* target = scene->FindObject(targetName);
			if (target) {
				valid = true;
				// Resolve index in flat allObjects list for UI state
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

		if (targetIndex >= 0 && targetIndex < (int)allObjects.size())
			ImGui::TextColored(ImVec4(0, 1, 0, 1), "Target: %s", allObjects[targetIndex]->GetName().c_str());
	}
}

json OutputNode::Serialize() const
{
	json j = GraphNode::Serialize();
	j["targetName"] = targetName;
	j["sameAsInput"] = sameAsInput;
	j["updateMesh"] = updateMesh;
	return j;
}

void OutputNode::Deserialize(const json& j)
{
	GraphNode::Deserialize(j);
	targetName = j.value("targetName", "(none)");
	sameAsInput = j.value("sameAsInput", false);
	updateMesh = j.value("updateMesh", true);
}

void OutputNode::Execute(SceneManager& scene, NodeProgressCallback progress)
{
	// Logic is handled in NodeGraph::Execute because it needs access to SceneManager
}
