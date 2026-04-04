#pragma once

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <filesystem>
#include "MeshData.h"
#include "Operation.h"

// =====================================================================
//  PinDefinition — describes one input or output pin of a custom node
// =====================================================================

struct PinDefinition
{
	std::string name;
	PinDataType type;

	PinDefinition() : name("Unnamed"), type(PinDataType::Mesh) {}
	PinDefinition(const std::string& n, PinDataType t) : name(n), type(t) {}
};

// =====================================================================
//  OperationSlot — one operation in the custom node's processing chain
// =====================================================================

struct OperationSlot
{
	std::string operationName;                    // Must match a registered name
	std::map<std::string, ParamValue> paramOverrides;  // Override default params
};

// =====================================================================
//  CustomNodeDef — serializable definition of a user-created node
// =====================================================================
//
//  This is the "recipe" — it describes what a custom node does
//  without any compiled code. The CustomNode class interprets this
//  at runtime by instantiating operations from the registry.
//

struct CustomNodeDef
{
	std::string name = "Custom Node";
	std::string category = "Custom";
	std::string filePath = ""; // Tracks the exact JSON file path on disk

	std::vector<PinDefinition> inputDefs;
	std::vector<PinDefinition> outputDefs;
	std::vector<OperationSlot> operations;

	// Validation — returns empty string if OK, error message otherwise
	std::string Validate() const
	{
		if (name.empty())
			return "Node name is empty";
		if (outputDefs.empty())
			return "Node has no outputs";
		if (operations.empty())
			return "Node has no operations";

		// Check for duplicate pin names
		std::map<std::string, int> pinNames;
		for (const auto& pin : inputDefs)
		{
			if (pin.name.empty()) return "Input pin has empty name";
			pinNames[pin.name]++;
		}
		for (const auto& pin : outputDefs)
		{
			if (pin.name.empty()) return "Output pin has empty name";
			pinNames[pin.name]++;
		}
		for (const auto& pair : pinNames)
		{
			if (pair.second > 1)
				return "Duplicate pin name: " + pair.first;
		}

		return "";
	}

	// ============ JSON Serialization ============

	bool SaveToFile(const std::string& path) const;
	static bool LoadFromFile(const std::string& path, CustomNodeDef& outDef);

	// Internal helpers
	static std::string PinTypeToString(PinDataType type);
	static PinDataType StringToPinType(const std::string& str);
	static std::string ParamTypeToString(ParamType type);
	static ParamType StringToParamType(const std::string& str);
};
