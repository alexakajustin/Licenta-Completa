#pragma once

// Prevent Windows min/max macros from conflicting with std::min/std::max
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Nodes/Operation.h"
#include "Nodes/OperationRegistry.h"
#include <map>
#include <set>
#include <cmath>
#include <algorithm>

// =====================================================================
//  MeshOp_Subdivide — Split each triangle into 4
// =====================================================================

class MeshOp_Subdivide : public Operation
{
public:
	std::string GetName() const override { return "Subdivide"; }
	std::string GetCategory() const override { return "Mesh"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Int("Iterations", 1, 1, 4)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty() || ctx.mesh.indices.empty()) return;

		int iterations = GetInt("Iterations");

		for (int iter = 0; iter < iterations; iter++)
		{
			MeshData newMesh;
			// Copy existing vertices
			newMesh.vertices = ctx.mesh.vertices;

			// Edge midpoint cache: (minIdx, maxIdx) -> midpoint vertex index
			std::map<std::pair<unsigned int, unsigned int>, unsigned int> edgeCache;

			int triCount = ctx.mesh.GetTriangleCount();
			for (int t = 0; t < triCount; t++)
			{
				unsigned int i0 = ctx.mesh.indices[t * 3];
				unsigned int i1 = ctx.mesh.indices[t * 3 + 1];
				unsigned int i2 = ctx.mesh.indices[t * 3 + 2];

				// Get or create midpoints for each edge
				unsigned int m01 = GetOrCreateMidpoint(ctx.mesh, newMesh, edgeCache, i0, i1);
				unsigned int m12 = GetOrCreateMidpoint(ctx.mesh, newMesh, edgeCache, i1, i2);
				unsigned int m20 = GetOrCreateMidpoint(ctx.mesh, newMesh, edgeCache, i2, i0);

				// Original triangle becomes 4 smaller triangles
				newMesh.AddTriangle(i0, m01, m20);
				newMesh.AddTriangle(m01, i1, m12);
				newMesh.AddTriangle(m20, m12, i2);
				newMesh.AddTriangle(m01, m12, m20);
			}

			ctx.mesh = newMesh;
		}
	}

private:
	unsigned int GetOrCreateMidpoint(
		const MeshData& srcMesh, MeshData& newMesh,
		std::map<std::pair<unsigned int, unsigned int>, unsigned int>& edgeCache,
		unsigned int a, unsigned int b)
	{
		unsigned int lo = a < b ? a : b;
		unsigned int hi = a < b ? b : a;
		auto key = std::make_pair(lo, hi);
		auto it = edgeCache.find(key);
		if (it != edgeCache.end()) return it->second;

		unsigned int newIdx = (unsigned int)(newMesh.vertices.size() / 14);

		// Interpolate all 14 components
		int baseA = a * 14, baseB = b * 14;
		for (int c = 0; c < 14; c++)
		{
			newMesh.vertices.push_back(
				(srcMesh.vertices[baseA + c] + srcMesh.vertices[baseB + c]) * 0.5f
			);
		}

		// Renormalize normal, tangent, bitangent
		int newBase = newIdx * 14;
		int offsets[] = { 5, 8, 11 };
		for (int k = 0; k < 3; k++)
		{
			int off = offsets[k];
			float x = newMesh.vertices[newBase + off];
			float y = newMesh.vertices[newBase + off + 1];
			float z = newMesh.vertices[newBase + off + 2];
			float len = sqrt(x*x + y*y + z*z);
			if (len > 0.0001f)
			{
				newMesh.vertices[newBase + off]     /= len;
				newMesh.vertices[newBase + off + 1] /= len;
				newMesh.vertices[newBase + off + 2] /= len;
			}
		}

		edgeCache[key] = newIdx;
		return newIdx;
	}
};

REGISTER_OPERATION(MeshOp_Subdivide, "Subdivide", "Mesh")


// =====================================================================
//  MeshOp_Smooth — Laplacian smoothing
// =====================================================================

class MeshOp_Smooth : public Operation
{
public:
	std::string GetName() const override { return "Smooth"; }
	std::string GetCategory() const override { return "Mesh"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Int("Iterations", 1, 1, 20),
			ParamDef::Float("Strength", 0.5f, 0.0f, 1.0f)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty() || ctx.mesh.indices.empty()) return;

