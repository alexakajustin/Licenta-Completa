#include "GenerationPipelineUI.h"
#include "GenerationStack.h"
#include "IGenerator.h"
#include "PerlinNoiseGenerator.h"
#include "SceneManager.h"

GenerationPipelineUI::GenerationPipelineUI()
	: selectedGeneratorIndex(-1), isOpen(true)
{
}

GenerationPipelineUI::~GenerationPipelineUI()
{
}

void GenerationPipelineUI::Render(GenerationStack& stack, SceneManager& scene, Texture* defaultTex, Material* defaultMat)
{
	if (!isOpen) return;

	ImGui::Begin("Generation Pipeline", &isOpen);

	// ========== Add Generator ==========
	if (ImGui::Button("Add Generator"))
	{
		ImGui::OpenPopup("AddGeneratorPopup");
	}

	if (ImGui::BeginPopup("AddGeneratorPopup"))
	{
		if (ImGui::MenuItem("Perlin Noise"))
		{
			stack.AddGenerator(new PerlinNoiseGenerator());
		}
		// Future generators go here:
		// if (ImGui::MenuItem("Scatter")) { stack.AddGenerator(new ScatterGenerator()); }
		// if (ImGui::MenuItem("Shape Grammar")) { stack.AddGenerator(new ShapeGrammarGenerator()); }
		// if (ImGui::MenuItem("WFC")) { stack.AddGenerator(new WFCGenerator()); }
		ImGui::EndPopup();
	}

	ImGui::SameLine();

	// ========== Execute Button ==========
	bool hasGenerators = stack.GetGeneratorCount() > 0;
	if (!hasGenerators) ImGui::BeginDisabled();

	if (ImGui::Button("Execute Pipeline"))
	{
		stack.Execute(scene, defaultTex, defaultMat);
	}

	if (!hasGenerators) ImGui::EndDisabled();

	ImGui::SameLine();

	// ========== Clear All ==========
	if (!hasGenerators) ImGui::BeginDisabled();
	if (ImGui::Button("Clear All"))
	{
		stack.Clear();
		selectedGeneratorIndex = -1;
	}
	if (!hasGenerators) ImGui::EndDisabled();

	ImGui::Separator();

	// ========== Generator List ==========
	auto& generators = stack.GetGenerators();
	int count = (int)generators.size();

	if (count == 0)
	{
		ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No generators in pipeline.\nClick 'Add Generator' to start.");
	}
	else
	{
		ImGui::Text("Pipeline (%d generator%s):", count, count > 1 ? "s" : "");
		ImGui::Spacing();

		int removeIndex = -1;

		for (int i = 0; i < count; i++)
		{
			ImGui::PushID(i);

			bool isSelected = (selectedGeneratorIndex == i);

			// Step number label
			char label[128];
			snprintf(label, sizeof(label), "%d. %s", i + 1, generators[i]->GetName().c_str());

			if (ImGui::Selectable(label, isSelected))
			{
				selectedGeneratorIndex = i;
			}

			// Reorder and remove buttons (on same line)
			ImGui::SameLine(ImGui::GetWindowWidth() - 100);

			if (i > 0)
			{
				if (ImGui::SmallButton("^"))
				{
					stack.MoveUp(i);
					if (selectedGeneratorIndex == i) selectedGeneratorIndex = i - 1;
					else if (selectedGeneratorIndex == i - 1) selectedGeneratorIndex = i;
				}
			}
			else
			{
				ImGui::SmallButton(" "); // Placeholder for alignment
			}

			ImGui::SameLine();

			if (i < count - 1)
			{
				if (ImGui::SmallButton("v"))
				{
					stack.MoveDown(i);
					if (selectedGeneratorIndex == i) selectedGeneratorIndex = i + 1;
					else if (selectedGeneratorIndex == i + 1) selectedGeneratorIndex = i;
				}
			}
			else
			{
				ImGui::SmallButton(" "); // Placeholder
			}

			ImGui::SameLine();

			if (ImGui::SmallButton("X"))
			{
				removeIndex = i;
			}

			ImGui::PopID();
		}

		// Handle deferred removal
		if (removeIndex >= 0)
		{
			stack.RemoveGenerator(removeIndex);
			if (selectedGeneratorIndex >= (int)generators.size())
				selectedGeneratorIndex = (int)generators.size() - 1;
		}

		// ========== Selected Generator Parameters ==========
		if (selectedGeneratorIndex >= 0 && selectedGeneratorIndex < (int)generators.size())
		{
			ImGui::Separator();
			ImGui::Text("Parameters: %s", generators[selectedGeneratorIndex]->GetName().c_str());
			ImGui::Spacing();

			generators[selectedGeneratorIndex]->RenderUI();
		}
	}

	ImGui::End();
}
