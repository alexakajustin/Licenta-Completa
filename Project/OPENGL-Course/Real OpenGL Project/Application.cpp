#define STB_IMAGE_IMPLEMENTATION
#define NOMINMAX
#include <Windows.h>
#include "Application.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <cmath>
#include <random>

#include "Rendering/Model.h"
#include "Rendering/Mesh.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Lighting/LightObject.h"
#include "Editor/DebugOverlay.h"
#include "External Libs/imnodes/imnodes.h"
#include "Rendering/PrimitiveGenerator.h"
#include "Core/AssetManager.h"
#include "Core/ServiceLocator.h"
#include "Nodes/AllOperations.h"
#include "Scene/SceneSerializer.h"
#include "Procedural/InteriorGenNode.h"
#include "Scene/Player.h"
#include <iostream>
#include <map>
#include <fstream>
#include "External Libs/nlohmann/json.hpp"

#include <unordered_map>
#include <string>

#include "Core/AssetManager.h"
#include "Core/ServiceLocator.h"
#include "Simulation/PhysicsSystem.h"
#include "Scene/RigidBody.h"
#include "Scene/BoxCollider.h"
#include "Scene/MeshCollider.h"
#include "Scene/CapsuleCollider.h"

// OpenGL Debug Callback
void GLAPIENTRY MessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
	if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return; // Skip notification noise
	
	struct MessageState {
		int count;
		double lastTime;
	};
	static std::map<GLuint, MessageState> messageMap;
	double now = glfwGetTime();

	MessageState& state = messageMap[id];
	state.count++;

	// Fetch current shader info for debugging
	GLint currentProgram = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
	std::string shaderName = "Unknown/None";
	if (currentProgram != 0 && ServiceLocator::GetAssetManager()->HasShader(currentProgram)) {
		shaderName = ServiceLocator::GetAssetManager()->GetShaderName(currentProgram);
	}

	// Print the first occurrence immediately
	if (state.count == 1)
	{
		fprintf(stderr, "GL CALLBACK: %s id = 0x%x, type = 0x%x, severity = 0x%x\n  [Active Shader %d: %s]\n  Message = %s\n",
			(type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""), id, type, severity, currentProgram, shaderName.c_str(), message);
		state.lastTime = now;
		
		// On the FIRST occurrence of the glUniformMatrix4fv error, dump all registered shaders
		if (id == 0x4b6) {
			fprintf(stderr, "\n=== SHADER REGISTRY DUMP ===\n");
			for (auto& [sid, sname] : ServiceLocator::GetAssetManager()->GetAllRegisteredShaders()) {
				fprintf(stderr, "  Program %u: [%s]\n", sid, sname.c_str());
			}
			fprintf(stderr, "=== END DUMP ===\n\n");
		}
		
		// Break here to catch the exact cause of the glUniform4f error
		if (std::string(message).find("glUniform4f") != std::string::npos) {
			abort();
		}
	}
	// For subsequent hits, throttle to once per second with a summary count
	else if (now - state.lastTime > 1.0)
	{
		fprintf(stderr, "GL CALLBACK [x%d more]: id = 0x%x\n  [Active Shader %d: %s]\n  Message = %s\n", 
			state.count - 1, id, currentProgram, shaderName.c_str(), message);
		state.lastTime = now;
		state.count = 1; // Reset count
	}
}

Application::Application()
	: pointLightCount(0), spotLightCount(0),
	  deltaTime(0.0f), lastTime(0.0f),
	  projection(1.0f)
{
}

Application::~Application()
{
	PhysicsSystem::GetInstance().Shutdown();
}

bool Application::Init()
{
	LoadGraphicsSettings();


	// Window
	mainWindow = Window(1920, 1080);
	mainWindow.Initialise();

	// Enable OpenGL Debug Output
	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	glDebugMessageCallback(MessageCallback, 0);

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
	InitReflectionFBO();

	// SSAO initialization
	InitSSAO();
	editorUI.SetGraphicsSettings(&graphicsSettings);
	sceneManager.SetGraphicsSettings(&graphicsSettings);

	// Enable VSync — caps FPS to monitor refresh rate, prevents GPU from running at 100%
	glfwSwapInterval(1);

	// Initialize editor subsystems
	sceneManager.InitPicking((int)mainWindow.getBufferWidth(), (int)mainWindow.getBufferHeight());
	sceneManager.InitIcons();
	sceneManager.InitGizmo();
	assetBrowser.Init();

	sceneManager.SetLightArrays(pointLights, &pointLightCount, spotLights, &spotLightCount);

	PhysicsSystem::GetInstance().Init();

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
	// Shadow frustum: 250 units gives good coverage. Edge-based fading in shader handles smooth boundaries.
	mainLight.SetShadowFrustum(250.0f, 0.1f, 500.0f);
	spotLightCount = 0;
}

void Application::SetupScene()
{
	// Setup default scene: Directional Light + 100x1x100 Plane
	
	sceneManager.SetGraphicsSettings(&graphicsSettings);
	editorUI.SetGraphicsSettings(&graphicsSettings);
	
	// Create the main window
	GameObject* plane = new GameObject("Plane");
	plane->GetTransform().SetScale(glm::vec3(100.0f, 1.0f, 100.0f));
	plane->GetTransform().SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
	plane->SetMesh(PrimitiveGenerator::CreatePlane(256, 256));
	plane->SetPrimitiveType("Plane");
	sceneManager.AddObject(plane);

	// Reset Directional Light
	if (mainLight.GetDirectionPtr()) {
		*mainLight.GetDirectionPtr() = glm::vec3(-20.0f, -5.0f, 7.0f);
	}

	// Create Directional Light
	LightObject* sun = new LightObject("Sun", &mainLight);
	sceneManager.AddLight(sun);

	pointLightCount = 0;
	spotLightCount = 0;
}

