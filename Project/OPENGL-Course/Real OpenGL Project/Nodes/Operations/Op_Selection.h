#pragma once

#include "Nodes/Operation.h"
#include "Nodes/OperationRegistry.h"
#include <cmath>
#include <cstdlib>

// =====================================================================
//  MeshOp_SelectByHeight — Select vertices by Y position range
// =====================================================================

class MeshOp_SelectByHeight : public Operation
{
public:
	std::string GetName() const override { return "Select By Height"; }
	std::string GetCategory() const override { return "Selection"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Float("Min Y", 0.0f, -100.0f, 100.0f),
			ParamDef::Float("Max Y", 1.0f, -100.0f, 100.0f),
			ParamDef::Bool("Invert", false)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty()) return;

		float minY = GetFloat("Min Y");
		float maxY = GetFloat("Max Y");
		bool invert = GetBool("Invert");

		int count = ctx.mesh.GetVertexCount();
		ctx.selection.resize(count);

		for (int i = 0; i < count; i++)
		{
			float y = ctx.mesh.vertices[i * 14 + 1];
			bool inRange = (y >= minY && y <= maxY);
			ctx.selection[i] = invert ? !inRange : inRange;
		}
	}
};

REGISTER_OPERATION(MeshOp_SelectByHeight, "Select By Height", "Selection")


// =====================================================================
//  MeshOp_SelectByNormal — Select vertices by normal direction
// =====================================================================

class MeshOp_SelectByNormal : public Operation
{
public:
	std::string GetName() const override { return "Select By Normal"; }
	std::string GetCategory() const override { return "Selection"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Vec3("Direction", glm::vec3(0.0f, 1.0f, 0.0f)),
			ParamDef::Float("Threshold", 0.7f, -1.0f, 1.0f),
			ParamDef::Bool("Invert", false)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty()) return;

		glm::vec3 dir = glm::normalize(GetVec3("Direction"));
		float threshold = GetFloat("Threshold");
		bool invert = GetBool("Invert");

		if (glm::length(dir) < 0.001f) dir = glm::vec3(0, 1, 0);

		int count = ctx.mesh.GetVertexCount();
		ctx.selection.resize(count);

		for (int i = 0; i < count; i++)
		{
			glm::vec3 normal = ctx.mesh.GetNormal(i);
			float dot = glm::dot(normal, dir);
			bool selected = (dot >= threshold);
			ctx.selection[i] = invert ? !selected : selected;
		}
	}
};

REGISTER_OPERATION(MeshOp_SelectByNormal, "Select By Normal", "Selection")


// =====================================================================
//  MeshOp_SelectByRandom — Random selection by percentage
// =====================================================================

class MeshOp_SelectByRandom : public Operation
{
public:
	std::string GetName() const override { return "Select By Random"; }
	std::string GetCategory() const override { return "Selection"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Float("Percentage", 50.0f, 0.0f, 100.0f),
			ParamDef::Int("Seed", 42, 0, 9999)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty()) return;

		float percentage = GetFloat("Percentage") / 100.0f;
		int seed = GetInt("Seed");

		srand(seed);

		int count = ctx.mesh.GetVertexCount();
		ctx.selection.resize(count);

		for (int i = 0; i < count; i++)
		{
			float r = (float)rand() / RAND_MAX;
			ctx.selection[i] = (r < percentage);
		}
	}
};

REGISTER_OPERATION(MeshOp_SelectByRandom, "Select By Random", "Selection")
