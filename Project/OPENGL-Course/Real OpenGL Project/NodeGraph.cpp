#include "NodeGraph.h"
#include "OutputNode.h"
#include "SceneInputNode.h"
#include "ScatterNode.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "PrimitiveGenerator.h"
#include "Texture.h"
#include "Material.h"
#include "imgui.h"

#include <algorithm>
#include <queue>
#include <set>
#include <cstdio>

// ========== GraphNode ==========

Pin* GraphNode::FindPin(int pinId)
{
	Pin* p = FindInputPin(pinId);
	if (p) return p;
	return FindOutputPin(pinId);
}

Pin* GraphNode::FindInputPin(int pinId)
{
	for (auto& pin : inputs)
		if (pin.id == pinId) return &pin;
	return nullptr;
}

Pin* GraphNode::FindOutputPin(int pinId)
{
	for (auto& pin : outputs)
		if (pin.id == pinId) return &pin;
	return nullptr;
}

// ========== NodeGraph ==========

NodeGraph::NodeGraph() : nextId(1)
{
}

NodeGraph::~NodeGraph()
{
	Clear();
}

void NodeGraph::AddNode(GraphNode* node)
{
	nodes.push_back(node);
}

void NodeGraph::RemoveNode(int nodeId)
{
	// Remove all links connected to this node's pins
	GraphNode* node = FindNode(nodeId);
	if (!node) return;

	// Collect pin IDs
	std::set<int> pinIds;
	for (auto& p : node->inputs) pinIds.insert(p.id);
	for (auto& p : node->outputs) pinIds.insert(p.id);

	// Remove links touching these pins
	links.erase(std::remove_if(links.begin(), links.end(),
		[&pinIds](const Link& l) {
			return pinIds.count(l.startPinId) || pinIds.count(l.endPinId);
		}), links.end());

	// Remove the node
	nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
		[nodeId](GraphNode* n) {
			if (n->id == nodeId) { delete n; return true; }
			return false;
		}), nodes.end());
}

GraphNode* NodeGraph::FindNode(int nodeId)
{
	for (auto* n : nodes)
		if (n->id == nodeId) return n;
	return nullptr;
}

GraphNode* NodeGraph::FindNodeByPinId(int pinId)
{
	for (auto* n : nodes)
	{
		for (auto& p : n->inputs) if (p.id == pinId) return n;
		for (auto& p : n->outputs) if (p.id == pinId) return n;
	}
	return nullptr;
}

bool NodeGraph::CanLink(int outputPinId, int inputPinId)
{
	GraphNode* outNode = FindNodeByPinId(outputPinId);
	GraphNode* inNode = FindNodeByPinId(inputPinId);
	if (!outNode || !inNode) return false;
	if (outNode == inNode) return false; // No self-links

	Pin* outPin = outNode->FindOutputPin(outputPinId);
	Pin* inPin = inNode->FindInputPin(inputPinId);
	if (!outPin || !inPin) return false;

	// Type check
	if (outPin->dataType != inPin->dataType) return false;

	// Check if input already has a link (only one input per pin)
	for (auto& link : links)
		if (link.endPinId == inputPinId) return false;

	return true;
}

bool NodeGraph::AddLink(int outputPinId, int inputPinId)
{
	if (!CanLink(outputPinId, inputPinId)) return false;

	Link newLink(NextLinkId(), outputPinId, inputPinId);
	links.push_back(newLink);
	return true;
}

void NodeGraph::RemoveLink(int linkId)
{
	links.erase(std::remove_if(links.begin(), links.end(),
		[linkId](const Link& l) { return l.id == linkId; }), links.end());
}

void NodeGraph::RemoveLinkByPinId(int pinId)
{
	links.erase(std::remove_if(links.begin(), links.end(),
		[pinId](const Link& l) { return l.startPinId == pinId || l.endPinId == pinId; }), links.end());
}



