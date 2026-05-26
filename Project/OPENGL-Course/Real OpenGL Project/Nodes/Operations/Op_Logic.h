#pragma once

#include "Nodes/Operation.h"
#include "Nodes/OperationRegistry.h"
#include <cmath>
#include <algorithm>

// =====================================================================
//  MeshOp_CompareFloat — Compare two float context vars → bool result
// =====================================================================

class MeshOp_CompareFloat : public Operation
{
public:
	std::string GetName() const override { return "Compare Float"; }
	std::string GetCategory() const override { return "Logic"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Float("Value A", 0.0f, -10000.0f, 10000.0f),
			ParamDef::Float("Value B", 0.0f, -10000.0f, 10000.0f),
			ParamDef::Enum("Operator", {"<", ">", "<=", ">=", "==", "!="}, 0)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		float a = GetFloat("Value A");
		float b = GetFloat("Value B");
		int op = GetEnum("Operator");

		// Override from context if available
		if (ctx.floatVars.count("A")) a = ctx.floatVars["A"];
		if (ctx.floatVars.count("B")) b = ctx.floatVars["B"];

		bool result = false;
		switch (op)
		{
		case 0: result = (a < b);  break;
		case 1: result = (a > b);  break;
		case 2: result = (a <= b); break;
		case 3: result = (a >= b); break;
		case 4: result = (fabsf(a - b) < 0.0001f); break;
		case 5: result = (fabsf(a - b) >= 0.0001f); break;
		}

		ctx.boolVars["CompareResult"] = result;
	}
};

REGISTER_OPERATION(MeshOp_CompareFloat, "Compare Float", "Logic")


// =====================================================================
//  MeshOp_IfElseMesh — Keep or clear mesh based on bool context var
// =====================================================================

class MeshOp_IfElseMesh : public Operation
{
public:
	std::string GetName() const override { return "If/Else Mesh"; }
	std::string GetCategory() const override { return "Logic"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Bool("Condition", true),
			ParamDef::Bool("Invert", false)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		bool condition = GetBool("Condition");
		bool invert = GetBool("Invert");

		// Override from context var if available (from a prior Compare)
		if (ctx.boolVars.count("CompareResult"))
			condition = ctx.boolVars["CompareResult"];

		if (invert) condition = !condition;

		if (!condition)
		{
			// Condition failed — clear the mesh (stop the pipeline)
			ctx.mesh.Clear();
			ctx.transforms.clear();
		}
		// If condition is true, do nothing — mesh passes through unchanged
	}
};

REGISTER_OPERATION(MeshOp_IfElseMesh, "If/Else Mesh", "Logic")


// =====================================================================
//  MeshOp_MathExpression — A op B = Result (stored in float context)
// =====================================================================

class MeshOp_MathExpression : public Operation
{
public:
	std::string GetName() const override { return "Math Expression"; }
	std::string GetCategory() const override { return "Logic"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Float("A", 0.0f, -10000.0f, 10000.0f),
			ParamDef::Float("B", 1.0f, -10000.0f, 10000.0f),
			ParamDef::Enum("Operation", {"Add", "Subtract", "Multiply", "Divide", "Power", "Min", "Max", "Modulo"}, 0)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		float a = GetFloat("A");
		float b = GetFloat("B");
		int op = GetEnum("Operation");

		// Override from context
		if (ctx.floatVars.count("A")) a = ctx.floatVars["A"];
		if (ctx.floatVars.count("B")) b = ctx.floatVars["B"];

		float result = 0.0f;
		switch (op)
		{
		case 0: result = a + b; break;
		case 1: result = a - b; break;
		case 2: result = a * b; break;
		case 3: result = (b != 0.0f) ? a / b : 0.0f; break;
		case 4: result = powf(a, b); break;
		case 5: result = std::min(a, b); break;
		case 6: result = std::max(a, b); break;
		case 7: result = (b != 0.0f) ? fmodf(a, b) : 0.0f; break;
		}

		ctx.floatVars["MathResult"] = result;
	}
};

REGISTER_OPERATION(MeshOp_MathExpression, "Math Expression", "Logic")


