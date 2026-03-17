#pragma once

#include "../Operation.h"
#include "../OperationRegistry.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =====================================================================
//  MeshOp_ProjectUV — Planar/Box UV projection
// =====================================================================

class MeshOp_ProjectUV : public Operation
{
public:
	std::string GetName() const override { return "Project UV"; }
	std::string GetCategory() const override { return "UV"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Enum("Mode", {"Planar XZ", "Planar XY", "Planar YZ", "Box"}, 0),
			ParamDef::Float("Scale", 1.0f, 0.01f, 100.0f),
			ParamDef::Vec2("Offset", glm::vec2(0.0f))
		};
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty()) return;

		int mode = GetEnum("Mode");
		float scale = GetFloat("Scale");
		glm::vec2 offset = GetVec2("Offset");

		int count = ctx.mesh.GetVertexCount();
		for (int i = 0; i < count; i++)
		{
			int base = i * 14;
			float px = ctx.mesh.vertices[base];
			float py = ctx.mesh.vertices[base + 1];
			float pz = ctx.mesh.vertices[base + 2];

			float u, v;

			if (mode == 3) // Box projection
			{
				float nx = fabs(ctx.mesh.vertices[base + 5]);
				float ny = fabs(ctx.mesh.vertices[base + 6]);
				float nz = fabs(ctx.mesh.vertices[base + 7]);

				if (nx >= ny && nx >= nz)      { u = pz; v = py; } // X-facing
				else if (ny >= nx && ny >= nz)  { u = px; v = pz; } // Y-facing
				else                            { u = px; v = py; } // Z-facing
			}
			else if (mode == 0) { u = px; v = pz; } // Planar XZ
			else if (mode == 1) { u = px; v = py; } // Planar XY
			else                { u = py; v = pz; } // Planar YZ

			ctx.mesh.vertices[base + 3] = u * scale + offset.x;
			ctx.mesh.vertices[base + 4] = v * scale + offset.y;
		}
	}
};

REGISTER_OPERATION(MeshOp_ProjectUV, "Project UV", "UV")


// =====================================================================
//  MeshOp_MergeSecondary — Merge secondary mesh into primary
// =====================================================================

class MeshOp_MergeSecondary : public Operation
{
public:
	std::string GetName() const override { return "Merge Secondary"; }
	std::string GetCategory() const override { return "Utility"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {}; // No parameters
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.secondaryMesh.vertices.empty()) return;
		ctx.mesh.Append(ctx.secondaryMesh);
	}
};

REGISTER_OPERATION(MeshOp_MergeSecondary, "Merge Secondary", "Utility")


// =====================================================================
//  MeshOp_ClearSelection — Reset selection to all vertices
// =====================================================================

class MeshOp_ClearSelection : public Operation
{
public:
	std::string GetName() const override { return "Clear Selection"; }
	std::string GetCategory() const override { return "Selection"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {}; // No parameters
	}

	void Execute(OperationContext& ctx) override
	{
		ctx.selection.clear(); // Empty = all selected
	}
};

REGISTER_OPERATION(MeshOp_ClearSelection, "Clear Selection", "Selection")


// =====================================================================
//  MeshOp_RecalcNormals — Recalculate face normals
// =====================================================================

class MeshOp_RecalcNormals : public Operation
{
public:
	std::string GetName() const override { return "Recalc Normals"; }
	std::string GetCategory() const override { return "Mesh"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Bool("Smooth", true)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty() || ctx.mesh.indices.empty()) return;

		bool smooth = GetBool("Smooth");
		int vertCount = ctx.mesh.GetVertexCount();

		// Zero out normals
		for (int i = 0; i < vertCount; i++)
		{
			int base = i * 14;
			ctx.mesh.vertices[base + 5] = 0;
			ctx.mesh.vertices[base + 6] = 0;
			ctx.mesh.vertices[base + 7] = 0;
		}

		// Accumulate face normals
		int triCount = ctx.mesh.GetTriangleCount();
		for (int t = 0; t < triCount; t++)
		{
			unsigned int i0 = ctx.mesh.indices[t * 3];
			unsigned int i1 = ctx.mesh.indices[t * 3 + 1];
			unsigned int i2 = ctx.mesh.indices[t * 3 + 2];

			glm::vec3 p0 = ctx.mesh.GetPosition(i0);
			glm::vec3 p1 = ctx.mesh.GetPosition(i1);
			glm::vec3 p2 = ctx.mesh.GetPosition(i2);

			glm::vec3 faceNormal = glm::cross(p1 - p0, p2 - p0);

			// Add to vertex normals
			for (unsigned int idx : {i0, i1, i2})
			{
				int base = idx * 14;
				ctx.mesh.vertices[base + 5] += faceNormal.x;
				ctx.mesh.vertices[base + 6] += faceNormal.y;
				ctx.mesh.vertices[base + 7] += faceNormal.z;
			}
		}

		// Normalize
		for (int i = 0; i < vertCount; i++)
		{
			int base = i * 14;
			float nx = ctx.mesh.vertices[base + 5];
			float ny = ctx.mesh.vertices[base + 6];
			float nz = ctx.mesh.vertices[base + 7];
			float len = sqrt(nx*nx + ny*ny + nz*nz);
			if (len > 0.0001f)
			{
				ctx.mesh.vertices[base + 5] /= len;
				ctx.mesh.vertices[base + 6] /= len;
				ctx.mesh.vertices[base + 7] /= len;
			}
		}
	}
};

REGISTER_OPERATION(MeshOp_RecalcNormals, "Recalc Normals", "Mesh")
