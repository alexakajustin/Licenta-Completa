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
		ImGui::EndCombo();
	}
}

json SceneInputNode::Serialize() const
{
	json j = GraphNode::Serialize();
	j["selectedName"] = selectedName;
	return j;
}

void SceneInputNode::Deserialize(const json& j)
{
	GraphNode::Deserialize(j);
	selectedName = j.value("selectedName", "(none)");
	selectedIndex = -1; // Force re-resolution on first Execute
}

void SceneInputNode::Execute(SceneManager& scene)
{
	outputs[0].data.Clear();
	outputs[0].data.type = PinDataType::Mesh;

	if (selectedName == "(none)") return;

	auto& objects = scene.GetObjects();
	GameObject* obj = nullptr;

	// Resolve index by name if needed (essential for load from file)
	if (selectedIndex < 0 || selectedIndex >= (int)objects.size() || objects[selectedIndex]->GetName() != selectedName)
	{
		selectedIndex = -1;
		for (int i = 0; i < (int)objects.size(); i++)
		{
			if (objects[i]->GetName() == selectedName)
			{
				selectedIndex = i;
				break;
			}
		}
	}

	MeshData data;
	bool found = false;

	if (selectedIndex >= 0 && selectedIndex < (int)objects.size())
	{
		obj = objects[selectedIndex];
		
		// 1. Try to retrieve persisted procedural mesh data if available
		if (obj->HasCustomMesh())
		{
			data = obj->GetCPUMeshData();
			found = true;
		}
		// 2. Fallback to primitive data if it matches standard names or primitive type
		else if (obj->GetPrimitiveType() == "Plane" || selectedName.find("Plane") != std::string::npos) { data = PrimitiveGenerator::GetPlaneData(); found = true; }
		else if (obj->GetPrimitiveType() == "Sphere" || selectedName.find("Sphere") != std::string::npos) { data = PrimitiveGenerator::GetSphereData(); found = true; }
		else if (obj->GetPrimitiveType() == "Cube" || selectedName.find("Cube") != std::string::npos) { data = PrimitiveGenerator::GetCubeData(); found = true; }
		// 3. Extract from Model if available (for loaded assets)
		else if (obj->GetModel() && !obj->GetModel()->GetMeshDataList().empty())
		{
			const auto& meshes = obj->GetModel()->GetMeshDataList();
			for (const auto& m : meshes)
			{
				int baseIdx = (int)data.vertices.size() / 14;
				data.vertices.insert(data.vertices.end(), m.vertices.begin(), m.vertices.end());
				for (unsigned int idx : m.indices)
				{
					data.indices.push_back(idx + baseIdx);
				}
			}
			if (!data.vertices.empty()) found = true;
		}
	}

	if (found) {
		glm::vec3 scale = objects[selectedIndex]->GetTransform().GetScale();
		
		outputs[0].data.meshData = data;
		outputs[0].data.sourceObjectName = selectedName;
		outputs[0].data.sourceMaterial = obj->GetMaterial();
		outputs[0].data.sourceTexture = obj->GetTexture();
		outputs[0].data.sourceNormalMap = obj->GetNormalMap();

		// Propagate transform data so downstream nodes can handle scale/restore
		TransformData t;
		t.position = obj->GetTransform().GetPosition();
		t.rotation = obj->GetTransform().GetRotation();
		t.scale = obj->GetTransform().GetScale();
		outputs[0].data.transforms.push_back(t);
	}
}
