#pragma once

#include <string>
#include <glm/glm.hpp>
#include <GL/glew.h>

#include "imgui.h"
#include "Shader.h"
#include "Mesh.h"
#include "Texture.h"

struct SSAOSettings;

class SceneManager;
class Camera;
class GameObject;
class NodeGraph;
class InputHandler;

class EditorUI
{
public:
	EditorUI();
	~EditorUI();

	// Draw all editor panels (hierarchy + inspector)
	void Render(SceneManager& scene, const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos, GLuint sceneTextureID, Camera* camera = nullptr, const InputHandler* inputHandler = nullptr);

	// Viewport metadata update (to eliminate 1-frame lag)
	void UpdateViewportMetadata();

	// Scene file actions (signaled from menu bar, handled by Application)
	enum class SceneAction { None, New, Save, Load };
	SceneAction GetPendingSceneAction() const { return pendingSceneAction; }
	const std::string& GetPendingScenePath() const { return pendingScenePath; }
	void ClearPendingSceneAction() { pendingSceneAction = SceneAction::None; pendingScenePath.clear(); }

	void RenderMainMenuBar(SceneManager& scene, NodeGraph& nodeGraph);

private:
	void RenderHierarchy(SceneManager& scene, int bufferHeight, Camera* camera = nullptr);
	void RenderHierarchyRecursive(SceneManager& scene, GameObject* obj, int index, Camera* camera);
	void RenderInspector(SceneManager& scene, int bufferWidth, int bufferHeight);
	void RenderViewport(SceneManager& scene, const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos, GLuint textureID, const InputHandler* inputHandler = nullptr);

	// Helper: Unity-style Vector3 input 
	static void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float speed = 0.1f);

	// Helper: handle ASSET_PATH drag-drop (DRY — used by hierarchy, inspector, and viewport)
	static void HandleAssetDrop(SceneManager& scene, glm::vec3 spawnPos = glm::vec3(0.0f));

public:
	struct WindowState {
		bool isHierarchyOpen = true;
		bool isInspectorOpen = true;
		bool isAssetBrowserOpen = true;
		bool isNodeEditorOpen = true;
		bool isDebugOverlayOpen = true;
		bool isNodeBuilderOpen = false; // Node Builder panel (off by default)
		bool isSSAOSettingsOpen = false; // SSAO settings panel (off by default)
		bool forceLayout = false;

		// Constraints
		const float minPanelWidth = 100.0f;
		const float minPanelHeight = 50.0f;

		// Dynamic layout memory to persist manual user resizing OS window changes
		float leftWidth = 260.0f;
		float rightWidth = 450.0f;
		float leftHeightRatio = 0.4f; // Hierarchy 40%, Inspector 60%
		float midHeightRatio = 0.75f; // Scene 75%, Project 25%
		float rightHeightRatio = 0.75f; // Node Editor 75%, CPU Debug 25%

		bool skipLayoutSave = false;
		int activeSplitterID = -1; // -1: none, 0: Left, 1: Right, 2: HorizLeft, 3: HorizMid
		int maximizedWindowID = -1; // -1: none, 0: Hierarchy, 1: Scene, 2: Inspector, 3: Project, 4: Node Editor, 5: Debug

		// Check if title bar was double clicked to maximize/minimize
		void CheckMaximize(int windowID);
	} windowState;

	// Centralized layout logic (Early frame)


	void UpdateLayoutLogic();

	// Centralized layout visuals (Late frame)
	void UpdateLayoutVisual();

	WindowState& GetWindowState() { return windowState; }



	// Viewport Info for InputHandler
	glm::vec2 GetViewportPos() const { return viewportPos; }
	glm::vec2 GetViewportSize() const { return viewportSize; }
	bool IsViewportHovered() const { return viewportHovered; }

	// SSAO settings link
	void SetSSAOSettings(SSAOSettings* s) { ssaoSettingsPtr = s; }
	void RenderSSAOSettings();

private:
	glm::vec2 viewportPos = glm::vec2(0.0f);
	glm::vec2 viewportSize = glm::vec2(1.0f, 1.0f);
	bool viewportHovered = false;
	SSAOSettings* ssaoSettingsPtr = nullptr;

	// Scene file action state
	SceneAction pendingSceneAction = SceneAction::None;
	std::string pendingScenePath;

private:

	// Material preview sphere
	void InitMaterialPreview();
	void RenderMaterialPreview(float specular, float shininess, glm::vec3 color, Texture* diffuse, Texture* normal, glm::vec2 tiling = glm::vec2(1,1), glm::vec2 offset = glm::vec2(0,0));
	GLuint previewFBO = 0;
	GLuint previewTexture = 0;
	GLuint previewDepth = 0;
	Shader previewShader;
	Mesh* previewSphere = nullptr;
	bool previewInitialized = false;
	static const int PREVIEW_SIZE = 128;
};
