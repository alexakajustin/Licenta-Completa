#include "SceneInputNode.h"
#include "PrimitiveGenerator.h"
#include "imgui.h"

SceneInputNode::SceneInputNode(NodeGraph& graph)
{
	id = graph.NextNodeId();
	title = "Scene Input";

	// No inputs
	// One output: Mesh
	Pin meshOut(graph.NextPinId(), PinDataType::Mesh, "Mesh");
	outputs.push_back(meshOut);
}

void SceneInputNode::RenderContent(SceneManager* scene)
{
	if (!scene) return;
	auto& objects = scene->GetObjects();

	if (ImGui::BeginCombo("Object", selectedName.c_str()))
	{
		for (int i = 0; i < (int)objects.size(); i++)
		{
			bool isSelected = (selectedIndex == i);
			if (ImGui::Selectable(objects[i]->GetName().c_str(), isSelected))
			{
				selectedIndex = i;
				selectedName = objects[i]->GetName();
			}
		}
	}
}

void SceneInputNode::Execute(SceneManager& scene)
{
	outputs[0].data.Clear();
	outputs[0].data.type = PinDataType::Mesh;

	if (selectedName == "(none)") return;

	MeshData data;
	bool found = false;

	// Check if it's a primitive or a scene object
	auto& objects = scene.GetObjects();
	if (selectedIndex >= 0 && selectedIndex < (int)objects.size())
	{
		GameObject* obj = objects[selectedIndex];
		if (obj->GetMesh())
		{
			// Start with primitive data if it's one of the standard primitives
			if (selectedName.find("Plane") != std::string::npos) data = PrimitiveGenerator::GetPlaneData();
			else if (selectedName.find("Sphere") != std::string::npos) data = PrimitiveGenerator::GetSphereData();
			else if (selectedName.find("Cube") != std::string::npos) data = PrimitiveGenerator::GetCubeData();
			
			found = true;
		}
	}

	if (found) {
		outputs[0].data.meshData = data;
	}
}
