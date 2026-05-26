#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <cstdio>

class Operation;

// =====================================================================
//  OperationRegistry — singleton registry of all available operations
// =====================================================================
//
//  All built-in operations register themselves here at startup.
//  The NodeBuilderUI queries this to populate the "Add Operation" dropdown.
//  CustomNode uses this to instantiate operations by name from serialized data.
//
class OperationRegistry
{
public:
	static OperationRegistry& Get()
	{
		static OperationRegistry instance;
		return instance;
	}

	// Register an operation factory by name.
	// The factory function creates a new instance of the operation.
	void Register(const std::string& name, const std::string& category, std::function<Operation*()> factory)
	{
		if (factories.find(name) != factories.end())
		{
			printf("[OperationRegistry] Warning: operation '%s' already registered, overwriting.\n", name.c_str());
		}
		factories[name] = factory;
		categoryMap[name] = category;
	}

	// Create a new instance of an operation by name.
	// Returns nullptr if the name is not registered.
	// Caller owns the returned pointer.
	Operation* Create(const std::string& name) const
	{
		auto it = factories.find(name);
		if (it == factories.end())
		{
			printf("[OperationRegistry] Error: unknown operation '%s'\n", name.c_str());
			return nullptr;
		}
		Operation* op = it->second();
		if (op) op->InitDefaults();
		return op;
	}

	// Get all registered operation names
	std::vector<std::string> GetAllNames() const
	{
		std::vector<std::string> names;
		names.reserve(factories.size());
		for (const auto& pair : factories)
			names.push_back(pair.first);
		return names;
	}

	// Get all unique category names
	std::vector<std::string> GetCategories() const
	{
		std::vector<std::string> cats;
		std::map<std::string, bool> seen;
		for (const auto& pair : categoryMap)
		{
			if (!seen[pair.second])
			{
				cats.push_back(pair.second);
				seen[pair.second] = true;
			}
		}
		return cats;
	}

	// Get all operation names in a specific category
	std::vector<std::string> GetNamesByCategory(const std::string& category) const
	{
		std::vector<std::string> names;
		for (const auto& pair : categoryMap)
		{
			if (pair.second == category)
				names.push_back(pair.first);
		}
		return names;
	}

	// Get category for a specific operation
	std::string GetCategory(const std::string& name) const
	{
		auto it = categoryMap.find(name);
		if (it != categoryMap.end()) return it->second;
		return "Unknown";
	}

	// Check if an operation is registered
	bool IsRegistered(const std::string& name) const
	{
		return factories.find(name) != factories.end();
	}

private:
	OperationRegistry() = default;
	~OperationRegistry() = default;
	OperationRegistry(const OperationRegistry&) = delete;
	OperationRegistry& operator=(const OperationRegistry&) = delete;

	std::map<std::string, std::function<Operation*()>> factories;
	std::map<std::string, std::string> categoryMap; // op name -> category
};

// =====================================================================
//  Helper macro for registration — use in .cpp files
// =====================================================================
//
//  Usage (at file scope in an operation's .cpp):
//    REGISTER_OPERATION(MeshOp_Subdivide, "Subdivide", "Mesh")
//
//  This creates a static initializer that registers the operation
//  before main() runs.
//
#define REGISTER_OPERATION(ClassName, OpName, CategoryName) \
	static bool _reg_##ClassName = []() { \
		OperationRegistry::Get().Register(OpName, CategoryName, []() -> Operation* { return new ClassName(); }); \
		return true; \
	}();
