#pragma once

#include "Rendering/MeshData.h"
#include <map>
#include <cmath>
#include <algorithm>

// =====================================================================
// MeshSimplifier — Automatic LOD Mesh Generation via Vertex Clustering
//
// Reduces polygon count by partitioning the mesh's bounding box into a
// 3D spatial grid, merging all vertices within each cell into a single
// representative vertex (averaged attributes), then rebuilding the
// index buffer while discarding degenerate triangles.
//
// Usage:
//   MeshData lod1 = MeshSimplifier::Simplify(originalMesh, 0.5f);  // ~50% reduction
//   MeshData lod2 = MeshSimplifier::Simplify(originalMesh, 0.25f); // ~75% reduction
// =====================================================================
class MeshSimplifier
{
public:
	// Simplify a MeshData to approximately targetRatio of its original vertex count.
	// targetRatio: 0.5 = keep ~50% of detail, 0.25 = keep ~25%, etc.
	// Returns a new MeshData with reduced geometry. Returns empty MeshData on failure.
	static MeshData Simplify(const MeshData& input, float targetRatio)
	{
		MeshData result;

		int vertCount = input.GetVertexCount();
		int triCount = input.GetTriangleCount();
		if (vertCount < 4 || triCount < 2) return result; // Too few to simplify

		targetRatio = std::max(0.05f, std::min(targetRatio, 1.0f));

		// 1. Compute bounding box
		glm::vec3 minB(1e10f), maxB(-1e10f);
		for (int i = 0; i < vertCount; i++) {
			glm::vec3 p = input.GetPosition(i);
			minB = glm::min(minB, p);
			maxB = glm::max(maxB, p);
		}

		glm::vec3 size = maxB - minB;

		// Prevent degenerate dimensions (flat meshes like grass planes)
		for (int i = 0; i < 3; i++) {
			if (size[i] < 0.0001f) size[i] = 0.0001f;
		}

		// 2. Compute grid resolution
		// We want roughly targetRatio * vertCount unique cells
		// Grid is 3D, so resolution per axis = cbrt(targetVerts)
		int targetVerts = std::max(4, (int)(vertCount * targetRatio));
		int gridRes = std::max(2, (int)std::cbrt((double)targetVerts));

		glm::vec3 cellSize = size / (float)gridRes;
		for (int i = 0; i < 3; i++) {
			if (cellSize[i] < 0.00001f) cellSize[i] = 0.00001f;
		}

		// 3. Accumulate vertex attributes per grid cell
		static const int STRIDE = 14; // pos(3) + uv(2) + normal(3) + tangent(3) + bitangent(3)

		struct CellAccum {
			glm::vec3 posSum = glm::vec3(0.0f);
			glm::vec2 uvSum = glm::vec2(0.0f);
			glm::vec3 normalSum = glm::vec3(0.0f);
			glm::vec3 tangentSum = glm::vec3(0.0f);
			glm::vec3 bitangentSum = glm::vec3(0.0f);
			int count = 0;
			int newIndex = -1;
		};

		// Use int64 key for grid cell (allows large grids without collision)
		std::map<int64_t, CellAccum> cells;
		std::vector<int64_t> vertexCellKey(vertCount);

		for (int v = 0; v < vertCount; v++) {
			int base = v * STRIDE;
			const float* d = &input.vertices[base];

			glm::vec3 pos(d[0], d[1], d[2]);
			glm::vec2 uv(d[3], d[4]);
			glm::vec3 normal(d[5], d[6], d[7]);
			glm::vec3 tangent(d[8], d[9], d[10]);
			glm::vec3 bitangent(d[11], d[12], d[13]);

			int cx = std::clamp((int)((pos.x - minB.x) / cellSize.x), 0, gridRes - 1);
			int cy = std::clamp((int)((pos.y - minB.y) / cellSize.y), 0, gridRes - 1);
			int cz = std::clamp((int)((pos.z - minB.z) / cellSize.z), 0, gridRes - 1);

			int64_t key = (int64_t)cx * gridRes * gridRes + (int64_t)cy * gridRes + cz;
			vertexCellKey[v] = key;

			auto& cell = cells[key];
			cell.posSum += pos;
			cell.uvSum += uv;
			cell.normalSum += normal;
			cell.tangentSum += tangent;
			cell.bitangentSum += bitangent;
			cell.count++;
		}

		// 4. Build new vertex array from cell averages
		int newVertIdx = 0;
		for (auto& [key, cell] : cells) {
			float inv = 1.0f / (float)cell.count;
			glm::vec3 avgPos = cell.posSum * inv;
			glm::vec2 avgUV = cell.uvSum * inv;

			glm::vec3 avgNormal = glm::length(cell.normalSum) > 0.001f
				? glm::normalize(cell.normalSum) : glm::vec3(0.0f, 1.0f, 0.0f);
			glm::vec3 avgTangent = glm::length(cell.tangentSum) > 0.001f
				? glm::normalize(cell.tangentSum) : glm::vec3(1.0f, 0.0f, 0.0f);
			glm::vec3 avgBitangent = glm::length(cell.bitangentSum) > 0.001f
				? glm::normalize(cell.bitangentSum) : glm::vec3(0.0f, 0.0f, 1.0f);

			result.AddVertex(
				avgPos.x, avgPos.y, avgPos.z,
				avgUV.x, avgUV.y,
				avgNormal.x, avgNormal.y, avgNormal.z,
				avgTangent.x, avgTangent.y, avgTangent.z,
				avgBitangent.x, avgBitangent.y, avgBitangent.z
			);

			cell.newIndex = newVertIdx++;
		}

		// 5. Rebuild index buffer, discarding degenerate triangles
		for (size_t i = 0; i + 2 < input.indices.size(); i += 3) {
			unsigned int v0 = input.indices[i];
			unsigned int v1 = input.indices[i + 1];
			unsigned int v2 = input.indices[i + 2];

			// Safety: skip out-of-range indices
			if (v0 >= (unsigned int)vertCount || v1 >= (unsigned int)vertCount || v2 >= (unsigned int)vertCount)
				continue;

			int n0 = cells[vertexCellKey[v0]].newIndex;
			int n1 = cells[vertexCellKey[v1]].newIndex;
			int n2 = cells[vertexCellKey[v2]].newIndex;

			// Skip degenerate triangles (all 3 verts collapsed into 1 or 2 cells)
			if (n0 == n1 || n1 == n2 || n0 == n2) continue;

			result.AddTriangle((unsigned int)n0, (unsigned int)n1, (unsigned int)n2);
		}

		// 6. Validate output — if simplification was too aggressive, return empty
		if (result.GetVertexCount() < 3 || result.GetTriangleCount() < 1) {
			result.Clear();
			return result;
		}

		printf("[MeshSimplifier] %d verts / %d tris -> %d verts / %d tris (ratio=%.0f%%)\n",
			vertCount, triCount,
			result.GetVertexCount(), result.GetTriangleCount(),
			targetRatio * 100.0f);

		return result;
	}
};
