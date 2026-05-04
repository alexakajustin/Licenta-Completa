#include "ScatterNode.h"
#include "OutputNode.h"
#include "SceneInputNode.h"
#include "PerlinNoiseNode.h"
#include "HydraulicErosionNode.h"
#include "RiverNode.h"
#include "CustomNode.h"
#include "MergeMeshNode.h"
#include "ConstantNode.h"
#include "MathNode.h"
#include "CompareNode.h"
#include "BranchNode.h"
#include "FilterTransformListNode.h"
#include "ForEachNode.h"
#include "CityGridNode.h"
#include "BuildingGenNode.h"
#include "ObjectIntersectionFilterNode.h"
#include "NodeBuilderUI.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "PrimitiveGenerator.h"
#include "Texture.h"
#include "Material.h"
#include "InstancedGroup.h"
#include "MeshSimplifier.h"
#include "AssetManager.h"
#include "imgui.h"

#include <algorithm>
#include <queue>
#include <set>
#include <cstdio>
#include <thread>
#include <future>

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

void NodeGraph::RemoveNode(int nodeId, SceneManager* scene)
{
	// Remove all links connected to this node's pins
	GraphNode* node = FindNode(nodeId);
	if (!node) return;

	if (scene) node->OnRemove(*scene);

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

void NodeGraph::Execute(SceneManager& scene, Texture* defaultTex, Material* defaultMat, std::function<void(float, float, const std::string&)> progressCallback)
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

	for (size_t i = 0; i < sorted.size(); i++)
	{
		auto* node = sorted[i];

		auto nodeCb = [&](float nodePct, const std::string& msg) {
			if (progressCallback) {
				float overallPct = ((float)i / (float)(sorted.size() * 2)) * 100.0f + (nodePct / (float)(sorted.size() * 2));
				progressCallback(overallPct, nodePct, msg);
			}
		};

		if (progressCallback) {
			float pct = ((float)i / (float)(sorted.size() * 2)) * 100.0f; // First half of progress
			progressCallback(pct, 0.0f, "Computing Node: " + node->title);
		}

		// Execute the node
		node->Execute(scene, nodeCb);

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

	for (size_t i = 0; i < sorted.size(); i++)
	{
		auto* node = sorted[i];

		if (progressCallback) {
			float pct = 50.0f + ((float)i / (float)(sorted.size() * 2)) * 100.0f; // Second half
			progressCallback(pct, 100.0f, "Applying Data: " + node->title);
		}

		// 1. Handle ScatterNode (Modular Spawning)
		if (node->title == "Scatter")
		{
			ScatterNode* scatterNode = static_cast<ScatterNode*>(node);
			if (scatterNode->IsSpawnMode())
			{
				Pin& instancesPin = node->outputs[1];
				std::string objName = instancesPin.data.sourceObjectName;
				if (objName.empty()) objName = "Unknown";

				// 1. Cleanup ONLY the old scatter results for THIS specific object type
				// This allows independent scatter layers for different objects from the same node.
				auto& sMap = scatterNode->GetSpawnedMap();
				if (sMap.count(objName)) {
					for (const auto& name : sMap[objName])
						scene.RemoveObject(name);
					sMap[objName].clear();
				}

				// Also remove the specific instanced group for this object type if it exists
				std::string groupName = "Scatter_Instanced_" + std::to_string(node->id) + "_" + objName;
				scene.RemoveInstancedGroup(groupName);
				
				// Pattern for previous single-object version (for backwards compatibility/cleanup)
				std::string legacyGroupName = "Scatter_Instanced_" + std::to_string(node->id);
				scene.RemoveInstancedGroup(legacyGroupName);

				auto& transforms = instancesPin.data.transforms;
				MeshData& defaultObjectMesh = node->inputs[1].data.meshData;

				// === Filter Chain Resolution ===
				// Follow links from Scatter's "Instances Only" output to find
				// downstream FilterTransformListNode(s) and use filtered transforms.
				TransformList* finalTransforms = &transforms;
				{
					Pin* currentPin = &instancesPin;
					bool foundFilter = true;
					while (foundFilter)
					{
						foundFilter = false;
						for (auto& link : links)
						{
							if (link.startPinId == currentPin->id)
							{
								GraphNode* downstream = FindNodeByPinId(link.endPinId);
								if (downstream && (downstream->title == "Filter Transforms" || downstream->title == "Object Filter"))
								{
									// Use this filter's "Passed" output (outputs[0])
									if (!downstream->outputs.empty())
									{
										finalTransforms = &downstream->outputs[0].data.transforms;
										currentPin = &downstream->outputs[0];
										foundFilter = true;
									}
									break;
								}
							}
						}
					}
				}

				if (!finalTransforms->empty())
				{
					// Register this group name so the node can clean it up on removal
					scatterNode->AddCreatedGroupName(groupName);

					// ================================================================
					// GPU-DRIVEN PATH: ALL scattered objects use instanced rendering.
					// For multi-mesh models (trees), we create one InstancedGroup per
					// sub-mesh so each part keeps its own texture/material.
					// ================================================================

					GameObject* sourceObj = instancesPin.data.sourceObject;

					// --- FIX FOR GENERATED TERRAINS (NO SURFACE TRANSFORM) ---
					bool hasSurfaceTransform = !node->inputs[0].data.transforms.empty();
					glm::mat4 targetMatrix = glm::mat4(1.0f);
					glm::mat3 normalMatrix = glm::mat3(1.0f);
					float targetScaleAvg = 1.0f;

					if (!hasSurfaceTransform) {
						// Traverse graph downstream from Combined pin to find an OutputNode
						int currentPinId = node->outputs[0].id;
						bool foundTarget = false;
						int sanityGuard = 0;
						while (!foundTarget && sanityGuard++ < 50) {
							bool moved = false;
							for (auto& link : links) {
								if (link.startPinId == currentPinId) {
									GraphNode* nextNode = FindNodeByPinId(link.endPinId);
									if (nextNode) {
										if (nextNode->title == "Output") {
											OutputNode* outNode = static_cast<OutputNode*>(nextNode);
											if (outNode->GetTargetIndex() != -1 && outNode->GetTargetIndex() < (int)objects.size()) {
												GameObject* target = objects[outNode->GetTargetIndex()];
												if (target) {
													targetMatrix = target->GetWorldMatrix();
													normalMatrix = glm::transpose(glm::inverse(glm::mat3(targetMatrix)));
													glm::vec3 s = target->GetTransform().GetScale();
													targetScaleAvg = (s.x + s.y + s.z) / 3.0f;
													foundTarget = true;
												}
											}
											break; // Found Output
										} else {
											// Move to next node's output pin 0
											if (!nextNode->outputs.empty()) {
												currentPinId = nextNode->outputs[0].id;
												moved = true;
												break;
											}
										}
									}
								}
							}
							if (!moved) break;
						}
					}
					// ---------------------------------------------------------

					// Build packed instance data MULTI-THREADED (shared by all sub-meshes)
					std::vector<InstancedGroup::PackedInstance> packedInstances(finalTransforms->size());
					int packCount = (int)finalTransforms->size();
					unsigned int packThreads = std::thread::hardware_concurrency();
					if (packThreads == 0) packThreads = 4;
					if (packThreads > (unsigned int)packCount) packThreads = (unsigned int)packCount;

					std::vector<std::future<void>> packFutures;
					int packPerThread = packCount / packThreads;
					bool alignToNorm = scatterNode->IsAlignToNormal();

					for (unsigned int tIdx = 0; tIdx < packThreads; tIdx++)
					{
						int startIdx = tIdx * packPerThread;
						int endIdx = (tIdx == packThreads - 1) ? packCount : (tIdx + 1) * packPerThread;

						packFutures.push_back(std::async(std::launch::async, [&, startIdx, endIdx, alignToNorm, hasSurfaceTransform, targetMatrix, normalMatrix, targetScaleAvg]() {
							for (int i = startIdx; i < endIdx; i++) {
								const auto& t = (*finalTransforms)[i];
								InstancedGroup::PackedInstance packed;
								
								glm::vec3 finalPos = t.position;
								glm::vec3 finalNormal = t.normal;
								float finalScale = (t.scale.x + t.scale.y + t.scale.z) / 3.0f;

								if (!hasSurfaceTransform) {
									finalPos = glm::vec3(targetMatrix * glm::vec4(t.position, 1.0f));
									finalScale *= targetScaleAvg;
									if (glm::length(t.normal) > 0.001f) {
										finalNormal = glm::normalize(normalMatrix * t.normal);
									}
								}

								packed.positionAndScale = glm::vec4(finalPos, finalScale);

								glm::vec3 euler = t.rotation;
								if (alignToNorm && glm::length(finalNormal) > 0.001f) {
									glm::vec3 up(0, 1, 0);
									if (glm::abs(glm::dot(up, finalNormal)) < 0.999f) {
										glm::vec3 axis = glm::normalize(glm::cross(up, finalNormal));
										float angle = acos(glm::clamp(glm::dot(up, finalNormal), -1.0f, 1.0f));
										euler.x += glm::degrees(angle * axis.x);
										euler.z += glm::degrees(angle * axis.z);
									}
								}
								packed.rotationAndFlags = glm::vec4(euler, 0.0f);
								packedInstances[i] = packed;
							}
						}));
					}
					for (auto& f : packFutures) f.get();

					// Determine if we have a multi-mesh model (tree, building, etc.)
					Model* multiMeshModel = nullptr;

					// Check source object's own model
					if (sourceObj && sourceObj->GetModel() && sourceObj->GetModel()->GetMeshCount() > 0)
						multiMeshModel = sourceObj->GetModel();

					// Fallback: check parent's model source path (for scene-loaded modular trees)
					if (!multiMeshModel && sourceObj && !sourceObj->GetModelSourcePath().empty())
						multiMeshModel = AssetManager::Get().GetModel(sourceObj->GetModelSourcePath());

					// Also check children's models
					if (!multiMeshModel && sourceObj) {
						for (auto* child : sourceObj->GetChildren()) {
							if (child->GetModel() && child->GetModel()->GetMeshCount() > 0) {
								multiMeshModel = child->GetModel();
								break;
							}
						}
					}

					float maxDist = 2000.0f;
					float shadowDist = 100.0f;

					if (multiMeshModel && multiMeshModel->GetMeshCount() > 1 && sourceObj) {
						// ============================================================
						// MULTI-MESH & LOD-AWARE PATH: Group LOD meshes by name.
						// One InstancedGroup per UNIQUE base mesh.
						// ============================================================
						const auto& meshDataList = multiMeshModel->GetMeshDataList();
						const auto& meshNames = multiMeshModel->GetMeshNames();
						const auto& children = sourceObj->GetChildren();

						struct MeshGroup {
							size_t lod0Idx = -1;
							size_t lod1Idx = -1;
							size_t lod2Idx = -1;
						};
						std::map<std::string, MeshGroup> groupsByName;

						for (size_t m = 0; m < multiMeshModel->GetMeshCount(); m++) {
							std::string name = (m < meshNames.size()) ? meshNames[m] : ("Mesh_" + std::to_string(m));
							
							int level = 0;
							std::string baseName = name;
							std::string upperName = name;
							for (auto& c : upperName) c = toupper(c);

							size_t lodPos = upperName.find("LOD");
							if (lodPos != std::string::npos) {
								std::string suffix = upperName.substr(lodPos + 3);
								if (!suffix.empty() && (suffix[0] == '_' || suffix[0] == ' ' || suffix[0] == '-'))
									suffix = suffix.substr(1);
								
								if (!suffix.empty() && isdigit(suffix[0])) {
									level = suffix[0] - '0';
									size_t baseEnd = lodPos;
									if (baseEnd > 0 && (name[baseEnd-1] == '_' || name[baseEnd-1] == ' ' || name[baseEnd-1] == '-'))
										baseEnd--;
									baseName = name.substr(0, baseEnd);
								}
							}

							if (level == 0) groupsByName[baseName].lod0Idx = m;
							else if (level == 1) groupsByName[baseName].lod1Idx = m;
							else if (level == 2) groupsByName[baseName].lod2Idx = m;
						}

						for (auto const& [baseName, mg] : groupsByName) {
							size_t m = mg.lod0Idx;
							if (m == -1) continue; 

							if (m >= meshDataList.size() || meshDataList[m].vertices.empty())
								continue;

							GameObject* matchingChild = nullptr;
							for (auto* child : children) {
								std::string childName = child->GetName();
								
								// Strip " (copy)" suffixes
								size_t copyPos = childName.find(" (");
								if (copyPos != std::string::npos) childName = childName.substr(0, copyPos);
								
								// Also strip "LOD" suffixes from child names to match baseName
								// (e.g. "Tree_LOD0" should match baseName "Tree")
								size_t lodPosChild = childName.find("_LOD");
								if (lodPosChild == std::string::npos) lodPosChild = childName.find(" LOD");
								if (lodPosChild == std::string::npos) lodPosChild = childName.find("-LOD");
								if (lodPosChild != std::string::npos) childName = childName.substr(0, lodPosChild);

								if (childName == baseName) { matchingChild = child; break; }
							}
							if (!matchingChild && m < children.size()) matchingChild = children[m];

							Texture* subTex = nullptr; Texture* subNorm = nullptr; Material* subMat = nullptr;
							std::vector<TextureLayer> subLayers;
							if (matchingChild) {
								subTex = matchingChild->GetTexture();
								subNorm = matchingChild->GetNormalMap();
								subMat = matchingChild->GetMaterial();
								subLayers = matchingChild->GetTextureLayers();
							}
							if (!subTex) {
								unsigned int matIdx = multiMeshModel->GetMaterialIndex((unsigned int)m);
								subTex = multiMeshModel->GetTexture(matIdx);
								subNorm = multiMeshModel->GetNormalMap(matIdx);
							}
							if (!subMat) subMat = sourceObj->GetMaterial();
							if (subLayers.empty()) subLayers = sourceObj->GetTextureLayers();

							std::string subGroupName = groupName + "_" + baseName;
							scene.RemoveInstancedGroup(subGroupName);

							InstancedGroup* group = new InstancedGroup(subGroupName);
							if (matchingChild) group->SetSourceObjectName(matchingChild->GetName());
							else group->SetSourceObjectName(objName);

							size_t key0 = (size_t)meshDataList[m].vertices.data() ^ meshDataList[m].vertices.size() ^ m;
							if (meshCache.find(key0) == meshCache.end()) meshCache[key0] = meshDataList[m].ToMesh(0);
							group->Setup(meshCache[key0], packedInstances, subMat, subTex, subNorm, subLayers);

							if (mg.lod1Idx != -1) {
								size_t m1 = mg.lod1Idx;
								size_t key1 = (size_t)meshDataList[m1].vertices.data() ^ meshDataList[m1].vertices.size() ^ m1;
								if (meshCache.find(key1) == meshCache.end()) meshCache[key1] = meshDataList[m1].ToMesh(0);
								group->SetLODMesh(1, meshCache[key1], 0.0f);
							}
							if (mg.lod2Idx != -1) {
								size_t m2 = mg.lod2Idx;
								size_t key2 = (size_t)meshDataList[m2].vertices.data() ^ meshDataList[m2].vertices.size() ^ m2;
								if (meshCache.find(key2) == meshCache.end()) meshCache[key2] = meshDataList[m2].ToMesh(0);
								group->SetLODMesh(2, meshCache[key2], 0.0f);
							}

							scene.AddInstancedGroup(group);
							scatterNode->AddCreatedGroupName(subGroupName);
						}
						printf("[NodeGraph] GPU-Driven: Created %d LOD-aware Groups for '%s'\n", (int)groupsByName.size(), objName.c_str());
					}
					else {
						// ============================================================
						// SINGLE-MESH PATH: One InstancedGroup (grass, rocks, etc.)
						// ============================================================
						MeshData singleMesh = defaultObjectMesh;

						// If empty, try to collect from hierarchy
						if (singleMesh.vertices.empty() && sourceObj) {
							std::function<void(GameObject*)> collectMeshes = [&](GameObject* obj) {
								if (obj->GetModel() && !obj->GetModel()->GetMeshDataList().empty()) {
									for (const auto& md : obj->GetModel()->GetMeshDataList()) {
										int baseIdx = singleMesh.GetVertexCount();
										singleMesh.vertices.insert(singleMesh.vertices.end(), md.vertices.begin(), md.vertices.end());
										for (unsigned int idx : md.indices) singleMesh.indices.push_back(idx + baseIdx);
									}
								} else if (obj->HasCustomMesh()) {
									const MeshData& data = obj->GetCPUMeshData();
									if (!data.vertices.empty()) {
										int baseIdx = singleMesh.GetVertexCount();
										singleMesh.vertices.insert(singleMesh.vertices.end(), data.vertices.begin(), data.vertices.end());
										for (unsigned int idx : data.indices) singleMesh.indices.push_back(idx + baseIdx);
									}
								}
								for (auto* child : obj->GetChildren()) collectMeshes(child);
							};
							collectMeshes(sourceObj);
						}

						// Fallback to parent model
						if (singleMesh.vertices.empty() && multiMeshModel && !multiMeshModel->GetMeshDataList().empty()) {
							const auto& md = multiMeshModel->GetMeshDataList()[0];
							singleMesh = md;
						}

						if (singleMesh.vertices.empty()) {
							printf("[NodeGraph] Warning: No mesh data for '%s', skipping.\n", objName.c_str());
						} else {
							size_t dataKey = (size_t)singleMesh.vertices.data() ^ singleMesh.vertices.size();
							if (meshCache.find(dataKey) == meshCache.end()) {
								meshCache[dataKey] = singleMesh.ToMesh(0);
							}

							Material* finalMat = instancesPin.data.sourceMaterial;
							Texture* finalTex = instancesPin.data.sourceTexture;
							Texture* finalNorm = instancesPin.data.sourceNormalMap;
							std::vector<TextureLayer> finalLayers = instancesPin.data.textureLayers;

							// If no texture from pin, try from model
							if (!finalTex && multiMeshModel) {
								finalTex = multiMeshModel->GetTexture(multiMeshModel->GetMaterialIndex(0));
								finalNorm = multiMeshModel->GetNormalMap(multiMeshModel->GetMaterialIndex(0));
							}

							InstancedGroup* group = new InstancedGroup(groupName);
							group->SetSourceObjectName(objName);
							group->Setup(meshCache[dataKey], packedInstances, finalMat, finalTex, finalNorm, finalLayers);
							group->SetMaxDrawDistance(maxDist);
							group->SetShadowDistance(shadowDist);

							scene.AddInstancedGroup(group);

							printf("[NodeGraph] GPU-Driven: Created InstancedGroup '%s' with %d instances (%d verts)\n",
								groupName.c_str(), (int)packedInstances.size(), singleMesh.GetVertexCount());
						}
					}

					// Placeholder parent for hierarchy
					std::string parentName = "Scatter_Group_" + std::to_string(node->id) + "_" + objName;
					GameObject* placeholder = scene.FindObject(parentName);
					if (!placeholder) {
						placeholder = new GameObject(parentName);
						scene.AddObject(placeholder);
					}
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

	if (progressCallback) {
		progressCallback(100.0f, 100.0f, "Done!");
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
			else if (title == "River") node = new RiverNode(*this);
			else if (title == "Merge Mesh") node = new MergeMeshNode(*this);
			else if (title == "Float") node = new FloatConstantNode(*this);
			else if (title == "Int") node = new IntConstantNode(*this);
			else if (title == "Vec3") node = new Vec3ConstantNode(*this);
			else if (title == "Bool") node = new BoolConstantNode(*this);
			else if (title == "Math") node = new MathNode(*this);
			else if (title == "Compare") node = new CompareNode(*this);
			else if (title == "Branch") node = new BranchNode(*this);
			else if (title == "Filter Transforms") node = new FilterTransformListNode(*this);
			else if (title == "Object Filter") node = new ObjectIntersectionFilterNode(*this);
			else if (title == "For Each") node = new ForEachNode(*this);
		else if (title == "City Grid") node = new CityGridNode(*this);
		else if (title == "Building Gen") node = new BuildingGenNode(*this);
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
	// (ScatterNode uses 'Instance_X_Y' or 'Instance_X_Name_Y' patterns)
	if (name.find("Instance_") != std::string::npos || 
		name.find("Scatter_Instanced_") != std::string::npos ||
		name.find("Scatter_Group_") != std::string::npos ||
		name.find("River_Water_") != std::string::npos ||
		name.find("Lake_Water_") != std::string::npos)
		return true;

	return false;
}

bool NodeGraph::IsObjectMeshModified(const std::string& name) const
{
	if (name == "(none)") return false;
	if (IsObjectGenerated(name)) return true;

	for (auto* node : nodes)
	{
		if (node->title == "Output")
		{
			// Safely check if this node targets the specific block for mesh update
			OutputNode* outNode = static_cast<OutputNode*>(node);
			if (outNode && outNode->ShouldUpdateMesh())
			{
				if (outNode->IsSameAsInput())
				{
					// If it targets the same object that was input, check the input pin
					if (!outNode->inputs.empty() && outNode->inputs[0].data.sourceObjectName == name)
						return true;
				}
				else if (outNode->GetTargetName() == name)
				{
					return true;
				}
			}
		}
	}
	return false;
}

void NodeGraph::NotifyObjectRenamed(const std::string& oldName, const std::string& newName)
{
	for (auto* node : nodes)
		node->OnObjectRenamed(oldName, newName);
}

void NodeGraph::Clear()
{
	for (auto* n : nodes)
		delete n;
	nodes.clear();
	links.clear();
	generatedObjectNames.clear();
}