std::vector<GraphNode*> NodeGraph::TopologicalSort()
{
	// Build adjacency: node depends on which other nodes?
	std::map<int, std::set<int>> dependencies; // nodeId -> set of nodeIds it depends on
	std::map<int, int> inDegree;

	for (auto* n : nodes)
	{
		dependencies[n->id] = {};
		inDegree[n->id] = 0;
	}

	// For each link, the node owning the input pin depends on the node owning the output pin
	for (auto& link : links)
	{
		GraphNode* srcNode = FindNodeByPinId(link.startPinId);
		GraphNode* dstNode = FindNodeByPinId(link.endPinId);
		if (srcNode && dstNode && srcNode != dstNode)
		{
			if (dependencies[dstNode->id].insert(srcNode->id).second)
			{
				inDegree[dstNode->id]++;
			}
		}
	}

	// Kahn's algorithm
	std::queue<int> ready;
	for (auto* n : nodes)
	{
		if (inDegree[n->id] == 0)
			ready.push(n->id);
	}

	std::vector<GraphNode*> sorted;
	while (!ready.empty())
	{
		int current = ready.front();
		ready.pop();
		sorted.push_back(FindNode(current));

		// For each node that depends on current, decrease in-degree
		for (auto* n : nodes)
		{
			if (dependencies[n->id].count(current))
			{
				inDegree[n->id]--;
				if (inDegree[n->id] == 0)
					ready.push(n->id);
			}
		}
	}

	return sorted;
}

