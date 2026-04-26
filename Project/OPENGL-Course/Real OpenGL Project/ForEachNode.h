#pragma once

#include "NodeGraph.h"
#include "imgui.h"
#include <glm/gtc/matrix_transform.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =====================================================================
//  ForEachNode — Places a mesh at each transform in a TransformList
// =====================================================================
//
//  This is a general-purpose loop node. It takes a list of transforms
//  (from Scatter or Filter) and a mesh, and produces a combined mesh
//  with the object placed at each transform position/rotation/scale.
//
//  Unlike Scatter (which generates transforms), ForEach CONSUMES an
//  existing TransformList and applies arbitrary mesh data to it.
//
class ForEachNode : public GraphNode
{
public:
	ForEachNode(NodeGraph& graph)
	{
		id = graph.NextNodeId();
		title = "For Each";

		// Inputs
		Pin meshIn(graph.NextPinId(), PinDataType::Mesh, "Transforms");  // Mesh carrying TransformList
		Pin objectIn(graph.NextPinId(), PinDataType::Mesh, "Object");     // Mesh to place at each transform
		inputs.push_back(meshIn);
		inputs.push_back(objectIn);

		// Outputs
		Pin meshOut(graph.NextPinId(), PinDataType::Mesh, "Combined");
		outputs.push_back(meshOut);
	}

	json Serialize() const override
	{
		json j = GraphNode::Serialize();
		j["alignToNormal"] = alignToNormal;
		j["maxInstances"] = maxInstances;
		return j;
	}

	void Deserialize(const json& j) override
	{
		GraphNode::Deserialize(j);
		alignToNormal = j.value("alignToNormal", true);
		maxInstances = j.value("maxInstances", 10000);
	}

	void RenderContent(SceneManager* scene) override
	{
		ImGui::Checkbox("Align to Normal", &alignToNormal);
		ImGui::DragInt("Max Instances", &maxInstances, 100, 1, 1000000);

		if (lastProcessedCount >= 0)
		{
			ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Processed: %d", lastProcessedCount);
		}
	}

	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override
	{
		outputs[0].data.Clear();
		outputs[0].data.type = PinDataType::Mesh;

		// Propagate source metadata
		outputs[0].data.sourceObjectName = inputs[0].data.sourceObjectName;
		outputs[0].data.sourceObject = inputs[0].data.sourceObject;
		outputs[0].data.sourceMaterial = inputs[0].data.sourceMaterial;
		outputs[0].data.sourceTexture = inputs[0].data.sourceTexture;
		outputs[0].data.sourceNormalMap = inputs[0].data.sourceNormalMap;
		outputs[0].data.textureLayers = inputs[0].data.textureLayers;
		outputs[0].data.transforms = inputs[0].data.transforms; // Pass through transforms

		auto& transforms = inputs[0].data.transforms;
		MeshData& objectMesh = inputs[1].data.meshData;

		if (transforms.empty() || objectMesh.vertices.empty())
		{
			lastProcessedCount = 0;
			return;
		}

		int count = std::min((int)transforms.size(), maxInstances);
		MeshData combined;

		for (int i = 0; i < count; i++)
		{
			const TransformData& t = transforms[i];

			// Build transform matrix
			glm::mat4 model = glm::mat4(1.0f);
			model = glm::translate(model, t.position);

			// Align to surface normal if enabled
			if (alignToNormal && glm::length(t.normal) > 0.001f)
			{
				glm::vec3 up(0, 1, 0);
				if (glm::abs(glm::dot(up, t.normal)) < 0.999f)
				{
					glm::vec3 axis = glm::normalize(glm::cross(up, t.normal));
					float angle = acos(glm::clamp(glm::dot(up, t.normal), -1.0f, 1.0f));
					model = glm::rotate(model, angle, axis);
				}
			}

			// Apply rotation
			model = glm::rotate(model, glm::radians(t.rotation.y), glm::vec3(0, 1, 0));
			model = glm::rotate(model, glm::radians(t.rotation.x), glm::vec3(1, 0, 0));
			model = glm::rotate(model, glm::radians(t.rotation.z), glm::vec3(0, 0, 1));

			// Apply scale
			model = glm::scale(model, t.scale);

			// Transform the object mesh and append to combined
			MeshData instance = objectMesh;
			instance.TransformBy(model);
			combined.Append(instance);

			// Progress callback
			if (progress && (i % 1000 == 0))
			{
				float pct = (float)i / (float)count * 100.0f;
				progress(pct, "ForEach: " + std::to_string(i) + "/" + std::to_string(count));
			}
		}

		outputs[0].data.meshData = std::move(combined);
		lastProcessedCount = count;
	}

private:
	bool alignToNormal = true;
	int maxInstances = 10000;
	int lastProcessedCount = -1;
};
