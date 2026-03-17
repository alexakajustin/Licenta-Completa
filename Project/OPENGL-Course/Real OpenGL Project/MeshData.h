#pragma once

#include <vector>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include "Mesh.h"

class Material;
class Texture;

// ========== Transform Data ==========
struct TransformData
{
	glm::vec3 position = glm::vec3(0.0f);
	glm::vec3 rotation = glm::vec3(0.0f); // Euler degrees
	glm::vec3 scale = glm::vec3(1.0f);
	glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
};

using TransformList = std::vector<TransformData>;

// ========== Pin Data Types ==========
enum class PinDataType
{
	None,
	Mesh,
	TransformList,
	Float,
	Int,
	Vec3,
	Vec2,
	Bool
};

// ========== CPU-side Mesh Data ==========
// Vertex layout: pos(3) + uv(2) + normal(3) + tangent(3) + bitangent(3) = 14 floats
struct MeshData
{
	std::vector<GLfloat> vertices;
	std::vector<unsigned int> indices;

	// Upload to GPU and return a new Mesh
	Mesh* ToMesh(int maxInstances = 0) const
	{
		if (vertices.empty() || indices.empty()) return nullptr;

		try {
			Mesh* mesh = new Mesh();
			if (maxInstances > 0)
			{
				mesh->CreateInstancedMesh(
					const_cast<GLfloat*>(vertices.data()),
					const_cast<unsigned int*>(indices.data()),
					(unsigned int)vertices.size(),
					(unsigned int)indices.size(),
					maxInstances
				);
			}
			else
			{
				mesh->CreateMesh(
					const_cast<GLfloat*>(vertices.data()),
					const_cast<unsigned int*>(indices.data()),
					(unsigned int)vertices.size(),
					(unsigned int)indices.size()
				);
			}
			
			glm::vec3 min, max;
			GetBounds(min, max);
			mesh->SetBounds(min, max);

			return mesh;
		}
		catch (const std::exception& e) {
			printf("[MeshData] GPU Upload failed: %s\n", e.what());
			return nullptr;
		}
	}

	// Helper: add a vertex (14 floats)
	void AddVertex(float px, float py, float pz,
		float u, float v,
		float nx, float ny, float nz,
		float tx, float ty, float tz,
		float bx, float by, float bz)
	{
		vertices.push_back(px); vertices.push_back(py); vertices.push_back(pz);
		vertices.push_back(u);  vertices.push_back(v);
		vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
		vertices.push_back(tx); vertices.push_back(ty); vertices.push_back(tz);
		vertices.push_back(bx); vertices.push_back(by); vertices.push_back(bz);
	}

	// Helper: add a triangle (3 indices)
	void AddTriangle(unsigned int i0, unsigned int i1, unsigned int i2)
	{
		indices.push_back(i0);
		indices.push_back(i1);
		indices.push_back(i2);
	}