void NodeGraph::Execute(SceneManager& scene, Texture* defaultTex, Material* defaultMat)
{
	if (nodes.empty()) return;

	// Clear all pin data
	for (auto* n : nodes)
	{
		for (auto& p : n->inputs) p.data.Clear();
		for (auto& p : n->outputs) p.data.Clear();
	}

	// Execute in topological order
	auto sorted = TopologicalSort();

	for (auto* node : sorted)
	{
		// Execute the node
		node->Execute(scene);

		// Propagate data from this node's outputs to connected inputs
		for (auto& link : links)
		{
			Pin* srcPin = node->FindOutputPin(link.startPinId);
			if (srcPin)
			{
				GraphNode* dstNode = FindNodeByPinId(link.endPinId);
				if (dstNode)
				{
					Pin* dstPin = dstNode->FindInputPin(link.endPinId);
					if (dstPin)
					{
						dstPin->data = srcPin->data;
					}
				}
			}
		}
	}

	// After execution, process nodes that modify the scene
	auto& objects = scene.GetObjects();

	for (auto* node : sorted)
	{
		// 1. Handle ScatterNode (Modular Spawning)
		if (node->title == "Scatter")
		{
			ScatterNode* scatterNode = static_cast<ScatterNode*>(node);
			if (scatterNode->IsSpawnMode())
			{
				// 1. Cleanup old spawned objects owned by THIS ScatterNode
				for (const auto& name : scatterNode->GetSpawnedNames())
				{
					scene.RemoveObject(name);
				}
				scatterNode->SetSpawnedNames({});

				Pin& instancesPin = node->outputs[1]; // "Instances Only"
				auto& transforms = instancesPin.data.transforms;
				auto& instanceMeshes = instancesPin.data.instanceMeshes;

				if (!transforms.empty())
				{
					int parentIdx = scatterNode->GetParentIndex();
					GameObject* targetParent = nullptr;

					if (parentIdx >= 0 && parentIdx < (int)objects.size())
						targetParent = objects[parentIdx];

					if (!targetParent)
					{
						std::string groupName = "Scatter_Group_" + std::to_string(node->id);
						targetParent = scene.FindObject(groupName);
						if (!targetParent)
						{
							targetParent = new GameObject(groupName);
							scene.AddObject(targetParent);
						}
					}

					std::vector<std::string> newSpawned;
					for (int i = 0; i < (int)transforms.size(); i++)
					{
						std::string name = "Instance_" + std::to_string(node->id) + "_" + std::to_string(i);
						GameObject* obj = new GameObject(name);

						glm::mat4 worldModel = glm::mat4(1.0f);
						worldModel = glm::translate(worldModel, transforms[i].position);
						
						glm::vec3 n = transforms[i].normal;
						if (glm::length(n) > 0.001f)
						{
							glm::vec3 up(0, 1, 0);
							if (glm::abs(glm::dot(up, n)) < 0.999f)
							{
								glm::vec3 axis = glm::normalize(glm::cross(up, n));
								float angle = acos(glm::clamp(glm::dot(up, n), -1.0f, 1.0f));
								worldModel = glm::rotate(worldModel, angle, axis);
							}
						}
						
						worldModel = glm::rotate(worldModel, glm::radians(transforms[i].rotation.x), glm::vec3(1, 0, 0));
						worldModel = glm::rotate(worldModel, glm::radians(transforms[i].rotation.y), glm::vec3(0, 1, 0));
						worldModel = glm::rotate(worldModel, glm::radians(transforms[i].rotation.z), glm::vec3(0, 0, 1));
						worldModel = glm::scale(worldModel, transforms[i].scale);

						// Set world pose BEFORE parenting, so SetParent can calculate correct local offset
						obj->GetTransform().SetFromMatrix(worldModel);
						obj->SetInheritScale(false); // Important: Set this BEFORE parenting so local scale isn't crushed
						obj->SetParent(targetParent);

						if (i < (int)instanceMeshes.size() && !instanceMeshes[i].vertices.empty())
							obj->SetMesh(instanceMeshes[i].ToMesh());

						if (defaultTex) obj->SetTexture(defaultTex);
						if (defaultMat) obj->SetMaterial(defaultMat);

						scene.AddObject(obj);
						newSpawned.push_back(name);
					}
					scatterNode->SetSpawnedNames(newSpawned);
					printf("Scatter spawned %d modular objects.\n", (int)newSpawned.size());
				}
			}
		}

		// 2. Handle OutputNode (Mesh Update)
		if (node->title == "Output")
		{
			OutputNode* updateNode = static_cast<OutputNode*>(node);
			int targetIdx = updateNode->GetTargetIndex();
			Pin& meshInput = node->inputs[0];

			// Modular Targeting: If "Same As Input" is on, find object by its name passed in the data
			if (updateNode->IsSameAsInput() && meshInput.data.sourceObjectName != "(none)")
			{
				targetIdx = -1;
				for (int i = 0; i < (int)objects.size(); i++)
				{
					if (objects[i]->GetName() == meshInput.data.sourceObjectName)
					{
						targetIdx = i;
						break;
					}
				}
			}
			
			if (targetIdx >= 0 && targetIdx < (int)objects.size())
			{
				if (updateNode->ShouldUpdateMesh() && meshInput.data.type == PinDataType::Mesh && !meshInput.data.meshData.vertices.empty())
				{
						// Update the existing mesh
						GameObject* target = objects[targetIdx];
						if (target->GetMesh())
						{
							MeshData uploadData = meshInput.data.meshData;
							bool restoredScale = false;

							// If we have transform data, we can "un-bake" the mesh to restore hierarchy scale
							if (!meshInput.data.transforms.empty())
							{
								glm::vec3 originalScale = meshInput.data.transforms[0].scale;
								if (glm::length(originalScale) > 0.001f)
								{
									for (size_t i = 0; i < uploadData.vertices.size(); i += 14)
									{
										uploadData.vertices[i] /= originalScale.x;
										uploadData.vertices[i + 1] /= originalScale.y;
										uploadData.vertices[i + 2] /= originalScale.z;
									}
									restoredScale = true;
								}
							}

							// Reuse VAO/VBO/IBO by calling CreateMesh again
							target->GetMesh()->CreateMesh(
								uploadData.vertices.data(),
								uploadData.indices.data(),
								(unsigned int)uploadData.vertices.size(),
								(unsigned int)uploadData.indices.size()
							);

							// Only reset scale if we DIDN'T restore it (e.g. for primitives or new objects)
							if (!restoredScale)
							{
								target->GetTransform().SetScale(glm::vec3(1.0f));
							}
							else
							{
								// Ensure the target object actually has the correct scale in its transform
								target->GetTransform().SetScale(meshInput.data.transforms[0].scale);
							}

							// PERSIST: Save the CPU-side data so it can be retrieved by SceneInputNode later
							target->SetCPUMeshData(uploadData);
							
							printf("Updated mesh for object: %s (restoredScale: %s)\n", target->GetName().c_str(), restoredScale ? "true" : "false");
						}
					else
					{
						// If object has no mesh, create one
						Mesh* newMesh = meshInput.data.meshData.ToMesh();
						target->SetMesh(newMesh);
						target->SetCPUMeshData(meshInput.data.meshData);
					}
				}
			}
		}
	}
}

// SceneInputNode and OutputNode implementations moved to separate .cpp files


void NodeGraph::Clear()
{
	for (auto* n : nodes)
		delete n;
	nodes.clear();
	links.clear();
	generatedObjectNames.clear();
}
