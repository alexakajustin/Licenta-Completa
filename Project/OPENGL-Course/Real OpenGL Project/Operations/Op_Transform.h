#pragma once

#include "../Operation.h"
#include "../OperationRegistry.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =====================================================================
//  MeshOp_Transform — Translate / Rotate / Scale
// =====================================================================

class MeshOp_Transform : public Operation
{
public:
	std::string GetName() const override { return "Transform"; }
	std::string GetCategory() const override { return "Transform"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Vec3("Translate", glm::vec3(0.0f)),
			ParamDef::Vec3("Rotate", glm::vec3(0.0f)),
			ParamDef::Vec3("Scale", glm::vec3(1.0f))
		};
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty()) return;

		glm::vec3 translate = GetVec3("Translate");
		glm::vec3 rotate    = GetVec3("Rotate");
		glm::vec3 scale     = GetVec3("Scale");

		glm::mat4 mat = glm::mat4(1.0f);
		mat = glm::translate(mat, translate);
		mat = glm::rotate(mat, glm::radians(rotate.x), glm::vec3(1, 0, 0));
		mat = glm::rotate(mat, glm::radians(rotate.y), glm::vec3(0, 1, 0));
		mat = glm::rotate(mat, glm::radians(rotate.z), glm::vec3(0, 0, 1));
		mat = glm::scale(mat, scale);

		ctx.mesh.TransformBy(mat);
	}
};

REGISTER_OPERATION(MeshOp_Transform, "Transform", "Transform")


// =====================================================================
//  MeshOp_Bend — Bend mesh around an axis
// =====================================================================

class MeshOp_Bend : public Operation
{
public:
	std::string GetName() const override { return "Bend"; }
	std::string GetCategory() const override { return "Transform"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Enum("Axis", {"X", "Y", "Z"}, 1),
			ParamDef::Float("Angle", 45.0f, -180.0f, 180.0f),
			ParamDef::Float("Center", 0.0f, -10.0f, 10.0f)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty()) return;

		int axis = GetEnum("Axis");
		float angle = glm::radians(GetFloat("Angle"));
		float center = GetFloat("Center");

		// Get bounds along the bend axis to normalize the effect
		glm::vec3 minB, maxB;
		ctx.mesh.GetBounds(minB, maxB);

		float axisMin, axisMax;
		if (axis == 0)      { axisMin = minB.x; axisMax = maxB.x; }
		else if (axis == 1) { axisMin = minB.y; axisMax = maxB.y; }
		else                { axisMin = minB.z; axisMax = maxB.z; }

		float range = axisMax - axisMin;
		if (range < 0.0001f) return;

		int count = ctx.mesh.GetVertexCount();
		for (int i = 0; i < count; i++)
		{
			int base = i * 14;

			float px = ctx.mesh.vertices[base];
			float py = ctx.mesh.vertices[base + 1];
			float pz = ctx.mesh.vertices[base + 2];

			// Normalized parameter along the axis [0,1]
			float t;
			if (axis == 0)      t = (px - axisMin) / range;
			else if (axis == 1) t = (py - axisMin) / range;
			else                t = (pz - axisMin) / range;

			float bendAngle = angle * (t - 0.5f); // Center the bend
			float cosA = cos(bendAngle);
			float sinA = sin(bendAngle);

			// Bend: rotate the perpendicular axis around the bend center
			if (axis == 1) // Bend Y-axis (most common — terrain bending)
			{
				float dist = pz - center;
				ctx.mesh.vertices[base + 1] = py * cosA - dist * sinA;
				ctx.mesh.vertices[base + 2] = py * sinA + dist * cosA + center;
			}
			else if (axis == 0)
			{
				float dist = pz - center;
				ctx.mesh.vertices[base]     = px * cosA - dist * sinA;
				ctx.mesh.vertices[base + 2] = px * sinA + dist * cosA + center;
			}
			else
			{
				float dist = py - center;
				ctx.mesh.vertices[base + 2] = pz * cosA - dist * sinA;
				ctx.mesh.vertices[base + 1] = pz * sinA + dist * cosA + center;
			}
		}
	}
};

REGISTER_OPERATION(MeshOp_Bend, "Bend", "Transform")


// =====================================================================
//  MeshOp_Twist — Twist mesh around an axis
// =====================================================================

