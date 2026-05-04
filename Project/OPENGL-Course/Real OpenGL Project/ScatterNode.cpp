#include "NodeGraph.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "PrimitiveGenerator.h"
#include "imgui.h"
#include "ScatterNode.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <random>
#include <thread>
#include <future>
#include <stdexcept>
#include <mutex>
#include <glm/gtx/norm.hpp>

json ScatterNode::Serialize() const
{
	json j = GraphNode::Serialize();
	j["count"] = count;
	j["minScale"] = minScale;
	j["maxScale"] = maxScale;
	j["randomRotation"] = randomRotation;
	j["alignToNormal"] = alignToNormal;
	j["seed"] = seed;
	j["spawnAsObjects"] = spawnAsObjects;
	j["targetParentName"] = targetParentName;

	// Save all created group names
	json groups = json::array();
	for (const auto& name : createdGroupNames) groups.push_back(name);
	j["createdGroupNames"] = groups;

	// Save spawned map
	json sMap = json::object();
	for (auto const& [objName, instances] : spawnedMap) {
		json instArr = json::array();
		for (const auto& name : instances) instArr.push_back(name);
		sMap[objName] = instArr;
	}
	j["spawnedMap"] = sMap;

	// Save deletion volumes
	json delVols = json::array();
	for (const auto& vol : deletionVolumes) {
		json v;
		v["x"] = vol.position.x;
		v["y"] = vol.position.y;
		v["z"] = vol.position.z;
		v["radius"] = vol.radius;
		delVols.push_back(v);
	}
	j["deletionVolumes"] = delVols;

	return j;
}

void ScatterNode::Deserialize(const json& j)
{
	GraphNode::Deserialize(j);
	count = j.value("count", 50);
	minScale = j.value("minScale", 0.8f);
	maxScale = j.value("maxScale", 1.2f);
	randomRotation = j.value("randomRotation", true);
	alignToNormal = j.value("alignToNormal", true);
	seed = j.value("seed", 42);
	spawnAsObjects = j.value("spawnAsObjects", false); // Default to high-performance instancing
	targetParentName = j.value("targetParentName", "(none)");
	targetParentIndex = -1; // Force re-resolution on first Execute

	// Restore group names
	createdGroupNames.clear();
	if (j.contains("createdGroupNames")) {
		for (const auto& g : j["createdGroupNames"]) createdGroupNames.insert(g.get<std::string>());
	}

	// Restore spawned map
	spawnedMap.clear();
	if (j.contains("spawnedMap")) {
		for (auto it = j["spawnedMap"].begin(); it != j["spawnedMap"].end(); ++it) {
			std::vector<std::string> instances;
			for (const auto& name : it.value()) instances.push_back(name.get<std::string>());
			spawnedMap[it.key()] = instances;
		}
	}

	// Restore deletion volumes
	deletionVolumes.clear();
	if (j.contains("deletionVolumes")) {
		for (const auto& v : j["deletionVolumes"]) {
			DeletionVolume vol;
			vol.position.x = v.value("x", 0.0f);
			vol.position.y = v.value("y", 0.0f);
			vol.position.z = v.value("z", 0.0f);
			vol.radius = v.value("radius", 1.0f);
			deletionVolumes.push_back(vol);
		}
	}
}

void ScatterNode::RenderContent(SceneManager* scene)
{
	ImGui::PushID(this);

	ImGui::DragInt("Count", &count, 1.0f, 1, 1000000);
	ImGui::DragFloat("Min Scale", &minScale, 0.01f, 0.01f, 10.0f);
	ImGui::DragFloat("Max Scale", &maxScale, 0.01f, 0.01f, 10.0f);
	ImGui::Checkbox("Random Rotation", &randomRotation);
	ImGui::Checkbox("Align to Normal", &alignToNormal);
	if (ImGui::InputInt("Seed", &seed)) {}
	ImGui::SameLine();
	if (ImGui::Button("Rand"))
		seed = std::rand();

	ImGui::Separator();
	ImGui::TextDisabled("Spawning Mode: Forced Instancing");
	if (scene)
	{
		auto& objects = scene->GetObjects();

		if (ImGui::BeginCombo("Spawning Parent", targetParentName.c_str()))
		{
			for (int i = 0; i < (int)objects.size(); i++)
			{
				bool isSelected = (targetParentIndex == i);
				if (ImGui::Selectable(objects[i]->GetName().c_str(), isSelected))
				{
					targetParentIndex = i;
					targetParentName = objects[i]->GetName();
				}
			}
			ImGui::EndCombo();
		}
	}

	ImGui::PopID();
}

float ScatterNode::RandRange(float min, float max)
{
	// This is a legacy helper, but we'll use mt19937 for real work
	float t = (float)std::rand() / (float)RAND_MAX;
	return min + t * (max - min);
}