void Application::UpdateProjection()
{
	float finalFarPlane = 20000.0f; // Stable 20km horizon
	projection = glm::perspective(glm::radians(60.0f),
		(GLfloat)mainWindow.getBufferWidth() / (GLfloat)mainWindow.getBufferHeight(),
		0.1f, finalFarPlane);
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

		ServiceLocator::GetAssetManager()->Update();

		// Process deferred LOD generation (a few meshes per frame)
		extern void SceneManager_ProcessDeferredLODs();
		SceneManager_ProcessDeferredLODs();

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




		// ========== Input: Editor vs Play Mode ==========
		int activeTab = editorUI.GetWindowState().activeViewportTab;

		if (playState == PlayState::PlayMode)
		{
			// Check for tab transitions to update cursor mode
			if (activeTab != lastActiveViewportTab)
			{
				if (activeTab == 0) // Switched to Scene tab
				{
					glfwSetInputMode(mainWindow.getWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
					mainWindow.setCursorEnabled(true);
				}
				else if (activeTab == 1) // Switched to Game tab
				{
					Player* player = sceneManager.FindPlayer();
					if (player)
					{
						glfwSetInputMode(mainWindow.getWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
						mainWindow.setCursorEnabled(false);
					}
					else
					{
						glfwSetInputMode(mainWindow.getWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
						mainWindow.setCursorEnabled(true);
					}
				}
				lastActiveViewportTab = activeTab;
			}
			// Auto-attach RigidBodies to any object that has a Collider but no RigidBody
			// (Needed for Jolt physics to see procedural and existing colliders)
			for (auto* obj : sceneManager.GetObjects()) {
				if ((obj->GetComponent<BoxCollider>() || obj->GetComponent<CapsuleCollider>() || obj->GetComponent<MeshCollider>()) && !obj->GetComponent<RigidBody>()) {
					RigidBody* rb = obj->AddComponent<RigidBody>();
					rb->SetType(RigidBody::BodyType::Static);
				}
			}

			Player* player = sceneManager.FindPlayer();
			if (player && activeTab == 1)
			{
				player->Update(deltaTime, mainWindow, sceneManager, gameCamera);
			}

			PhysicsSystem::GetInstance().Update(deltaTime);

			// Update components on all objects
			for (auto* obj : sceneManager.GetObjects()) {
				obj->UpdateComponents(deltaTime);
			}
		}

		// 2. Editor Camera Input (Only if Scene tab is active)
		if (activeTab == 0)
		{
			inputHandler.UpdateCamera(mainWindow, camera, deltaTime);
		}

		// Debug info
		debugOverlay.SetCameraInfo(camera.getCameraPosition(), camera.getCameraDirection());
		debugOverlay.SetSceneInfo((int)sceneManager.GetObjects().size(), (int)sceneManager.GetLights().size());
		debugOverlay.SetSelectionInfo(sceneManager.GetSelectedName());
		debugOverlay.SetViewportInfo(fbw, fbh);

		// Editor UI 
		// Since docking is not available, we use fixed window layout to emulate Unity
		
		// 1. Render Top Bar first
		editorUI.RenderMainMenuBar(sceneManager, sceneManager.GetNodeGraph(), &camera, playState == PlayState::PlayMode);

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

		// Update projection matrix based on FRESH viewport info
		glm::vec2 vSize = editorUI.GetViewportSize();
		int vWidth = (int)vSize.x;
		int vHeight = (int)vSize.y;
		if (vWidth < 1) vWidth = 1;
		if (vHeight < 1) vHeight = 1;

		float aspect = (float)vWidth / (float)vHeight;
		float finalFarPlane = 20000.0f; // Stable 20km horizon
		projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, finalFarPlane);

		if (vWidth != currentViewportWidth || vHeight != currentViewportHeight || vWidth == 0 || vHeight == 0)
		{
			ResizeViewportFBO(vWidth, vHeight);
			ResizeReflectionFBO(vWidth / 2, vHeight / 2); // Half resolution for performance
			ResizeRefractionFBO(vWidth, vHeight); // Full resolution for crisp refraction
		}
		glm::mat4 view = camera.calculateViewMatrix();
		glm::vec3 activeCameraPos = camera.getCameraPosition();

		// Override the entire rendering pipeline to use the Game Camera if the Game Tab is active
		if (playState == PlayState::PlayMode && uiState.activeViewportTab == 1) {
			int gw = currentGameViewportWidth > 0 ? currentGameViewportWidth : vWidth;
			int gh = currentGameViewportHeight > 0 ? currentGameViewportHeight : vHeight;
			if (gw < 1) gw = 1;
			if (gh < 1) gh = 1;
			projection = glm::perspective(glm::radians(60.0f), (float)gw / (float)gh, 0.1f, 20000.0f);
			view = gameCamera.calculateViewMatrix();
			activeCameraPos = gameCamera.getCameraPosition();
		}

		// Main render pass — clear backbuffer (the part around the UI)
		glViewport(0, 0, fbw, fbh);
		glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Synchronize Graphics Settings
		sceneManager.SetRenderDistanceMultiplier(1.0f);
		sceneManager.SetShadowDistanceMultiplier(1.0f);

		// Update Frustum
		Frustum currentFrustum = Frustum::CreateFrustumFromMatrix(projection * view);

		if (graphicsSettings.debugFreezeCulling) {
			if (activeFrustum != &frozenFrustum) {
				frozenFrustum = currentFrustum;
				activeFrustum = &frozenFrustum;
			}
		} else {
			activeFrustum = nullptr; // Uses per-pass live frustum if null
		}

		// Shadow passes
		float shadowFar = graphicsSettings.shadowDistance;
		debugOverlay.BeginPass("Shadow Maps");
		renderer.DirectionalShadowMapPass(&mainLight, sceneManager, activeCameraPos, projection, view, 0.1f, shadowFar, &graphicsSettings);
		for (unsigned int i = 0; i < pointLightCount; i++)
			renderer.OmniShadowMapPass(&pointLights[i], sceneManager, &graphicsSettings);
		for (unsigned int i = 0; i < spotLightCount; i++)
			renderer.OmniShadowMapPass(&spotLights[i], sceneManager, &graphicsSettings);
		debugOverlay.EndPass("Shadow Maps");

		// 1. Reflection Pass (if there is water)
		float waterHeight = -1000.0f; 
		bool hasWater = false;
		for (auto* obj : sceneManager.GetObjects()) {
			Material* mat = obj->GetMaterial();
			if (mat && mat->GetShader() && (mat->GetShader()->GetVertexPath().find("water.vert") != std::string::npos)) {
				waterHeight = obj->GetTransform().GetPosition().y;
				hasWater = true;
				break;
			}
		}

		if (hasWater) {
			float minCamDist = 1e10f;
			GameObject* bestWater = nullptr;
			glm::vec3 camPos = camera.getCameraPosition();

			for (auto* obj : sceneManager.GetObjects()) {
				Material* mat = obj->GetMaterial();
				if (mat && mat->GetShader()) {
					std::string vPath = mat->GetShader()->GetVertexPath();
					if (vPath.find("water.vert") != std::string::npos || vPath.find("river.vert") != std::string::npos) {
						float dist = glm::distance(obj->GetTransform().GetPosition(), camPos);
						if (dist < minCamDist) {
							minCamDist = dist;
							bestWater = obj;
						}
					}
				}
			}

			if (bestWater) {
				float targetWaterHeight = bestWater->GetTransform().GetPosition().y;

				// Only perform expensive/jittery sampling for non-flat water (rivers/sloped)
				if (bestWater->GetPrimitiveType() != "Plane") {
					const MeshData& md = bestWater->GetCPUMeshData();
					if (md.GetVertexCount() > 0) {
						glm::mat4 invModel = glm::inverse(bestWater->GetWorldMatrix());
						glm::vec3 localCam = glm::vec3(invModel * glm::vec4(camPos, 1.0f));
						
						float closestDistSq = 1e10f;
						float localY = md.vertices[1];
						
						int vCount = md.GetVertexCount();
						int step = vCount > 2000 ? vCount / 500 : 1; 
						for (int i = 0; i < vCount; i += step) {
							int base = i * 14;
							float dx = md.vertices[base] - localCam.x;
							float dz = md.vertices[base + 2] - localCam.z;
							float d2 = dx*dx + dz*dz;
							if (d2 < closestDistSq) {
								closestDistSq = d2;
								localY = md.vertices[base + 1];
							}
						}
						glm::vec4 worldHeightPos = bestWater->GetWorldMatrix() * glm::vec4(0.0f, localY, 0.0f, 1.0f);
						targetWaterHeight = worldHeightPos.y;
					} else {
						glm::vec3 bmin, bmax;
						bestWater->GetWorldBounds(bmin, bmax);
						targetWaterHeight = bmax.y;
					}
				}

				// Stability smoothing: prevent frame-to-frame height jumps
				// We use a map to store smoothed heights for each water object individually
				static std::map<GameObject*, float> smoothedHeights;
				if (smoothedHeights.find(bestWater) == smoothedHeights.end()) {
					smoothedHeights[bestWater] = targetWaterHeight;
				}
				smoothedHeights[bestWater] = glm::mix(smoothedHeights[bestWater], targetWaterHeight, 0.05f); // Very aggressive smoothing
				waterHeight = smoothedHeights[bestWater];
			}

			glBindFramebuffer(GL_FRAMEBUFFER, reflectionFBO);
			glViewport(0, 0, reflectionWidth, reflectionHeight);
			glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			// RENDER SKY FOR REFLECTIONS
			if (graphicsSettings.volumetricSkyEnabled)
			{
				glDisable(GL_DEPTH_TEST);
				glDisable(GL_BLEND);
				
				// Calculate reflected matrices
				glm::mat4 reflectionMatrix(1.0f);
				reflectionMatrix[1][1] = -1.0f;
				reflectionMatrix[3][1] = 2.0f * waterHeight;
				glm::mat4 reflectedView = view * reflectionMatrix;

				glm::mat4 invProj = glm::inverse(projection);
				glm::mat4 invView = glm::inverse(reflectedView);

				if (graphicsSettings.skyboxType == SkyboxType::Atmospheric) 
				{
					volumetricSkyShader.UseShader();
					glUniformMatrix4fv(glGetUniformLocation(volumetricSkyShader.GetShaderID(), "invProjection"), 1, GL_FALSE, glm::value_ptr(invProj));
					glUniformMatrix4fv(glGetUniformLocation(volumetricSkyShader.GetShaderID(), "invView"), 1, GL_FALSE, glm::value_ptr(invView));

					glm::vec3 sunDir = *mainLight.GetDirectionPtr();
					glm::vec3 dirToSun = -glm::normalize(sunDir);
					glUniform3fv(glGetUniformLocation(volumetricSkyShader.GetShaderID(), "sunDir"), 1, glm::value_ptr(dirToSun));
					glUniform3fv(glGetUniformLocation(volumetricSkyShader.GetShaderID(), "sunColor"), 1, glm::value_ptr(*mainLight.GetColourPtr()));

					glUniform1f(glGetUniformLocation(volumetricSkyShader.GetShaderID(), "time"), (float)glfwGetTime());
					glUniform1i(glGetUniformLocation(volumetricSkyShader.GetShaderID(), "cloudsEnabled"), graphicsSettings.cloudsEnabled ? 1 : 0);
					glUniform1f(glGetUniformLocation(volumetricSkyShader.GetShaderID(), "cloudsDensity"), graphicsSettings.cloudsDensity);
					glUniform1f(glGetUniformLocation(volumetricSkyShader.GetShaderID(), "cloudsSpeed"), graphicsSettings.cloudsSpeed);
					glUniform1f(glGetUniformLocation(volumetricSkyShader.GetShaderID(), "cloudsSharpness"), graphicsSettings.cloudsSharpness);
				}
				else if (graphicsSettings.skyboxType == SkyboxType::Universe)
				{
					universeSkyShader.UseShader();
					glUniformMatrix4fv(glGetUniformLocation(universeSkyShader.GetShaderID(), "invProjection"), 1, GL_FALSE, glm::value_ptr(invProj));
					glUniformMatrix4fv(glGetUniformLocation(universeSkyShader.GetShaderID(), "invView"), 1, GL_FALSE, glm::value_ptr(invView));
					glUniform1f(glGetUniformLocation(universeSkyShader.GetShaderID(), "time"), (float)glfwGetTime());
					glUniform1f(glGetUniformLocation(universeSkyShader.GetShaderID(), "starDensity"), graphicsSettings.universeStarDensity);
					glUniform1f(glGetUniformLocation(universeSkyShader.GetShaderID(), "starBrightness"), graphicsSettings.universeStarBrightness);
					glUniform1f(glGetUniformLocation(universeSkyShader.GetShaderID(), "nebulaIntensity"), graphicsSettings.universeNebulaIntensity);
					glUniform1f(glGetUniformLocation(universeSkyShader.GetShaderID(), "universeSpeed"), graphicsSettings.universeSpeed);
					glUniform3fv(glGetUniformLocation(universeSkyShader.GetShaderID(), "nebulaColor1"), 1, glm::value_ptr(graphicsSettings.universeNebulaColor1));
					glUniform3fv(glGetUniformLocation(universeSkyShader.GetShaderID(), "nebulaColor2"), 1, glm::value_ptr(graphicsSettings.universeNebulaColor2));
				}

				RenderQuad();
				glEnable(GL_DEPTH_TEST);
			}

			debugOverlay.BeginPass("Reflection");
			renderer.ReflectionPass(projection, view, activeCameraPos, sceneManager,
				mainLight, pointLights, pointLightCount, spotLights, spotLightCount, reflectionWidth, reflectionHeight, waterHeight, &graphicsSettings);
			debugOverlay.EndPass("Reflection");
		}
		
		// 2. Final Scene Render (Viewport FBO)
		glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
		glViewport(0, 0, currentViewportWidth, currentViewportHeight);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Define sky rendering lambda so we can reuse it for both viewports
		auto renderSky = [&](const glm::mat4& proj, const glm::mat4& vw) {
			if (!graphicsSettings.volumetricSkyEnabled) return;
			glDisable(GL_DEPTH_TEST);
			glDisable(GL_BLEND); // Solid background
			glm::mat4 invProj = glm::inverse(proj);
			glm::mat4 invView = glm::inverse(vw);

			if (graphicsSettings.skyboxType == SkyboxType::Atmospheric)
			{
				volumetricSkyShader.UseShader();
				glUniformMatrix4fv(glGetUniformLocation(volumetricSkyShader.GetShaderID(), "invProjection"), 1, GL_FALSE, glm::value_ptr(invProj));
				glUniformMatrix4fv(glGetUniformLocation(volumetricSkyShader.GetShaderID(), "invView"), 1, GL_FALSE, glm::value_ptr(invView));
				glm::vec3 sunDir = *mainLight.GetDirectionPtr();
				glm::vec3 dirToSun = -glm::normalize(sunDir);
				glUniform3fv(glGetUniformLocation(volumetricSkyShader.GetShaderID(), "sunDir"), 1, glm::value_ptr(dirToSun));
				glUniform3fv(glGetUniformLocation(volumetricSkyShader.GetShaderID(), "sunColor"), 1, glm::value_ptr(*mainLight.GetColourPtr()));

				glUniform1f(glGetUniformLocation(volumetricSkyShader.GetShaderID(), "time"), (float)glfwGetTime());
				glUniform1i(glGetUniformLocation(volumetricSkyShader.GetShaderID(), "cloudsEnabled"), graphicsSettings.cloudsEnabled ? 1 : 0);
				glUniform1f(glGetUniformLocation(volumetricSkyShader.GetShaderID(), "cloudsDensity"), graphicsSettings.cloudsDensity);
				glUniform1f(glGetUniformLocation(volumetricSkyShader.GetShaderID(), "cloudsSpeed"), graphicsSettings.cloudsSpeed);
				glUniform1f(glGetUniformLocation(volumetricSkyShader.GetShaderID(), "cloudsSharpness"), graphicsSettings.cloudsSharpness);
			}
			else if (graphicsSettings.skyboxType == SkyboxType::Universe)
			{
				universeSkyShader.UseShader();
				glUniformMatrix4fv(glGetUniformLocation(universeSkyShader.GetShaderID(), "invProjection"), 1, GL_FALSE, glm::value_ptr(invProj));
				glUniformMatrix4fv(glGetUniformLocation(universeSkyShader.GetShaderID(), "invView"), 1, GL_FALSE, glm::value_ptr(invView));
				glUniform1f(glGetUniformLocation(universeSkyShader.GetShaderID(), "time"), (float)glfwGetTime());
				glUniform1f(glGetUniformLocation(universeSkyShader.GetShaderID(), "starDensity"), graphicsSettings.universeStarDensity);
				glUniform1f(glGetUniformLocation(universeSkyShader.GetShaderID(), "starBrightness"), graphicsSettings.universeStarBrightness);
				glUniform1f(glGetUniformLocation(universeSkyShader.GetShaderID(), "nebulaIntensity"), graphicsSettings.universeNebulaIntensity);
				glUniform1f(glGetUniformLocation(universeSkyShader.GetShaderID(), "universeSpeed"), graphicsSettings.universeSpeed);
				glUniform3fv(glGetUniformLocation(universeSkyShader.GetShaderID(), "nebulaColor1"), 1, glm::value_ptr(graphicsSettings.universeNebulaColor1));
				glUniform3fv(glGetUniformLocation(universeSkyShader.GetShaderID(), "nebulaColor2"), 1, glm::value_ptr(graphicsSettings.universeNebulaColor2));
			}

			RenderQuad();
			glEnable(GL_DEPTH_TEST);
		};

		// RENDER SKY AS BACKGROUND FOR EDITOR VIEWPORT
		renderSky(projection, view);

		debugOverlay.BeginPass("Main Render");
		renderer.RenderPass(projection, view, activeCameraPos, sceneManager,
			mainLight, pointLights, pointLightCount, spotLights, spotLightCount, currentViewportWidth, currentViewportHeight, viewportDepth, reflectionTexture, refractionTexture, activeFrustum, &graphicsSettings);
		debugOverlay.EndPass("Main Render");
		
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// SSAO PASS
		if (graphicsSettings.ssaoEnabled)
		{
			debugOverlay.BeginPass("SSAO");
			// 1. Generate SSAO texture
			glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
			glViewport(0, 0, currentViewportWidth, currentViewportHeight);
			glClear(GL_COLOR_BUFFER_BIT);
			ssaoShader.UseShader();
			glUniformMatrix4fv(ssaoShader.GetProjectionLocation(), 1, GL_FALSE, glm::value_ptr(projection));
			glm::mat4 invProj = glm::inverse(projection);
			glUniformMatrix4fv(glGetUniformLocation(ssaoShader.GetShaderID(), "invProjection"), 1, GL_FALSE, glm::value_ptr(invProj));
			glUniform1i(glGetUniformLocation(ssaoShader.GetShaderID(), "depthMap"), 0);
			glUniform1i(glGetUniformLocation(ssaoShader.GetShaderID(), "texNoise"), 1);
			glUniform2f(glGetUniformLocation(ssaoShader.GetShaderID(), "noiseScale"), currentViewportWidth / 4.0f, currentViewportHeight / 4.0f);
			// Pass configurable SSAO parameters
			glUniform1f(glGetUniformLocation(ssaoShader.GetShaderID(), "radius"), graphicsSettings.ssaoRadius);
			glUniform1f(glGetUniformLocation(ssaoShader.GetShaderID(), "bias"), graphicsSettings.ssaoBias);
			glUniform1f(glGetUniformLocation(ssaoShader.GetShaderID(), "intensity"), graphicsSettings.ssaoIntensity);
			glUniform1i(glGetUniformLocation(ssaoShader.GetShaderID(), "kernelSize"), graphicsSettings.ssaoKernelSize);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, viewportDepth);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, noiseTexture);
			RenderQuad();
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			// 2. Blur SSAO texture
			glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO);
			glViewport(0, 0, currentViewportWidth, currentViewportHeight);
			glClear(GL_COLOR_BUFFER_BIT);
			ssaoBlurShader.UseShader();
			glUniform1i(glGetUniformLocation(ssaoBlurShader.GetShaderID(), "ssaoInput"), 0);
			glUniform1i(glGetUniformLocation(ssaoBlurShader.GetShaderID(), "blurSize"), graphicsSettings.ssaoBlurSize);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, ssaoColorBuffer);
			RenderQuad();
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			// 3. Apply SSAO to viewportTexture using Multiplicative Blend
			glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
			glViewport(0, 0, currentViewportWidth, currentViewportHeight);
			glDisable(GL_DEPTH_TEST);
			glEnable(GL_BLEND);
			glBlendFunc(GL_ZERO, GL_SRC_COLOR); 
			ssaoApplyShader.UseShader();
			glUniform1i(glGetUniformLocation(ssaoApplyShader.GetShaderID(), "ssaoText"), 0);
			glUniform1i(glGetUniformLocation(ssaoShader.GetShaderID(), "depthMap"), 1);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, ssaoColorBufferBlur);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, viewportDepth);
			RenderQuad();
			glDisable(GL_BLEND);
			glEnable(GL_DEPTH_TEST);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			debugOverlay.EndPass("SSAO");
		}


		// GOD RAYS PASS
		if (graphicsSettings.godraysEnabled)
		{
			debugOverlay.BeginPass("God Rays");
			glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
			glViewport(0, 0, currentViewportWidth, currentViewportHeight);
			
			glDisable(GL_DEPTH_TEST);
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE); // Additive blending for god rays

			godrayShader.UseShader();
			
			// Matrices
			glUniformMatrix4fv(glGetUniformLocation(godrayShader.GetShaderID(), "projection"), 1, GL_FALSE, glm::value_ptr(projection));
			glUniformMatrix4fv(glGetUniformLocation(godrayShader.GetShaderID(), "view"), 1, GL_FALSE, glm::value_ptr(view));
			
			glm::mat4 invProj = glm::inverse(projection);
			glm::mat4 invView = glm::inverse(view);
			glUniformMatrix4fv(glGetUniformLocation(godrayShader.GetShaderID(), "invProjection"), 1, GL_FALSE, glm::value_ptr(invProj));
			glUniformMatrix4fv(glGetUniformLocation(godrayShader.GetShaderID(), "invView"), 1, GL_FALSE, glm::value_ptr(invView));

			// Sun data
			glm::vec3 sunDir = *mainLight.GetDirectionPtr();
			glm::vec3 dirToSun = -glm::normalize(sunDir);
			glUniform3fv(glGetUniformLocation(godrayShader.GetShaderID(), "sunDir"), 1, glm::value_ptr(dirToSun));
			glUniform3fv(glGetUniformLocation(godrayShader.GetShaderID(), "sunColor"), 1, glm::value_ptr(*mainLight.GetColourPtr()));

			// God ray parameters - Linked to sun intensity (Ambient + Diffuse)
			float sunIntensity = *mainLight.GetAmbientIntensityPtr() + *mainLight.GetDiffuseIntensityPtr();
			glUniform1f(glGetUniformLocation(godrayShader.GetShaderID(), "exposure"), sunIntensity * 0.18f);
			glUniform1f(glGetUniformLocation(godrayShader.GetShaderID(), "decay"), graphicsSettings.godraysDecay);
			glUniform1f(glGetUniformLocation(godrayShader.GetShaderID(), "density"), graphicsSettings.godraysDensity);
			glUniform1f(glGetUniformLocation(godrayShader.GetShaderID(), "weight"), sunIntensity * 0.07f);

			// Depth map
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, viewportDepth);
			glUniform1i(glGetUniformLocation(godrayShader.GetShaderID(), "depthMap"), 0);

			RenderQuad();

			glDisable(GL_BLEND);
			glEnable(GL_DEPTH_TEST);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			debugOverlay.EndPass("God Rays");
		}

		// Scene Selection (Move before depth clear so we can use scene depth)
		if (activeTab == 0)
		{
			inputHandler.UpdateEditor(mainWindow, camera, sceneManager, projection, editorUI, viewportFBO);
		}

		// Render gizmos/icons AFTER SSAO so depth buffer had valid object data for SSAO
		if (activeTab == 0)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
			glViewport(0, 0, currentViewportWidth, currentViewportHeight);
			glClear(GL_DEPTH_BUFFER_BIT); // Always render gizmos/icons on top of the scene
			sceneManager.RenderIcons(projection, view);
			sceneManager.RenderGizmo(projection, view, camera.getCameraPosition());
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}

		// Both Scene and Game tabs now use the same fully-featured viewport texture. 
		// The active camera is seamlessly swapped in the main pipeline depending on the active tab.
		editorUI.Render(sceneManager, projection, view, camera.getCameraPosition(), viewportTexture, &camera, &inputHandler, viewportTexture, playState == PlayState::PlayMode);
		
		assetBrowser.Render(sceneManager, uiState);
		NodeEditorAction graphAction = nodeEditorUI.Render(sceneManager, &plainTexture, &plainMaterial, uiState);
		nodeBuilderUI.Render(sceneManager.GetNodeGraph(), uiState);
		editorUI.RenderGraphicsSettings(&sceneManager);


		// Editor picking & gizmo (REMOVED: moved up)

		glUseProgram(0);

		// Debug overlay end timing
		debugOverlay.EndFrame();

		// Debug overlay + ImGui render
		debugOverlay.Render(uiState);

		// 3. Render Splitter Visuals (On top of EVERYTHING)
		editorUI.UpdateLayoutVisual();

		ImGui::Render();

		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		// UpdateProjection() was removed here as it is now handled at the start of the loop with dynamic far planes

		mainWindow.swapBuffers();

		// Reset forceLayout after ALL ImGui panels have rendered for this frame
		editorUI.GetWindowState().forceLayout = false;

		// Handle pending scene file actions
		auto sceneAction = editorUI.GetPendingSceneAction();
		if (sceneAction != EditorUI::SceneAction::None)
		{
			if (sceneAction == EditorUI::SceneAction::Save)
			{
				SceneSerializer::SaveScene(editorUI.GetPendingScenePath(), sceneManager, &camera);
			}
			else if (sceneAction == EditorUI::SceneAction::Load)
			{
				progressTitle = "Loading Project...";
				SceneSerializer::LoadScene(editorUI.GetPendingScenePath(), sceneManager,
					mainLight, pointLights, pointLightCount, spotLights, spotLightCount,
					&plainTexture, &plainMaterial, &camera, progressCallback);
			}
			else if (sceneAction == EditorUI::SceneAction::New)
			{
				sceneManager.Clear();
				pointLightCount = 0;
				spotLightCount = 0;
				SetupScene();
			}
			editorUI.ClearPendingSceneAction();
		}


		if (graphAction == NodeEditorAction::ExecutePipeline) {
			progressTitle = "Executing Pipeline...";
			sceneManager.ExecutePipeline(&plainTexture, &plainMaterial, progressCallback);
		}
		else if (graphAction == NodeEditorAction::ExecuteActiveTab) {
			progressTitle = "Executing Active Tab...";
			sceneManager.GetNodeGraph().Execute(sceneManager, &plainTexture, &plainMaterial, progressCallback);
		}

		// Handle pending Play/Stop actions
		auto playAction = editorUI.GetPendingPlayAction();
		if (playAction != EditorUI::PlayAction::None)
		{
			if (playAction == EditorUI::PlayAction::Play)
			{
				StartPlayMode();
			}
			else if (playAction == EditorUI::PlayAction::Stop)
			{
				StopPlayMode();
			}
			editorUI.ClearPendingPlayAction();
		}
	}

	SaveGraphicsSettings();
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

	glGenTextures(1, &viewportDepth);
	glBindTexture(GL_TEXTURE_2D, viewportDepth);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	// No wrap values needed for simple clamping usually, but good practice:
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, viewportDepth, 0);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		printf("Viewport Framebuffer not complete!\n");

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	InitRefractionFBO();
}

