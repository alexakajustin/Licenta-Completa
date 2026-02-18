#define STB_IMAGE_IMPLEMENTATION
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <string.h>
#include <cmath>
#include <random>
#include <Windows.h>
#include <vector>

#include <GL\glew.h>

#include <GLFW\glfw3.h>

#include <glm\glm.hpp>
#include <glm\gtc\matrix_transform.hpp>
#include <glm\gtc\type_ptr.hpp>

#include "CommonValues.h"

#include "Window.h"
#include "Mesh.h"
#include "Shader.h"
#include "Camera.h"
#include "Texture.h"
#include "DirectionalLight.h"
#include "Material.h"
#include "PointLight.h"
#include "SpotLight.h"
#include "assimp\Importer.hpp"
 
#include "Model.h"

#include "Skybox.h"
#include "SceneManager.h"


Window mainWindow;

Camera camera;

GLfloat deltaTime = 0.0f;
GLfloat lastTime = 0.0f;

GLuint shader;
static const char* vShader = "Shaders/shader.vert";
static const char* fShader = "Shaders/shader.frag";

Texture brickTexture, dirtTexture, plainTexture, brokenBrickTexture, brokenBrickNormal;

Material shinyMaterial, dullMaterial, plainMaterial;

DirectionalLight mainLight;
PointLight pointLights[MAX_POINT_LIGHTS];
SpotLight spotLights[MAX_SPOT_LIGHTS];

Model rug, monitor, statue;

GLuint uniformProjection = 0, uniformModel = 0, uniformView = 0, uniformEyePosition = 0, uniformSpecularIntensity = 0, uniformShininess = 0,
uniformOmniLightPos, uniformFarPlane, uniformUseNormalMap;


std::vector<Mesh*> meshList;
std::vector<Shader> shaderList;
Shader directionalShadowShader;
Shader omniShadowShader;


unsigned int pointLightCount = 0;
unsigned int spotLightCount = 0;

Skybox skybox;

SceneManager sceneManager;

void calcAverageNormals(unsigned int * indices, unsigned int indiceCount, GLfloat * vertices, unsigned int verticeCount, 
						unsigned int vLength, unsigned int normalOffset)
{
	for (size_t i = 0; i < indiceCount; i += 3)
	{
		unsigned int in0 = indices[i] * vLength;
		unsigned int in1 = indices[i + 1] * vLength;
		unsigned int in2 = indices[i + 2] * vLength;
		glm::vec3 v1(vertices[in1] - vertices[in0], vertices[in1 + 1] - vertices[in0 + 1], vertices[in1 + 2] - vertices[in0 + 2]);
		glm::vec3 v2(vertices[in2] - vertices[in0], vertices[in2 + 1] - vertices[in0 + 1], vertices[in2 + 2] - vertices[in0 + 2]);
		glm::vec3 normal = glm::cross(v1, v2);
		normal = glm::normalize(normal);
		
		in0 += normalOffset; in1 += normalOffset; in2 += normalOffset;
		vertices[in0] += normal.x; vertices[in0 + 1] += normal.y; vertices[in0 + 2] += normal.z;
		vertices[in1] += normal.x; vertices[in1 + 1] += normal.y; vertices[in1 + 2] += normal.z;
		vertices[in2] += normal.x; vertices[in2 + 1] += normal.y; vertices[in2 + 2] += normal.z;
	}

	for (size_t i = 0; i < verticeCount / vLength; i++)
	{
		unsigned int nOffset = i * vLength + normalOffset;
		glm::vec3 vec(vertices[nOffset], vertices[nOffset + 1], vertices[nOffset + 2]);
		vec = glm::normalize(vec);
		vertices[nOffset] = vec.x; vertices[nOffset + 1] = vec.y; vertices[nOffset + 2] = vec.z;
	}
}