	void TransformBy(const glm::mat4& matrix)
	{
		glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(matrix)));
		int count = GetVertexCount();
		for (int i = 0; i < count; i++)
		{
			int base = i * 14;
			// Position
			glm::vec4 p = matrix * glm::vec4(vertices[base], vertices[base + 1], vertices[base + 2], 1.0f);
			vertices[base] = p.x;
			vertices[base + 1] = p.y;
			vertices[base + 2] = p.z;

			// Normal
			glm::vec3 n = glm::normalize(normalMatrix * glm::vec3(vertices[base + 5], vertices[base + 6], vertices[base + 7]));
			vertices[base + 5] = n.x;
			vertices[base + 6] = n.y;
			vertices[base + 7] = n.z;

			// Tangent
			glm::vec3 t = glm::normalize(normalMatrix * glm::vec3(vertices[base + 8], vertices[base + 9], vertices[base + 10]));
			vertices[base + 8] = t.x;
			vertices[base + 9] = t.y;
			vertices[base + 10] = t.z;

			// Bitangent
			glm::vec3 b = glm::normalize(normalMatrix * glm::vec3(vertices[base + 11], vertices[base + 12], vertices[base + 13]));
			vertices[base + 11] = b.x;
			vertices[base + 12] = b.y;
			vertices[base + 13] = b.z;
		}
	}

	void Append(const MeshData& other)
	{
		if (other.vertices.empty()) return;

		try {
			size_t oldVertSize = vertices.size();
			size_t oldIndexSize = indices.size();
			unsigned int baseVertex = (unsigned int)(oldVertSize / 14);
			
			// Reserve to avoid multiple reallocations and temporary capacity doubling
			vertices.reserve(oldVertSize + other.vertices.size());
			indices.reserve(oldIndexSize + other.indices.size());

			// Bulk append vertices
			vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
			
			// Bulk append and offset indices
			size_t otherIndexCount = other.indices.size();
			for (size_t i = 0; i < otherIndexCount; i++)
			{
				indices.push_back(other.indices[i] + baseVertex);
			}
		}
		catch (const std::exception& e) {
			printf("[MeshData] Append failed (RAM full): %s\n", e.what());
		}
	}

	// Get position of vertex at index
	glm::vec3 GetPosition(int vertIndex) const
	{
		int base = vertIndex * 14;
		return glm::vec3(vertices[base], vertices[base + 1], vertices[base + 2]);
	}

	// Get normal of vertex at index
	glm::vec3 GetNormal(int vertIndex) const
	{
		int base = vertIndex * 14;
		return glm::vec3(vertices[base + 5], vertices[base + 6], vertices[base + 7]);
	}

	void GetBounds(glm::vec3& min, glm::vec3& max) const
	{
		if (vertices.empty()) {
			min = glm::vec3(0.0f);
			max = glm::vec3(0.0f);
			return;
		}
		min = glm::vec3(1e10f);
		max = glm::vec3(-1e10f);
		int count = GetVertexCount();
		for (int i = 0; i < count; i++) {
			glm::vec3 p = GetPosition(i);
			min = glm::min(min, p);
			max = glm::max(max, p);
		}
	}

	int GetVertexCount() const { return (int)vertices.size() / 14; }
	int GetTriangleCount() const { return (int)indices.size() / 3; }

	void Clear() { vertices.clear(); indices.clear(); }

	// Truly free RAM by swapping with empty vectors (standard C++ swap trick)
	void DeepClear()
	{
		std::vector<GLfloat>().swap(vertices);
		std::vector<unsigned int>().swap(indices);
	}
};

// ========== Tagged union for data flowing between nodes ==========
struct PinData
{
	PinDataType type = PinDataType::None;
	MeshData meshData;
	TransformList transforms;
	std::vector<MeshData> instanceMeshes;
	std::string sourceObjectName = "(none)";
	Material* sourceMaterial = nullptr;
	Texture* sourceTexture = nullptr;
	Texture* sourceNormalMap = nullptr;

	// Scalar values for new pin types
	float floatValue = 0.0f;
	int intValue = 0;
	glm::vec3 vec3Value = glm::vec3(0.0f);
	glm::vec2 vec2Value = glm::vec2(0.0f);
	bool boolValue = false;

	void Clear()
	{
		type = PinDataType::None;
		meshData.Clear();
		transforms.clear();
		instanceMeshes.clear();
		sourceObjectName = "(none)";
		sourceMaterial = nullptr;
		sourceTexture = nullptr;
		sourceNormalMap = nullptr;
		floatValue = 0.0f;
		intValue = 0;
		vec3Value = glm::vec3(0.0f);
		vec2Value = glm::vec2(0.0f);
		boolValue = false;
	}

	void DeepClear()
	{
		type = PinDataType::None;
		meshData.DeepClear();
		std::vector<TransformData>().swap(transforms);
		for (auto& m : instanceMeshes) m.DeepClear();
		std::vector<MeshData>().swap(instanceMeshes);
		sourceObjectName = "(none)";
		sourceMaterial = nullptr;
		sourceTexture = nullptr;
		sourceNormalMap = nullptr;
		floatValue = 0.0f;
		intValue = 0;
		vec3Value = glm::vec3(0.0f);
		vec2Value = glm::vec2(0.0f);
		boolValue = false;
	}
};