void Application::InitReflectionFBO()
{
	glGenFramebuffers(1, &reflectionFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, reflectionFBO);

	reflectionWidth = 1920 / 2;
	reflectionHeight = 1080 / 2;

	glGenTextures(1, &reflectionTexture);
	glBindTexture(GL_TEXTURE_2D, reflectionTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, reflectionWidth, reflectionHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, reflectionTexture, 0);

	glGenTextures(1, &reflectionDepth);
	glBindTexture(GL_TEXTURE_2D, reflectionDepth);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, reflectionWidth, reflectionHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, reflectionDepth, 0);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		printf("Reflection Framebuffer not complete!\n");

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Application::InitRefractionFBO()
{
	glGenTextures(1, &refractionTexture);
	glBindTexture(GL_TEXTURE_2D, refractionTexture);
	
	refractionWidth = 1920; 
	refractionHeight = 1080;

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, refractionWidth, refractionHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Application::ResizeRefractionFBO(int width, int height)
{
	if (refractionTexture == 0 || width <= 0 || height <= 0) return;
	if (width == refractionWidth && height == refractionHeight) return;

	refractionWidth = width;
	refractionHeight = height;

	glBindTexture(GL_TEXTURE_2D, refractionTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, refractionWidth, refractionHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
}

void Application::ResizeReflectionFBO(int width, int height)
{
	if (reflectionTexture == 0 || width <= 0 || height <= 0) return;
	if (width == reflectionWidth && height == reflectionHeight) return;
	
	reflectionWidth = width;
	reflectionHeight = height;

	glBindTexture(GL_TEXTURE_2D, reflectionTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, reflectionWidth, reflectionHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	
	glBindTexture(GL_TEXTURE_2D, reflectionDepth);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, reflectionWidth, reflectionHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
}

void Application::ResizeViewportFBO(int width, int height)
{
	if (width < 1) width = 1;
	if (height < 1) height = 1;

	if (width == currentViewportWidth && height == currentViewportHeight && viewportTexture != 0) return;

	currentViewportWidth = width;
	currentViewportHeight = height;

	// Update Texture
	glBindTexture(GL_TEXTURE_2D, viewportTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);

	// Update Depth Buffer Texture
	glBindTexture(GL_TEXTURE_2D, viewportDepth);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

	// Update SSAO FBOs
	glBindTexture(GL_TEXTURE_2D, ssaoColorBuffer);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, width, height, 0, GL_RED, GL_FLOAT, NULL);
	glBindTexture(GL_TEXTURE_2D, ssaoColorBufferBlur);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, width, height, 0, GL_RED, GL_FLOAT, NULL);

	printf("Viewport FBO resized to %dx%d\n", width, height);
}

void Application::SetupDockSpace()
{
    // Not supported without docking branch
}

void Application::InitSSAO()
{
	auto am = ServiceLocator::GetAssetManager();
	ssaoShader.CreateFromFiles(am->GetShaderPath("ssao.vert").c_str(), am->GetShaderPath("ssao.frag").c_str());
	ssaoBlurShader.CreateFromFiles(am->GetShaderPath("ssao.vert").c_str(), am->GetShaderPath("ssao_blur.frag").c_str());
	ssaoApplyShader.CreateFromFiles(am->GetShaderPath("ssao.vert").c_str(), am->GetShaderPath("ssao_apply.frag").c_str());
	godrayShader.CreateFromFiles(am->GetShaderPath("godrays.vert").c_str(), am->GetShaderPath("godrays.frag").c_str());
	volumetricSkyShader.CreateFromFiles(am->GetShaderPath("volumetric_sky.vert").c_str(), am->GetShaderPath("volumetric_sky.frag").c_str());
	universeSkyShader.CreateFromFiles(am->GetShaderPath("volumetric_sky.vert").c_str(), am->GetShaderPath("universe_sky.frag").c_str());

	// Gen FBOs
	glGenFramebuffers(1, &ssaoFBO);
	glGenFramebuffers(1, &ssaoBlurFBO);

	int w = currentViewportWidth;
	int h = currentViewportHeight;

	// SSAO color buffer
	glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
	glGenTextures(1, &ssaoColorBuffer);
	glBindTexture(GL_TEXTURE_2D, ssaoColorBuffer);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, w, h, 0, GL_RED, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaoColorBuffer, 0);

	// SSAO blur buffer
	glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO);
	glGenTextures(1, &ssaoColorBufferBlur);
	glBindTexture(GL_TEXTURE_2D, ssaoColorBufferBlur);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, w, h, 0, GL_RED, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaoColorBufferBlur, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// Generate Sample Kernel
	std::uniform_real_distribution<GLfloat> randomFloats(0.0, 1.0);
	std::default_random_engine generator;
	for (unsigned int i = 0; i < 64; ++i)
	{
		glm::vec3 sample(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, randomFloats(generator));
		sample = glm::normalize(sample);
		sample *= randomFloats(generator);
		float scale = float(i) / 64.0f;
		scale = glm::mix(0.1f, 1.0f, scale * scale);
		sample *= scale;
		ssaoKernel.push_back(sample);
	}

	// Generate Noise Texture
	std::vector<glm::vec3> ssaoNoise;
	for (unsigned int i = 0; i < 16; i++)
	{
		glm::vec3 noise(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, 0.0f);
		ssaoNoise.push_back(noise);
	}
	glGenTextures(1, &noiseTexture);
	glBindTexture(GL_TEXTURE_2D, noiseTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, 4, 4, 0, GL_RGB, GL_FLOAT, &ssaoNoise[0]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	// Send kernel
	ssaoShader.UseShader();
	for (unsigned int i = 0; i < 64; ++i)
	{
		std::string name = "samples[" + std::to_string(i) + "]";
		glUniform3fv(glGetUniformLocation(ssaoShader.GetShaderID(), name.c_str()), 1, &ssaoKernel[i][0]);
	}
}

