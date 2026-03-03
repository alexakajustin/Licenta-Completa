#pragma once

#include "Window.h"
#include "Camera.h"
#include "Renderer.h"
#include "SceneManager.h"
#include "InputHandler.h"
#include "EditorUI.h"
#include "AssetBrowser.h"
#include "DebugOverlay.h"
#include "NodeGraph.h"
#include "NodeEditorUI.h"
#include "Texture.h"
#include "Material.h"
#include "DirectionalLight.h"
#include "PointLight.h"
#include "SpotLight.h"
#include "CommonValues.h"

class Application
{
public:
	Application();
	~Application();

	bool Init();
	void Run();
	void Shutdown();

private:
	void LoadResources();
	void SetupScene();
	void UpdateProjection();

	// Core systems
	Window mainWindow;
	Camera camera;
	Renderer renderer;
	SceneManager sceneManager;
	InputHandler inputHandler;
	EditorUI editorUI;
	AssetBrowser assetBrowser;
	DebugOverlay debugOverlay;
	NodeGraph nodeGraph;
	NodeEditorUI nodeEditorUI;

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
	void SetupDockSpace();
	GLuint viewportFBO = 0;
	GLuint viewportTexture = 0;
	GLuint viewportDepth = 0;

	// Models
	// (None currently hardcoded in Application)

	// Frame timing
	GLfloat deltaTime;
	GLfloat lastTime;

	// Projection
	glm::mat4 projection;
};
