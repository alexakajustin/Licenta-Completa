#include "ScatterNode.h"
#include "OutputNode.h"
#include "SceneInputNode.h"
#include "PerlinNoiseNode.h"
#include "HydraulicErosionNode.h"
#include "CustomNode.h"
#include "MergeMeshNode.h"
#include "NodeBuilderUI.h"
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

json GraphNode::Serialize() const
{
	json j;
	j["id"] = id;
	j["title"] = title;
	j["editorPos"] = { editorPos.x, editorPos.y };
	
	json inPins = json::array();
	for (const auto& p : inputs) inPins.push_back(p.id);
	j["inputPins"] = inPins;

	json outPins = json::array();
	for (const auto& p : outputs) outPins.push_back(p.id);
	j["outputPins"] = outPins;

	return j;
}

void GraphNode::Deserialize(const json& j)
{
	id = j.value("id", 0);
	title = j.value("title", "");
	if (j.contains("editorPos")) {
		editorPos.x = j["editorPos"][0];
		editorPos.y = j["editorPos"][1];
		positionSet = false; // Trigger UI snap on next frame
	}

	// Restore Pin IDs (very important for links!)
	if (j.contains("inputPins")) {
		const auto& pinIds = j["inputPins"];
		for (size_t i = 0; i < pinIds.size() && (size_t)i < inputs.size(); i++)
			inputs[i].id = pinIds[i].get<int>();
	}
	if (j.contains("outputPins")) {
		const auto& pinIds = j["outputPins"];
		for (size_t i = 0; i < pinIds.size() && (size_t)i < outputs.size(); i++)
			outputs[i].id = pinIds[i].get<int>();
	}
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

	// Cache for sharing GPU meshes across instances
	// Mapping from a pair of data pointer + size to a Mesh*
	struct MeshRef { const float* verts; size_t vSize; const unsigned int* indices; size_t iSize; };
	auto meshHash = [](const MeshRef& m) { return m.vSize ^ m.iSize; }; // Simple hash for illustration, ideally use a better one
	std::map<size_t, Mesh*> meshCache; 
	auto& objects = scene.GetObjects();

	for (auto* node : sorted)
	{
		// 1. Handle ScatterNode (Modular Spawning)
		if (node->title == "Scatter")
		{
			ScatterNode* scatterNode = static_cast<ScatterNode*>(node);
			if (scatterNode->IsSpawnMode())
			{
				// 1. Cleanup old spawned objects
				for (const auto& name : scatterNode->GetSpawnedNames())
					scene.RemoveObject(name);
				scatterNode->SetSpawnedNames({});

				Pin& instancesPin = node->outputs[1];
				auto& transforms = instancesPin.data.transforms;
				auto& instanceMeshes = instancesPin.data.instanceMeshes;
				MeshData& defaultObjectMesh = node->inputs[1].data.meshData;

				if (!transforms.empty())
				{
					// Resolve parent by name if needed (essential for load from file)
					int parentIdx = scatterNode->GetParentIndex();
					std::string parentName = scatterNode->GetParentName();

					if (parentIdx < 0 || parentIdx >= (int)objects.size() || objects[parentIdx]->GetName() != parentName)
					{
						parentIdx = -1;
						if (parentName != "(none)")
						{
							for (int i = 0; i < (int)objects.size(); i++)
							{
								if (objects[i]->GetName() == parentName)
								{
									parentIdx = i;
									scatterNode->SetTargetParent(i, parentName); // Update node for future frames
									break;
								}
							}
						}
					}

					GameObject* targetParent = nullptr;
					if (parentIdx >= 0 && parentIdx < (int)objects.size())
						targetParent = objects[parentIdx];

					if (!targetParent)
					{
						std::string groupName = "Scatter_Group_" + std::to_string(node->id);
						targetParent = scene.FindObject(groupName);
						if (!targetParent) { targetParent = new GameObject(groupName); scene.AddObject(targetParent); }
					}

					// Per-batch temporaries for mesh/material sharing (NOT static — reset each Execute).
					std::shared_ptr<MeshData> sharedInputMesh = nullptr;
					MeshData* lastSource = nullptr;
					Material* groupMat = nullptr;

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

						obj->GetTransform().SetFromMatrix(worldModel);
						obj->SetInheritScale(false);
						obj->SetParent(targetParent);

						// OPTIMIZATION: Share GPU Mesh
						// If unique instance meshes exist (e.g. from noise), use them, otherwise use the shared input mesh
						MeshData* targetData = &defaultObjectMesh;
						if (i < (int)instanceMeshes.size() && !instanceMeshes[i].vertices.empty())
							targetData = &instanceMeshes[i];

						// Simple caching based on vector addresses (fine within one execution pass)
						size_t dataKey = (size_t)targetData->vertices.data() ^ (size_t)targetData->vertices.size();
						if (meshCache.find(dataKey) == meshCache.end())
						{
							meshCache[dataKey] = targetData->ToMesh((int)transforms.size());
						}
						obj->SetMesh(meshCache[dataKey]);
						
						// Optimized: Share CPU Mesh Memory across instances of the same source mesh.
						// NOTE: NOT static — must be reset each Execute() call to avoid stale pointers.
						if (lastSource != targetData) {
							sharedInputMesh = std::make_shared<MeshData>(*targetData);
							lastSource = targetData;
						}
						obj->SetCPUMeshData(sharedInputMesh);

						// --- Material Handling ---
						// Priority: sourceMaterial from object input > a fresh default material (if textures exist) > scene defaultMat.
						Material* finalMat = instancesPin.data.sourceMaterial;
						if (!finalMat && (instancesPin.data.sourceTexture || instancesPin.data.sourceNormalMap))
						{
							// Create a fresh unique material per scatter group (NOT static — avoids stale/leaked materials).
							if (i == 0) groupMat = new Material(0.1f, 32.0f); // Only allocate once per scatter batch
							finalMat = groupMat;
						}

						if (finalMat) obj->SetMaterial(finalMat);
						else if (defaultMat) obj->SetMaterial(defaultMat);

						if (instancesPin.data.sourceTexture) obj->SetTexture(instancesPin.data.sourceTexture);
						else if (defaultTex) obj->SetTexture(defaultTex);

						if (instancesPin.data.sourceNormalMap) obj->SetNormalMap(instancesPin.data.sourceNormalMap);

						// Apply texture layers from the scatter OBJECT (inputs[1]), not the surface.
						// These were already filtered to inputs[1] in ScatterNode::Execute().
						for (const auto& layer : instancesPin.data.textureLayers)
						{
							obj->AddTextureLayer(layer);
						}

						scene.AddObject(obj);
						newSpawned.push_back(name);
					}
					scatterNode->SetSpawnedNames(newSpawned);
					printf("Scatter spawned %d modular objects (Sharing %d GPU meshes).\n", (int)newSpawned.size(), (int)meshCache.size());
				}
			}
		}

		// 2. Handle OutputNode (Mesh Update)
		if (node->title == "Output")
		{
			OutputNode* updateNode = static_cast<OutputNode*>(node);
			int targetIdx = updateNode->GetTargetIndex();
			std::string targetName = updateNode->GetTargetName();
			Pin& meshInput = node->inputs[0];

			if (updateNode->IsSameAsInput())
			{
				targetIdx = -1;
				if (meshInput.data.sourceObjectName != "(none)")
				{
					for (int i = 0; i < (int)objects.size(); i++)
						if (objects[i]->GetName() == meshInput.data.sourceObjectName) { targetIdx = i; break; }
				}
			}
			else if (targetIdx < 0 || targetIdx >= (int)objects.size() || objects[targetIdx]->GetName() != targetName)
			{
				// Resolve by name for manual target mode
				targetIdx = -1;
				if (targetName != "(none)")
				{
					for (int i = 0; i < (int)objects.size(); i++)
					{
						if (objects[i]->GetName() == targetName)
						{
							targetIdx = i;
							updateNode->SetTargetIndex(i, targetName);
							break;
						}
					}
				}
			}
			
			if (targetIdx >= 0 && targetIdx < (int)objects.size())
			{
				if (updateNode->ShouldUpdateMesh() && meshInput.data.type == PinDataType::Mesh && !meshInput.data.meshData.vertices.empty())
				{
					GameObject* target = objects[targetIdx];
					MeshData& uploadData = meshInput.data.meshData;

					bool isShared = false;
					if (target->GetMesh())
					{
						for (auto* obj : objects)
						{
							if (obj != target && obj->GetMesh() == target->GetMesh())
							{
								isShared = true;
								break;
							}
						}
					}

					if (target->GetMesh() && !isShared)
					{
						// Mesh is uniquely owned by this target. We can safely overwrite it in-place for fast real-time updates.
						target->GetMesh()->CreateMesh(
							uploadData.vertices.data(),
							uploadData.indices.data(),
							(unsigned int)uploadData.vertices.size(),
							(unsigned int)uploadData.indices.size()
						);

						// Always strictly apply the scale from the input transform
						if (!meshInput.data.transforms.empty())
							target->GetTransform().SetScale(meshInput.data.transforms[0].scale);

						target->SetCPUMeshData(uploadData);
						// printf("Updated unique mesh for object: %s\n", target->GetName().c_str());
					}
					else
					{
						// Mesh is either null or shared with other instances (e.g. from ScatterNode).
						// We must allocate a new unique mesh to avoid mutating other objects.
						Mesh* newMesh = uploadData.ToMesh();
						target->SetMesh(newMesh);
						if (!meshInput.data.transforms.empty())
							target->GetTransform().SetScale(meshInput.data.transforms[0].scale);
						target->SetCPUMeshData(uploadData);
						printf("Branched new unique mesh for object: %s\n", target->GetName().c_str());
					}

					// Inherit visual properties from the pipeline ONLY if this target has none set yet.
					// Existing material/texture/layers set via the Inspector are NEVER overwritten by the
					// node pipeline — they are authoritative user data.
					if (meshInput.data.sourceMaterial && !target->GetMaterial())
						target->SetMaterial(meshInput.data.sourceMaterial);

					if (meshInput.data.sourceTexture && !target->GetTexture())
						target->SetTexture(meshInput.data.sourceTexture);
					if (meshInput.data.sourceNormalMap && !target->GetNormalMap())
						target->SetNormalMap(meshInput.data.sourceNormalMap);

					// IMPORTANT: Texture layers are NEVER copied from pin data to the target object.
					// Layers are managed exclusively through the Inspector UI per-object.
					// Copying them here caused terrain layers to bleed into scatter instances and vice versa.
				}
			}
		}
	}

	// TRIPLE CRITICAL: Clear all pin data at the end of execution to free RAM!
	// Pins are transient vehicles; keeping 400M triangles in them after spawning is a massive leak.
	for (auto* n : nodes)
	{
		for (auto& p : n->inputs) p.data.DeepClear();
		for (auto& p : n->outputs) p.data.DeepClear();
	}
}

