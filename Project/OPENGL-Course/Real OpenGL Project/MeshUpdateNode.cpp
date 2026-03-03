#include "MeshUpdateNode.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "Mesh.h"
#include "imgui.h"

MeshUpdateNode::MeshUpdateNode(NodeGraph& graph)
{
	id = graph.NextNodeId();
	title = "Update Scene Mesh";

	// One input: Mesh
	Pin meshIn(graph.NextPinId(), PinDataType::Mesh, "Mesh");
	inputs.push_back(meshIn);
}

void MeshUpdateNode::RenderContent(SceneManager* scene)
{
	if (!scene) return;
	auto& objects = scene->GetObjects();

	if (ImGui::BeginCombo("Target Object", targetName.c_str()))
	{
		for (int i = 0; i < (int)objects.size(); i++)
		{
			bool isSelected = (targetIndex == i);
			if (ImGui::Selectable(objects[i]->GetName().c_str(), isSelected))
			{
				targetIndex = i;
				targetName = objects[i]->GetName();
			}
		}
		ImGui::EndCombo();
	}

	if (targetIndex >= 0 && targetIndex < (int)objects.size())
		ImGui::TextColored(ImVec4(0, 1, 0, 1), "Target: %s", objects[targetIndex]->GetName().c_str());
}

void MeshUpdateNode::Execute()
{
	// Logic is handled in NodeGraph::Execute because it needs access to SceneManager
}
