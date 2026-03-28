#pragma once

#include <string>
#include <vector>
#include <map>
#include <glm/glm.hpp>
#include "External Libs/nlohmann/json.hpp"
#include "MeshData.h"

using json = nlohmann::json;

// =====================================================================
//  Parameter System — type-safe, serializable parameter definitions
// =====================================================================

enum class ParamType
{
	Float,
	Int,
	Vec2,
	Vec3,
	Bool,
	Enum
};

NLOHMANN_JSON_SERIALIZE_ENUM(ParamType, {
	{ParamType::Float, "Float"},
	{ParamType::Int,   "Int"},
	{ParamType::Vec2,  "Vec2"},
	{ParamType::Vec3,  "Vec3"},
	{ParamType::Bool,  "Bool"},
	{ParamType::Enum,  "Enum"},
})

// A single parameter value (tagged union)
struct ParamValue
{
	ParamType type = ParamType::Float;
	float floatVal = 0.0f;
	int intVal = 0;
	glm::vec2 vec2Val = glm::vec2(0.0f);
	glm::vec3 vec3Val = glm::vec3(0.0f);
	bool boolVal = false;
	int enumVal = 0;          // Index into enumOptions
	std::string stringVal;    // For display name in Enum mode

	// Convenience constructors
	static ParamValue MakeFloat(float v)     { ParamValue p; p.type = ParamType::Float; p.floatVal = v; return p; }
	static ParamValue MakeInt(int v)         { ParamValue p; p.type = ParamType::Int;   p.intVal = v;   return p; }
	static ParamValue MakeVec2(glm::vec2 v)  { ParamValue p; p.type = ParamType::Vec2;  p.vec2Val = v;  return p; }
	static ParamValue MakeVec3(glm::vec3 v)  { ParamValue p; p.type = ParamType::Vec3;  p.vec3Val = v;  return p; }
	static ParamValue MakeBool(bool v)       { ParamValue p; p.type = ParamType::Bool;  p.boolVal = v;  return p; }
	static ParamValue MakeEnum(int v)        { ParamValue p; p.type = ParamType::Enum;  p.enumVal = v;  return p; }

	// JSON Serialization
	friend void to_json(json& j, const ParamValue& p) {
		j = json{ {"type", p.type} };
		switch (p.type) {
			case ParamType::Float: j["value"] = p.floatVal; break;
			case ParamType::Int:   j["value"] = p.intVal;   break;
			case ParamType::Vec2:  j["value"] = {p.vec2Val.x, p.vec2Val.y}; break;
			case ParamType::Vec3:  j["value"] = {p.vec3Val.x, p.vec3Val.y, p.vec3Val.z}; break;
			case ParamType::Bool:  j["value"] = p.boolVal;  break;
			case ParamType::Enum:  j["value"] = p.enumVal;  j["name"] = p.stringVal; break;
		}
	}

	friend void from_json(const json& j, ParamValue& p) {
		p.type = j.at("type").get<ParamType>();
		if (j.contains("value")) {
			switch (p.type) {
				case ParamType::Float: p.floatVal = j.at("value").get<float>(); break;
				case ParamType::Int:   p.intVal   = j.at("value").get<int>();   break;
				case ParamType::Vec2:  p.vec2Val  = glm::vec2(j.at("value")[0], j.at("value")[1]); break;
				case ParamType::Vec3:  p.vec3Val  = glm::vec3(j.at("value")[0], j.at("value")[1], j.at("value")[2]); break;
				case ParamType::Bool:  p.boolVal  = j.at("value").get<bool>();  break;
				case ParamType::Enum:  p.enumVal  = j.at("value").get<int>();   p.stringVal = j.value("name", ""); break;
			}
		}
	}
};

// Definition of a parameter (metadata + default value)
struct ParamDef
{
	std::string name;         // Display name and key (must be unique per operation)
	ParamType type;
	ParamValue defaultValue;

	// Range limits (for Float/Int sliders)
	float minFloat = 0.0f;
	float maxFloat = 1.0f;
	int minInt = 0;
	int maxInt = 10;

	// Enum options (for Enum type)
	std::vector<std::string> enumOptions;

	// Convenience constructors
	static ParamDef Float(const std::string& name, float defaultVal, float minVal, float maxVal)
	{
		ParamDef d;
		d.name = name; d.type = ParamType::Float;
		d.defaultValue = ParamValue::MakeFloat(defaultVal);
		d.minFloat = minVal; d.maxFloat = maxVal;
		return d;
	}

	static ParamDef Int(const std::string& name, int defaultVal, int minVal, int maxVal)
	{
		ParamDef d;
		d.name = name; d.type = ParamType::Int;
		d.defaultValue = ParamValue::MakeInt(defaultVal);
		d.minInt = minVal; d.maxInt = maxVal;
		return d;
	}

	static ParamDef Vec2(const std::string& name, glm::vec2 defaultVal)
	{
		ParamDef d;
		d.name = name; d.type = ParamType::Vec2;
		d.defaultValue = ParamValue::MakeVec2(defaultVal);
		return d;
	}

	static ParamDef Vec3(const std::string& name, glm::vec3 defaultVal)
	{
		ParamDef d;
		d.name = name; d.type = ParamType::Vec3;
		d.defaultValue = ParamValue::MakeVec3(defaultVal);
		return d;
	}

	static ParamDef Bool(const std::string& name, bool defaultVal)
	{
		ParamDef d;
		d.name = name; d.type = ParamType::Bool;
		d.defaultValue = ParamValue::MakeBool(defaultVal);
		return d;
	}