void Application::RenderQuad()
{
	if (quadVAO == 0)
	{
		float quadVertices[] = {
			// positions        // texture Coords
			-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
			-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
			 1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
			 1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
		};
		glGenVertexArrays(1, &quadVAO);
		glGenBuffers(1, &quadVBO);
		glBindVertexArray(quadVAO);
		glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	}
	glBindVertexArray(quadVAO);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
}

void Application::LoadGraphicsSettings()
{
	std::ifstream file("graphics_settings.json");
	if (!file.is_open()) return;

	nlohmann::json j;
	try {
		file >> j;
		
		if (j.contains("ssaoEnabled")) graphicsSettings.ssaoEnabled = j["ssaoEnabled"];
		if (j.contains("ssaoRadius")) graphicsSettings.ssaoRadius = j["ssaoRadius"];
		if (j.contains("ssaoBias")) graphicsSettings.ssaoBias = j["ssaoBias"];
		if (j.contains("ssaoIntensity")) graphicsSettings.ssaoIntensity = j["ssaoIntensity"];
		if (j.contains("ssaoKernelSize")) graphicsSettings.ssaoKernelSize = j["ssaoKernelSize"];
		if (j.contains("ssaoBlurSize")) graphicsSettings.ssaoBlurSize = j["ssaoBlurSize"];

		if (j.contains("lod0Distance")) graphicsSettings.lod0Distance = j["lod0Distance"];
		if (j.contains("lod1Distance")) graphicsSettings.lod1Distance = j["lod1Distance"];
		if (j.contains("lod2Distance")) graphicsSettings.lod2Distance = j["lod2Distance"];
		if (j.contains("renderDistance")) graphicsSettings.renderDistance = j["renderDistance"];
		if (j.contains("shadowDistance")) graphicsSettings.shadowDistance = j["shadowDistance"];
		if (j.contains("shadowCascades")) graphicsSettings.shadowCascades = j["shadowCascades"];

		if (j.contains("godraysEnabled")) graphicsSettings.godraysEnabled = j["godraysEnabled"];
		if (j.contains("godraysDecay")) graphicsSettings.godraysDecay = j["godraysDecay"];
		if (j.contains("godraysDensity")) graphicsSettings.godraysDensity = j["godraysDensity"];

		if (j.contains("volumetricSkyEnabled")) graphicsSettings.volumetricSkyEnabled = j["volumetricSkyEnabled"];
		if (j.contains("cloudsEnabled")) graphicsSettings.cloudsEnabled = j["cloudsEnabled"];
		if (j.contains("cloudsDensity")) graphicsSettings.cloudsDensity = j["cloudsDensity"];
		if (j.contains("cloudsSpeed")) graphicsSettings.cloudsSpeed = j["cloudsSpeed"];
		if (j.contains("cloudsSharpness")) graphicsSettings.cloudsSharpness = j["cloudsSharpness"];

		if (j.contains("skyboxType")) graphicsSettings.skyboxType = (SkyboxType)j["skyboxType"];

		if (j.contains("universeStarDensity")) graphicsSettings.universeStarDensity = j["universeStarDensity"];
		if (j.contains("universeStarBrightness")) graphicsSettings.universeStarBrightness = j["universeStarBrightness"];
		if (j.contains("universeNebulaIntensity")) graphicsSettings.universeNebulaIntensity = j["universeNebulaIntensity"];
		if (j.contains("universeSpeed")) graphicsSettings.universeSpeed = j["universeSpeed"];
		
		if (j.contains("universeNebulaColor1")) {
			auto& c = j["universeNebulaColor1"];
			graphicsSettings.universeNebulaColor1 = glm::vec3(c[0], c[1], c[2]);
		}
		if (j.contains("universeNebulaColor2")) {
			auto& c = j["universeNebulaColor2"];
			graphicsSettings.universeNebulaColor2 = glm::vec3(c[0], c[1], c[2]);
		}

		if (j.contains("debugLODColoring")) graphicsSettings.debugLODColoring = j["debugLODColoring"];
		if (j.contains("debugShowBounds")) graphicsSettings.debugShowBounds = j["debugShowBounds"];
		if (j.contains("debugFreezeCulling")) graphicsSettings.debugFreezeCulling = j["debugFreezeCulling"];
		if (j.contains("showWireframe")) graphicsSettings.showWireframe = j["showWireframe"];
		if (j.contains("enableOcclusionCulling")) graphicsSettings.enableOcclusionCulling = j["enableOcclusionCulling"];
		if (j.contains("debugShowHiZ")) graphicsSettings.debugShowHiZ = j["debugShowHiZ"];
		if (j.contains("debugShowCulling")) graphicsSettings.debugShowCulling = j["debugShowCulling"];
	} catch (const std::exception& e) {
		printf("Failed to load graphics settings: %s\n", e.what());
	}
}

