#define STB_IMAGE_IMPLEMENTATION
#define NOMINMAX
#include <Windows.h>
#include "Application.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <cmath>

#include "Model.h"
#include "Mesh.h"
#include "LightObject.h"
#include "DebugOverlay.h"
#include "External Libs/imnodes/imnodes.h"
#include "PrimitiveGenerator.h"
#include "AssetManager.h"
#include "AllOperations.h"
#include "SceneSerializer.h"

Application::Application()
	: pointLightCount(0), spotLightCount(0),
	  deltaTime(0.0f), lastTime(0.0f),
	  projection(1.0f)
{
}

Application::~Application()
{
}

bool Application::Init()
{
	// Window
	mainWindow = Window(1920, 1080);
	mainWindow.Initialise();

	// Camera
	camera = Camera(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f, 1.0f, 0.25f);

	// Renderer (shaders + skybox)
	renderer.Init();
	sceneManager.SetMainShader(&renderer.GetMainShader());

	std::vector<std::string> skyboxFaces;
	skyboxFaces.push_back("Assets/Textures/Skybox/cupertin-lake_rt.tga");
	skyboxFaces.push_back("Assets/Textures/Skybox/cupertin-lake_lf.tga");
	skyboxFaces.push_back("Assets/Textures/Skybox/cupertin-lake_up.tga");
	skyboxFaces.push_back("Assets/Textures/Skybox/cupertin-lake_dn.tga");
	skyboxFaces.push_back("Assets/Textures/Skybox/cupertin-lake_bk.tga");
	skyboxFaces.push_back("Assets/Textures/Skybox/cupertin-lake_ft.tga");
	renderer.LoadSkybox(skyboxFaces);

	// Resources
	LoadResources();

	// Scene
	SetupScene();

	// Projection
	UpdateProjection();

	// ImGui
	// Initialize ImGui
	ImGui::CreateContext();
	ImNodes::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
	// io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Missing in this ImGui branch
	
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(mainWindow.getWindow(), true);
	ImGui_ImplOpenGL3_Init("#version 330");

	// Viewport FBO initialization
	InitViewportFBO();

	// Enable VSync — caps FPS to monitor refresh rate, prevents GPU from running at 100%
	glfwSwapInterval(1);

	// Initialize editor subsystems
	sceneManager.InitPicking((int)mainWindow.getBufferWidth(), (int)mainWindow.getBufferHeight());
	sceneManager.InitIcons();
	sceneManager.InitGizmo();
	assetBrowser.Init();

	sceneManager.SetLightArrays(pointLights, &pointLightCount, spotLights, &spotLightCount);

	return true;
}

void Application::LoadResources()
{
	plainTexture = Texture("Assets/Textures/grid_prototype.png");
	plainTexture.LoadTextureA();

	plainMaterial = Material(0.1f, 32.0f);
	plainMaterial.SetTiling(glm::vec2(10.0f, 10.0f));

	sceneManager.SetDefaultResources(&plainTexture, &plainMaterial);

	// Initial Directional Light - 4096 resolution for large landscapes
	mainLight = DirectionalLight(4096, 4096,
		1.0f, 1.0f, 1.0f,
		0.4f, 0.6f,
		-10.0f, -5.0f, 20.0f);
	// Focused frustum for better shadow resolution (30 units area)
	mainLight.SetShadowFrustum(30.0f, 0.1f, 200.0f);
	spotLightCount = 0;
}

void Application::SetupScene()
{
	// Setup default scene: Directional Light + 20x1x20 Plane
	
	// Create Plane
	GameObject* plane = new GameObject("Plane");
	plane->GetTransform().SetScale(glm::vec3(1000.0f, 1.0f, 1000.0f));
	// Set position to 0, -500, 0
	plane->GetTransform().SetPosition(glm::vec3(0.0f, -500.0f, 0.0f));
	plane->SetMesh(PrimitiveGenerator::CreatePlane());
	plane->SetPrimitiveType("Plane");
	sceneManager.AddObject(plane);

	// Create Directional Light
	LightObject* sun = new LightObject("Sun", &mainLight);
	sceneManager.AddLight(sun);

	pointLightCount = 0;
	spotLightCount = 0;
}

