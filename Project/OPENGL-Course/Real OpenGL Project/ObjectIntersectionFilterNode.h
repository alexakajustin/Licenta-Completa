#pragma once

#include "NodeGraph.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "MeshData.h"
#include "imgui.h"
#include <string>
#include <algorithm>

// Filters a TransformList by checking intersections or relative height with another Scene Object.
// Useful for preventing trees from spawning in water, or confining grass to specific areas.
class ObjectIntersectionFilterNode : public GraphNode
{
public:
	ObjectIntersectionFilterNode(NodeGraph& graph)
	{
		id = graph.NextNodeId();
		title = "Object Filter";

		// Inputs
		Pin meshIn(graph.NextPinId(), PinDataType::Mesh, "Input");
		inputs.push_back(meshIn);

		// Outputs
		Pin passedOut(graph.NextPinId(), PinDataType::Mesh, "Passed");
		Pin rejectedOut(graph.NextPinId(), PinDataType::Mesh, "Rejected");
		outputs.push_back(passedOut);
		outputs.push_back(rejectedOut);
	}

	json Serialize() const override
	{
		json j = GraphNode::Serialize();
		j["targetName"] = targetName;
		j["filterMode"] = filterMode;
		j["verticalMode"] = verticalMode;
		j["bufferDistance"] = bufferDistance;
		return j;
	}

	void Deserialize(const json& j) override
	{
		GraphNode::Deserialize(j);
		targetName = j.value("targetName", "");
		filterMode = j.value("filterMode", 0);
		verticalMode = j.value("verticalMode", 0);
		bufferDistance = j.value("bufferDistance", 0.1f);
	}