void Application::SaveGraphicsSettings()
{
	nlohmann::json j;
	
	j["ssaoEnabled"] = graphicsSettings.ssaoEnabled;
	j["ssaoRadius"] = graphicsSettings.ssaoRadius;
	j["ssaoBias"] = graphicsSettings.ssaoBias;
	j["ssaoIntensity"] = graphicsSettings.ssaoIntensity;
	j["ssaoKernelSize"] = graphicsSettings.ssaoKernelSize;
	j["ssaoBlurSize"] = graphicsSettings.ssaoBlurSize;

	j["lod0Distance"] = graphicsSettings.lod0Distance;
	j["lod1Distance"] = graphicsSettings.lod1Distance;
	j["lod2Distance"] = graphicsSettings.lod2Distance;
	j["renderDistance"] = graphicsSettings.renderDistance;
	j["shadowDistance"] = graphicsSettings.shadowDistance;
	j["shadowCascades"] = graphicsSettings.shadowCascades;

	j["godraysEnabled"] = graphicsSettings.godraysEnabled;
	j["godraysDecay"] = graphicsSettings.godraysDecay;
	j["godraysDensity"] = graphicsSettings.godraysDensity;

	j["volumetricSkyEnabled"] = graphicsSettings.volumetricSkyEnabled;
	j["cloudsEnabled"] = graphicsSettings.cloudsEnabled;
	j["cloudsDensity"] = graphicsSettings.cloudsDensity;
	j["cloudsSpeed"] = graphicsSettings.cloudsSpeed;
	j["cloudsSharpness"] = graphicsSettings.cloudsSharpness;

	j["skyboxType"] = (int)graphicsSettings.skyboxType;

	j["universeStarDensity"] = graphicsSettings.universeStarDensity;
	j["universeStarBrightness"] = graphicsSettings.universeStarBrightness;
	j["universeNebulaIntensity"] = graphicsSettings.universeNebulaIntensity;
	j["universeSpeed"] = graphicsSettings.universeSpeed;
	
	j["universeNebulaColor1"] = { graphicsSettings.universeNebulaColor1.x, graphicsSettings.universeNebulaColor1.y, graphicsSettings.universeNebulaColor1.z };
	j["universeNebulaColor2"] = { graphicsSettings.universeNebulaColor2.x, graphicsSettings.universeNebulaColor2.y, graphicsSettings.universeNebulaColor2.z };

	j["debugLODColoring"] = graphicsSettings.debugLODColoring;
	j["debugShowBounds"] = graphicsSettings.debugShowBounds;
	j["debugFreezeCulling"] = graphicsSettings.debugFreezeCulling;
	j["showWireframe"] = graphicsSettings.showWireframe;
	j["enableOcclusionCulling"] = graphicsSettings.enableOcclusionCulling;
	j["debugShowHiZ"] = graphicsSettings.debugShowHiZ;
	j["debugShowCulling"] = graphicsSettings.debugShowCulling;

	std::ofstream file("graphics_settings.json");
	if (file.is_open()) {
		file << j.dump(4);
	}
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
	if (viewportDepth) glDeleteTextures(1, &viewportDepth);
	if (refractionTexture) glDeleteTextures(1, &refractionTexture);

	if (ssaoFBO) glDeleteFramebuffers(1, &ssaoFBO);
	if (ssaoBlurFBO) glDeleteFramebuffers(1, &ssaoBlurFBO);
	if (ssaoColorBuffer) glDeleteTextures(1, &ssaoColorBuffer);
	if (ssaoColorBufferBlur) glDeleteTextures(1, &ssaoColorBufferBlur);
	if (noiseTexture) glDeleteTextures(1, &noiseTexture);
	if (quadVAO) glDeleteVertexArrays(1, &quadVAO);
	if (quadVBO) glDeleteBuffers(1, &quadVBO);

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImNodes::DestroyContext();
	ImGui::DestroyContext();
}

