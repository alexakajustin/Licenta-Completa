#pragma once

#include <GL/glew.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "imgui.h"
#include "Rendering/Shader.h"
#include "Rendering/Texture.h"
#include "Rendering/Model.h"
#include "Editor/EditorUI.h"

class SceneManager;

enum class AssetType {
	Folder,
	Texture,
	Model,
	MaterialAsset,
	Scene,
	Script,
	ShaderSource,
	Data,
	Other
};

// One file or folder shown in the browser
struct AssetInfo {
	std::string name;           // File name (UTF-8, for display)
	std::string lowerName;      // For search and sorting
	std::filesystem::path path; // As the engine stores it ("Assets\\Models\\tree.fbx")
	std::string pathStr;        // path.string(): drag payloads, thumbnail keys
	AssetType type = AssetType::Other;
	uintmax_t size = 0;
	int64_t modified = 0;       // File time (100 ns ticks since 1601)
};

// =====================================================================
// Project window: the files under Assets/
//
//  - Folder tree, clickable path, back/forward, grid or list (sortable), size slider, type filter
//  - Search through the current folder and everything below it (background thread)
//  - Thumbnails: textures decoded by a small pool of worker threads and shrunk to 128 px, models
//    and materials rendered on the GPU; all of them cached on disk (Cache/Thumbnails), so the
//    next session shows them at once without loading the models again. Only visible items ask
//    for one; least recently seen ones are freed past a budget.
//  - Drag into the scene (models), onto texture slots (textures), objects (materials), script
//    fields (Lua scripts); double click opens folders and scenes, other files in their app
//  - New folder / material, rename, delete (to the Recycle Bin), show in Explorer, copy path
// =====================================================================
class AssetBrowser
{
public:
	AssetBrowser();
	~AssetBrowser();

	void Init();
	void Render(SceneManager& scene, EditorUI::WindowState& uiState);
	void RefreshAssetList();

	// A scene the user chose to open from the browser (empty if none); handled by Application
	std::string TakeSceneOpenRequest();

private:
	// ---------------------------------------------------------------- listing
	std::filesystem::path root = "Assets";
	std::filesystem::path currentAssetPath;
	std::vector<AssetInfo> currentAssets;
	int64_t currentDirStamp = 0;
	double lastStampCheck = 0.0;
	std::vector<std::filesystem::path> history;
	int historyIndex = -1;
	std::map<std::string, std::vector<std::filesystem::path>> treeChildren; // Folder -> subfolders (lazily listed)
	bool revealInTree = true;

	void Navigate(const std::filesystem::path& dir, bool addToHistory = true);
	void ListCurrentDirectory();
	const std::vector<std::filesystem::path>& SubFolders(const std::filesystem::path& dir);

	// ---------------------------------------------------------------- view
	enum class SortKey { Name, Type, Size, Modified };
	SortKey sortKey = SortKey::Name;
	bool sortAscending = true;
	bool listView = false;
	float cellSize = 96.0f;
	int typeFilter = 0; // 0 = all, else (int)AssetType + 1
	bool showSceneData = false; // The .mesh / .inst files saved next to scenes
	std::vector<AssetInfo> displayAssets; // Filtered, sorted: what the grid shows this frame
	bool displayDirty = true;

	void RebuildDisplayList();
	void RenderToolbar();
	void RenderFolderTree();
	void RenderTreeNode(const std::filesystem::path& dir);
	void RenderGrid(SceneManager& scene);
	void RenderList(SceneManager& scene);
	void RenderStatusBar();
	void RenderPopups();
	void HandleItemInteraction(int index, bool hovered, bool clicked, bool doubleClicked);
	void ItemContextMenu(const AssetInfo& asset);
	void BeginItemDrag(const AssetInfo& asset);
	void ItemTooltip(const AssetInfo& asset);
	void DrawThumbnail(ImDrawList* dl, const AssetInfo& asset, ImVec2 min, ImVec2 max);

