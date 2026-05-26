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
	void LoadGraphicsSettings();
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

	// SSAO
	void InitSSAO();
	void RenderQuad();
	
	GLuint ssaoFBO = 0, ssaoBlurFBO = 0;
	GLuint ssaoColorBuffer = 0, ssaoColorBufferBlur = 0;
	GLuint noiseTexture = 0;
	std::vector<glm::vec3> ssaoKernel;

	Shader ssaoShader;
	Shader ssaoBlurShader;
	Shader ssaoApplyShader;
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
