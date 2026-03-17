#pragma once

#include "../Operation.h"
#include "../OperationRegistry.h"
#include "../PrimitiveGenerator.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =====================================================================
//  MeshOp_Primitive — Generate a primitive shape from scratch
// =====================================================================

class MeshOp_Primitive : public Operation
{
public:
	std::string GetName() const override { return "Primitive"; }
	std::string GetCategory() const override { return "Generate"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Enum("Shape", {"Plane", "Cube", "Sphere", "Cylinder", "Cone", "Torus"}, 0),
			ParamDef::Int("Resolution", 20, 2, 100),
			ParamDef::Float("Size", 1.0f, 0.1f, 100.0f)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		int shape = GetEnum("Shape");
		int res   = GetInt("Resolution");
		float size = GetFloat("Size");

		MeshData data;

		switch (shape)
		{
		case 0: // Plane
			data = PrimitiveGenerator::GetPlaneData(res, res);
			break;
		case 1: // Cube
			data = PrimitiveGenerator::GetCubeData();
			break;
		case 2: // Sphere
			data = PrimitiveGenerator::GetSphereData(res, res);
			break;
		case 3: // Cylinder
			data = GenerateCylinder(res, size);
			break;
		case 4: // Cone
			data = GenerateCone(res, size);
			break;
		case 5: // Torus
			data = GenerateTorus(res, size);
			break;
		}

		// Apply size scaling if not 1.0
		if (std::abs(size - 1.0f) > 0.001f && shape != 3 && shape != 4 && shape != 5)
		{
			glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(size));
			data.TransformBy(scaleMatrix);
		}

		ctx.mesh = data;
	}

private:
	MeshData GenerateCylinder(int segments, float size)
	{
		MeshData data;
		float radius = size * 0.5f;
		float height = size;
		float halfH = height * 0.5f;

		// Generate vertices for top and bottom circles
		for (int i = 0; i <= segments; i++)
		{
			float angle = (float)i / (float)segments * 2.0f * (float)M_PI;
			float x = cos(angle) * radius;
			float z = sin(angle) * radius;
			float u = (float)i / (float)segments;

			// Bottom vertex
			data.AddVertex(x, -halfH, z, u, 0.0f, x, 0.0f, z, 0, 0, 0, 0, 0, 0);
			// Top vertex
			data.AddVertex(x, halfH, z, u, 1.0f, x, 0.0f, z, 0, 0, 0, 0, 0, 0);
		}

		// Side triangles
		for (int i = 0; i < segments; i++)
		{
			int b0 = i * 2, t0 = i * 2 + 1;
			int b1 = (i + 1) * 2, t1 = (i + 1) * 2 + 1;
			data.AddTriangle(b0, b1, t1);
			data.AddTriangle(b0, t1, t0);
		}

		// Bottom cap center
		int bottomCenter = data.GetVertexCount();
		data.AddVertex(0, -halfH, 0, 0.5f, 0.5f, 0, -1, 0, 0, 0, 0, 0, 0, 0);
		// Top cap center
		int topCenter = data.GetVertexCount();
		data.AddVertex(0, halfH, 0, 0.5f, 0.5f, 0, 1, 0, 0, 0, 0, 0, 0, 0);

		for (int i = 0; i < segments; i++)
		{
			int b0 = i * 2, b1 = (i + 1) * 2;
			int t0 = i * 2 + 1, t1 = (i + 1) * 2 + 1;
			data.AddTriangle(bottomCenter, b1, b0);
			data.AddTriangle(topCenter, t0, t1);
		}

		return data;
	}

	MeshData GenerateCone(int segments, float size)
	{
		MeshData data;
		float radius = size * 0.5f;
		float height = size;

		// Apex
		int apex = 0;
		data.AddVertex(0, height, 0, 0.5f, 1.0f, 0, 1, 0, 0, 0, 0, 0, 0, 0);

		// Base circle
		for (int i = 0; i <= segments; i++)
		{
			float angle = (float)i / (float)segments * 2.0f * (float)M_PI;
			float x = cos(angle) * radius;
			float z = sin(angle) * radius;
			float u = (float)i / (float)segments;

			glm::vec3 sideNormal = glm::normalize(glm::vec3(x, radius, z));
			data.AddVertex(x, 0, z, u, 0.0f, sideNormal.x, sideNormal.y, sideNormal.z, 0, 0, 0, 0, 0, 0);
		}

		// Side triangles
		for (int i = 0; i < segments; i++)
		{
			data.AddTriangle(apex, i + 1, i + 2);
		}

		// Base cap
		int baseCenter = data.GetVertexCount();
		data.AddVertex(0, 0, 0, 0.5f, 0.5f, 0, -1, 0, 0, 0, 0, 0, 0, 0);
		for (int i = 0; i < segments; i++)
		{
			data.AddTriangle(baseCenter, i + 2, i + 1);
		}

		return data;
	}

	MeshData GenerateTorus(int segments, float size)
	{
		MeshData data;
		float majorRadius = size * 0.5f;
		float minorRadius = size * 0.15f;
		int ringSegments = segments;
		int tubeSegments = segments;

		for (int i = 0; i <= ringSegments; i++)
		{
			float theta = (float)i / (float)ringSegments * 2.0f * (float)M_PI;
			float cosTheta = cos(theta);
			float sinTheta = sin(theta);

			for (int j = 0; j <= tubeSegments; j++)
			{
				float phi = (float)j / (float)tubeSegments * 2.0f * (float)M_PI;
				float cosPhi = cos(phi);
				float sinPhi = sin(phi);

				float x = (majorRadius + minorRadius * cosPhi) * cosTheta;
				float y = minorRadius * sinPhi;
				float z = (majorRadius + minorRadius * cosPhi) * sinTheta;

				float nx = cosPhi * cosTheta;
				float ny = sinPhi;
				float nz = cosPhi * sinTheta;

				float u = (float)i / (float)ringSegments;
				float v = (float)j / (float)tubeSegments;

				data.AddVertex(x, y, z, u, v, nx, ny, nz, 0, 0, 0, 0, 0, 0);
			}
		}

		for (int i = 0; i < ringSegments; i++)
		{
			for (int j = 0; j < tubeSegments; j++)
			{
				int a = i * (tubeSegments + 1) + j;
				int b = (i + 1) * (tubeSegments + 1) + j;
				int c = (i + 1) * (tubeSegments + 1) + j + 1;
				int d = i * (tubeSegments + 1) + j + 1;
				data.AddTriangle(a, b, d);
				data.AddTriangle(b, c, d);
			}
		}

		return data;
	}
};