// SceneInputNode and OutputNode implementations moved to separate .cpp files

json NodeGraph::Serialize() const
{
	json j;
	j["nextId"] = nextId;

	json nodesArray = json::array();
	for (auto* n : nodes) nodesArray.push_back(n->Serialize());
	j["nodes"] = nodesArray;

	json linksArray = json::array();
	for (const auto& l : links)
	{
		json lj;
		lj["id"] = l.id;
		lj["start"] = l.startPinId;
		lj["end"] = l.endPinId;
		linksArray.push_back(lj);
	}
	j["links"] = linksArray;

	return j;
}

void NodeGraph::Deserialize(const json& j, SceneManager& scene)
{
	Clear();
	generatedObjectNames.clear();
	nextId = j.value("nextId", 1);

	if (j.contains("nodes"))
	{
		for (const auto& nj : j["nodes"])
		{
			std::string title = nj.value("title", "");
			GraphNode* node = nullptr;

			// Factory based on title
			if (title == "Scatter") node = new ScatterNode(*this);
			else if (title == "Output") node = new OutputNode(*this);
			else if (title == "Scene Input") node = new SceneInputNode(*this);
			else if (title == "Perlin Noise") node = new PerlinNoiseNode(*this);
			else if (title == "Hydraulic Erosion") node = new HydraulicErosionNode(*this);
			else if (title == "Merge Mesh") node = new MergeMeshNode(*this);
			else
			{
				// Check if it's a Custom Node
				const auto& defs = NodeBuilderUI::GetSavedDefinitions();
				std::string defName = nj.value("definitionName", "");
				for (const auto& def : defs)
					if (def.name == defName) { node = new CustomNode(*this, def); break; }
			}

			if (node)
			{
				node->Deserialize(nj);
				AddNode(node);
			}
		}
	}

	if (j.contains("links"))
	{
		for (const auto& lj : j["links"])
		{
			int id = lj.value("id", 0);
			int start = lj.value("start", 0);
			int end = lj.value("end", 0);
			links.push_back(Link(id, start, end));
		}
	}
}

bool NodeGraph::IsObjectGenerated(const std::string& name) const
{
	for (const auto& spawned : generatedObjectNames)
		if (spawned == name) return true;
	
	// Also check child objects of generated groups
	// (ScatterNode uses 'Instance_X_Y' pattern)
	if (name.find("Instance_") != std::string::npos || name.find("Scatter_Group_") != std::string::npos)
		return true;

	return false;
}
void NodeGraph::Clear()
{
	for (auto* n : nodes)
		delete n;
	nodes.clear();
	links.clear();
	generatedObjectNames.clear();
}
