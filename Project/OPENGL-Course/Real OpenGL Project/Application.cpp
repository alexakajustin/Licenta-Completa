#define STB_IMAGE_IMPLEMENTATION
#include "Application.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <cmath>
#include <Windows.h>

#include "Model.h"
#include "Mesh.h"
#include "LightObject.h"
#include "DebugOverlay.h"
#include "External Libs/imnodes/imnodes.h"
#include "PrimitiveGenerator.h"

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
	plainTexture = Texture("Assets/Textures/plain.png");
	plainTexture.LoadTextureA();

	plainMaterial = Material(0.1f, 32.0f);

	sceneManager.SetDefaultResources(&plainTexture, &plainMaterial);

	// Initial Directional Light
	mainLight = DirectionalLight(2048, 2048,
		1.0f, 1.0f, 1.0f,
		0.4f, 0.6f,
		-10.0f, -5.0f, 20.0f);

	pointLightCount = 0;
	spotLightCount = 0;
}

void Application::SetupScene()
{
	// Setup default scene: Directional Light + 20x1x20 Plane
	
	// Create Plane
	GameObject* plane = new GameObject("Plane");
	plane->GetTransform().SetScale(glm::vec3(100.0f, 1.0f, 100.0f));
	plane->SetMesh(PrimitiveGenerator::CreatePlane());
	plane->SetTexture(&plainTexture);
	plane->SetMaterial(&plainMaterial);
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
		// Timing
		GLfloat now = (GLfloat)glfwGetTime();
		deltaTime = now - lastTime;
		lastTime = now;

		// Debug overlay timing
		debugOverlay.BeginFrame();
		debugOverlay.ResetCounters();

		// ImGui new frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		// Input — camera only (no ImGui dependency)
		glfwPollEvents();
		inputHandler.UpdateCamera(mainWindow, camera, deltaTime);

		// Debug info
		debugOverlay.SetCameraInfo(camera.getCameraPosition(), camera.getCameraDirection());
		debugOverlay.SetSceneInfo((int)sceneManager.GetObjects().size(), (int)sceneManager.GetLights().size());
		debugOverlay.SetSelectionInfo(sceneManager.GetSelectedName());
		debugOverlay.SetViewportInfo((int)mainWindow.getBufferWidth(), (int)mainWindow.getBufferHeight());

		// Editor UI
		// Since docking is not available, we use fixed window layout to emulate Unity
		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(300, 600), ImGuiCond_FirstUseEver);
		// ... (EditorUI handles its own windows)

		glm::mat4 view = camera.calculateViewMatrix();
		
		// Rendering to Viewport FBO
		glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
		glViewport(0, 0, (GLint)mainWindow.getBufferWidth(), (GLint)mainWindow.getBufferHeight());
		
		// Shadow passes (already bind their own FBOs)
		renderer.DirectionalShadowMapPass(&mainLight, sceneManager);
		for (unsigned int i = 0; i < pointLightCount; i++)
			renderer.OmniShadowMapPass(&pointLights[i], sceneManager);
		for (unsigned int i = 0; i < spotLightCount; i++)
			renderer.OmniShadowMapPass(&spotLights[i], sceneManager);

		// Main render pass into FBO
		renderer.RenderPass(projection, view, camera.getCameraPosition(), sceneManager,
			mainLight, pointLights, pointLightCount, spotLights, spotLightCount, mainWindow);
		
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// Now render ImGui windows (creates "Scene" window with correct bounds)
		editorUI.Render(sceneManager, projection, view, camera.getCameraPosition(), viewportTexture);
		assetBrowser.Render(sceneManager);
		nodeEditorUI.Render(nodeGraph, sceneManager, &plainTexture, &plainMaterial);

		// Editor picking & gizmo (AFTER UI so "Scene" window exists)
		inputHandler.UpdateEditor(mainWindow, camera, sceneManager, projection);

		glUseProgram(0);

		// Debug overlay end timing
		debugOverlay.EndFrame();

		// Debug overlay + ImGui render
		debugOverlay.Render();
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		// Update projection for window resize
		UpdateProjection();

		mainWindow.swapBuffers();
	}
}

void Application::InitViewportFBO()
{
	if (viewportFBO) glDeleteFramebuffers(1, &viewportFBO);
	if (viewportTexture) glDeleteTextures(1, &viewportTexture);
	if (viewportDepth) glDeleteRenderbuffers(1, &viewportDepth);

	glGenFramebuffers(1, &viewportFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);

	glGenTextures(1, &viewportTexture);
	glBindTexture(GL_TEXTURE_2D, viewportTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, (GLsizei)mainWindow.getBufferWidth(), (GLsizei)mainWindow.getBufferHeight(), 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, viewportTexture, 0);

	glGenRenderbuffers(1, &viewportDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, viewportDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, (GLsizei)mainWindow.getBufferWidth(), (GLsizei)mainWindow.getBufferHeight());
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, viewportDepth);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		printf("Viewport Framebuffer not complete!\n");

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
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

	ImNodes::DestroyContext();
	ImGui::DestroyContext();
}
