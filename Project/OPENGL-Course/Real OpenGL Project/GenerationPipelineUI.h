#pragma once

#include "imgui.h"

class GenerationStack;
class SceneManager;
class Texture;
class Material;

// ImGui panel for the procedural generation pipeline.
// Shows generator list, parameter editing, and execution controls.
class GenerationPipelineUI
{
public:
	GenerationPipelineUI();
	~GenerationPipelineUI();

	void Render(GenerationStack& stack, SceneManager& scene, Texture* defaultTex, Material* defaultMat);

private:
	int selectedGeneratorIndex;
	bool isOpen;
};