void CreateObjects()
{
	unsigned int indices[] = {
		0, 3, 1,
		1, 3, 2,
		2, 3, 0,
		0, 1, 2
	};

	//                                                                                              tangent         bitangent
	//	x      y      z			u	  v			nx	  ny    nz          tx   ty   tz       bx   by   bz
	GLfloat vertices[] = {
			-1.0f, -1.0f, -0.6f,		0.0f, 0.0f,		0.0f, 0.0f, 0.0f,		0.0f, 0.0f, 0.0f,		0.0f, 0.0f, 0.0f,
			0.0f, -1.0f, 1.0f,		0.5f, 0.0f,		0.0f, 0.0f, 0.0f,		0.0f, 0.0f, 0.0f,		0.0f, 0.0f, 0.0f,
			1.0f, -1.0f, -0.6f,		1.0f, 0.0f,		0.0f, 0.0f, 0.0f,		0.0f, 0.0f, 0.0f,		0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f,		0.5f, 1.0f,		0.0f, 0.0f, 0.0f,		0.0f, 0.0f, 0.0f,		0.0f, 0.0f, 0.0f
	};

	unsigned int floorIndices[] = {
		0, 2, 1,
		1, 2, 3
	};

	GLfloat floorVertices[] = {
		-10.0f, 0.0f, -10.0f,	0.0f, 0.0f,		0.0f, 1.0f, 0.0f,		1.0f, 0.0f, 0.0f,		0.0f, 0.0f, 1.0f,
		10.0f, 0.0f, -10.0f,	10.0f, 0.0f,	0.0f, 1.0f, 0.0f,		1.0f, 0.0f, 0.0f,		0.0f, 0.0f, 1.0f,
		-10.0f, 0.0f, 10.0f,	0.0f, 10.0f,	0.0f, 1.0f, 0.0f,		1.0f, 0.0f, 0.0f,		0.0f, 0.0f, 1.0f,
		10.0f, 0.0f, 10.0f,		10.0f, 10.0f,	0.0f, 1.0f, 0.0f,		1.0f, 0.0f, 0.0f,		0.0f, 0.0f, 1.0f
	};

	calcAverageNormals(indices, 12, vertices, 56, 14, 5);

	Mesh* obj1 = new Mesh();
	obj1->CreateMesh(vertices, indices, 56, 12);
	meshList.push_back(obj1);

	Mesh* obj2 = new Mesh();
	obj2->CreateMesh(vertices, indices, 56, 12);
	meshList.push_back(obj2);

	Mesh* obj3 = new Mesh();
	obj3->CreateMesh(floorVertices, floorIndices, 56, 6);
	meshList.push_back(obj3);
}

void CreateShaders()
{
	Shader* shader1 = new Shader();
	shader1->CreateFromFiles(vShader, fShader);
	shaderList.push_back(*shader1);

	directionalShadowShader = Shader();
	directionalShadowShader.CreateFromFiles("Shaders/directional_shadow_map.vert", "Shaders/directional_shadow_map.frag");
	omniShadowShader.CreateFromFiles("Shaders/omni_shadow_map.vert", "Shaders/omni_shadow_map.geom", "Shaders/omni_shadow_map.frag");
}

void RenderScene()
{
	sceneManager.RenderAll(uniformModel, uniformSpecularIntensity, uniformShininess, uniformUseNormalMap);
}