void Application::UpdateProjection()
{
	projection = glm::perspective(glm::radians(60.0f),
		(GLfloat)mainWindow.getBufferWidth() / (GLfloat)mainWindow.getBufferHeight(),
		0.1f, 1000.0f);
}

void Application::Run()
{
	while (!mainWindow.getShouldClose())
	{
		// Input and Window system events
		glfwPollEvents();

		// Timing
		GLfloat now = (GLfloat)glfwGetTime();
		deltaTime = now - lastTime;
		lastTime = now;

		AssetManager::Get().Update();

		// Debug overlay timing
		debugOverlay.BeginFrame();
		debugOverlay.ResetCounters();

		// ImGui new frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		
		// Get physical framebuffer size (pixels) for 3D
		int fbw, fbh;
		glfwGetFramebufferSize(mainWindow.getWindow(), &fbw, &fbh);
		if (fbw == 0 || fbh == 0) { fbw = 1; fbh = 1; }

		// Get logical window size (screen units) for UI layout
		int ww, wh;
		glfwGetWindowSize(mainWindow.getWindow(), &ww, &wh);
		
		bool isMinimized = glfwGetWindowAttrib(mainWindow.getWindow(), GLFW_ICONIFIED);

		if (ww < 1000 || wh < 700 || isMinimized) {
			editorUI.GetWindowState().skipLayoutSave = true;
		} else {
			editorUI.GetWindowState().skipLayoutSave = false;
		}

		// Layout Responsiveness: If main window size changed, force UI items back to their relative positions
		if (fbw != lastWindowWidth || fbh != lastWindowHeight)
		{
			if (!isMinimized && fbw > 0 && fbh > 0) {
				editorUI.GetWindowState().forceLayout = true;
			}
			lastWindowWidth = fbw;
			lastWindowHeight = fbh;
		}

		// Update viewport metadata (size/pos) AFTER potential resize detection but BEFORE rendering 3D
		editorUI.UpdateViewportMetadata();

		inputHandler.UpdateCamera(mainWindow, camera, deltaTime);

		// Debug info
		debugOverlay.SetCameraInfo(camera.getCameraPosition(), camera.getCameraDirection());
		debugOverlay.SetSceneInfo((int)sceneManager.GetObjects().size(), (int)sceneManager.GetLights().size());
		debugOverlay.SetSelectionInfo(sceneManager.GetSelectedName());
		debugOverlay.SetViewportInfo(fbw, fbh);

		// Editor UI 
		// Since docking is not available, we use fixed window layout to emulate Unity
		
		// 1. Render Top Bar first
		editorUI.RenderMainMenuBar(sceneManager, sceneManager.GetNodeGraph());

		// Handle pending scene file actions
		auto sceneAction = editorUI.GetPendingSceneAction();
		if (sceneAction != EditorUI::SceneAction::None)
		{
			if (sceneAction == EditorUI::SceneAction::Save)
			{
				SceneSerializer::SaveScene(editorUI.GetPendingScenePath(), sceneManager);
			}
			else if (sceneAction == EditorUI::SceneAction::Load)
			{
				sceneManager.GetNodeGraph().Clear();
				SceneSerializer::LoadScene(editorUI.GetPendingScenePath(), sceneManager,
					mainLight, pointLights, pointLightCount, spotLights, spotLightCount,
					&plainTexture, &plainMaterial);
			}
			else if (sceneAction == EditorUI::SceneAction::New)
			{
				sceneManager.GetNodeGraph().Clear();
				sceneManager.Clear();
				pointLightCount = 0;
				spotLightCount = 0;
				SetupScene();
			}
			editorUI.ClearPendingSceneAction();
		}

		EditorUI::WindowState& uiState = editorUI.GetWindowState();

		// ... (EditorUI handles its own windows)

		// Update projection matrix based on FRESH viewport info
		glm::vec2 vSize = editorUI.GetViewportSize();
		int vWidth = (int)vSize.x;
		int vHeight = (int)vSize.y;

		// Minimal safety
		if (vWidth < 1) vWidth = 1;
		if (vHeight < 1) vHeight = 1;

		ResizeViewportFBO(vWidth, vHeight);

		float aspect = (float)vWidth / (float)vHeight;
		projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 2000.0f);
		glm::mat4 view = camera.calculateViewMatrix();

		// Main render pass — clear backbuffer (the part around the UI)
		glViewport(0, 0, fbw, fbh);
		glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Shadow passes (use main window resolution or fixed size for shadows)
		renderer.DirectionalShadowMapPass(&mainLight, sceneManager, camera.getCameraPosition());
		for (unsigned int i = 0; i < pointLightCount; i++)
			renderer.OmniShadowMapPass(&pointLights[i], sceneManager);
		for (unsigned int i = 0; i < spotLightCount; i++)
			renderer.OmniShadowMapPass(&spotLights[i], sceneManager);

		// Final Scene Render (Viewport FBO)
		glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
		glViewport(0, 0, currentViewportWidth, currentViewportHeight);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		renderer.RenderPass(projection, view, camera.getCameraPosition(), sceneManager,
			mainLight, pointLights, pointLightCount, spotLights, spotLightCount, currentViewportWidth, currentViewportHeight);
		
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// Now render ImGui windows using centralized states over the scene
		editorUI.Render(sceneManager, projection, view, camera.getCameraPosition(), viewportTexture, &camera);
		
		assetBrowser.Render(sceneManager, uiState);
		nodeEditorUI.Render(sceneManager.GetNodeGraph(), sceneManager, &plainTexture, &plainMaterial, uiState);
		nodeBuilderUI.Render(sceneManager.GetNodeGraph(), uiState);

		// Editor picking & gizmo (AFTER UI so "Scene" window exists)
		inputHandler.UpdateEditor(mainWindow, camera, sceneManager, projection, editorUI);

		glUseProgram(0);

		// Debug overlay end timing
		debugOverlay.EndFrame();

		// Debug overlay + ImGui render
		debugOverlay.Render(uiState);
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		// Update projection for window resize
		UpdateProjection();

		mainWindow.swapBuffers();

		// Reset forceLayout after ALL ImGui panels have rendered for this frame
		editorUI.GetWindowState().forceLayout = false;
	}
}

