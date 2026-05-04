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
#include "Mesh.h"

class Material;
class Texture;
class GameObject;
#include "TextureLayer.h"

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
	MeshData() = default;

	// Destructor: free height cache
	~MeshData() { if (heightCache) { delete heightCache; heightCache = nullptr; } }

	// Copy constructor: copy data, don't copy cache (it's lazy)
	MeshData(const MeshData& other)
		: vertices(other.vertices), indices(other.indices) {}

	// Move constructor: steal data, don't move cache
	MeshData(MeshData&& other) noexcept
		: vertices(std::move(other.vertices)), indices(std::move(other.indices)) {}

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
			// Direct cleanup (no mutex — no concurrent access during move)
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

		// Multithread for large meshes (>1000 vertices)
		if (count > 1000)
		{
			unsigned int numThreads = std::thread::hardware_concurrency();
			if (numThreads == 0) numThreads = 4;
			if (numThreads > (unsigned int)count) numThreads = (unsigned int)count;

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
						vertices[base] = p.x;
						vertices[base + 1] = p.y;
						vertices[base + 2] = p.z;

						glm::vec3 n = glm::normalize(normalMatrix * glm::vec3(vertices[base + 5], vertices[base + 6], vertices[base + 7]));
						vertices[base + 5] = n.x;
						vertices[base + 6] = n.y;
						vertices[base + 7] = n.z;

						glm::vec3 t = glm::normalize(normalMatrix * glm::vec3(vertices[base + 8], vertices[base + 9], vertices[base + 10]));
						vertices[base + 8] = t.x;
						vertices[base + 9] = t.y;
						vertices[base + 10] = t.z;

						glm::vec3 b = glm::normalize(normalMatrix * glm::vec3(vertices[base + 11], vertices[base + 12], vertices[base + 13]));
						vertices[base + 11] = b.x;
						vertices[base + 12] = b.y;
						vertices[base + 13] = b.z;
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
				vertices[base] = p.x;
				vertices[base + 1] = p.y;
				vertices[base + 2] = p.z;

				glm::vec3 n = glm::normalize(normalMatrix * glm::vec3(vertices[base + 5], vertices[base + 6], vertices[base + 7]));
				vertices[base + 5] = n.x;
				vertices[base + 6] = n.y;
				vertices[base + 7] = n.z;

				glm::vec3 t = glm::normalize(normalMatrix * glm::vec3(vertices[base + 8], vertices[base + 9], vertices[base + 10]));
				vertices[base + 8] = t.x;
				vertices[base + 9] = t.y;
				vertices[base + 10] = t.z;

				glm::vec3 b = glm::normalize(normalMatrix * glm::vec3(vertices[base + 11], vertices[base + 12], vertices[base + 13]));
				vertices[base + 11] = b.x;
				vertices[base + 12] = b.y;
				vertices[base + 13] = b.z;
			}
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
		int count = GetVertexCount();

		// Multithread for large meshes (>2000 vertices)
		if (count > 2000)
		{
			unsigned int numThreads = std::thread::hardware_concurrency();
			if (numThreads == 0) numThreads = 4;
			if (numThreads > (unsigned int)count) numThreads = (unsigned int)count;

			std::vector<glm::vec3> threadMins(numThreads, glm::vec3(1e10f));
			std::vector<glm::vec3> threadMaxs(numThreads, glm::vec3(-1e10f));
			std::vector<std::thread> threads;
			int perThread = count / numThreads;

			for (unsigned int t = 0; t < numThreads; t++)
			{
				int start = t * perThread;
				int end = (t == numThreads - 1) ? count : (t + 1) * perThread;

				threads.emplace_back([this, &threadMins, &threadMaxs, t, start, end]() {
					glm::vec3 localMin(1e10f), localMax(-1e10f);
					for (int i = start; i < end; i++) {
						glm::vec3 p = GetPosition(i);
						localMin = glm::min(localMin, p);
						localMax = glm::max(localMax, p);
					}
					threadMins[t] = localMin;
					threadMaxs[t] = localMax;
				});
			}
			for (auto& th : threads) th.join();

			min = glm::vec3(1e10f);
			max = glm::vec3(-1e10f);
			for (unsigned int t = 0; t < numThreads; t++) {
				min = glm::min(min, threadMins[t]);
				max = glm::max(max, threadMaxs[t]);
			}
		}
		else
		{
			min = glm::vec3(1e10f);
			max = glm::vec3(-1e10f);
			for (int i = 0; i < count; i++) {
				glm::vec3 p = GetPosition(i);
				min = glm::min(min, p);
				max = glm::max(max, p);
			}
		}
	}

	int GetVertexCount() const { return (int)vertices.size() / 14; }
	int GetTriangleCount() const { return (int)indices.size() / 3; }

	// =====================================================================
	// Fast Height Lookup — Lazy 2D Heightfield Cache
	// Rasterizes all triangles into a grid on first call, O(1) lookups after.
	// Returns -1e10f if the query point is outside the mesh footprint.
	// Thread-safe: multiple threads can call GetHeightAt concurrently.
	// =====================================================================
	float GetHeightAt(float x, float z) const
	{
		BuildHeightCacheIfNeeded();
		if (!heightCache || heightCacheRes <= 0) return -1e10f;

		// Convert world XZ to grid cell
		float gx = (x - heightCacheMinX) / heightCacheCellSize;
		float gz = (z - heightCacheMinZ) / heightCacheCellSize;

		int ix = (int)gx;
		int iz = (int)gz;

		if (ix < 0 || ix >= heightCacheRes || iz < 0 || iz >= heightCacheResZ)
			return -1e10f;

		return (*heightCache)[iz * heightCacheRes + ix];
	}

	void Clear()
	{
		vertices.clear();
		indices.clear();
		InvalidateHeightCache();
	}

	// Truly free RAM by swapping with empty vectors (standard C++ swap trick)
	void DeepClear()
	{
		std::vector<GLfloat>().swap(vertices);
		std::vector<unsigned int>().swap(indices);
		InvalidateHeightCache();
	}

	// =====================================================================
	// Fast Binary Serialization for Baking Procedural Geometry
	// =====================================================================
	bool SaveToBinary(const std::string& path) const
	{
		FILE* file = nullptr;
		fopen_s(&file, path.c_str(), "wb");
		if (!file) return false;

		size_t vCount = vertices.size();
		fwrite(&vCount, sizeof(size_t), 1, file);
		if (vCount > 0) fwrite(vertices.data(), sizeof(GLfloat), vCount, file);

		size_t iCount = indices.size();
		fwrite(&iCount, sizeof(size_t), 1, file);
		if (iCount > 0) fwrite(indices.data(), sizeof(unsigned int), iCount, file);

		fclose(file);
		return true;
	}

	bool LoadFromBinary(const std::string& path)
	{
		FILE* file = nullptr;
		fopen_s(&file, path.c_str(), "rb");
		if (!file) return false;

		size_t vCount = 0;
		if (fread(&vCount, sizeof(size_t), 1, file) != 1) { fclose(file); return false; }
		vertices.resize(vCount);
		if (vCount > 0) fread(vertices.data(), sizeof(GLfloat), vCount, file);

		size_t iCount = 0;
		if (fread(&iCount, sizeof(size_t), 1, file) != 1) { fclose(file); return false; }
		indices.resize(iCount);
		if (iCount > 0) fread(indices.data(), sizeof(unsigned int), iCount, file);

		fclose(file);
		return true;
	}

