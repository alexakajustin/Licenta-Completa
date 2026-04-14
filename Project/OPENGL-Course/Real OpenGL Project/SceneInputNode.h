#pragma once

#include "NodeGraph.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "PrimitiveGenerator.h"
#include "imgui.h"
#include <string>

// Picks a GameObject from the scene hierarchy and outputs its mesh data.
class SceneInputNode : public GraphNode
{
public:
	SceneInputNode(NodeGraph& graph);

	void RenderContent(SceneManager* scene) override;
	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override;

	json Serialize() const override;
	void Deserialize(const json& j) override;

	std::string GetSelectedName() const { return selectedName; }
	int GetSelectedIndex() const { return selectedIndex; }

	void SetSelection(int index, const std::string& name)
	{
		selectedIndex = index;
		selectedName = name;
	}

private:
	int selectedIndex = -1;
	std::string selectedName = "(none)";

	// Fallback Cache (Protects against user deleting the base object)
	std::string cachedPrimitiveType = "";
	std::string cachedModelPath = "";
	glm::vec3 cachedPosition = glm::vec3(0.0f);
	glm::vec3 cachedRotation = glm::vec3(0.0f);
	glm::vec3 cachedScale = glm::vec3(1.0f);

	std::string cachedTexturePath = "";
	std::string cachedNormalMapPath = "";
	
	std::vector<TextureLayer> cachedTextureLayers;

	bool hasCachedMaterial = false;
	float cachedMatSpecular = 0.5f;
	float cachedMatShininess = 32.0f;
	glm::vec3 cachedMatColor = glm::vec3(1.0f);
	glm::vec2 cachedMatTiling = glm::vec2(1.0f);
	glm::vec2 cachedMatOffset = glm::vec2(0.0f);

	// Instances created purely for fallback
	Texture* fallbackTexture = nullptr;
	Texture* fallbackNormalMap = nullptr;
	Material* fallbackMaterial = nullptr;
	
	std::vector<Texture*> fallbackLayersTextures;
	std::vector<Texture*> fallbackLayersNormals;

	~SceneInputNode(); // Need destructor to clean up fallback memory
	void CleanupFallbacks();
};
