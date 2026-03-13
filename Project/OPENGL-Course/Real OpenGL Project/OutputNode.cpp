#include "OutputNode.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "Mesh.h"
#include "imgui.h"

OutputNode::OutputNode(NodeGraph& graph)
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

void OutputNode::Execute(SceneManager& scene)
{
	// Logic is handled in NodeGraph::Execute because it needs access to SceneManager
}
