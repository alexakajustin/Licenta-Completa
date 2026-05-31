#pragma once

#include <vector>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <thread>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <cmath>
#include <cfloat>
#include "Rendering/Mesh.h"

class Material;
class Texture;
class GameObject;
#include "Rendering/TextureLayer.h"

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

	// Default constructor
	MeshData() : heightCache(nullptr), heightCacheRes(0), heightCacheResZ(0), heightCacheMinX(0), heightCacheMinZ(0), heightCacheCellSize(1.0f) 
	{
		heightCacheBuilt.store(false);
	}

	// Destructor: free height cache
	~MeshData() { if (heightCache) { delete heightCache; heightCache = nullptr; } }

	// Copy constructor: copy data, don't copy cache (it's lazy)
	MeshData(const MeshData& other)
		: vertices(other.vertices), indices(other.indices),
		  heightCache(nullptr), heightCacheRes(0), heightCacheResZ(0),
		  heightCacheMinX(0), heightCacheMinZ(0), heightCacheCellSize(1.0f)
	{
		heightCacheBuilt.store(false);
	}

	// Move constructor: steal data, don't move cache
	MeshData(MeshData&& other) noexcept
		: vertices(std::move(other.vertices)), indices(std::move(other.indices)),
		  heightCache(nullptr), heightCacheRes(0), heightCacheResZ(0),
		  heightCacheMinX(0), heightCacheMinZ(0), heightCacheCellSize(1.0f)
	{
		heightCacheBuilt.store(false);
	}

	// Copy assignment
	MeshData& operator=(const MeshData& other)
	{
		if (this != &other) {
			vertices = other.vertices;
			indices = other.indices;
			InvalidateHeightCache();
		}
		return *this;
	}

	// Move assignment
	MeshData& operator=(MeshData&& other) noexcept
	{
		if (this != &other) {
			vertices = std::move(other.vertices);
			indices = std::move(other.indices);
			if (heightCache) { delete heightCache; heightCache = nullptr; }
			heightCacheBuilt.store(false);
			heightCacheRes = 0;
			heightCacheResZ = 0;
		}
		return *this;
	}

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
		if (count > 1000)
		{
			unsigned int numThreads = std::thread::hardware_concurrency();
			if (numThreads == 0) numThreads = 4;
			std::vector<std::thread> threads;
			int perThread = count / numThreads;
			for (unsigned int t = 0; t < numThreads; t++)
			{
				int start = t * perThread;
				int end = (t == numThreads - 1) ? count : (t + 1) * perThread;
				threads.emplace_back([this, &matrix, &normalMatrix, start, end]() {
					for (int i = start; i < end; i++)
					{
						int base = i * 14;
						glm::vec4 p = matrix * glm::vec4(vertices[base], vertices[base + 1], vertices[base + 2], 1.0f);
						vertices[base] = p.x; vertices[base + 1] = p.y; vertices[base + 2] = p.z;
						glm::vec3 n = glm::normalize(normalMatrix * glm::vec3(vertices[base + 5], vertices[base + 6], vertices[base + 7]));
						vertices[base + 5] = n.x; vertices[base + 6] = n.y; vertices[base + 7] = n.z;
						glm::vec3 t = glm::normalize(normalMatrix * glm::vec3(vertices[base + 8], vertices[base + 9], vertices[base + 10]));
						vertices[base + 8] = t.x; vertices[base + 9] = t.y; vertices[base + 10] = t.z;
						glm::vec3 b = glm::normalize(normalMatrix * glm::vec3(vertices[base + 11], vertices[base + 12], vertices[base + 13]));
						vertices[base + 11] = b.x; vertices[base + 12] = b.y; vertices[base + 13] = b.z;
					}
				});
			}
			for (auto& th : threads) th.join();
		}
		else
		{
			for (int i = 0; i < count; i++)
			{
				int base = i * 14;
				glm::vec4 p = matrix * glm::vec4(vertices[base], vertices[base + 1], vertices[base + 2], 1.0f);
				vertices[base] = p.x; vertices[base + 1] = p.y; vertices[base + 2] = p.z;
				glm::vec3 n = glm::normalize(normalMatrix * glm::vec3(vertices[base + 5], vertices[base + 6], vertices[base + 7]));
				vertices[base + 5] = n.x; vertices[base + 6] = n.y; vertices[base + 7] = n.z;
				glm::vec3 t = glm::normalize(normalMatrix * glm::vec3(vertices[base + 8], vertices[base + 9], vertices[base + 10]));
				vertices[base + 8] = t.x; vertices[base + 9] = t.y; vertices[base + 10] = t.z;
				glm::vec3 b = glm::normalize(normalMatrix * glm::vec3(vertices[base + 11], vertices[base + 12], vertices[base + 13]));
				vertices[base + 11] = b.x; vertices[base + 12] = b.y; vertices[base + 13] = b.z;
			}
		}
	}

	void Append(const MeshData& other)
	{
		if (other.vertices.empty()) return;
		size_t oldVertSize = vertices.size();
		unsigned int baseVertex = (unsigned int)(oldVertSize / 14);
		vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
		for (size_t i = 0; i < other.indices.size(); i++) indices.push_back(other.indices[i] + baseVertex);
	}

	glm::vec3 GetPosition(int vertIndex) const
	{
		int base = vertIndex * 14;
		return glm::vec3(vertices[base], vertices[base + 1], vertices[base + 2]);
	}

	glm::vec3 GetNormal(int vertIndex) const
	{
		int base = vertIndex * 14;
		return glm::vec3(vertices[base + 5], vertices[base + 6], vertices[base + 7]);
	}

	void GetBounds(glm::vec3& min, glm::vec3& max) const
	{
		if (vertices.empty()) { min = glm::vec3(0.0f); max = glm::vec3(0.0f); return; }
		int count = GetVertexCount();
		min = glm::vec3(1e10f); max = glm::vec3(-1e10f);
		for (int i = 0; i < count; i++) {
			glm::vec3 p = GetPosition(i);
			min = glm::min(min, p);
			max = glm::max(max, p);
		}
	}

	int GetVertexCount() const { return (int)vertices.size() / 14; }
	int GetTriangleCount() const { return (int)indices.size() / 3; }

	void GetTriangle(int triangleIndex, glm::vec3& v0, glm::vec3& v1, glm::vec3& v2) const
	{
		int baseIndex = triangleIndex * 3;
		v0 = GetPosition(indices[baseIndex]);
		v1 = GetPosition(indices[baseIndex + 1]);
		v2 = GetPosition(indices[baseIndex + 2]);
	}

	float GetHeightAt(float x, float z, float radius = 0.0f) const
	{
		BuildHeightCacheIfNeeded();
		if (!heightCache || heightCacheRes <= 0) return -1e10f;

		float gx = (x - heightCacheMinX) / heightCacheCellSize;
		float gz = (z - heightCacheMinZ) / heightCacheCellSize;

		if (radius <= 0.0f) {
			int ix = (int)gx, iz = (int)gz;
			if (ix < 0 || ix >= heightCacheRes || iz < 0 || iz >= heightCacheResZ) return -1e10f;
			return (*heightCache)[iz * heightCacheRes + ix];
		}

		// Radius search (find max height within radius)
		int searchRange = (int)std::ceil(radius / heightCacheCellSize);
		float radiusSq = radius * radius;
		float maxH = -1e10f;
		int ixCenter = (int)gx, izCenter = (int)gz;

		for (int dz = -searchRange; dz <= searchRange; dz++) {
			for (int dx = -searchRange; dx <= searchRange; dx++) {
				int ix = ixCenter + dx, iz = izCenter + dz;
				if (ix < 0 || ix >= heightCacheRes || iz < 0 || iz >= heightCacheResZ) continue;

				float dxDist = (ix - gx) * heightCacheCellSize;
				float dzDist = (iz - gz) * heightCacheCellSize;
				if (dxDist * dxDist + dzDist * dzDist <= radiusSq) {
					float h = (*heightCache)[iz * heightCacheRes + ix];
					if (h > maxH) maxH = h;
				}
			}
		}
		return maxH;
	}

	void Clear() { vertices.clear(); indices.clear(); InvalidateHeightCache(); }
	void DeepClear() { std::vector<GLfloat>().swap(vertices); std::vector<unsigned int>().swap(indices); InvalidateHeightCache(); }

	bool SaveToBinary(const std::string& path) const
	{
		FILE* file = nullptr; fopen_s(&file, path.c_str(), "wb");
		if (!file) return false;
		size_t vCount = vertices.size(); fwrite(&vCount, sizeof(size_t), 1, file);
		if (vCount > 0) fwrite(vertices.data(), sizeof(GLfloat), vCount, file);
		size_t iCount = indices.size(); fwrite(&iCount, sizeof(size_t), 1, file);
		if (iCount > 0) fwrite(indices.data(), sizeof(unsigned int), iCount, file);
		fclose(file); return true;
	}

	bool LoadFromBinary(const std::string& path)
	{
		FILE* file = nullptr; fopen_s(&file, path.c_str(), "rb");
		if (!file) return false;
		size_t vCount = 0; fread(&vCount, sizeof(size_t), 1, file);
		vertices.resize(vCount); if (vCount > 0) fread(vertices.data(), sizeof(GLfloat), vCount, file);
		size_t iCount = 0; fread(&iCount, sizeof(size_t), 1, file);
		indices.resize(iCount); if (iCount > 0) fread(indices.data(), sizeof(unsigned int), iCount, file);
		fclose(file); return true;
	}