REGISTER_OPERATION(MeshOp_Primitive, "Primitive", "Generate")


// =====================================================================
//  MeshOp_Grid — Generate a subdivided grid
// =====================================================================

class MeshOp_Grid : public Operation
{
public:
	std::string GetName() const override { return "Grid"; }
	std::string GetCategory() const override { return "Generate"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Int("Resolution X", 10, 2, 200),
			ParamDef::Int("Resolution Z", 10, 2, 200),
			ParamDef::Float("Spacing", 1.0f, 0.01f, 10.0f)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		int resX = GetInt("Resolution X");
		int resZ = GetInt("Resolution Z");
		float spacing = GetFloat("Spacing");

		MeshData data;

		float halfW = (resX - 1) * spacing * 0.5f;
		float halfH = (resZ - 1) * spacing * 0.5f;

		for (int z = 0; z < resZ; z++)
		{
			for (int x = 0; x < resX; x++)
			{
				float px = x * spacing - halfW;
				float pz = z * spacing - halfH;
				float u = (float)x / (float)(resX - 1);
				float v = (float)z / (float)(resZ - 1);

				data.AddVertex(px, 0, pz, u, v, 0, 1, 0, 1, 0, 0, 0, 0, 1);
			}
		}

		for (int z = 0; z < resZ - 1; z++)
		{
			for (int x = 0; x < resX - 1; x++)
			{
				int topLeft = z * resX + x;
				int topRight = topLeft + 1;
				int botLeft = (z + 1) * resX + x;
				int botRight = botLeft + 1;

				data.AddTriangle(topLeft, botLeft, topRight);
				data.AddTriangle(topRight, botLeft, botRight);
			}
		}

		ctx.mesh = data;
	}
};

REGISTER_OPERATION(MeshOp_Grid, "Grid", "Generate")
