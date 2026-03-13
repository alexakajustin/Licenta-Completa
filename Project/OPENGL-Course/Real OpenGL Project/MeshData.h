#pragma once

#include <vector>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "Mesh.h"

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
	TransformList
};

// ========== CPU-side Mesh Data ==========
// Vertex layout: pos(3) + uv(2) + normal(3) + tangent(3) + bitangent(3) = 14 floats
struct MeshData
{
	std::vector<GLfloat> vertices;
	std::vector<unsigned int> indices;

	// Upload to GPU and return a new Mesh
	Mesh* ToMesh() const
	{
		if (vertices.empty() || indices.empty()) return nullptr;

		Mesh* mesh = new Mesh();
		mesh->CreateMesh(
			const_cast<GLfloat*>(vertices.data()),
			const_cast<unsigned int*>(indices.data()),
			(unsigned int)vertices.size(),
			(unsigned int)indices.size()
		);
		return mesh;
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

	int GetVertexCount() const { return (int)vertices.size() / 14; }
	int GetTriangleCount() const { return (int)indices.size() / 3; }

	void Clear() { vertices.clear(); indices.clear(); }
};

// ========== Tagged union for data flowing between nodes ==========
struct PinData
{
	PinDataType type = PinDataType::None;
	MeshData meshData;
	TransformList transforms;

	void Clear()
	{
		type = PinDataType::None;
		meshData.Clear();
		transforms.clear();
	}
};