void ScatterNode::RandomPointOnMesh(const MeshData& mesh, glm::vec3& outPos, glm::vec3& outNormal)
{
	if (mesh.indices.empty() || mesh.vertices.empty())
	{
		outPos = glm::vec3(0.0f);
		outNormal = glm::vec3(0.0f, 1.0f, 0.0f);
		return;
	}

	// Pick a random triangle
	int triCount = (int)mesh.indices.size() / 3;
	int triIdx = std::rand() % triCount;

	unsigned int i0 = mesh.indices[triIdx * 3];
	unsigned int i1 = mesh.indices[triIdx * 3 + 1];
	unsigned int i2 = mesh.indices[triIdx * 3 + 2];

	glm::vec3 v0 = mesh.GetPosition(i0);
	glm::vec3 v1 = mesh.GetPosition(i1);
	glm::vec3 v2 = mesh.GetPosition(i2);

	// Random barycentric coordinates
	float r1 = RandRange(0.0f, 1.0f);
	float r2 = RandRange(0.0f, 1.0f);
	if (r1 + r2 > 1.0f)
	{
		r1 = 1.0f - r1;
		r2 = 1.0f - r2;
	}
	float r0 = 1.0f - r1 - r2;

	outPos = v0 * r0 + v1 * r1 + v2 * r2;

	// Interpolate normal
	glm::vec3 n0 = mesh.GetNormal(i0);
	glm::vec3 n1 = mesh.GetNormal(i1);
	glm::vec3 n2 = mesh.GetNormal(i2);
	outNormal = glm::normalize(n0 * r0 + n1 * r1 + n2 * r2);
}

void ScatterNode::MergeTransformed(const MeshData& objectMesh, const glm::vec3& pos,
	const glm::vec3& rotation, const glm::vec3& scaleVec,
	const glm::vec3& surfaceNormal, MeshData& output)
{
	// Build transform matrix
	glm::mat4 model = glm::mat4(1.0f);
	model = glm::translate(model, pos);

	// Align Y-up to surface normal if requested
	if (alignToNormal && glm::length(surfaceNormal) > 0.001f)
	{
		glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 normal = glm::normalize(surfaceNormal);

		if (glm::abs(glm::dot(up, normal)) < 0.999f)
		{
			glm::vec3 rotAxis = glm::normalize(glm::cross(up, normal));
			float angle = acos(glm::clamp(glm::dot(up, normal), -1.0f, 1.0f));
			model = glm::rotate(model, angle, rotAxis);
		}
	}

	// Apply rotation
	model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1, 0, 0));
	model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0, 1, 0));
	model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0, 0, 1));

	// Apply scale
	model = glm::scale(model, scaleVec);

	// Normal matrix (inverse transpose of upper 3x3)
	glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));

	// Merge vertices
	int baseVertex = output.GetVertexCount();
	int srcVertCount = objectMesh.GetVertexCount();

	for (int i = 0; i < srcVertCount; i++)
	{
		int base = i * 14;

		// Transform position
		glm::vec4 p = model * glm::vec4(
			objectMesh.vertices[base], objectMesh.vertices[base + 1], objectMesh.vertices[base + 2], 1.0f);

		// Keep UVs
		float u = objectMesh.vertices[base + 3];
		float v = objectMesh.vertices[base + 4];

		// Transform normal
		glm::vec3 n = glm::normalize(normalMatrix * glm::vec3(
			objectMesh.vertices[base + 5], objectMesh.vertices[base + 6], objectMesh.vertices[base + 7]));

		// Transform tangent and bitangent
		glm::vec3 t = glm::normalize(normalMatrix * glm::vec3(
			objectMesh.vertices[base + 8], objectMesh.vertices[base + 9], objectMesh.vertices[base + 10]));

		glm::vec3 b = glm::normalize(normalMatrix * glm::vec3(
			objectMesh.vertices[base + 11], objectMesh.vertices[base + 12], objectMesh.vertices[base + 13]));

		output.AddVertex(p.x, p.y, p.z, u, v, n.x, n.y, n.z, t.x, t.y, t.z, b.x, b.y, b.z);
	}

	// Merge indices (offset by base vertex)
	for (int i = 0; i < (int)objectMesh.indices.size(); i++)
	{
		output.indices.push_back(objectMesh.indices[i] + baseVertex);
	}
}

