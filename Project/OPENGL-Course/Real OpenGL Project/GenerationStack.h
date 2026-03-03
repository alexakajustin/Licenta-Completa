#pragma once

#include <vector>
#include <string>
#include "IGenerator.h"

class SceneManager;
class Texture;
class Material;

// Manages an ordered list of generators and executes them as a pipeline.
// Each generator's output becomes the next generator's input.
class GenerationStack
{
public:
	GenerationStack();
	~GenerationStack();

	void AddGenerator(IGenerator* gen);
	void RemoveGenerator(int index);
	void MoveUp(int index);
	void MoveDown(int index);

	// Execute all generators in order and add the result to the scene.
	// Clears previously generated objects before adding new ones.
	void Execute(SceneManager& scene, Texture* defaultTex, Material* defaultMat);

	std::vector<IGenerator*>& GetGenerators() { return generators; }
	int GetGeneratorCount() const { return (int)generators.size(); }

	void Clear();

private:
	std::vector<IGenerator*> generators;

	// Track names of objects we generated so we can remove them on re-execute
	std::vector<std::string> generatedObjectNames;
};
