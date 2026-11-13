#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <filesystem>
#include <functional>
#include <type_traits>

#include "imgui.h"
#include "Editor/ScriptAnalysis.h"
#include "Editor/ScriptReference.h"

class SceneManager;
class GameObject;
class TextEditor;

/**
 * @class ScriptEditor
 * @brief Lua code editor window.
 *
 * - Syntax highlighted editor (ImGuiColorTextEdit) with tabs, find / replace, undo, zoom.
 * - Per-script compiler: every edit is checked (debounced) with the real Lua parser plus engine
 *   aware checks (unknown members, missing scene objects / assets / scripts...). Problems show as
 *   squiggles, line markers and in the Problems panel.
 * - Type-aware autocomplete, hover help and signature help, driven by ScriptApi metadata.
 * - Drag & drop: objects, transforms, materials, texture layers, components, other scripts and
 *   asset files can be dropped onto a line; Ctrl+drop inserts the expression at the exact spot.
 * - The window can leave the main window (multi-viewport) and live on another monitor.
 */
class ScriptEditor
{
public:
	struct ScriptTab
	{
		std::string path;          // Path on disk (e.g., "Assets/scripts/player.lua"), empty if untitled
		std::string title;         // Tab title (e.g., "player.lua", "untitled_1.lua")
		std::string buffer;        // Script text (kept in sync with the text editor)
		bool isDirty = false;      // Unsaved modifications flag
		int cursorPos = 0;         // Text caret offset
		int cursorLine = 1;        // 1-based line
		int cursorColumn = 1;      // 1-based column
		int selectionStart = 0;    // Selection start offset
		int selectionEnd = 0;      // Selection end offset

		// Accessors & helpers
		const std::string& GetPath() const { return path; }
		const std::string& GetTitle() const { return title; }
		const std::string& GetBuffer() const { return buffer; }
		const std::string& GetContent() const { return buffer; }
		bool IsDirty() const { return isDirty; }

		bool Save(const std::string& targetPath = "");
		bool Load(const std::string& targetPath);
		void InsertText(size_t pos, const std::string& text);
		void AppendText(const std::string& text);

		// --- editor state (created when the tab is first shown) ---
		int uid = 0;                         // stable ImGui id across reorders / reallocations
		std::shared_ptr<TextEditor> editor;
		std::string savedContent;            // content on disk, for the dirty flag
		bool pushToEditor = false;           // buffer changed through the API: refresh the editor
		size_t lastUndoIndex = (size_t)-1;
		std::filesystem::file_time_type diskTime{};

		ScriptAnalysis::DocumentModel model;
		ScriptAnalysis::CompileResult compile;
		bool compiled = false;
		double compileDue = -1.0;            // ImGui time when the debounced compile runs

		void MarkChanged();                  // after the buffer changed
	};

	ScriptEditor();
	~ScriptEditor();

	void Init();

	// Primary render entry point
	void Render(SceneManager& scene, bool& isOpen);

	// Template overload to accept EditorUI::WindowState or any struct with isScriptEditorOpen
	template <typename T>
	void Render(SceneManager& scene, T& windowState)
	{
		if constexpr (std::is_same_v<std::decay_t<T>, bool>)
		{
			Render(scene, windowState);
		}
		else
		{
			Render(scene, windowState.isScriptEditorOpen);
		}
	}

	// Document & Tab Management
	int NewScript(const std::string& initialContent = "");
	int OpenFile(const std::string& filepath);
	bool SaveActiveScript();
	bool SaveScriptAs(int tabIndex = -1);
	bool SaveAll();
	void CloseTab(int tabIndex);
	void CloseActiveTab();

	// State queries
	int GetActiveTabIndex() const { return m_ActiveTabIndex; }
	void SetActiveTabIndex(int index);
	size_t GetTabCount() const { return m_Tabs.size(); }
	ScriptTab* GetActiveTab();
	const ScriptTab* GetActiveTab() const;
	ScriptTab* GetTab(int index);
	const ScriptTab* GetTab(int index) const;
	std::vector<ScriptTab>& GetTabs() { return m_Tabs; }
	const std::vector<ScriptTab>& GetTabs() const { return m_Tabs; }

	// Inserts at the caret; a statement (ending with '\n') goes on its own line
	void InsertAtCursor(const std::string& text);

	// Ask the editor to open a script from anywhere (Asset Browser, Inspector...). Handled on the
	// next Render, which also opens and focuses the window.
	static void RequestOpen(const std::string& path);
	static std::string NormalizePath(const std::string& path);
	static std::string SanitizeVarName(const std::string& name);
	static std::string FormatAssetSnippet(const std::string& rawPath, const std::string& varName = "");
	static std::string FormatGameObjectSnippet(const std::string& objName, const std::string& varName = "");

	// Open tabs, layout and visibility, restored on the next start (the window position itself is
	// kept by imgui.ini, in desktop coordinates: the editor comes back on the same monitor)
	void SaveSession() const;