void ScatterNode::Execute(SceneManager& scene, NodeProgressCallback progress)
{
  try {
	outputs[0].data.Clear(); // Combined
	outputs[0].data.type = PinDataType::Mesh;
	outputs[1].data.Clear(); // Instances Only
	outputs[1].data.type = PinDataType::Mesh;

	// Get surface mesh from input 0
	MeshData& surfaceMesh = inputs[0].data.meshData;
	// Get object mesh from input 1
	MeshData& objectMesh = inputs[1].data.meshData;

	bool hasSurface = (inputs[0].data.type == PinDataType::Mesh && !surfaceMesh.vertices.empty());
	
	// An object is valid if it has a mesh OR it has a source object with children (hierarchy)
	bool hasObject = (inputs[1].data.type == PinDataType::Mesh && 
		(!objectMesh.vertices.empty() || (inputs[1].data.sourceObject && !inputs[1].data.sourceObject->GetChildren().empty())));

	if (!hasSurface || !hasObject)
	{
		// If no object mesh connected, just pass through the surface to Combined
		if (hasSurface) outputs[0].data.meshData = surfaceMesh;
		return;
	}

	// 1. Determine how many transforms we want (Spawning/Instancing)
	int workingCount = count;
	if (workingCount > 10000000) workingCount = 10000000;

	// Setup modular output lists (Full workingCount!)
	outputs[1].data.transforms.resize(workingCount); // Pre-allocate for parallel writing
	outputs[1].data.instanceMeshes.clear();

	// Setup modular output lists (Full workingCount!)
	outputs[1].data.transforms.resize(workingCount); // Pre-allocate for parallel writing
	outputs[1].data.instanceMeshes.clear(); // We don't usually generate unique meshes here unless noise is integrated

	// Calculate surface world matrix (INCLUDING scale, so spawned objects spread properly over scaled surfaces)
	glm::mat4 surfaceWorld = glm::mat4(1.0f);
	glm::mat3 surfaceNormalMatrix = glm::mat3(1.0f);
	if (!inputs[0].data.transforms.empty())
	{
		const TransformData& st = inputs[0].data.transforms[0];
		surfaceWorld = glm::translate(surfaceWorld, st.position);
		surfaceWorld = glm::rotate(surfaceWorld, glm::radians(st.rotation.x), glm::vec3(1, 0, 0));
		surfaceWorld = glm::rotate(surfaceWorld, glm::radians(st.rotation.y), glm::vec3(0, 1, 0));
		surfaceWorld = glm::rotate(surfaceWorld, glm::radians(st.rotation.z), glm::vec3(0, 0, 1));
		surfaceWorld = glm::scale(surfaceWorld, st.scale);
		
		surfaceNormalMatrix = glm::transpose(glm::inverse(glm::mat3(surfaceWorld)));
	}

	// === HIGH PERFORMANCE PARALLEL TRANSFORM GENERATION ===
	unsigned int transThreads = std::thread::hardware_concurrency();
	if (transThreads == 0) transThreads = 4;
	if (transThreads > (unsigned int)workingCount) transThreads = (unsigned int)workingCount;

	std::vector<std::future<void>> transFutures;
	int transPerThread = workingCount / transThreads;

	for (unsigned int tIdx = 0; tIdx < transThreads; tIdx++)
	{
		int startIdx = tIdx * transPerThread;
		int endIdx = (tIdx == transThreads - 1) ? workingCount : (tIdx + 1) * transPerThread;

		transFutures.push_back(std::async(std::launch::async, [&, startIdx, endIdx, tIdx]() {
			// Thread-local random engine and distributions prevent racing and bias
			std::mt19937 localGen(seed + tIdx);
			std::uniform_int_distribution<> triDist(0, (int)surfaceMesh.indices.size() / 3 - 1);
			std::uniform_real_distribution<float> floatDist(0.0f, 1.0f);
			std::uniform_real_distribution<float> rotDist(0.0f, 360.0f);
			std::uniform_real_distribution<float> scaleDist(minScale, maxScale);

			glm::vec3 baseScale(1.0f);
			if (!inputs[1].data.transforms.empty())
				baseScale = inputs[1].data.transforms[0].scale;

			for (int i = startIdx; i < endIdx; i++)
			{
				int triIdx = triDist(localGen);
				unsigned int i0 = surfaceMesh.indices[triIdx * 3];
				unsigned int i1 = surfaceMesh.indices[triIdx * 3 + 1];
				unsigned int i2 = surfaceMesh.indices[triIdx * 3 + 2];

				glm::vec3 v0 = surfaceMesh.GetPosition(i0);
				glm::vec3 v1 = surfaceMesh.GetPosition(i1);
				glm::vec3 v2 = surfaceMesh.GetPosition(i2);

				float r1 = floatDist(localGen);
				float r2 = floatDist(localGen);
				if (r1 + r2 > 1.0f)
				{
					r1 = 1.0f - r1;
					r2 = 1.0f - r2;
				}
				float r0 = 1.0f - r1 - r2;

				glm::vec3 localPos = v0 * r0 + v1 * r1 + v2 * r2;

				glm::vec3 n0 = surfaceMesh.GetNormal(i0);
				glm::vec3 n1 = surfaceMesh.GetNormal(i1);
				glm::vec3 n2 = surfaceMesh.GetNormal(i2);
				glm::vec3 localNormal = glm::normalize(n0 * r0 + n1 * r1 + n2 * r2);

				glm::vec3 worldPos = glm::vec3(surfaceWorld * glm::vec4(localPos, 1.0f));
				glm::vec3 worldNormal = glm::normalize(surfaceNormalMatrix * localNormal);

				float s = scaleDist(localGen);
				glm::vec3 scaleVec = baseScale * s;

				glm::vec3 rot(0.0f);
				if (randomRotation)
				{
					rot.y = rotDist(localGen);
				}

				TransformData t;
				t.position = worldPos;
				t.rotation = rot;
				t.scale = scaleVec;
				t.normal = worldNormal;

				outputs[1].data.transforms[i] = t;
			}
		}));
	}

	// Wait for transform generation threads to finish
	if (progress) progress(10.0f, "Computing Transforms...");
	int transCompleted = 0;
	std::vector<bool> transDone(transFutures.size(), false);
	while (transCompleted < (int)transFutures.size()) {
		for (size_t i = 0; i < transFutures.size(); i++) {
			if (!transDone[i]) {
				if (transFutures[i].wait_for(std::chrono::milliseconds(5)) == std::future_status::ready) {
					transDone[i] = true;
					transCompleted++;
				}
			}
		}
		if (progress) progress(10.0f + (transCompleted * 15.0f / transFutures.size()), "Computing Transforms... (" + std::to_string(transCompleted) + "/" + std::to_string(transFutures.size()) + ") Threads");
	}

	// Filter out deleted volumes (Spatial Masking)
	if (!deletionVolumes.empty()) {
		if (progress) progress(25.0f, "Applying Deletion Masks...");
		std::vector<TransformData> filtered;
		filtered.reserve(outputs[1].data.transforms.size());
		for (const auto& t : outputs[1].data.transforms) {
			bool skip = false;
			for (const auto& vol : deletionVolumes) {
				// Fast squared distance check
				float distSq = glm::distance2(t.position, vol.position);
				if (distSq <= (vol.radius * vol.radius)) {
					skip = true;
					break;
				}
			}
			if (!skip) filtered.push_back(t);
		}
		outputs[1].data.transforms = std::move(filtered);
	}

	// We only output transforms and surface now. 
	// The "Spawn as Objects" path is handled by NodeGraph observing this node.
	outputs[0].data.meshData = surfaceMesh; // Pass through surface
	outputs[0].data.sourceObjectName = std::move(inputs[0].data.sourceObjectName);
	outputs[0].data.transforms = std::move(inputs[0].data.transforms); 
	outputs[0].data.sourceMaterial = inputs[0].data.sourceMaterial;
	outputs[0].data.sourceTexture = inputs[0].data.sourceTexture;
	outputs[0].data.sourceNormalMap = inputs[0].data.sourceNormalMap;
	outputs[0].data.textureLayers = inputs[0].data.textureLayers;

	outputs[1].data.sourceObject = inputs[1].data.sourceObject;
	outputs[1].data.sourceObjectName = inputs[1].data.sourceObjectName;
	outputs[1].data.sourceMaterial = inputs[1].data.sourceMaterial;
	outputs[1].data.sourceTexture = inputs[1].data.sourceTexture;
	outputs[1].data.sourceNormalMap = inputs[1].data.sourceNormalMap;
	outputs[1].data.textureLayers = inputs[1].data.textureLayers;
  }
  catch (const std::exception& e) {
	printf("[ScatterNode] Execution failed (Memory exhausted): %s\n", e.what());
	// Fallback: Just pass through surface
	outputs[0].data.meshData = inputs[0].data.meshData;
  }
}

void ScatterNode::OnRemove(SceneManager& scene)
{
	// 1. Clean up ALL "Spawn as Objects" instantiated meshes across all object types
	for (auto const& [objName, instances] : spawnedMap) {
		for (const auto& name : instances)
			scene.RemoveObject(name);
	}
	spawnedMap.clear();

	// 2. Clean up ALL tracked Instanced Groups
	for (const auto& groupName : createdGroupNames) {
		scene.RemoveInstancedGroup(groupName);
		
		// Also try to remove the placeholder parent if it exists
		// Pattern: "Scatter_Group_{id}_{name}" or "Scatter_Group_{id}"
		std::string parentName = groupName;
		size_t pos = parentName.find("Instanced_");
		if (pos != std::string::npos) {
			parentName.replace(pos, 10, "Group_");
			scene.RemoveObject(parentName);
		}
	}
	createdGroupNames.clear();
}