// =====================================================================
// Game Viewport FBO
// =====================================================================

void Application::InitGameViewportFBO()
{
	int w = 640;
	int h = 480;

	currentGameViewportWidth = w;
	currentGameViewportHeight = h;

	glGenFramebuffers(1, &gameViewportFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, gameViewportFBO);

	glGenTextures(1, &gameViewportTexture);
	glBindTexture(GL_TEXTURE_2D, gameViewportTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gameViewportTexture, 0);

	glGenTextures(1, &gameViewportDepth);
	glBindTexture(GL_TEXTURE_2D, gameViewportDepth);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gameViewportDepth, 0);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		printf("Game Viewport Framebuffer not complete!\n");

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Application::ResizeGameViewportFBO(int width, int height)
{
	if (width < 1) width = 1;
	if (height < 1) height = 1;
	if (width == currentGameViewportWidth && height == currentGameViewportHeight && gameViewportTexture != 0) return;

	currentGameViewportWidth = width;
	currentGameViewportHeight = height;

	glBindTexture(GL_TEXTURE_2D, gameViewportTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);

	glBindTexture(GL_TEXTURE_2D, gameViewportDepth);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
}

// =====================================================================
// Play Mode State Transitions
// =====================================================================

void Application::StartPlayMode()
{
	printf("[Application] Entering Play Mode.\n");
	playState = PlayState::PlayMode;
	lastActiveViewportTab = -1; // Reset tab tracking

	// Backup all object transforms so we can restore them when stopping
	transformBackups.clear();
	for (auto* obj : sceneManager.GetObjects())
	{
		TransformBackup backup;
		backup.position = obj->GetTransform().GetPosition();
		backup.rotation = obj->GetTransform().GetRotation();
		backup.scale = obj->GetTransform().GetScale();
		transformBackups[obj] = backup;
	}

	// Sync/Recreate Jolt bodies for any pre-existing RigidBodies
	for (auto* obj : sceneManager.GetObjects())
	{
		if (auto* rb = obj->GetComponent<RigidBody>())
		{
			rb->RecreateBody();
		}
	}

	Player* player = sceneManager.FindPlayer();
	if (player)
	{
		// Initialize game camera from the player's position + eye height and rotation
		glm::vec3 playerPos = player->GetGameObject()->GetTransform().GetPosition();
		glm::vec3 playerRot = player->GetGameObject()->GetTransform().GetRotation(); // Euler angles (pitch, yaw, roll)
		
		// Default OpenGL camera has yaw = -90.0f pointing along negative Z.
		// If the player has 0 rotation in editor, they face along negative Z (yaw = -90.0f).
		float initialYaw = -90.0f + playerRot.y;
		float initialPitch = playerRot.x;

		gameCamera = Camera(
			playerPos + glm::vec3(0.0f, player->GetEyeHeight(), 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			initialYaw, initialPitch, 5.0f, 0.15f
		);

		// Reset the player's runtime physics state with initial rotation
		player->ResetPlayState(initialYaw, initialPitch);
	}

	// Keep cursor enabled by default so the user can interact with the scene/editor.
	// (They can lock/unlock it by pressing ESC or switching to the Game tab if a player exists).
	glfwSetInputMode(mainWindow.getWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	mainWindow.setCursorEnabled(true);
}

void Application::StopPlayMode()
{
	printf("[Application] Exiting Play Mode.\n");
	playState = PlayState::EditMode;
	lastActiveViewportTab = -1; // Reset tab tracking

	// Restore all object transforms to their pre-play state
	for (auto& [obj, backup] : transformBackups)
	{
		obj->GetTransform().SetPosition(backup.position);
		obj->GetTransform().SetRotation(backup.rotation);
		obj->GetTransform().SetScale(backup.scale);

		// Recreate body at original transform to clear velocities and snap back
		if (auto* rb = obj->GetComponent<RigidBody>())
		{
			rb->RecreateBody();
		}
	}
	transformBackups.clear();

	// Restore cursor for editor interaction
	glfwSetInputMode(mainWindow.getWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	mainWindow.setCursorEnabled(true);
}