void Application::InitViewportFBO()
{
	int w = (int)mainWindow.getBufferWidth();
	int h = (int)mainWindow.getBufferHeight();
	if (w < 1) w = 1;
	if (h < 1) h = 1;

	currentViewportWidth = w;
	currentViewportHeight = h;

	glGenFramebuffers(1, &viewportFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);

	glGenTextures(1, &viewportTexture);
	glBindTexture(GL_TEXTURE_2D, viewportTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, viewportTexture, 0);

	glGenRenderbuffers(1, &viewportDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, viewportDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, w, h);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, viewportDepth);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		printf("Viewport Framebuffer not complete!\n");

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Application::ResizeViewportFBO(int width, int height)
{
	if (width < 1) width = 1;
	if (height < 1) height = 1;

	if (width == currentViewportWidth && height == currentViewportHeight) return;

	currentViewportWidth = width;
	currentViewportHeight = height;

	// Update Texture
	glBindTexture(GL_TEXTURE_2D, viewportTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);

	// Update Depth Buffer
	glBindRenderbuffer(GL_RENDERBUFFER, viewportDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);

	printf("Viewport FBO resized to %dx%d\n", width, height);
}

void Application::SetupDockSpace()
{
    // Not supported without docking branch
}

void Application::Shutdown()
{
	if (viewportFBO) glDeleteFramebuffers(1, &viewportFBO);
	if (viewportTexture) glDeleteTextures(1, &viewportTexture);
	if (viewportDepth) glDeleteRenderbuffers(1, &viewportDepth);

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImNodes::DestroyContext();
	ImGui::DestroyContext();
}