private:
	mutable std::vector<float>* heightCache = nullptr;
	mutable int heightCacheRes = 0;
	mutable int heightCacheResZ = 0;
	mutable float heightCacheMinX = 0, heightCacheMinZ = 0, heightCacheCellSize = 1.0f;
	mutable std::atomic<bool> heightCacheBuilt{false};
	mutable std::mutex heightCacheMutex;

	void InvalidateHeightCache() const
	{
		std::lock_guard<std::mutex> lock(heightCacheMutex);
		if (heightCache) { delete heightCache; heightCache = nullptr; }
		heightCacheBuilt.store(false);
		heightCacheRes = 0; heightCacheResZ = 0;
	}

	void BuildHeightCacheIfNeeded() const
	{
		if (heightCacheBuilt.load(std::memory_order_acquire)) return;
		std::lock_guard<std::mutex> lock(heightCacheMutex);
		if (heightCacheBuilt.load(std::memory_order_relaxed)) return;
		int vertCount = GetVertexCount();
		int triCount = GetTriangleCount();
		if (vertCount == 0 || triCount == 0) { heightCacheBuilt.store(true, std::memory_order_release); return; }

		float minX = FLT_MAX, maxX = -FLT_MAX, minZ = FLT_MAX, maxZ = -FLT_MAX;
		for (int i = 0; i < vertCount; i++) {
			glm::vec3 p = GetPosition(i);
			if (p.x < minX) minX = p.x; if (p.x > maxX) maxX = p.x;
			if (p.z < minZ) minZ = p.z; if (p.z > maxZ) maxZ = p.z;
		}

		float rangeX = std::max(0.001f, maxX - minX), rangeZ = std::max(0.001f, maxZ - minZ);
		float maxRange = std::max(rangeX, rangeZ);
		int res = (int)glm::clamp(maxRange, 32.0f, 1024.0f);
		if (maxRange < 50.0f && triCount > 1000) {
			res = (triCount > 10000) ? 1024 : 512;
		}
		float cellSize = maxRange / (float)res;
		int resX = std::min(res, (int)std::ceil(rangeX / cellSize));
		int resZ = std::min(res, (int)std::ceil(rangeZ / cellSize));

		auto* cache = new std::vector<float>(resX * resZ, -1e10f);
		for (int t = 0; t < triCount; t++) {
			glm::vec3 v0 = GetPosition(indices[t * 3]), v1 = GetPosition(indices[t * 3 + 1]), v2 = GetPosition(indices[t * 3 + 2]);
			int gxMin = std::max(0, (int)((std::min({v0.x, v1.x, v2.x}) - minX) / cellSize));
			int gxMax = std::min(resX - 1, (int)((std::max({v0.x, v1.x, v2.x}) - minX) / cellSize));
			int gzMin = std::max(0, (int)((std::min({v0.z, v1.z, v2.z}) - minZ) / cellSize));
			int gzMax = std::min(resZ - 1, (int)((std::max({v0.z, v1.z, v2.z}) - minZ) / cellSize));

			glm::vec2 a2(v0.x, v0.z), b2(v1.x, v1.z), c2(v2.x, v2.z);
			float denom = (b2.y - c2.y) * (a2.x - c2.x) + (c2.x - b2.x) * (a2.y - c2.y);
			if (std::abs(denom) < 1e-10f) continue;
			float invDenom = 1.0f / denom;

			for (int gz = gzMin; gz <= gzMax; gz++) {
				for (int gx = gxMin; gx <= gxMax; gx++) {
					float px = minX + (gx + 0.5f) * cellSize, pz = minZ + (gz + 0.5f) * cellSize;
					float u = ((b2.y - c2.y) * (px - c2.x) + (c2.x - b2.x) * (pz - c2.y)) * invDenom;
					float v = ((c2.y - a2.y) * (px - c2.x) + (a2.x - c2.x) * (pz - c2.y)) * invDenom;
					float w = 1.0f - u - v;
					if (u >= -0.01f && v >= -0.01f && w >= -0.01f) {
						float h = u * v0.y + v * v1.y + w * v2.y;
						int idx = gz * resX + gx; if (h > (*cache)[idx]) (*cache)[idx] = h;
					}
				}
			}
		}

		heightCacheMinX = minX; heightCacheMinZ = minZ; heightCacheCellSize = cellSize;
		heightCacheRes = resX; heightCacheResZ = resZ; heightCache = cache;
		heightCacheBuilt.store(true, std::memory_order_release);
	}
};