		int iterations = GetInt("Iterations");
		float strength = GetFloat("Strength");

		int vertCount = ctx.mesh.GetVertexCount();

		// Build adjacency: for each vertex, which other vertices is it connected to?
		std::vector<std::set<int>> neighbors(vertCount);
		int triCount = ctx.mesh.GetTriangleCount();
		for (int t = 0; t < triCount; t++)
		{
			int i0 = ctx.mesh.indices[t * 3];
			int i1 = ctx.mesh.indices[t * 3 + 1];
			int i2 = ctx.mesh.indices[t * 3 + 2];
			neighbors[i0].insert(i1); neighbors[i0].insert(i2);
			neighbors[i1].insert(i0); neighbors[i1].insert(i2);
			neighbors[i2].insert(i0); neighbors[i2].insert(i1);
		}

		for (int iter = 0; iter < iterations; iter++)
		{
			std::vector<glm::vec3> newPositions(vertCount);

			for (int i = 0; i < vertCount; i++)
			{
				if (!ctx.IsVertexSelected(i) || neighbors[i].empty())
				{
					newPositions[i] = ctx.mesh.GetPosition(i);
					continue;
				}

				// Compute average of neighbors
				glm::vec3 avg(0.0f);
				for (int n : neighbors[i])
					avg += ctx.mesh.GetPosition(n);
				avg /= (float)neighbors[i].size();

				glm::vec3 current = ctx.mesh.GetPosition(i);
				newPositions[i] = glm::mix(current, avg, strength);
			}

			// Write back
			for (int i = 0; i < vertCount; i++)
			{
				int base = i * 14;
				ctx.mesh.vertices[base]     = newPositions[i].x;
				ctx.mesh.vertices[base + 1] = newPositions[i].y;
				ctx.mesh.vertices[base + 2] = newPositions[i].z;
			}
		}
	}
};

REGISTER_OPERATION(MeshOp_Smooth, "Smooth", "Mesh")


// =====================================================================
//  MeshOp_Extrude — Extrude faces along normals
// =====================================================================

class MeshOp_Extrude : public Operation
{
public:
	std::string GetName() const override { return "Extrude"; }
	std::string GetCategory() const override { return "Mesh"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Float("Distance", 0.5f, -10.0f, 10.0f)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty()) return;

		float distance = GetFloat("Distance");
		int originalVertCount = ctx.mesh.GetVertexCount();

		// Duplicate all vertices, offset along their normals
		for (int i = 0; i < originalVertCount; i++)
		{
			int base = i * 14;
			float px = ctx.mesh.vertices[base] + ctx.mesh.vertices[base + 5] * distance;
			float py = ctx.mesh.vertices[base + 1] + ctx.mesh.vertices[base + 6] * distance;
			float pz = ctx.mesh.vertices[base + 2] + ctx.mesh.vertices[base + 7] * distance;

			ctx.mesh.AddVertex(px, py, pz,
				ctx.mesh.vertices[base + 3], ctx.mesh.vertices[base + 4],    // UV
				ctx.mesh.vertices[base + 5], ctx.mesh.vertices[base + 6], ctx.mesh.vertices[base + 7],    // Normal
				ctx.mesh.vertices[base + 8], ctx.mesh.vertices[base + 9], ctx.mesh.vertices[base + 10],   // Tangent
				ctx.mesh.vertices[base + 11], ctx.mesh.vertices[base + 12], ctx.mesh.vertices[base + 13]  // Bitangent
			);
		}

		// Create side faces connecting original and extruded edges
		int triCount = (int)ctx.mesh.indices.size() / 3;
		// We'll track boundary edges by counting how many times each edge appears
		std::map<std::pair<int, int>, int> edgeCounts;
		for (int t = 0; t < triCount; t++)
		{
			unsigned int i0 = ctx.mesh.indices[t * 3];
			unsigned int i1 = ctx.mesh.indices[t * 3 + 1];
			unsigned int i2 = ctx.mesh.indices[t * 3 + 2];
			
			AddEdgeCount(edgeCounts, i0, i1);
			AddEdgeCount(edgeCounts, i1, i2);
			AddEdgeCount(edgeCounts, i2, i0);
		}