class MeshOp_Twist : public Operation
{
public:
	std::string GetName() const override { return "Twist"; }
	std::string GetCategory() const override { return "Transform"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Enum("Axis", {"X", "Y", "Z"}, 1),
			ParamDef::Float("Angle", 90.0f, -720.0f, 720.0f)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty()) return;

		int axis = GetEnum("Axis");
		float totalAngle = glm::radians(GetFloat("Angle"));

		glm::vec3 minB, maxB;
		ctx.mesh.GetBounds(minB, maxB);

		float axisMin, axisMax;
		if (axis == 0)      { axisMin = minB.x; axisMax = maxB.x; }
		else if (axis == 1) { axisMin = minB.y; axisMax = maxB.y; }
		else                { axisMin = minB.z; axisMax = maxB.z; }

		float range = axisMax - axisMin;
		if (range < 0.0001f) return;

		int count = ctx.mesh.GetVertexCount();
		for (int i = 0; i < count; i++)
		{
			int base = i * 14;
			float px = ctx.mesh.vertices[base];
			float py = ctx.mesh.vertices[base + 1];
			float pz = ctx.mesh.vertices[base + 2];

			float t;
			if (axis == 0)      t = (px - axisMin) / range;
			else if (axis == 1) t = (py - axisMin) / range;
			else                t = (pz - axisMin) / range;

			float twistAngle = totalAngle * t;
			float cosA = cos(twistAngle);
			float sinA = sin(twistAngle);

			if (axis == 1) // Twist around Y
			{
				ctx.mesh.vertices[base]     = px * cosA - pz * sinA;
				ctx.mesh.vertices[base + 2] = px * sinA + pz * cosA;
			}
			else if (axis == 0) // Twist around X
			{
				ctx.mesh.vertices[base + 1] = py * cosA - pz * sinA;
				ctx.mesh.vertices[base + 2] = py * sinA + pz * cosA;
			}
			else // Twist around Z
			{
				ctx.mesh.vertices[base]     = px * cosA - py * sinA;
				ctx.mesh.vertices[base + 1] = px * sinA + py * cosA;
			}
		}
	}
};

REGISTER_OPERATION(MeshOp_Twist, "Twist", "Transform")


// =====================================================================
//  MeshOp_Taper — Progressive scaling along an axis
// =====================================================================

class MeshOp_Taper : public Operation
{
public:
	std::string GetName() const override { return "Taper"; }
	std::string GetCategory() const override { return "Transform"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Enum("Axis", {"X", "Y", "Z"}, 1),
			ParamDef::Float("Factor", 0.5f, 0.0f, 2.0f),
			ParamDef::Bool("Invert", false)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty()) return;

		int axis = GetEnum("Axis");
		float factor = GetFloat("Factor");
		bool invert = GetBool("Invert");

		glm::vec3 minB, maxB;
		ctx.mesh.GetBounds(minB, maxB);

		float axisMin, axisMax;
		if (axis == 0)      { axisMin = minB.x; axisMax = maxB.x; }
		else if (axis == 1) { axisMin = minB.y; axisMax = maxB.y; }
		else                { axisMin = minB.z; axisMax = maxB.z; }

		float range = axisMax - axisMin;
		if (range < 0.0001f) return;

		int count = ctx.mesh.GetVertexCount();
		for (int i = 0; i < count; i++)
		{
			int base = i * 14;
			float px = ctx.mesh.vertices[base];
			float py = ctx.mesh.vertices[base + 1];
			float pz = ctx.mesh.vertices[base + 2];

			float t;
			if (axis == 0)      t = (px - axisMin) / range;
			else if (axis == 1) t = (py - axisMin) / range;
			else                t = (pz - axisMin) / range;

			if (invert) t = 1.0f - t;

			float scale = 1.0f + (factor - 1.0f) * t;

			// Scale the two axes perpendicular to the taper axis
			if (axis == 0)      { ctx.mesh.vertices[base + 1] *= scale; ctx.mesh.vertices[base + 2] *= scale; }
			else if (axis == 1) { ctx.mesh.vertices[base]     *= scale; ctx.mesh.vertices[base + 2] *= scale; }
			else                { ctx.mesh.vertices[base]     *= scale; ctx.mesh.vertices[base + 1] *= scale; }
		}
	}
};

REGISTER_OPERATION(MeshOp_Taper, "Taper", "Transform")