	static ParamDef Enum(const std::string& name, const std::vector<std::string>& options, int defaultIndex = 0)
	{
		ParamDef d;
		d.name = name; d.type = ParamType::Enum;
		d.defaultValue = ParamValue::MakeEnum(defaultIndex);
		d.enumOptions = options;
		return d;
	}
};


// =====================================================================
//  OperationContext — the workspace flowing through operations
// =====================================================================

struct OperationContext
{
	// Primary mesh being modified by operations
	MeshData mesh;

	// Secondary mesh (for binary ops like merge, boolean)
	MeshData secondaryMesh;

	// Per-vertex selection mask (true = selected, empty = all selected)
	std::vector<bool> selection;

	// Named variables from input pins (allows operations to reference pin values)
	std::map<std::string, float>     floatVars;
	std::map<std::string, int>       intVars;
	std::map<std::string, glm::vec2> vec2Vars;
	std::map<std::string, glm::vec3> vec3Vars;
	std::map<std::string, bool>      boolVars;

	// Transform list (propagated through for scatter workflows)
	TransformList transforms;

	// Source metadata (propagated through)
	std::string sourceObjectName = "(none)";
	Material* sourceMaterial = nullptr;
	Texture* sourceTexture = nullptr;
	Texture* sourceNormalMap = nullptr;

	// Utility: check if a vertex is selected (empty selection = all selected)
	bool IsVertexSelected(int index) const
	{
		if (selection.empty()) return true;
		if (index < 0 || index >= (int)selection.size()) return false;
		return selection[index];
	}

	// Utility: ensure selection mask matches vertex count
	void EnsureSelectionSize()
	{
		int vertCount = mesh.GetVertexCount();
		if ((int)selection.size() != vertCount)
			selection.assign(vertCount, true);
	}

	void Clear()
	{
		mesh.Clear();
		secondaryMesh.Clear();
		selection.clear();
		floatVars.clear();
		intVars.clear();
		vec2Vars.clear();
		vec3Vars.clear();
		boolVars.clear();
		transforms.clear();
		sourceObjectName = "(none)";
		sourceMaterial = nullptr;
		sourceTexture = nullptr;
		sourceNormalMap = nullptr;
	}
};


// =====================================================================
//  Operation — base class for all composable mesh operations
// =====================================================================

class Operation
{
public:
	virtual ~Operation() = default;

	// --- Identity ---
	virtual std::string GetName() const = 0;
	virtual std::string GetCategory() const = 0;

	// --- Parameter Schema ---
	// Returns the parameter definitions for this operation.
	// Called once to build the UI and to initialize default values.
	virtual std::vector<ParamDef> GetParamDefs() const = 0;

	// --- Execution ---
	// Modifies the OperationContext in-place.
	// Operations should be defensive: check for empty meshes, invalid params, etc.
	virtual void Execute(OperationContext& ctx) = 0;

	// --- Parameter Access ---
	// Get/set parameter values by name.
	// These are populated from paramDefs defaults on creation,
	// and can be overridden by the user in the UI or by serialization.

	void InitDefaults()
	{
		auto defs = GetParamDefs();
		for (const auto& def : defs)
		{
			if (params.find(def.name) == params.end())
				params[def.name] = def.defaultValue;
		}
	}

	float GetFloat(const std::string& name) const
	{
		auto it = params.find(name);
		if (it != params.end()) return it->second.floatVal;
		return 0.0f;
	}

	int GetInt(const std::string& name) const
	{
		auto it = params.find(name);
		if (it != params.end()) return it->second.intVal;
		return 0;
	}

	glm::vec2 GetVec2(const std::string& name) const
	{
		auto it = params.find(name);
		if (it != params.end()) return it->second.vec2Val;
		return glm::vec2(0.0f);
	}

	glm::vec3 GetVec3(const std::string& name) const
	{
		auto it = params.find(name);
		if (it != params.end()) return it->second.vec3Val;
		return glm::vec3(0.0f);
	}

	bool GetBool(const std::string& name) const
	{
		auto it = params.find(name);
		if (it != params.end()) return it->second.boolVal;
		return false;
	}

	int GetEnum(const std::string& name) const
	{
		auto it = params.find(name);
		if (it != params.end()) return it->second.enumVal;
		return 0;
	}

	void SetFloat(const std::string& name, float v)     { params[name].floatVal = v; params[name].type = ParamType::Float; }
	void SetInt(const std::string& name, int v)          { params[name].intVal = v;   params[name].type = ParamType::Int; }
	void SetVec2(const std::string& name, glm::vec2 v)   { params[name].vec2Val = v;  params[name].type = ParamType::Vec2; }
	void SetVec3(const std::string& name, glm::vec3 v)   { params[name].vec3Val = v;  params[name].type = ParamType::Vec3; }
	void SetBool(const std::string& name, bool v)        { params[name].boolVal = v;  params[name].type = ParamType::Bool; }
	void SetEnum(const std::string& name, int v)         { params[name].enumVal = v;  params[name].type = ParamType::Enum; }

	// Direct access to parameter map (for serialization and UI)
	std::map<std::string, ParamValue>& GetParams() { return params; }
	const std::map<std::string, ParamValue>& GetParams() const { return params; }

	// --- UI Rendering ---
	// Default implementation renders controls for all paramDefs.
	// Override for custom UI if needed.
	virtual void RenderUI();

protected:
	std::map<std::string, ParamValue> params;
};
