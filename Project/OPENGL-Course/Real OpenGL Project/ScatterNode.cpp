#include "NodeGraph.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "PrimitiveGenerator.h"
#include "imgui.h"
#include "ScatterNode.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <random>

void ScatterNode::RenderContent(SceneManager* scene)
{
	ImGui::PushID(this);

	ImGui::SliderInt("Count", &count, 1, 1000);
	ImGui::DragFloat("Min Scale", &minScale, 0.01f, 0.01f, 10.0f);
	ImGui::DragFloat("Max Scale", &maxScale, 0.01f, 0.01f, 10.0f);
	ImGui::Checkbox("Random Rotation", &randomRotation);
	ImGui::Checkbox("Align to Normal", &alignToNormal);
	if (ImGui::InputInt("Seed", &seed)) {}
	ImGui::SameLine();
	if (ImGui::Button("Rand"))
		seed = std::rand();

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

	MeshData combinedResult = surfaceMesh;
	MeshData instancesOnly;

	int addedVerts = count * objectMesh.GetVertexCount();
	int addedIndices = count * objectMesh.indices.size();
	
	combinedResult.vertices.reserve(combinedResult.vertices.size() + addedVerts * 14);
	combinedResult.indices.reserve(combinedResult.indices.size() + addedIndices);
	instancesOnly.vertices.reserve(addedVerts * 14);
	instancesOnly.indices.reserve(addedIndices);

	// Setup modular output lists
	outputs[1].data.transforms.reserve(count);
	outputs[1].data.instanceMeshes.reserve(count);

	for (int i = 0; i < count; i++)
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

		glm::vec3 pos = v0 * r0 + v1 * r1 + v2 * r2;

		// Interpolate normal
		glm::vec3 n0 = surfaceMesh.GetNormal(i0);
		glm::vec3 n1 = surfaceMesh.GetNormal(i1);
		glm::vec3 n2 = surfaceMesh.GetNormal(i2);
		glm::vec3 normal = glm::normalize(n0 * r0 + n1 * r1 + n2 * r2);

		// Random scale
		float s = scaleDist(gen);
		glm::vec3 scaleVec(s);

		// Random rotation
		glm::vec3 rot(0.0f);
		if (randomRotation)
		{
			rot.y = rotDist(gen);
		}

		// Store transform for modular downstream use
		TransformData t;
		t.position = pos;
		t.rotation = rot;
		t.scale = scaleVec;
		t.normal = normal;
		lastTransforms.push_back(t); // Compatibility
		outputs[1].data.transforms.push_back(t);
		outputs[1].data.instanceMeshes.push_back(objectMesh); // Modular: individual copy for noise node to hit

		// Compute baked result for Combined and Instances Only
		MergeTransformed(objectMesh, pos, rot, scaleVec, normal, combinedResult);
		MergeTransformed(objectMesh, pos, rot, scaleVec, normal, instancesOnly);
	}

	outputs[0].data.meshData = combinedResult;
	outputs[0].data.sourceObjectName = inputs[0].data.sourceObjectName; // Combined takes surface name

	outputs[1].data.meshData = instancesOnly;
	outputs[1].data.sourceObjectName = "(none)"; 
}
