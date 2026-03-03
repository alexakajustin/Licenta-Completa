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
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(mainWindow.getWindow(), true);
	ImGui_ImplOpenGL3_Init("#version 330");

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

		// Input
		glfwPollEvents();
		inputHandler.Update(mainWindow, camera, sceneManager, projection, deltaTime);

		// Debug info
		debugOverlay.SetCameraInfo(camera.getCameraPosition(), camera.getCameraDirection());
		debugOverlay.SetSceneInfo((int)sceneManager.GetObjects().size(), (int)sceneManager.GetLights().size());
		debugOverlay.SetSelectionInfo(sceneManager.GetSelectedName());
		debugOverlay.SetViewportInfo((int)mainWindow.getBufferWidth(), (int)mainWindow.getBufferHeight());

		// Editor UI
		glm::mat4 view = camera.calculateViewMatrix();
		editorUI.Render(sceneManager, projection, view, camera.getCameraPosition());
		assetBrowser.Render(sceneManager);

		// Shadow passes
		renderer.DirectionalShadowMapPass(&mainLight, sceneManager);

		for (unsigned int i = 0; i < pointLightCount; i++)
			renderer.OmniShadowMapPass(&pointLights[i], sceneManager);

		for (unsigned int i = 0; i < spotLightCount; i++)
			renderer.OmniShadowMapPass(&spotLights[i], sceneManager);

		// Main render pass
		renderer.RenderPass(projection, view, camera.getCameraPosition(), sceneManager,
			mainLight, pointLights, pointLightCount, spotLights, spotLightCount, mainWindow);

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

void Application::Shutdown()
{
	ImGui::DestroyContext();
}
