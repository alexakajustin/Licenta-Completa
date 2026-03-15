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

	// RE-VALIDATE: If reference is lost or index shifted, try to find by name
	bool valid = false;
	if (selectedIndex >= 0 && selectedIndex < (int)objects.size())
	{
		if (objects[selectedIndex]->GetName() == selectedName) valid = true;
	}

	if (!valid && selectedName != "(none)")
	{
		// Try to recover index by name
		selectedIndex = -1;
		for (int i = 0; i < (int)objects.size(); i++)
		{
			if (objects[i]->GetName() == selectedName)
			{
				selectedIndex = i;
				valid = true;
				break;
			}
		}

		// If still not found, object was deleted
		if (!valid)
		{
			selectedName = "(none)";
			selectedIndex = -1;
		}
	}

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
		ImGui::EndCombo();
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
			// Try to retrieve persisted procedural mesh data if available
			if (obj->HasCustomMesh())
			{
				data = obj->GetCPUMeshData();
				found = true;
			}
			else
			{
				// Fallback to primitive data if it's one of the standard primitives
				if (selectedName.find("Plane") != std::string::npos) { data = PrimitiveGenerator::GetPlaneData(); found = true; }
				else if (selectedName.find("Sphere") != std::string::npos) { data = PrimitiveGenerator::GetSphereData(); found = true; }
				else if (selectedName.find("Cube") != std::string::npos) { data = PrimitiveGenerator::GetCubeData(); found = true; }
			}
		}
	}

	if (found) {
		glm::vec3 scale = objects[selectedIndex]->GetTransform().GetScale();
		if (scale != glm::vec3(1.0f))
		{
			for (size_t i = 0; i < data.vertices.size(); i += 14)
			{
				data.vertices[i] *= scale.x;
				data.vertices[i + 1] *= scale.y;
				data.vertices[i + 2] *= scale.z;
			}
		}
		outputs[0].data.meshData = data;
		outputs[0].data.sourceObjectName = selectedName;

		// Propagate transform data so downstream nodes can handle scale/restore
		TransformData t;
		t.position = objects[selectedIndex]->GetTransform().GetPosition();
		t.rotation = objects[selectedIndex]->GetTransform().GetRotation();
		t.scale = objects[selectedIndex]->GetTransform().GetScale();
		outputs[0].data.transforms.push_back(t);
	}
}
