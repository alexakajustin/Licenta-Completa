#pragma once

#include <vector>
#include <string>
#include <map>
#include <filesystem>
#include <future>

struct TextureLoadData {
	unsigned char* data;
	int width;
	int height;
	int bitDepth;
	std::string path;
};

#include <GL/glew.h>
#include "imgui.h"
#include "Texture.h"
#include "Shader.h"
#include "Model.h"

class SceneManager;

enum class AssetType {
	Folder,
	Texture,
	Model,
	MaterialAsset,
	Other
};

struct AssetInfo {
	std::string name;
	std::filesystem::path path;
	AssetType type;
	Texture* thumbnail = nullptr;
};

#include "EditorUI.h"

class AssetBrowser
{
public:
	AssetBrowser();
	~AssetBrowser();

	void Init();
	void Render(SceneManager& scene, EditorUI::WindowState& uiState);
	void RefreshAssetList();

private:
	void LoadAssetIcons();
	void InitThumbnailFBO();
	void CleanupThumbnailFBO();
	bool GenerateModelThumbnail(const std::filesystem::path& modelPath, Texture* targetSlot);
	void GenerateMaterialThumbnail(const std::string& matPath, Texture* targetSlot);

	// State
	std::filesystem::path currentAssetPath;
	std::filesystem::path selectedAssetPath;
	std::vector<AssetInfo> currentAssets;
	std::map<std::string, Texture*> assetTextureCache;
	bool isOpen = true;

	std::vector<std::future<TextureLoadData*>> asyncTextureTasks;

	// Icons
	Texture* folderIconSlot = nullptr;
	Texture* modelIconSlot = nullptr;
	std::map<std::string, bool> thumbnailGenerationMap; // Paths that have ALREADY attempted/finished generation

	// Thumbnail resources
	GLuint thumbnailFBO = 0;
	GLuint thumbnailTexture = 0;
	GLuint thumbnailDepth = 0;
	Shader thumbnailShader;
	const int thumbnailSize = 128;
};
