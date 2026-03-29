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
	spawnAsObjects = j.value("spawnAsObjects", false);
	targetParentName = j.value("targetParentName", "(none)");
	targetParentIndex = -1; // Force re-resolution on first Execute
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
	ImGui::Checkbox("Spawn as Objects", &spawnAsObjects);
	if (spawnAsObjects && scene)
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

void ScatterNode::Execute(SceneManager& scene)
{
  try {
	lastTransforms.clear();
	outputs[0].data.Clear(); // Combined
	outputs[0].data.type = PinDataType::Mesh;
	outputs[1].data.Clear(); // Instances Only
	outputs[1].data.type = PinDataType::Mesh;

	// Get surface mesh from input 0
	MeshData& surfaceMesh = inputs[0].data.meshData;
	// Get object mesh from input 1
	MeshData& objectMesh = inputs[1].data.meshData;

	bool hasSurface = (inputs[0].data.type == PinDataType::Mesh && !surfaceMesh.vertices.empty());
	bool hasObject = (inputs[1].data.type == PinDataType::Mesh && !objectMesh.vertices.empty());

	if (!hasSurface || !hasObject)
	{
		// If no object mesh connected, just pass through the surface to Combined
		if (hasSurface) outputs[0].data.meshData = surfaceMesh;
		return;
	}

	// Use a modern random engine to avoid biased std::rand()
	std::mt19937 gen(seed);
	std::uniform_int_distribution<> triDist(0, (int)surfaceMesh.indices.size() / 3 - 1);
	std::uniform_real_distribution<float> floatDist(0.0f, 1.0f);
	std::uniform_real_distribution<float> rotDist(0.0f, 360.0f);
	std::uniform_real_distribution<float> scaleDist(minScale, maxScale);

	// Memory optimization: Build instancesOnly first.
	// We only build combinedResult at the very end by appending to the surfaceMesh.
	// This avoids having two giant result meshes in memory at the same time.
	MeshData instancesOnly;

	// 1. Determine how many transforms we want (Spawning/Instancing)
	// We cap this at a high but safe number (e.g. 500,000) for transform storage.
	int workingCount = count;
	if (workingCount > 500000) workingCount = 500000;

	// 2. Determine how many meshes can fit in the "Combined" output
	int meshCount = workingCount;
	int meshVerts = meshCount * objectMesh.GetVertexCount();
	int meshIndices = (int)(meshCount * objectMesh.indices.size());
	
	long long projectedMeshBytes = ((long long)meshVerts * 14 * 4) + ((long long)meshIndices * 4);
	// REDUCED FOR FRAGMENTATION: 600MB is the safest "contiguous" block we can expect in 32-bit.
	const long long MAX_SAFE_32BIT_ALLOC = 600LL * 1024 * 1024; 
	
	// HARD BYPASS: If a single instance of the mesh already exceeds our safe allocation limit,
	// we skip combined mesh generation entirely to avoid a guaranteed std::bad_alloc.
	long long singleInstanceBytes = ((long long)objectMesh.GetVertexCount() * 14 * 4) + ((long long)objectMesh.indices.size() * 4);
	if (singleInstanceBytes > MAX_SAFE_32BIT_ALLOC) {
		printf("[ScatterNode] Hard Bypass: Input mesh is too large for combined output (%lld MB). Skipping combined mesh.\n", singleInstanceBytes / (1024 * 1024));
		meshCount = 0;
	}
	else if (projectedMeshBytes > MAX_SAFE_32BIT_ALLOC) {
		printf("[ScatterNode] Safety: Combined mesh would exceed 32-bit limits (~%.2f MB).\n", (float)projectedMeshBytes / (1024.0f * 1024.0f));
		// Calculate how many meshes we can ACTUALLY afford to merge
		meshCount = (int)((double)MAX_SAFE_32BIT_ALLOC / (double)projectedMeshBytes * (double)meshCount);
		if (meshCount < 1) meshCount = 1;

		printf("[ScatterNode] Capping Combined mesh to %d instances, but keeping all %d transforms for spawning.\n", meshCount, workingCount);
	}
	
	meshVerts = meshCount * objectMesh.GetVertexCount();
	meshIndices = (int)(meshCount * objectMesh.indices.size());
	
	int objectVerts = objectMesh.GetVertexCount();
	int objectIndices = (int)objectMesh.indices.size();

	// Pre-allocate Combined/InstancesOnly mesh to meshCount
	instancesOnly.vertices.resize(meshVerts * 14);
	instancesOnly.indices.resize(meshIndices);

	// Setup modular output lists (Full workingCount!)
	outputs[1].data.transforms.reserve(workingCount);
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

	for (int i = 0; i < workingCount; i++)
	{
		// Pick a random triangle properly
		int triIdx = triDist(gen);
		unsigned int i0 = surfaceMesh.indices[triIdx * 3];
		unsigned int i1 = surfaceMesh.indices[triIdx * 3 + 1];
		unsigned int i2 = surfaceMesh.indices[triIdx * 3 + 2];

		glm::vec3 v0 = surfaceMesh.GetPosition(i0);
		glm::vec3 v1 = surfaceMesh.GetPosition(i1);
		glm::vec3 v2 = surfaceMesh.GetPosition(i2);

		// Random barycentric coordinates
		float r1 = floatDist(gen);
		float r2 = floatDist(gen);
		if (r1 + r2 > 1.0f)
		{
			r1 = 1.0f - r1;
			r2 = 1.0f - r2;
		}
		float r0 = 1.0f - r1 - r2;

		glm::vec3 localPos = v0 * r0 + v1 * r1 + v2 * r2;

		// Interpolate normal
		glm::vec3 n0 = surfaceMesh.GetNormal(i0);
		glm::vec3 n1 = surfaceMesh.GetNormal(i1);
		glm::vec3 n2 = surfaceMesh.GetNormal(i2);
		glm::vec3 localNormal = glm::normalize(n0 * r0 + n1 * r1 + n2 * r2);

		// Transform to World Space
		glm::vec3 worldPos = glm::vec3(surfaceWorld * glm::vec4(localPos, 1.0f));
		glm::vec3 worldNormal = glm::normalize(surfaceNormalMatrix * localNormal);

		// Random scale, preserving the original object's scale
		float s = scaleDist(gen);
		glm::vec3 baseScale(1.0f);
		if (!inputs[1].data.transforms.empty())
			baseScale = inputs[1].data.transforms[0].scale;
		glm::vec3 scaleVec = baseScale * s;

		// Random rotation
		glm::vec3 rot(0.0f);
		if (randomRotation)
		{
			rot.y = rotDist(gen);
		}

		// Store transform for modular downstream use (World Space!)
		TransformData t;
		t.position = worldPos;
		t.rotation = rot; // Note: rotation might need to be added to surface rotation if total-world-rot desired
		t.scale = scaleVec;
		t.normal = worldNormal;
		
		lastTransforms.push_back(t); // Compatibility
		outputs[1].data.transforms.push_back(t);
	}

	// === HIGH PERFORMANCE PARALLEL MESH GENERATION ===
	// Note: We only generate mesh data for meshCount instances to stay within 32-bit limits.
	unsigned int numThreads = std::thread::hardware_concurrency();
	if (numThreads == 0) numThreads = 4;
	if (numThreads > (unsigned int)meshCount) numThreads = (unsigned int)meshCount;


	std::vector<std::future<void>> futures;
	int instancesPerThread = meshCount / numThreads;

	for (unsigned int tIdx = 0; tIdx < numThreads; tIdx++)
	{
		int startInstance = tIdx * instancesPerThread;
		int endInstance = (tIdx == numThreads - 1) ? meshCount : (tIdx + 1) * instancesPerThread;

		futures.push_back(std::async(std::launch::async, [&, startInstance, endInstance]() {
			for (int i = startInstance; i < endInstance; i++)
			{
				const TransformData& tData = outputs[1].data.transforms[i];
				
				// Build transform matrix
				glm::mat4 model = glm::mat4(1.0f);
				model = glm::translate(model, tData.position);
				
				// Align Y-up to surface normal if requested
				if (alignToNormal && glm::length(tData.normal) > 0.001f)
				{
					glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
					if (glm::abs(glm::dot(up, tData.normal)) < 0.999f)
					{
						glm::vec3 rotAxis = glm::normalize(glm::cross(up, tData.normal));
						float angle = acos(glm::clamp(glm::dot(up, tData.normal), -1.0f, 1.0f));
						model = glm::rotate(model, angle, rotAxis);
					}
				}

				model = glm::rotate(model, glm::radians(tData.rotation.x), glm::vec3(1, 0, 0));
				model = glm::rotate(model, glm::radians(tData.rotation.y), glm::vec3(0, 1, 0));
				model = glm::rotate(model, glm::radians(tData.rotation.z), glm::vec3(0, 0, 1));
				model = glm::scale(model, tData.scale);

				glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));

				// Write directly to pre-allocated buffers
				float* destVerts = &instancesOnly.vertices[i * objectVerts * 14];
				const float* srcVerts = objectMesh.vertices.data();

				for (int v = 0; v < objectVerts; v++)
				{
					int base = v * 14;
					glm::vec4 p = model * glm::vec4(srcVerts[base], srcVerts[base + 1], srcVerts[base + 2], 1.0f);
					glm::vec3 n = glm::normalize(normalMatrix * glm::vec3(srcVerts[base + 5], srcVerts[base + 6], srcVerts[base + 7]));
					glm::vec3 tan = glm::normalize(normalMatrix * glm::vec3(srcVerts[base + 8], srcVerts[base + 9], srcVerts[base + 10]));
					glm::vec3 bit = glm::normalize(normalMatrix * glm::vec3(srcVerts[base + 11], srcVerts[base + 12], srcVerts[base + 13]));

					destVerts[base] = p.x; destVerts[base + 1] = p.y; destVerts[base + 2] = p.z;
					destVerts[base + 3] = srcVerts[base + 3]; destVerts[base + 4] = srcVerts[base + 4]; // UVs
					destVerts[base + 5] = n.x; destVerts[base + 6] = n.y; destVerts[base + 7] = n.z;
					destVerts[base + 8] = tan.x; destVerts[base + 9] = tan.y; destVerts[base + 10] = tan.z;
					destVerts[base + 11] = bit.x; destVerts[base + 12] = bit.y; destVerts[base + 13] = bit.z;
				}

				// Write indices with offset
				unsigned int* destIndices = &instancesOnly.indices[i * objectIndices];
				const unsigned int* srcIndices = objectMesh.indices.data();
				unsigned int vertexOffset = i * objectVerts;

				for (int idx = 0; idx < objectIndices; idx++)
				{
					destIndices[idx] = srcIndices[idx] + vertexOffset;
				}
			}
		}));
	}

	// Wait for all threads to finish
	for (auto& f : futures) f.get();

	// Now build combined result sequentially: surface + instances
	// We copy surfaceMesh first, then append instancesOnly
	MeshData combinedResult = surfaceMesh; 
	combinedResult.Append(instancesOnly);
	outputs[0].data.meshData = std::move(combinedResult);
	outputs[0].data.sourceObjectName = std::move(inputs[0].data.sourceObjectName);
	outputs[0].data.transforms = std::move(inputs[0].data.transforms); 

	outputs[1].data.meshData = std::move(instancesOnly);
	outputs[1].data.sourceObjectName = "(none)"; 
	outputs[1].data.sourceMaterial = inputs[1].data.sourceMaterial;
	outputs[1].data.sourceTexture = inputs[1].data.sourceTexture;
	outputs[1].data.sourceNormalMap = inputs[1].data.sourceNormalMap;
	outputs[1].data.textureLayers = inputs[1].data.textureLayers;

	// Combined output represents the full terrain surface visually:
	// - Material/texture/normal come from the SURFACE (inputs[0]) so the terrain renders correctly.
	// - Texture layers come from the SURFACE (inputs[0]) for the same reason.
	// The Instances-Only output already has the object's layers (set above).
	outputs[0].data.sourceMaterial = inputs[0].data.sourceMaterial;
	outputs[0].data.sourceTexture = inputs[0].data.sourceTexture;
	outputs[0].data.sourceNormalMap = inputs[0].data.sourceNormalMap;
	outputs[0].data.textureLayers = inputs[0].data.textureLayers;
  }
  catch (const std::exception& e) {
	printf("[ScatterNode] Execution failed (Memory exhausted): %s\n", e.what());
	// Fallback: Just pass through surface
	outputs[0].data.meshData = inputs[0].data.meshData;
  }
}
