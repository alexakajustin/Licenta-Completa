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
	skyboxFaces.push_back("Assets/Textures/Skybox_Cute/px.png");
	skyboxFaces.push_back("Assets/Textures/Skybox_Cute/nx.png");
	skyboxFaces.push_back("Assets/Textures/Skybox_Cute/py.png");
	skyboxFaces.push_back("Assets/Textures/Skybox_Cute/ny.png");
	skyboxFaces.push_back("Assets/Textures/Skybox_Cute/pz.png");
	skyboxFaces.push_back("Assets/Textures/Skybox_Cute/nz.png");
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
	
	// Load Modern System Font (Segoe UI) at 18px
	if (GetFileAttributesA("C:\\Windows\\Fonts\\segoeui.ttf") != INVALID_FILE_ATTRIBUTES) {
		io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
	} else {
		// Fallback: Just scale the default font
		io.FontGlobalScale = 1.15f; 
	}

	SetupModernTheme();
	ImNodes::StyleColorsDark();
	SetupModernNodeTheme();
	ImGui_ImplGlfw_InitForOpenGL(mainWindow.getWindow(), true);
	ImGui_ImplOpenGL3_Init("#version 460");

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
		-20.0f, -5.0f, 7.0f);
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
	plane->SetMesh(PrimitiveGenerator::CreatePlane(1, 1));
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
		
		// If cursor is captured for camera movement, tell ImGui to ignore mouse input
		// This prevents "hovering" over UI elements while flying around the scene
		ImGuiIO& io = ImGui::GetIO();
		if (!mainWindow.isCursorEnabled()) {
			io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
			io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX); // Teleport mouse far away so nothing is hovered
		} else {
			io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
		}

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

		// 1. Process Splitter Logic (Early frame for zero lag)
		editorUI.UpdateLayoutLogic();

		// 2. Update Viewport Metadata (Using fresh positions)
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

		// Define the progress callback for both loading projects and executing graphs
		std::string progressTitle = "Loading...";
		auto progressCallback = [&](float overallPct, float nodePct, const std::string& msg) {
			glfwPollEvents();
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			ImGui::SetNextWindowPos(ImVec2(mainWindow.getBufferWidth() * 0.5f, mainWindow.getBufferHeight() * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(450, 150));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 0.98f));
			ImGui::Begin("ProgressWindow", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
			ImGui::Text(progressTitle.c_str());
			ImGui::TextColored(ImVec4(0.61f, 0.31f, 0.91f, 1.0f), "%s", msg.c_str());

			ImGui::Spacing();
			char buf[32];
			snprintf(buf, sizeof(buf), "Overall: %d%%", (int)overallPct);
			ImGui::ProgressBar(overallPct / 100.0f, ImVec2(-1, 20), buf);

			ImGui::Spacing();
			snprintf(buf, sizeof(buf), "Detail: %d%%", (int)nodePct);
			ImGui::ProgressBar(nodePct / 100.0f, ImVec2(-1, 20), buf);

			ImGui::End();
			ImGui::PopStyleColor();
			ImGui::PopStyleVar();

			ImGui::Render();
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			// Purposefully DO NOT glClear so the app remains visible in the background
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			mainWindow.swapBuffers();
		};

		// forceLayout reset and sceneAction handling previously here were moved to the end of the loop.


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
		editorUI.Render(sceneManager, projection, view, camera.getCameraPosition(), viewportTexture, &camera, &inputHandler);
		
		assetBrowser.Render(sceneManager, uiState);
		bool executeGraph = nodeEditorUI.Render(sceneManager.GetNodeGraph(), sceneManager, &plainTexture, &plainMaterial, uiState);
		nodeBuilderUI.Render(sceneManager.GetNodeGraph(), uiState);


		// Editor picking & gizmo (AFTER UI so "Scene" window exists)

		inputHandler.UpdateEditor(mainWindow, camera, sceneManager, projection, editorUI);

		glUseProgram(0);

		// Debug overlay end timing
		debugOverlay.EndFrame();

		// Debug overlay + ImGui render
		debugOverlay.Render(uiState);

		// 3. Render Splitter Visuals (On top of EVERYTHING)
		editorUI.UpdateLayoutVisual();

		ImGui::Render();

		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		// Update projection for window resize
		UpdateProjection();

		mainWindow.swapBuffers();

		// Reset forceLayout after ALL ImGui panels have rendered for this frame
		editorUI.GetWindowState().forceLayout = false;

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
				progressTitle = "Loading Project...";
				sceneManager.GetNodeGraph().Clear();
				SceneSerializer::LoadScene(editorUI.GetPendingScenePath(), sceneManager,
					mainLight, pointLights, pointLightCount, spotLights, spotLightCount,
					&plainTexture, &plainMaterial, progressCallback);
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


		if (executeGraph) {
			progressTitle = "Executing Node Graph...";
			sceneManager.GetNodeGraph().Execute(sceneManager, &plainTexture, &plainMaterial, progressCallback);
		}
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

