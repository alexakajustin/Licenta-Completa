#include "GenerationStack.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "Texture.h"
#include "Material.h"

#include <cstdio>

GenerationStack::GenerationStack()
{
}

GenerationStack::~GenerationStack()
{
	Clear();
}

void GenerationStack::AddGenerator(IGenerator* gen)
{
	generators.push_back(gen);
}

void GenerationStack::RemoveGenerator(int index)
{
	if (index < 0 || index >= (int)generators.size()) return;
	delete generators[index];
	generators.erase(generators.begin() + index);
}

void GenerationStack::MoveUp(int index)
{
	if (index <= 0 || index >= (int)generators.size()) return;
	std::swap(generators[index], generators[index - 1]);
}

void GenerationStack::MoveDown(int index)
{
	if (index < 0 || index >= (int)generators.size() - 1) return;
	std::swap(generators[index], generators[index + 1]);
}

void GenerationStack::Execute(SceneManager& scene, Texture* defaultTex, Material* defaultMat)
{
	if (generators.empty()) return;

	// Remove previously generated objects from the scene
	for (auto& name : generatedObjectNames)
	{
		scene.RemoveObject(name);
	}
	generatedObjectNames.clear();

	// Execute the pipeline: chain generator outputs
	MeshData currentData;
	bool hasData = false;

	for (int i = 0; i < (int)generators.size(); i++)
	{
		MeshData result = generators[i]->Generate(hasData ? &currentData : nullptr);
		currentData = result;
		hasData = true;
	}

	// Convert final result to a scene object
	if (hasData && !currentData.vertices.empty())
	{
		Mesh* mesh = currentData.ToMesh();
		if (mesh)
		{
			// Generate a unique name
			static int genCounter = 0;
			std::string objName = "Generated_" + std::to_string(genCounter++);

			GameObject* newObj = new GameObject(objName);
			newObj->SetMesh(mesh);

			if (defaultTex) newObj->SetTexture(defaultTex);
			if (defaultMat) newObj->SetMaterial(defaultMat);

			scene.AddObject(newObj);
			generatedObjectNames.push_back(objName);

			printf("Generation complete: %s (%d verts, %d tris)\n",
				objName.c_str(), currentData.GetVertexCount(), currentData.GetTriangleCount());
		}
	}
}

void GenerationStack::Clear()
{
	for (auto* gen : generators)
	{
		delete gen;
	}
	generators.clear();
	generatedObjectNames.clear();
}