struct PinData
{
	PinDataType type = PinDataType::None;
	MeshData meshData;
	TransformList transforms;
	std::vector<MeshData> instanceMeshes;
	std::string sourceObjectName = "(none)";
	GameObject* sourceObject = nullptr;
	Material* sourceMaterial = nullptr;
	Texture* sourceTexture = nullptr;
	Texture* sourceNormalMap = nullptr;
	std::vector<TextureLayer> textureLayers;
	float floatValue = 0.0f; int intValue = 0;
	glm::vec3 vec3Value = glm::vec3(0.0f); glm::vec2 vec2Value = glm::vec2(0.0f);
	bool boolValue = false;

	void Clear() { type = PinDataType::None; meshData.Clear(); transforms.clear(); instanceMeshes.clear(); sourceObjectName = "(none)"; sourceObject = nullptr; sourceMaterial = nullptr; sourceTexture = nullptr; sourceNormalMap = nullptr; textureLayers.clear(); floatValue = 0.0f; int intValue = 0; vec3Value = glm::vec3(0.0f); vec2Value = glm::vec2(0.0f); boolValue = false; }
	void DeepClear() { type = PinDataType::None; meshData.DeepClear(); std::vector<TransformData>().swap(transforms); for (auto& m : instanceMeshes) m.DeepClear(); std::vector<MeshData>().swap(instanceMeshes); sourceObjectName = "(none)"; sourceObject = nullptr; sourceMaterial = nullptr; sourceTexture = nullptr; sourceNormalMap = nullptr; std::vector<TextureLayer>().swap(textureLayers); floatValue = 0.0f; int intValue = 0; vec3Value = glm::vec3(0.0f); vec2Value = glm::vec2(0.0f); boolValue = false; }
};
