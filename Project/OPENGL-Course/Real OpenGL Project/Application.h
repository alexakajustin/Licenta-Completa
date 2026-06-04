#pragma once

#include "GraphicsSettings.h"


#include "Core/Window.h"
#include "Core/Camera.h"
#include "Rendering/Renderer.h"
#include "Scene/SceneManager.h"
#include "Core/InputHandler.h"
#include "Editor/EditorUI.h"
#include "Editor/AssetBrowser.h"
#include "Editor/DebugOverlay.h"
#include "Nodes/NodeGraph.h"
#include "Editor/NodeEditorUI.h"
#include "Editor/NodeBuilderUI.h"
#include "Rendering/Texture.h"
#include "Rendering/Material.h"
#include "Lighting/DirectionalLight.h"
#include "Lighting/PointLight.h"
#include "Lighting/SpotLight.h"
#include "CommonValues.h"
#include "Rendering/Shader.h"
#include <vector>
#include <unordered_map>

/**
 * @class Application
 * @brief Main engine application controller managing systems lifecycle, rendering loop, and UI.
 */
class Application
{
public:
	/**
	 * @brief Constructor initializes basic timing fields.
	 */
	Application();

	/**
	 * @brief Destructor shuts down subsystem components.
	 */
	~Application();

	/**
	 * @brief Initializes the application window, OpenGL context, renderer, scene, and ImGui.
	 * @return True if initialization succeeded, false otherwise.
	 */
	bool Init();

	/**
	 * @brief Starts and runs the main loop until the window is closed.
	 */
	void Run();

	/**
	 * @brief Performs cleanup of OpenGL assets and subsystems.
	 */
	void Shutdown();

private:
	// ========== Play Mode ==========
	enum class PlayState { EditMode, PlayMode };
	PlayState playState = PlayState::EditMode;
	int lastActiveViewportTab = -1;

	void StartPlayMode();
	void StopPlayMode();

	// Transform backup for restoring scene state after Play Mode
	struct TransformBackup {
		glm::vec3 position;
		glm::vec3 rotation;
		glm::vec3 scale;
	};
	std::unordered_map<GameObject*, TransformBackup> transformBackups;

	// Game camera (drives the player's view during play mode)
	Camera gameCamera;
	/**
	 * @brief Loads default textures and setups default resources.
	 */
	void LoadResources();

	/**
	 * @brief Creates the initial game object scene hierarchy and lights.
	 */
	void SetupScene();

	/**
	 * @brief Updates the projection matrix based on current window dimensions.
	 */
	void UpdateProjection();

	/**
	 * @brief Loads graphics options from the graphics_settings.json file.
	 */
	void LoadGraphicsSettings();

	/**
	 * @brief Saves current graphics settings to graphics_settings.json.
	 */
	void SaveGraphicsSettings();


	// Core systems
	Window mainWindow;
	Camera camera;
	Renderer renderer;
	SceneManager sceneManager;
	InputHandler inputHandler;
	EditorUI editorUI;
	AssetBrowser assetBrowser;
	DebugOverlay debugOverlay;
	NodeEditorUI nodeEditorUI;
	NodeBuilderUI nodeBuilderUI;

	// Resources
	Texture plainTexture;
	Material plainMaterial;

	// Lighting
	DirectionalLight mainLight;
	PointLight pointLights[MAX_POINT_LIGHTS];
	SpotLight spotLights[MAX_SPOT_LIGHTS];
	unsigned int pointLightCount;
	unsigned int spotLightCount;

	// Viewport FBO
	void InitViewportFBO();
	void ResizeViewportFBO(int width, int height);
	void SetupDockSpace();
	void SetupModernTheme();
	void SetupModernNodeTheme();
	GLuint viewportFBO = 0;
	GLuint viewportTexture = 0;
	GLuint viewportDepth = 0;
	int currentViewportWidth = 0;
	int currentViewportHeight = 0;

	// Game Viewport FBO (for the player camera during Play Mode)
	void InitGameViewportFBO();
	void ResizeGameViewportFBO(int width, int height);
	GLuint gameViewportFBO = 0;
	GLuint gameViewportTexture = 0;
	GLuint gameViewportDepth = 0;
	int currentGameViewportWidth = 0;
	int currentGameViewportHeight = 0;

	// Water Reflection FBO
	void InitReflectionFBO();
	void ResizeReflectionFBO(int width, int height);
	GLuint reflectionFBO = 0;
	GLuint reflectionTexture = 0;
	GLuint reflectionDepth = 0;
	int reflectionWidth = 0;
	int reflectionHeight = 0;

	// Water Refraction FBO
	void InitRefractionFBO();
	void ResizeRefractionFBO(int width, int height);
	GLuint refractionFBO = 0;
	GLuint refractionTexture = 0;
	GLuint refractionDepth = 0;
	int refractionWidth = 0;
	int refractionHeight = 0;

	// Post-processing
	void InitPostEffects();
	void RenderQuad();
	
	Shader godrayShader;
	Shader volumetricSkyShader;
	Shader universeSkyShader;

	GLuint quadVAO = 0;
	GLuint quadVBO = 0;

public:
	GraphicsSettings& GetGraphicsSettings() { return graphicsSettings; }
private:
	GraphicsSettings graphicsSettings;

	// Models
	// (None currently hardcoded in Application)

	// Frame timing
	GLfloat deltaTime;
	GLfloat lastTime;

	// Window tracking
	int lastWindowWidth = 0;
	int lastWindowHeight = 0;

	// Projection
	glm::mat4 projection;
	
	// Debug Culling
	struct Frustum* activeFrustum = nullptr;
	struct Frustum frozenFrustum;
};