	// ---------------------------------------------------------------- selection, actions
	std::set<std::string> selected; // pathStr
	int anchorIndex = -1;
	bool windowFocused = false;
	void OpenAsset(const AssetInfo& asset);
	void RevealInExplorer(const std::filesystem::path& path);
	void StartRename(const AssetInfo& asset);
	void CreateFolder();
	void CreateMaterial();

	std::string renameTarget;
	char renameBuffer[256] = {};
	bool renameOpenRequest = false;
	std::vector<std::string> deleteTargets;
	bool deleteOpenRequest = false;
	std::string sceneToConfirm;
	bool sceneOpenRequestPopup = false;
	std::string sceneOpenRequest;
	std::string errorMessage;
	bool errorOpenRequest = false;

	// ---------------------------------------------------------------- search
	char searchBuffer[256] = {};
	std::string activeQuery;
	bool searchDebouncePending = false;
	double searchChangedAt = 0.0;
	std::thread searchThread;
	std::atomic<bool> searchCancel{ false };
	std::atomic<bool> searchRunning{ false };
	std::mutex searchMutex;
	std::vector<AssetInfo> searchResultsAsync; // Written by the search thread
	int searchResultsAsyncGen = 0;
	int searchResultsShownGen = 0;
	std::vector<AssetInfo> searchResults;
	ImGuiID searchInputId = 0;
	bool IsSearching() const { return !activeQuery.empty(); }
	void ClearSearchBox();
	void StartSearch(const std::string& query);
	void StopSearch();

	// ---------------------------------------------------------------- thumbnails
	enum class ThumbState { None, Queued, NeedsRender, Ready, Failed };
	struct Thumb {
		ThumbState state = ThumbState::None;
		GLuint tex = 0;
		int width = 0, height = 0;       // The thumbnail
		int srcWidth = 0, srcHeight = 0; // The texture it shows (0 for models, materials)
		double lastSeen = 0.0;
		bool lowMemory = false;
	};
	struct ThumbJob { std::string key, cacheFile; AssetType type; };
	struct ThumbResult { std::string key; bool ok = false; bool cacheMiss = false; int w = 0, h = 0, srcW = 0, srcH = 0; std::vector<unsigned char> rgba; };
	std::unordered_map<std::string, Thumb> thumbs;
	struct CacheName { int64_t modified = 0; uintmax_t size = 0; std::string file; };
	std::unordered_map<std::string, CacheName> cacheFiles; // key -> disk cache file of its current version
	std::deque<ThumbJob> jobs;
	std::vector<ThumbResult> results;
	std::mutex jobMutex, resultMutex;
	std::condition_variable jobSignal;
	std::vector<std::thread> workers;
	std::atomic<bool> stopWorkers{ false };
	bool renderedThisFrame = false;
	double lastMemoryCheck = 0.0;
	bool memoryLow = false;

	void StartWorkers();
	void WorkerLoop();
	void RequestThumbnail(const AssetInfo& asset);
	void UploadResults();
	void EvictThumbnails();
	void ForgetThumbnail(const std::string& key);
	std::string CacheFileFor(const AssetInfo& asset);
	GLuint UploadRGBA(const unsigned char* rgba, int w, int h);
	bool RenderModelThumbnail(const AssetInfo& asset, Thumb& thumb);
	bool RenderMaterialThumbnail(const AssetInfo& asset, Thumb& thumb);
	bool ReadBackThumbnail(Thumb& thumb, const std::string& cacheFile);

	// Offscreen target for model and material thumbnails
	void InitThumbnailFBO();
	void CleanupThumbnailFBO();
	GLuint thumbnailFBO = 0;
	GLuint thumbnailTexture = 0;
	GLuint thumbnailDepth = 0;
	Shader thumbnailShader;
	static constexpr int THUMB_SIZE = 128;
};