	void RenderContent(SceneManager* scene) override
	{
		if (!scene) return;
		auto& objects = scene->GetObjects();

		// Selection Dropdown
		if (ImGui::BeginCombo("Target Object", targetName.empty() ? "(none)" : targetName.c_str()))
		{
			for (auto* obj : objects)
			{
				bool isSelected = (targetName == obj->GetName());
				if (ImGui::Selectable(obj->GetName().c_str(), isSelected))
				{
					targetName = obj->GetName();
				}
			}
			ImGui::EndCombo();
		}

		const char* modes[] = { "Avoid (Exclude)", "Confine (Include Only)" };
		ImGui::Combo("Filter Mode", &filterMode, modes, 2);

		const char* vModes[] = { "Any (Intersection)", "Spawn Above", "Spawn Below" };
		ImGui::Combo("Vertical Rule", &verticalMode, vModes, 3);

		ImGui::DragFloat("Buffer Dist", &bufferDistance, 0.1f, -10.0f, 10.0f);
	}

	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override
	{
		outputs[0].data.Clear();
		outputs[1].data.Clear();

		// Metadata pass-through
		outputs[0].data.type = PinDataType::Mesh;
		outputs[0].data.meshData = inputs[0].data.meshData;
		outputs[0].data.sourceObjectName = inputs[0].data.sourceObjectName;
		outputs[0].data.sourceObject = inputs[0].data.sourceObject;
		outputs[0].data.sourceMaterial = inputs[0].data.sourceMaterial;
		outputs[0].data.sourceTexture = inputs[0].data.sourceTexture;
		outputs[0].data.sourceNormalMap = inputs[0].data.sourceNormalMap;
		outputs[0].data.textureLayers = inputs[0].data.textureLayers;

		outputs[1].data.type = PinDataType::Mesh;
		outputs[1].data.sourceObjectName = inputs[0].data.sourceObjectName;
		outputs[1].data.sourceObject = inputs[0].data.sourceObject;
		outputs[1].data.sourceMaterial = inputs[0].data.sourceMaterial;
		outputs[1].data.sourceTexture = inputs[0].data.sourceTexture;
		outputs[1].data.sourceNormalMap = inputs[0].data.sourceNormalMap;
		outputs[1].data.textureLayers = inputs[0].data.textureLayers;

		auto& inputTransforms = inputs[0].data.transforms;
		if (inputTransforms.empty() || targetName.empty())
		{
			outputs[0].data.transforms = inputTransforms;
			return;
		}

		GameObject* target = scene.FindObject(targetName);
		if (!target)
		{
			outputs[0].data.transforms = inputTransforms;
			return;
		}

		const MeshData& targetMeshData = target->GetCPUMeshData();
		if (targetMeshData.vertices.empty())
		{
			outputs[0].data.transforms = inputTransforms;
			return;
		}

		// --- SPATIAL MATRICES ---
		glm::mat4 modelMatrix = target->GetWorldMatrix();
		glm::mat4 invModel = glm::inverse(modelMatrix);

		// --- PRE-BUILD CACHE (Avoid thread race condition) ---
		if (progress) progress(5.0f, "Rasterizing target mesh...");
		targetMeshData.GetHeightAt(0, 0); // Trigger lazy build outside threads

		// --- MULTI-THREADED EXTREME OPTIMIZATION ---
		unsigned int numThreads = std::thread::hardware_concurrency();
		if (numThreads == 0) numThreads = 8;
		
		std::vector<TransformList> threadPassed(numThreads);
		std::vector<std::thread> threads;
		
		size_t perThread = inputTransforms.size() / numThreads;

		if (progress) progress(10.0f, "Filtering millions of objects...");

		for (unsigned int i = 0; i < numThreads; i++)
		{
			size_t start = i * perThread;
			size_t end = (i == numThreads - 1) ? inputTransforms.size() : (i + 1) * perThread;

			threads.emplace_back([this, &inputTransforms, &targetMeshData, &threadPassed, invModel, modelMatrix, i, start, end]() {
				threadPassed[i].reserve((end - start) / 2);

				for (size_t j = start; j < end; j++)
				{
					const auto& t = inputTransforms[j];
					
					// 1. Transform World-Space Tree to Local-Space of the Target Object
					glm::vec4 localPos = invModel * glm::vec4(t.position, 1.0f);
					
					// 2. Sample Height in Local Space (High Performance)
					float localTargetHeight = targetMeshData.GetHeightAt(localPos.x, localPos.z);
					bool intersects = (localTargetHeight > -1e9f);
					
					bool conditionMet = false;
					if (intersects)
					{
						// 3. Transform sampled local height back to world space for vertical rules
						glm::vec4 worldTargetPos = modelMatrix * glm::vec4(localPos.x, localTargetHeight, localPos.z, 1.0f);
						float worldTargetHeight = worldTargetPos.y;

						float dist = t.position.y - worldTargetHeight;
						
						if (verticalMode == 0) conditionMet = true; 
						else if (verticalMode == 1) conditionMet = (dist >= bufferDistance);
						else if (verticalMode == 2) conditionMet = (dist <= -bufferDistance);
					}

					bool pass = (filterMode == 0) ? !conditionMet : conditionMet;
					if (pass) threadPassed[i].push_back(t);
				}
			});
		}

		for (auto& th : threads) th.join();

		// Combine results efficiently
		if (progress) progress(90.0f, "Assembling results...");
		TransformList finalPassed;
		size_t totalCount = 0;
		for (const auto& list : threadPassed) totalCount += list.size();
		finalPassed.reserve(totalCount);

		for (auto& list : threadPassed)
		{
			finalPassed.insert(finalPassed.end(), list.begin(), list.end());
			list.clear();
			list.shrink_to_fit(); // Free memory ASAP
		}

		outputs[0].data.transforms = std::move(finalPassed);
		// Note: we skip output[1] for extreme performance unless requested to save memory
		
		if (progress) progress(100.0f, "Object Filter Done!");
	}

private:
	std::string targetName = "";
	int filterMode = 0;   // 0: Avoid, 1: Confine
	int verticalMode = 0; // 0: Any, 1: Above, 2: Below
	float bufferDistance = 0.1f;
};