// =====================================================================
//  MeshOp_Clamp — Clamp a float context var to [min, max]
// =====================================================================

class MeshOp_Clamp : public Operation
{
public:
	std::string GetName() const override { return "Clamp"; }
	std::string GetCategory() const override { return "Logic"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Float("Value", 0.0f, -10000.0f, 10000.0f),
			ParamDef::Float("Min", 0.0f, -10000.0f, 10000.0f),
			ParamDef::Float("Max", 1.0f, -10000.0f, 10000.0f)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		float val = GetFloat("Value");
		float minVal = GetFloat("Min");
		float maxVal = GetFloat("Max");

		if (ctx.floatVars.count("MathResult")) val = ctx.floatVars["MathResult"];

		val = std::max(minVal, std::min(maxVal, val));
		ctx.floatVars["MathResult"] = val;
	}
};

REGISTER_OPERATION(MeshOp_Clamp, "Clamp", "Logic")


// =====================================================================
//  MeshOp_Remap — Remap a float from [inMin, inMax] to [outMin, outMax]
// =====================================================================

class MeshOp_Remap : public Operation
{
public:
	std::string GetName() const override { return "Remap"; }
	std::string GetCategory() const override { return "Logic"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Float("Value", 0.0f, -10000.0f, 10000.0f),
			ParamDef::Float("In Min", 0.0f, -10000.0f, 10000.0f),
			ParamDef::Float("In Max", 1.0f, -10000.0f, 10000.0f),
			ParamDef::Float("Out Min", 0.0f, -10000.0f, 10000.0f),
			ParamDef::Float("Out Max", 1.0f, -10000.0f, 10000.0f),
			ParamDef::Bool("Clamp Output", true)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		float val = GetFloat("Value");
		float inMin = GetFloat("In Min");
		float inMax = GetFloat("In Max");
		float outMin = GetFloat("Out Min");
		float outMax = GetFloat("Out Max");
		bool clampOut = GetBool("Clamp Output");

		if (ctx.floatVars.count("MathResult")) val = ctx.floatVars["MathResult"];

		float range = inMax - inMin;
		float t = (range != 0.0f) ? (val - inMin) / range : 0.0f;
		
		if (clampOut) t = std::max(0.0f, std::min(1.0f, t));

		float result = outMin + t * (outMax - outMin);
		ctx.floatVars["MathResult"] = result;
	}
};

REGISTER_OPERATION(MeshOp_Remap, "Remap", "Logic")


// =====================================================================
//  MeshOp_FilterTransformsByHeight — Remove transforms above/below Y
// =====================================================================
//
//  This is the CustomNode operation version of FilterTransformListNode.
//  It works on the OperationContext's transform list directly.
//

class MeshOp_FilterTransformsByHeight : public Operation
{
public:
	std::string GetName() const override { return "Filter Transforms By Height"; }
	std::string GetCategory() const override { return "Logic"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Enum("Axis", {"X", "Y", "Z"}, 1),
			ParamDef::Enum("Operator", {"<", ">", "<=", ">="}, 0),
			ParamDef::Float("Threshold", 10.0f, -10000.0f, 10000.0f)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		int axis = GetEnum("Axis");
		int op = GetEnum("Operator");
		float threshold = GetFloat("Threshold");

		TransformList filtered;
		filtered.reserve(ctx.transforms.size());

		for (const auto& t : ctx.transforms)
		{
			float val = 0.0f;
			switch (axis)
			{
			case 0: val = t.position.x; break;
			case 1: val = t.position.y; break;
			case 2: val = t.position.z; break;
			}

			bool pass = false;
			switch (op)
			{
			case 0: pass = (val < threshold);  break;
			case 1: pass = (val > threshold);  break;
			case 2: pass = (val <= threshold); break;
			case 3: pass = (val >= threshold); break;
			}

			if (pass) filtered.push_back(t);
		}

		ctx.transforms = std::move(filtered);
	}
};

REGISTER_OPERATION(MeshOp_FilterTransformsByHeight, "Filter Transforms By Height", "Logic")