		// Boundary edges appear exactly once — create side quads
		for (auto& pair : edgeCounts)
		{
			if (pair.second == 1)
			{
				int a = pair.first.first;
				int b = pair.first.second;
				int a2 = a + originalVertCount;
				int b2 = b + originalVertCount;
				ctx.mesh.AddTriangle(a, b, a2);
				ctx.mesh.AddTriangle(b, b2, a2);
			}
		}

		// Re-index extruded face triangles
		for (int t = 0; t < triCount; t++)
		{
			unsigned int i0 = ctx.mesh.indices[t * 3] + originalVertCount;
			unsigned int i1 = ctx.mesh.indices[t * 3 + 1] + originalVertCount;
			unsigned int i2 = ctx.mesh.indices[t * 3 + 2] + originalVertCount;
			ctx.mesh.AddTriangle(i0, i2, i1); // Reversed winding for top face
		}
	}

private:
	static void AddEdgeCount(std::map<std::pair<int, int>, int>& counts, int a, int b)
	{
		int lo = a < b ? a : b;
		int hi = a < b ? b : a;
		counts[std::make_pair(lo, hi)]++;
	}
};

REGISTER_OPERATION(MeshOp_Extrude, "Extrude", "Mesh")


// =====================================================================
//  MeshOp_Mirror — Mirror mesh across a plane
// =====================================================================

class MeshOp_Mirror : public Operation
{
public:
	std::string GetName() const override { return "Mirror"; }
	std::string GetCategory() const override { return "Mesh"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Enum("Axis", {"X", "Y", "Z"}, 0),
			ParamDef::Bool("Merge Original", true)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty()) return;

		int axis = GetEnum("Axis");
		bool merge = GetBool("Merge Original");

		MeshData mirrored;
		int vertCount = ctx.mesh.GetVertexCount();

		// Copy and mirror vertices
		for (int i = 0; i < vertCount; i++)
		{
			int base = i * 14;
			float v[14];
			for (int c = 0; c < 14; c++) v[c] = ctx.mesh.vertices[base + c];

			// Mirror position and normal along the chosen axis
			if (axis == 0)      { v[0] = -v[0]; v[5] = -v[5]; v[8] = -v[8]; v[11] = -v[11]; }
			else if (axis == 1) { v[1] = -v[1]; v[6] = -v[6]; v[9] = -v[9]; v[12] = -v[12]; }
			else                { v[2] = -v[2]; v[7] = -v[7]; v[10] = -v[10]; v[13] = -v[13]; }

			mirrored.AddVertex(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12], v[13]);
		}

		// Copy and reverse winding of indices
		int triCount = ctx.mesh.GetTriangleCount();
		for (int t = 0; t < triCount; t++)
		{
			unsigned int i0 = ctx.mesh.indices[t * 3];
			unsigned int i1 = ctx.mesh.indices[t * 3 + 1];
			unsigned int i2 = ctx.mesh.indices[t * 3 + 2];
			mirrored.AddTriangle(i0, i2, i1); // Reversed winding
		}

		if (merge)
			ctx.mesh.Append(mirrored);
		else
			ctx.mesh = mirrored;
	}
};

REGISTER_OPERATION(MeshOp_Mirror, "Mirror", "Mesh")


// =====================================================================
//  MeshOp_FlipNormals — Reverse all face normals
// =====================================================================

class MeshOp_FlipNormals : public Operation
{
public:
	std::string GetName() const override { return "Flip Normals"; }
	std::string GetCategory() const override { return "Mesh"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {}; // No parameters
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty()) return;

		// Flip normals
		int count = ctx.mesh.GetVertexCount();
		for (int i = 0; i < count; i++)
		{
			int base = i * 14;
			ctx.mesh.vertices[base + 5] = -ctx.mesh.vertices[base + 5];
			ctx.mesh.vertices[base + 6] = -ctx.mesh.vertices[base + 6];
			ctx.mesh.vertices[base + 7] = -ctx.mesh.vertices[base + 7];
		}

		// Reverse winding of all triangles
		int triCount = ctx.mesh.GetTriangleCount();
		for (int t = 0; t < triCount; t++)
		{
			std::swap(ctx.mesh.indices[t * 3 + 1], ctx.mesh.indices[t * 3 + 2]);
		}
	}
};