void DirectionalShadowMapPass(DirectionalLight* light)
{
	directionalShadowShader.UseShader();

	// make a viewport square viewport 
	glViewport(0, 0, light->GetShadowMap()->GetShadowWidth(), light->GetShadowMap()->GetShadowHeight());

	// activate write mode
	light->GetShadowMap()->Write();
	// clear information
	glClear(GL_DEPTH_BUFFER_BIT);

	uniformModel = directionalShadowShader.GetModelLocation();
	directionalShadowShader.SetDirectionalLightTransform(light->CalculateLightTransform());

	directionalShadowShader.Validate();

	RenderScene();

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OmniShadowMapPass(PointLight* light)
{
	omniShadowShader.UseShader();

	// make a viewport square viewport 
	glViewport(0, 0, light->GetShadowMap()->GetShadowWidth(), light->GetShadowMap()->GetShadowHeight());

	// activate write mode
	light->GetShadowMap()->Write();
	// clear information
	glClear(GL_DEPTH_BUFFER_BIT);

	uniformModel = omniShadowShader.GetModelLocation();
	uniformOmniLightPos = omniShadowShader.getOmniLightPosLocation();
	uniformFarPlane = omniShadowShader.getFarPlaneLocation();

	glUniform3f(uniformOmniLightPos, light->GetPosition().x, light->GetPosition().y, light->GetPosition().z);
	glUniform1f(uniformFarPlane, light->GetFarPlane());
	omniShadowShader.SetLightMatrices(light->CalculateLightTransform());

	omniShadowShader.Validate();

	RenderScene();

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderPass(glm::mat4 projectionMatrix, glm::mat4 viewMatrix)
{
	glViewport(0, 0, (GLint)mainWindow.getBufferWidth(), (GLint)mainWindow.getBufferHeight());

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Skybox is viewed from inside, so disable face culling for it
	glDisable(GL_CULL_FACE);
	skybox.DrawSkybox(viewMatrix, projectionMatrix);
	glEnable(GL_CULL_FACE);

	shaderList[0].UseShader();
	uniformModel = shaderList[0].GetModelLocation();
	uniformProjection = shaderList[0].GetProjectionLocation();
	uniformView = shaderList[0].GetViewLocation();
	uniformEyePosition = shaderList[0].GetEyePositionLocation();
	uniformSpecularIntensity = shaderList[0].GetSpecularIntensityLocation();
	uniformShininess = shaderList[0].GetShininessLocation();

	glUniformMatrix4fv(uniformProjection, 1, GL_FALSE, glm::value_ptr(projectionMatrix));
	glUniformMatrix4fv(uniformView, 1, GL_FALSE, glm::value_ptr(viewMatrix));
	glUniform3f(uniformEyePosition, camera.getCameraPosition().x, camera.getCameraPosition().y, camera.getCameraPosition().z);

	shaderList[0].SetDirectionalLight(&mainLight);
	shaderList[0].SetPointLights(pointLights, pointLightCount, 4, 0);
	shaderList[0].SetSpotLights(spotLights, spotLightCount, 4 + pointLightCount, pointLightCount);
	shaderList[0].SetDirectionalLightTransform(mainLight.
		CalculateLightTransform());

	mainLight.GetShadowMap()->Read(GL_TEXTURE3);
	shaderList[0].SetTexture(1);
	shaderList[0].SetNormalMap(2);
	shaderList[0].SetDirectionalShadowMap(3);
	uniformUseNormalMap = glGetUniformLocation(shaderList[0].GetShaderID(), "useNormalMap");

	glm::vec3 lowerLight = camera.getCameraPosition();
	lowerLight.y -= 0.1f;
	// spotLights[0].SetFlash(lowerLight, camera.getCameraDirection());

	shaderList[0].Validate();

	sceneManager.RenderAll(uniformModel, uniformSpecularIntensity, uniformShininess, uniformUseNormalMap);

	// Render light icons (billboards)
	sceneManager.RenderIcons(projectionMatrix, viewMatrix);

	// Render gizmo arrows for selected object
	sceneManager.RenderGizmo(projectionMatrix, viewMatrix, camera.getCameraPosition());
}

int main()
{
	mainWindow = Window(1920, 1080);

	mainWindow.Initialise();

	CreateObjects();

	CreateShaders();

	camera = Camera(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f, 1.0f, 0.25f);

	brickTexture = Texture( "Assets/Textures/brick.png");
	brickTexture.LoadTextureA();

	dirtTexture = Texture("Assets/Textures/dirt.png");
	dirtTexture.LoadTextureA();

	plainTexture = Texture("Assets/Textures/plain.png");
	plainTexture.LoadTextureA();

	brokenBrickTexture = Texture("Assets/Textures/broken_brick.jpg");
	brokenBrickTexture.LoadTextureA();

	brokenBrickNormal = Texture("Assets/Textures/broken_brick_normal.png");
	brokenBrickNormal.LoadTextureA();

	shinyMaterial = Material(1.0f, 32);
	dullMaterial = Material(0.3f, 4);
	plainMaterial = Material(0.1f, 32.0f); // Default shininess 32.0 for better highlights

	sceneManager.SetDefaultResources(&plainTexture, &plainMaterial);

	rug = Model();
	rug.LoadModel("Assets/Models/rug.obj");

	monitor = Model();	
	monitor.LoadModel("Assets/Models/monitor.obj");

	statue = Model();
	statue.LoadModel("Assets/Models/statue.obj");

	mainLight = DirectionalLight(2048, 2048, // width, height (keep shadow resolution)
		1.0f, 0.6f, 0.3f, // color: warm orange-gold tones
		0.6f, 0.7f, // ambient intensity (lower for softer shadows), diffuse intensity
		-10.0f, -5.0f, 20.0f); // direction: low angle from the horizon
	

	// POINT LIGHTS
	pointLights[0] = PointLight(1024, 1024, // width, height
								0.01f, 100.0f, // near, far
								0.0f, 1.0f, 0.0f,  // color
								0.0f, 0.4f, // ambient, diffuse
								10.0f, 5.0f, 0.0f, // position
								0.3f, 0.01f, 0.01f); // attenuation

	pointLightCount++;

	pointLights[1] = PointLight(1024, 1024, // width, height
		0.01f, 100.0f, // near, far
		0.0f, 0.0f, 1.0f,
		0.0f, 0.4f,
		-10.0f, 5.0f, 0.0f,
		0.3f, 0.01f, 0.01f);

	pointLightCount++;

	// SPOT LIGHTS
	spotLights[0] = SpotLight(1024, 1024, // width, height
		0.01f, 100.0f, // near, far
		1.0f, 1.0f, 1.0f, // red green blue
		0.0f, 2.0f, // ambient, diffuse
		20.0f, 5.0f, 0.0f, // xpos, ypos, zpos (will be overwritten by SetFlash)
		-50.0f, -1.0f, 0.0f, // xdir, ydir, zdir
		1.0f, 0.01f, 0.01f, // ct lin ex (added attenuation to prevent stripes!)
		20.0f); // edge angle
	spotLightCount++;

	spotLights[1] = SpotLight(1024, 1024, // width, height
							 0.01f, 100.0f, // near, far
							 1.0f, 0.0f, 0.0f, // red green blue
							 0.0f, 2.0f, // ambient, diffuse
							 5.0f, 8.0f, -5.0f, // xpos (higher up, to the side)
							 -2.0f, -1.0f, 0.5f, // xdir (angled towards center, hitting floor at angle)
						     0.3f, 0.01f, 0.01f, // ct lin ex
							 25.0f); // edge angle
	spotLightCount++;

	std::vector<std::string> skyboxFaces;
	skyboxFaces.push_back("Assets/Textures/Skybox/cupertin-lake_rt.tga");
	skyboxFaces.push_back("Assets/Textures/Skybox/cupertin-lake_lf.tga");
	skyboxFaces.push_back("Assets/Textures/Skybox/cupertin-lake_up.tga");
	skyboxFaces.push_back("Assets/Textures/Skybox/cupertin-lake_dn.tga");
	skyboxFaces.push_back("Assets/Textures/Skybox/cupertin-lake_bk.tga");
	skyboxFaces.push_back("Assets/Textures/Skybox/cupertin-lake_ft.tga");

	skybox = Skybox(skyboxFaces);

	// ============== SCENE SETUP ==============
	// Create GameObjects and add them to the SceneManager

	// Floor/Plane
	GameObject* floor = new GameObject("Plane");
	floor->GetTransform().SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
	floor->GetTransform().SetScale(glm::vec3(10.0f, 10.0f, 10.0f));
	floor->SetMesh(meshList[2]);
	floor->SetTexture(&brokenBrickTexture);
	floor->SetNormalMap(&brokenBrickNormal);
	floor->SetMaterial(&plainMaterial);
	sceneManager.AddObject(floor);

	// Monitor (has normal map: OldMonitor03Texture_normal.png)
	GameObject* monitorObj = new GameObject("Monitor");
	// Rotate slightly and lift off the floor to avoid z-fighting
	monitorObj->GetTransform().SetPosition(glm::vec3(0.0f, 0.1f, 0.0f));
	monitorObj->GetTransform().SetRotation(glm::vec3(0.0f, -45.0f, 0.0f));
	monitorObj->SetModel(&monitor);
	monitorObj->SetMaterial(&shinyMaterial);
	sceneManager.AddObject(monitorObj);

	// ============ LIGHTS IN HIERARCHY ============
	// Wrap lights in LightObjects for hierarchy display
	LightObject* mainLightObj = new LightObject("Sun", &mainLight);
	sceneManager.AddLight(mainLightObj);

	// Reset point/spot light counts to empty the scene of extra lights
	pointLightCount = 0;
	spotLightCount = 0;

	// ============================================

	glm::mat4 projection = glm::perspective(glm::radians(60.0f), (GLfloat)mainWindow.getBufferWidth() / (GLfloat)mainWindow.getBufferHeight(), 0.1f, 1000.0f);


	// Initialize ImGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(mainWindow.getWindow(), true);
	ImGui_ImplOpenGL3_Init("#version 330");

	// Initialize color picking
	sceneManager.InitPicking((int)mainWindow.getBufferWidth(), (int)mainWindow.getBufferHeight());
	sceneManager.InitIcons();
	sceneManager.InitGizmo();
	sceneManager.RefreshAssetList();

	sceneManager.SetLightArrays(pointLights, &pointLightCount, spotLights, &spotLightCount);

	bool lastLMBState = false;

	// ---------------- DONE WITH INITS------------------------------
	// -------------NOW IT IS THE TIME FOR THE LOOP-------------------
	while (!mainWindow.getShouldClose())
	{
		GLfloat now = glfwGetTime();
		deltaTime = now - lastTime;
		lastTime = now;

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		// handle user input
		glfwPollEvents();

		// Only process camera input when not in ImGui mode
		if (!mainWindow.isCursorEnabled())
		{
			camera.keyControl(mainWindow.getKeys(), deltaTime);
			camera.mouseControl(mainWindow.getXChange(), mainWindow.getYChange());
		}
		else
		{
			// Handle mouse input for picking and gizmo dragging
			bool currentLMBState = glfwGetMouseButton(mainWindow.getWindow(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
			double mouseX, mouseY;
			glfwGetCursorPos(mainWindow.getWindow(), &mouseX, &mouseY);

			if (!ImGui::GetIO().WantCaptureMouse)
			{
				if (currentLMBState && !lastLMBState)
				{
					// Button just pressed
					sceneManager.HandleMousePress(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, (float)mouseX, (float)mouseY, projection, camera.calculateViewMatrix(), camera.getCameraPosition());
				}
				else if (!currentLMBState && lastLMBState)
				{
					// Button just released
					sceneManager.HandleMousePress(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, (float)mouseX, (float)mouseY, projection, camera.calculateViewMatrix(), camera.getCameraPosition());
				}
				else if (currentLMBState)
				{
					// Button held - update dragging
					sceneManager.HandleMouseMove((float)mouseX, (float)mouseY, projection, camera.calculateViewMatrix());
				}
			}
			lastLMBState = currentLMBState;
		}

		// Scene Hierarchy and Inspector panels
		sceneManager.RenderImGui();


		
		DirectionalShadowMapPass(&mainLight);

		for (size_t i = 0; i < pointLightCount; i++)
		{
			OmniShadowMapPass(&pointLights[i]);
		}

		for (size_t i = 0; i < spotLightCount; i++)
		{
			OmniShadowMapPass(&spotLights[i]);
		}


		RenderPass(projection, camera.calculateViewMatrix());

		glUseProgram(0);

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		// Update projection matrix if window size changes
		projection = glm::perspective(glm::radians(60.0f), (GLfloat)mainWindow.getBufferWidth() / (GLfloat)mainWindow.getBufferHeight(), 0.1f, 1000.0f);

		// swap frame buffers (back -> front)
		mainWindow.swapBuffers();
	}
	ImGui::DestroyContext();
}
