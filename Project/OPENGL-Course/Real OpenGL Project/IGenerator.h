#pragma once

#include <string>
#include "MeshData.h"

// Abstract base class for all procedural generators.
// Each generator can optionally receive input from the previous generator in the stack.
class IGenerator
{
public:
	virtual ~IGenerator() = default;

	// Display name shown in the Pipeline UI
	virtual std::string GetName() const = 0;

	// Render ImGui parameter controls for this generator
	virtual void RenderUI() = 0;

	// Execute the generator.
	// 'input' is nullptr for the first generator in the stack,
	// or the output of the previous generator otherwise.
	virtual MeshData Generate(const MeshData* input) = 0;
};