void Application::SetupModernTheme()
{
	auto& style = ImGui::GetStyle();
	auto& colors = style.Colors;

	// Modern Rounding 
	style.WindowRounding = 6.0f;
	style.ChildRounding = 4.0f;
	style.FrameRounding = 4.0f;
	style.PopupRounding = 4.0f;
	style.ScrollbarRounding = 9.0f;
	style.GrabRounding = 4.0f;
	style.TabRounding = 4.0f;
	style.WindowBorderSize = 0.0f;
	style.FramePadding = ImVec2(5, 5);

	// Cyber-Purple/Black Palette
	colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.09f, 0.98f); // Very dark purple-black
	colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.10f, 0.94f);
	colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.12f, 0.28f, 0.54f); // Subtle purple
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.39f, 0.19f, 0.61f, 0.40f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.39f, 0.19f, 0.61f, 0.67f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.25f, 0.15f, 0.35f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.08f, 0.18f, 1.00f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.61f, 0.31f, 0.91f, 1.00f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.61f, 0.31f, 0.91f, 1.00f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.71f, 0.41f, 1.00f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.25f, 0.15f, 0.35f, 1.00f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.47f, 0.23f, 0.71f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.57f, 0.33f, 0.81f, 1.00f);
	colors[ImGuiCol_Header] = ImVec4(0.20f, 0.12f, 0.28f, 0.55f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.39f, 0.19f, 0.61f, 0.80f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.47f, 0.23f, 0.71f, 1.00f);
	colors[ImGuiCol_Separator] = ImVec4(1.0f, 1.0f, 1.0f, 0.1f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.39f, 0.19f, 0.61f, 0.78f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.47f, 0.23f, 0.71f, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
}

void Application::SetupModernNodeTheme()
{
	auto& style = ImNodes::GetStyle();
	auto& colors = style.Colors;

	// Sync with ImGui Purple Theme
	colors[ImNodesCol_TitleBar] = ImGui::GetColorU32(ImVec4(0.20f, 0.12f, 0.28f, 1.00f));
	colors[ImNodesCol_TitleBarHovered] = ImGui::GetColorU32(ImVec4(0.39f, 0.19f, 0.61f, 1.00f));
	colors[ImNodesCol_TitleBarSelected] = ImGui::GetColorU32(ImVec4(0.47f, 0.23f, 0.71f, 1.00f));
	colors[ImNodesCol_NodeBackground] = ImGui::GetColorU32(ImVec4(0.11f, 0.11f, 0.15f, 0.98f));
	colors[ImNodesCol_NodeBackgroundHovered] = ImGui::GetColorU32(ImVec4(0.11f, 0.11f, 0.15f, 1.00f));
	colors[ImNodesCol_NodeBackgroundSelected] = ImGui::GetColorU32(ImVec4(0.15f, 0.15f, 0.20f, 1.00f));
	colors[ImNodesCol_GridBackground] = ImGui::GetColorU32(ImVec4(0.05f, 0.05f, 0.08f, 1.00f));
	colors[ImNodesCol_GridLine] = ImGui::GetColorU32(ImVec4(0.20f, 0.20f, 0.25f, 0.15f));
	colors[ImNodesCol_Link] = ImGui::GetColorU32(ImVec4(0.47f, 0.23f, 0.71f, 1.00f));
	colors[ImNodesCol_LinkHovered] = ImGui::GetColorU32(ImVec4(0.57f, 0.33f, 0.81f, 1.00f));
	colors[ImNodesCol_LinkSelected] = ImGui::GetColorU32(ImVec4(0.67f, 0.43f, 0.91f, 1.00f));
	colors[ImNodesCol_Pin] = ImGui::GetColorU32(ImVec4(0.39f, 0.19f, 0.61f, 1.00f));
	colors[ImNodesCol_PinHovered] = ImGui::GetColorU32(ImVec4(0.47f, 0.23f, 0.71f, 1.00f));
	colors[ImNodesCol_BoxSelector] = ImGui::GetColorU32(ImVec4(0.47f, 0.23f, 0.71f, 0.25f));
	colors[ImNodesCol_BoxSelectorOutline] = ImGui::GetColorU32(ImVec4(0.47f, 0.23f, 0.71f, 0.75f));
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