REGISTER_OPERATION(MeshOp_FlipNormals, "Flip Normals", "Mesh")


// =====================================================================
//  MeshOp_Decimate — Reduce polygon count (simple vertex merging)
// =====================================================================

class MeshOp_Decimate : public Operation
{
public:
	std::string GetName() const override { return "Decimate"; }
	std::string GetCategory() const override { return "Mesh"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Float("Ratio", 0.5f, 0.01f, 1.0f)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty()) return;

		float ratio = GetFloat("Ratio");
		if (ratio >= 0.99f) return; // Nothing to do

		int vertCount = ctx.mesh.GetVertexCount();
		int targetCount = (vertCount * ratio > 3.0f) ? (int)(vertCount * ratio) : 3;

		if (targetCount >= vertCount) return;

		// Simple grid-based decimation: merge vertices within cells
		glm::vec3 minB, maxB;
		ctx.mesh.GetBounds(minB, maxB);
		glm::vec3 size = maxB - minB;

		// Determine grid resolution from target count
		float cellSize = pow(size.x * size.y * size.z / (float)targetCount, 1.0f / 3.0f);
		if (cellSize < 0.0001f) cellSize = 0.01f;

		// Map vertices to grid cells
		struct Cell {
			glm::vec3 posSum = glm::vec3(0.0f);
			glm::vec3 normalSum = glm::vec3(0.0f);
			float uvSumU = 0, uvSumV = 0;
			int count = 0;
			int newIndex = -1;
		};

		std::map<int64_t, Cell> cells;
		std::vector<int> vertexToCell(vertCount);

		for (int i = 0; i < vertCount; i++)
		{
			glm::vec3 p = ctx.mesh.GetPosition(i);
			int cx = (int)((p.x - minB.x) / cellSize);
			int cy = (int)((p.y - minB.y) / cellSize);
			int cz = (int)((p.z - minB.z) / cellSize);
			int64_t key = ((int64_t)cx * 10007 + (int64_t)cy) * 10007 + (int64_t)cz;

			Cell& cell = cells[key];
			cell.posSum += p;
			cell.normalSum += ctx.mesh.GetNormal(i);
			int base = i * 14;
			cell.uvSumU += ctx.mesh.vertices[base + 3];
			cell.uvSumV += ctx.mesh.vertices[base + 4];
			cell.count++;
			vertexToCell[i] = (int)key; // Store for remapping
		}

		// Build new mesh
		MeshData newMesh;
		int newIdx = 0;
		for (auto& pair : cells)
		{
			Cell& cell = pair.second;
			glm::vec3 avgPos = cell.posSum / (float)cell.count;
			glm::vec3 avgNorm = glm::length(cell.normalSum) > 0.0001f
				? glm::normalize(cell.normalSum)
				: glm::vec3(0, 1, 0);
			float avgU = cell.uvSumU / cell.count;
			float avgV = cell.uvSumV / cell.count;

			newMesh.AddVertex(avgPos.x, avgPos.y, avgPos.z,
				avgU, avgV, avgNorm.x, avgNorm.y, avgNorm.z,
				0, 0, 0, 0, 0, 0);
			cell.newIndex = newIdx++;
		}

		// Remap indices
		int triCount = ctx.mesh.GetTriangleCount();
		for (int t = 0; t < triCount; t++)
		{
			unsigned int i0 = ctx.mesh.indices[t * 3];
			unsigned int i1 = ctx.mesh.indices[t * 3 + 1];
			unsigned int i2 = ctx.mesh.indices[t * 3 + 2];

			int64_t key0 = vertexToCell[i0];
			int64_t key1 = vertexToCell[i1];
			int64_t key2 = vertexToCell[i2];

			int n0 = cells[key0].newIndex;
			int n1 = cells[key1].newIndex;
			int n2 = cells[key2].newIndex;

			// Skip degenerate triangles
			if (n0 != n1 && n1 != n2 && n0 != n2)
				newMesh.AddTriangle(n0, n1, n2);
		}

		ctx.mesh = newMesh;
	}
};

REGISTER_OPERATION(MeshOp_Decimate, "Decimate", "Mesh")