private:
	// --- Height Cache Internals ---
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
		heightCacheRes = 0;
		heightCacheResZ = 0;
	}

	void BuildHeightCacheIfNeeded() const
	{
		if (heightCacheBuilt.load(std::memory_order_acquire)) return;
		std::lock_guard<std::mutex> lock(heightCacheMutex);
		if (heightCacheBuilt.load(std::memory_order_relaxed)) return; // Double-check

		int vertCount = GetVertexCount();
		int triCount = GetTriangleCount();
		if (vertCount == 0 || triCount == 0) {
			heightCacheBuilt.store(true, std::memory_order_release);
			return;
		}

		// Find XZ bounds
		float minX = FLT_MAX, maxX = -FLT_MAX;
		float minZ = FLT_MAX, maxZ = -FLT_MAX;
		for (int i = 0; i < vertCount; i++) {
			glm::vec3 p = GetPosition(i);
			if (p.x < minX) minX = p.x;
			if (p.x > maxX) maxX = p.x;
			if (p.z < minZ) minZ = p.z;
			if (p.z > maxZ) maxZ = p.z;
		}

		float rangeX = maxX - minX;
		float rangeZ = maxZ - minZ;
		if (rangeX < 0.001f) rangeX = 0.001f;
		if (rangeZ < 0.001f) rangeZ = 0.001f;

		// Use separate X/Z resolutions for non-square meshes
		float maxRange = std::max(rangeX, rangeZ);
		int res = (int)maxRange;
		if (res < 32) res = 32;
		if (res > 1024) res = 1024;

		// Cell size based on the larger dimension, so cells are square
		float cellSize = maxRange / (float)res;

		// Grid dimensions in cells
		int resX = std::max(1, (int)std::ceil(rangeX / cellSize));
		int resZ = std::max(1, (int)std::ceil(rangeZ / cellSize));
		if (resX > res) resX = res;
		if (resZ > res) resZ = res;

		// Use resX for row stride (the grid is resX * resZ)
		auto* cache = new std::vector<float>(resX * resZ, -1e10f);

		// Rasterize every triangle into the grid
		for (int t = 0; t < triCount; t++) {
			unsigned int i0 = indices[t * 3];
			unsigned int i1 = indices[t * 3 + 1];
			unsigned int i2 = indices[t * 3 + 2];

			glm::vec3 v0 = GetPosition(i0);
			glm::vec3 v1 = GetPosition(i1);
			glm::vec3 v2 = GetPosition(i2);

			// Triangle AABB in grid space
			float tMinX = std::min({v0.x, v1.x, v2.x});
			float tMaxX = std::max({v0.x, v1.x, v2.x});
			float tMinZ = std::min({v0.z, v1.z, v2.z});
			float tMaxZ = std::max({v0.z, v1.z, v2.z});

			int gxMin = std::max(0, (int)((tMinX - minX) / cellSize));
			int gxMax = std::min(resX - 1, (int)((tMaxX - minX) / cellSize));
			int gzMin = std::max(0, (int)((tMinZ - minZ) / cellSize));
			int gzMax = std::min(resZ - 1, (int)((tMaxZ - minZ) / cellSize));

			// Precompute barycentric denominator
			glm::vec2 a2(v0.x, v0.z), b2(v1.x, v1.z), c2(v2.x, v2.z);
			float denom = (b2.y - c2.y) * (a2.x - c2.x) + (c2.x - b2.x) * (a2.y - c2.y);
			if (std::abs(denom) < 1e-10f) continue; // Degenerate triangle
			float invDenom = 1.0f / denom;

			for (int gz = gzMin; gz <= gzMax; gz++) {
				for (int gx = gxMin; gx <= gxMax; gx++) {
					float px = minX + (gx + 0.5f) * cellSize;
					float pz = minZ + (gz + 0.5f) * cellSize;

					// Barycentric test (with small epsilon for edge coverage)
					float u = ((b2.y - c2.y) * (px - c2.x) + (c2.x - b2.x) * (pz - c2.y)) * invDenom;
					float v = ((c2.y - a2.y) * (px - c2.x) + (a2.x - c2.x) * (pz - c2.y)) * invDenom;
					float w = 1.0f - u - v;

					if (u >= -0.05f && v >= -0.05f && w >= -0.05f) {
						float h = u * v0.y + v * v1.y + w * v2.y;
						int idx = gz * resX + gx;
						if (h > (*cache)[idx]) (*cache)[idx] = h;
					}
				}
			}
		}

		// Flood-fill pass: expand filled cells into empty neighbors
		// This handles sparse meshes (procedural lakes) where gaps exist between triangles
		for (int pass = 0; pass < 3; pass++) {
			std::vector<float> prev = *cache;
			for (int gz = 0; gz < resZ; gz++) {
				for (int gx = 0; gx < resX; gx++) {
					int idx = gz * resX + gx;
					if (prev[idx] > -1e9f) continue; // Already filled

					// Check 4-connected neighbors for a filled cell
					float bestH = -1e10f;
					if (gx > 0 && prev[idx - 1] > -1e9f) bestH = std::max(bestH, prev[idx - 1]);
					if (gx < resX - 1 && prev[idx + 1] > -1e9f) bestH = std::max(bestH, prev[idx + 1]);
					if (gz > 0 && prev[idx - resX] > -1e9f) bestH = std::max(bestH, prev[idx - resX]);
					if (gz < resZ - 1 && prev[idx + resX] > -1e9f) bestH = std::max(bestH, prev[idx + resX]);

					if (bestH > -1e9f) (*cache)[idx] = bestH;
				}
			}
		}

		heightCacheMinX = minX;
		heightCacheMinZ = minZ;
		heightCacheCellSize = cellSize;
		heightCacheRes = resX; // Store resX as the row stride
		heightCacheResZ = resZ;
		heightCache = cache;

		heightCacheBuilt.store(true, std::memory_order_release);
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
	GameObject* sourceObject = nullptr;
	Material* sourceMaterial = nullptr;
	Texture* sourceTexture = nullptr;
	Texture* sourceNormalMap = nullptr;
	std::vector<TextureLayer> textureLayers;

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
		sourceObject = nullptr;
		sourceMaterial = nullptr;
		sourceTexture = nullptr;
		sourceNormalMap = nullptr;
		textureLayers.clear();
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
		sourceObject = nullptr;
		sourceMaterial = nullptr;
		sourceTexture = nullptr;
		sourceNormalMap = nullptr;
		std::vector<TextureLayer>().swap(textureLayers);
		floatValue = 0.0f;
		intValue = 0;
		vec3Value = glm::vec3(0.0f);
		vec2Value = glm::vec2(0.0f);
		boolValue = false;
	}
};