	// Monospace font used for code (set by the application after loading fonts)
	static void SetCodeFont(ImFont* font) { s_CodeFont = font; }

private:
	struct CompletionState
	{
		bool open = false;
		bool manual = false;
		int selected = 0;
		bool scrollToSelected = false;
		ScriptAnalysis::CompletionContext ctx;
		std::vector<ScriptAnalysis::Completion> items;
		enum class Action { None, Up, Down, PageUp, PageDown, Accept, Close, Trigger } pending = Action::None;
	};

	struct AssetLists
	{
		std::vector<std::string> scripts, textures, materials, models, audio;
	};

	std::vector<ScriptTab> m_Tabs;
	int m_ActiveTabIndex = -1;
	int m_UntitledCounter = 1;
	bool m_FocusRequested = false;
	int m_PendingTabSelect = -1;
	int m_NextTabUid = 1;

	// Layout / view
	float m_ExplorerWidth = 250.0f;
	float m_ProblemsHeight = 140.0f;
	bool m_ShowExplorer = true;
	bool m_ShowProblems = true;
	float m_FontSize = 16.0f;
	bool m_PopOut = false;          // always its own OS window
	int m_PendingMonitor = -1;      // move the window to this monitor next frame
	char m_ExplorerFilter[128] = {};
	int m_ExplorerTab = 0;

	// Editor interaction state (active tab)
	CompletionState m_Completion;
	ImVec4 m_EditorRect = ImVec4(0, 0, 0, 0); // x, y, w, h of the code area (screen)
	bool m_EditorFocused = false;
	bool m_EditorHovered = false;
	std::vector<ImWchar> m_TypedThisFrame;
	int m_HoverLine = -1, m_HoverCol = -1;
	double m_HoverStart = 0.0;
	std::string m_HoverTitle, m_HoverBody;
	int m_FlashLine = -1;
	double m_FlashUntil = 0.0;
	int m_CloseConfirmTab = -1;

	// Project knowledge for the analysis
	ScriptAnalysis::Environment m_Env;
	double m_EnvBuiltAt = -100.0;
	double m_AssetsScannedAt = -100.0;
	std::future<AssetLists> m_AssetScan;
	double m_DiskCheckAt = 0.0;

	SceneManager* m_Scene = nullptr;

	// Saving: running copies of the script are reloaded (Play mode)
	void AfterSave(ScriptTab& tab);
	int HotReload(const std::string& path);
	std::string m_StatusMessage;
	ImVec4 m_StatusColor = ImVec4(1, 1, 1, 1);
	double m_StatusUntil = 0.0;

	// Session persistence
	bool m_RestoreOpen = false;
	bool m_LastOpen = false;
	std::string m_SessionSignature;
	double m_SessionCheckAt = 0.0;
	void LoadSession();
	std::string SessionSignature() const;

	static std::vector<std::string> s_PendingOpen;
	static ImFont* s_CodeFont;

	// UI layout subroutines
	void RenderMenuBar(SceneManager& scene);
	void RenderToolbar();
	void RenderExplorer(SceneManager& scene);
	void RenderSceneTree(SceneManager& scene);
	void RenderApiReference();
	void RenderTabBar();
	void RenderEditorCanvas(SceneManager& scene);
	void RenderProblems();
	void RenderStatusBar();
	void RenderCompletionPopup(ScriptTab& tab);
	void RenderHoverAndSignature(ScriptTab& tab);
	void RenderCloseConfirm();
	void HandleShortcuts();

	// Editor plumbing
	void EnsureEditor(ScriptTab& tab);
	void SyncFromEditor(ScriptTab& tab);
	void Recompile(ScriptTab& tab);
	void ApplyDiagnostics(ScriptTab& tab);
	void DrawTextOverlay(ScriptTab& tab, ImDrawList* drawList);
	void GoToLine(ScriptTab& tab, int line, int column = 0);

	// Completion
	bool InterceptKeys();
	void UpdateCompletion(ScriptTab& tab, bool textChanged);
	void OpenCompletion(ScriptTab& tab, bool manual);
	void AcceptCompletion(ScriptTab& tab);

	// Drag & drop of references into code
	struct DropTarget
	{
		int line = 0, column = 0;
		bool inlineExpression = false; // over code: the expression goes right there
		bool append = false;           // below the last line
		bool blankLine = false;
	};
	DropTarget ComputeDrop(ScriptTab& tab, ImVec2 mouse) const;
	std::string ResolveSelfReference(const std::string& expression, const std::string& objectName) const;
	bool PayloadForDrop(SceneManager& scene, const ImGuiPayload* payload, ScriptReference::Payload& out);
	void HandleDrop(SceneManager& scene, ScriptTab& tab);
	void InsertReference(ScriptTab& tab, const ScriptReference::Payload& ref, int line, int column, bool inlineExpression);
	std::string UniqueVariable(const ScriptTab& tab, const std::string& base) const;

	// Environment
	void RefreshEnvironment(SceneManager& scene, const ScriptTab* tab, bool force = false);
	static AssetLists ScanAssets();
	void CheckDiskChanges();
};
