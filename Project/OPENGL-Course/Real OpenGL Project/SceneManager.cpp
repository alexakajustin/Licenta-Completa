#include "SceneManager.h"
#include "PrimitiveGenerator.h"
#include <iostream>

// Helper for Unity-like Vector3 input
static void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float speed = 0.1f)
{
	ImGui::PushID(label.c_str());

	ImGui::Text(label.c_str());
	
	// Right-align the inputs
	float totalWidth = ImGui::GetContentRegionAvail().x;
	float inputWidth = (totalWidth - 20.0f) / 3.0f; // Roughly divide remaining space
	
	ImGui::SameLine(70.0f); // Fixed label width

	ImGui::PushItemWidth(inputWidth);
	
	// X
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
	ImGui::DragFloat("##X", &values.x, speed, 0.0f, 0.0f, "X:%.2f");
	ImGui::PopStyleColor();
	if (ImGui::IsItemClicked(1)) values.x = resetValue; // Right click to reset
	
	ImGui::SameLine(0, 5);
	
	// Y
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
	ImGui::DragFloat("##Y", &values.y, speed, 0.0f, 0.0f, "Y:%.2f");
	ImGui::PopStyleColor();
	if (ImGui::IsItemClicked(1)) values.y = resetValue;
	
	ImGui::SameLine(0, 5);
	
	// Z
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 1.0f, 1.0f));
	ImGui::DragFloat("##Z", &values.z, speed, 0.0f, 0.0f, "Z:%.2f");
	ImGui::PopStyleColor();
	if (ImGui::IsItemClicked(1)) values.z = resetValue;

	ImGui::PopItemWidth();
	ImGui::PopID();
}

SceneManager::SceneManager()
	: selectedObjectIndex(-1), selectedLightIndex(-1), pickingFBO(0), pickingTexture(0), pickingDepth(0),
	  pickWidth(0), pickHeight(0), pickingInitialized(false),
	  lightIconTexture(nullptr), iconMesh(nullptr)
{
}

SceneManager::~SceneManager()
{
	Clear();
	
	// Cleanup picking resources
	if (pickingFBO) glDeleteFramebuffers(1, &pickingFBO);
	if (pickingTexture) glDeleteTextures(1, &pickingTexture);
	if (pickingDepth) glDeleteRenderbuffers(1, &pickingDepth);

	// Cleanup icons
	if (lightIconTexture) delete lightIconTexture;
	if (iconMesh) delete iconMesh;
}

void SceneManager::AddObject(GameObject* obj)
{
	if (obj)
	{
		objects.push_back(obj);
	}
}

void SceneManager::RemoveObject(const std::string& name)
{
	for (auto it = objects.begin(); it != objects.end(); ++it)
	{
		if ((*it)->GetName() == name)
		{
			delete* it;
			objects.erase(it);
			return;
		}
	}
}

GameObject* SceneManager::FindObject(const std::string& name)
{
	for (auto* obj : objects)
	{
		if (obj->GetName() == name)
		{
			return obj;
		}
	}
	return nullptr;
}

void SceneManager::RenderAll(GLuint uniformModel, GLuint uniformSpecularIntensity, GLuint uniformShininess, GLuint uniformUseNormalMap)
{
	for (auto* obj : objects)
	{
		obj->Render(uniformModel, uniformSpecularIntensity, uniformShininess, uniformUseNormalMap);
	}
}

void SceneManager::AddLight(LightObject* light)
{
	if (light)
	{
		lights.push_back(light);
	}
}

void SceneManager::InitPicking(int width, int height)
{
	pickWidth = width;
	pickHeight = height;

	// Create framebuffer
	glGenFramebuffers(1, &pickingFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, pickingFBO);

	// Create color texture
	glGenTextures(1, &pickingTexture);
	glBindTexture(GL_TEXTURE_2D, pickingTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pickingTexture, 0);

	// Create depth buffer
	glGenRenderbuffers(1, &pickingDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, pickingDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, pickingDepth);

	// Check completeness
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		printf("Picking framebuffer not complete!\n");
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// Load picking shader
	pickingShader.CreateFromFiles("Shaders/picking.vert", "Shaders/picking.frag");

	pickingInitialized = true;
}

int SceneManager::PickObject(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view)
{
	if (!pickingInitialized) return -1;

	// Bind picking framebuffer
	glBindFramebuffer(GL_FRAMEBUFFER, pickingFBO);
	glViewport(0, 0, pickWidth, pickHeight);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	pickingShader.UseShader();

	// Set projection and view
	glUniformMatrix4fv(pickingShader.GetProjectionLocation(), 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(pickingShader.GetViewLocation(), 1, GL_FALSE, glm::value_ptr(view));

	GLuint modelLoc = pickingShader.GetModelLocation();
	GLuint colorLoc = glGetUniformLocation(pickingShader.GetShaderID(), "pickingColor");
	GLuint isBillboardLoc = glGetUniformLocation(pickingShader.GetShaderID(), "isBillboard");
	GLuint worldPosLoc = glGetUniformLocation(pickingShader.GetShaderID(), "worldPos");
	GLuint iconSizeLoc = glGetUniformLocation(pickingShader.GetShaderID(), "iconSize");

	glUniform1i(isBillboardLoc, 0); // Not a billboard for normal objects

	// Render each object with unique color based on index
	for (int i = 0; i < (int)objects.size(); i++)
	{
		// Convert index to RGB color (index + 1 to avoid black background)
		int id = i + 1;
		float r = ((id & 0x0000FF) >> 0) / 255.0f;
		float g = ((id & 0x00FF00) >> 8) / 255.0f;
		float b = ((id & 0xFF0000) >> 16) / 255.0f;

		glUniform3f(colorLoc, r, g, b);

		// Set model matrix
		glm::mat4 modelMatrix = objects[i]->GetTransform().GetModelMatrix();
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(modelMatrix));

		// Render geometry only
		if (objects[i]->GetModel())
		{
			objects[i]->GetModel()->RenderModel(0); // picking pass doesn't use normal maps
		}
		else if (objects[i]->GetMesh())
		{
			objects[i]->GetMesh()->RenderMesh();
		}
	}

	// Render light icons for picking
	if (iconMesh)
	{
		glUniform1i(isBillboardLoc, 1);
		glUniform1f(iconSizeLoc, 0.5f);

		for (int i = 0; i < (int)lights.size(); i++)
		{
			// Light IDs start at 10000
			int id = i + 10000;
			float r = ((id & 0x0000FF) >> 0) / 255.0f;
			float g = ((id & 0x00FF00) >> 8) / 255.0f;
			float b = ((id & 0xFF0000) >> 16) / 255.0f;

			glUniform3f(colorLoc, r, g, b);

			glm::vec3* pos = lights[i]->GetPositionPtr();
			if (pos)
			{
				glUniform3f(worldPosLoc, pos->x, pos->y, pos->z);
				iconMesh->RenderMesh();
			}
		}
	}

	// Read pixel at mouse position
	glFlush();
	glFinish();

	unsigned char pixel[3];
	int readX = (int)mouseX;
	int readY = pickHeight - (int)mouseY; // Flip Y
	glReadPixels(readX, readY, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);

	// Convert color back to index
	int pickedID = pixel[0] + pixel[1] * 256 + pixel[2] * 256 * 256;

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// Handle object picking
	if (pickedID > 0 && pickedID <= (int)objects.size())
	{
		selectedObjectIndex = pickedID - 1;
		selectedLightIndex = -1;
		return 1; // Object picked
	}

	// Handle light picking
	if (pickedID >= 10000 && pickedID < 10000 + (int)lights.size())
	{
		selectedLightIndex = pickedID - 10000;
		selectedObjectIndex = -1;
		return 2; // Light picked
	}

	// Nothing picked - clear selection
	selectedObjectIndex = -1;
	selectedLightIndex = -1;
	return 0;
}

void SceneManager::RenderImGui()
{
	// 1. Scene Hierarchy Window
	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(300, 450), ImGuiCond_FirstUseEver);
	
	ImGui::Begin("Scene Hierarchy", &windowState.isHierarchyOpen);
	
	if (ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (int i = 0; i < (int)objects.size(); i++)
		{
			ImGui::PushID(i);
			bool isSelected = (selectedObjectIndex == i && selectedLightIndex < 0);
			if (ImGui::Selectable(objects[i]->GetName().c_str(), isSelected))
			{
				selectedObjectIndex = i;
				selectedLightIndex = -1;
			}
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Delete")) {
					delete objects[i];
					objects.erase(objects.begin() + i);
					selectedObjectIndex = -1;
					ImGui::EndPopup(); ImGui::PopID(); break;
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
	}

	if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (int i = 0; i < (int)lights.size(); i++)
		{
			ImGui::PushID(1000 + i);
			const char* icon = (lights[i]->GetLightType() == LightType::Directional) ? "[D] " : 
							   (lights[i]->GetLightType() == LightType::Point) ? "[P] " : "[S] ";
			std::string label = std::string(icon) + lights[i]->GetName();
			bool isSelected = (selectedLightIndex == i && selectedObjectIndex < 0);
			if (ImGui::Selectable(label.c_str(), isSelected))
			{
				selectedLightIndex = i;
				selectedObjectIndex = -1;
			}
			ImGui::PopID();
		}
	}

	if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight))
	{
		if (ImGui::BeginMenu("Create GameObject"))
		{
			if (ImGui::MenuItem("Plane")) CreateGameObject("Plane");
			if (ImGui::MenuItem("Cube")) CreateGameObject("Cube");
			if (ImGui::MenuItem("Sphere")) CreateGameObject("Sphere");
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Create Light"))
		{
			if (ImGui::MenuItem("Point Light")) CreateLight(LightType::Point);
			if (ImGui::MenuItem("Spot Light")) CreateLight(LightType::Spot);
			ImGui::EndMenu();
		}
		ImGui::EndPopup();
	}
	ImGui::End();

	// 2. Inspector Window
	bool showObjectInspector = (selectedObjectIndex >= 0 && selectedLightIndex < 0);
	bool showLightInspector = (selectedLightIndex >= 0 && selectedObjectIndex < 0);

	if (showObjectInspector || showLightInspector)
	{
		ImGui::SetNextWindowPos(ImVec2(0, 450), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(300, 500), ImGuiCond_FirstUseEver);

		ImGui::Begin("Inspector", &windowState.isInspectorOpen);

		if (showObjectInspector)
		{
			GameObject* selected = objects[selectedObjectIndex];
			Transform& transform = selected->GetTransform();
			ImGui::Text("Object: %s", selected->GetName().c_str());
			ImGui::Separator();
			ImGui::Text("Transform");
			
			DrawVec3Control("Position", *transform.GetPositionPtr(), 0.0f, 0.1f);
			DrawVec3Control("Rotation", *transform.GetRotationPtr(), 0.0f, 1.0f);
			DrawVec3Control("Scale", *transform.GetScalePtr(), 1.0f, 0.01f);
			
			ImGui::Separator();
			ImGui::Text("Components");
			if (selected->GetMesh()) ImGui::BulletText("Mesh");
			if (selected->GetModel()) ImGui::BulletText("Model");
			if (selected->GetTexture()) ImGui::BulletText("Texture");
			if (selected->GetMaterial()) ImGui::BulletText("Material");
		}
		else if (showLightInspector)
		{
			LightObject* light = lights[selectedLightIndex];
			ImGui::Text("Light: %s", light->GetName().c_str());
			ImGui::Separator();
			ImGui::ColorEdit3("Color", &light->GetColorPtr()->x);
			ImGui::SliderFloat("Ambient", light->GetAmbientIntensityPtr(), 0.0f, 1.0f);
			ImGui::SliderFloat("Diffuse", light->GetDiffuseIntensityPtr(), 0.0f, 2.0f);
			
			if (light->GetPositionPtr()) DrawVec3Control("Position", *light->GetPositionPtr(), 0.0f, 0.1f);
			if (light->GetDirectionPtr()) DrawVec3Control("Direction", *light->GetDirectionPtr(), 0.0f, 0.01f);
			
			if (light->GetConstantPtr()) {
				ImGui::Separator();
				ImGui::Text("Attenuation");
				ImGui::SliderFloat("Constant", light->GetConstantPtr(), 0.01f, 2.0f);
				ImGui::SliderFloat("Linear", light->GetLinearPtr(), 0.001f, 0.5f);
				ImGui::SliderFloat("Exponent", light->GetExponentPtr(), 0.001f, 0.5f);
			}
		}
		ImGui::End();
	}
}

void SceneManager::CreateGameObject(const std::string& type)
{
	GameObject* newObj = new GameObject(type + " " + std::to_string(objects.size()));
	
	if (type == "Plane") {
		newObj->SetMesh(PrimitiveGenerator::CreatePlane());
	} else if (type == "Cube") {
		newObj->SetMesh(PrimitiveGenerator::CreateCube());
	} else if (type == "Sphere") {
		newObj->SetMesh(PrimitiveGenerator::CreateSphere());
	}

	// Set default texture and material
	if (defaultTexture) newObj->SetTexture(defaultTexture);
	if (defaultMaterial) newObj->SetMaterial(defaultMaterial);
	
	objects.push_back(newObj);
	selectedObjectIndex = (int)objects.size() - 1;
	selectedLightIndex = -1;
}

void SceneManager::CreateLight(LightType type)
{
	if (type == LightType::Point) {
		if (globalPointLights && globalPointLightCount && *globalPointLightCount < MAX_POINT_LIGHTS) {
			unsigned int idx = *globalPointLightCount;
			globalPointLights[idx] = PointLight(1024, 1024, 0.01f, 100.0f, 1.0f, 1.0f, 1.0f, 0.1f, 0.8f, 0.0f, 5.0f, 0.0f, 0.3f, 0.02f, 0.01f);
			
			LightObject* newLightObj = new LightObject("Point Light " + std::to_string(lights.size()), &globalPointLights[idx]);
			lights.push_back(newLightObj);
			(*globalPointLightCount)++;
			selectedLightIndex = (int)lights.size() - 1;
			selectedObjectIndex = -1;
		}
	} else if (type == LightType::Spot) {
		if (globalSpotLights && globalSpotLightCount && *globalSpotLightCount < MAX_SPOT_LIGHTS) {
			unsigned int idx = *globalSpotLightCount;
			globalSpotLights[idx] = SpotLight(1024, 1024, 0.01f, 100.0f, 1.0f, 1.0f, 1.0f, 0.1f, 1.0f, 0.0f, 5.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.02f, 0.01f, 20.0f);
			
			LightObject* newLightObj = new LightObject("Spot Light " + std::to_string(lights.size()), &globalSpotLights[idx]);
			lights.push_back(newLightObj);
			(*globalSpotLightCount)++;
			selectedLightIndex = (int)lights.size() - 1;
			selectedObjectIndex = -1;
		}
	}
}

void SceneManager::InitIcons()
{
	iconShader.CreateFromFiles("Shaders/icon.vert", "Shaders/icon.frag");
	
	if (iconShader.GetShaderID() == 0) {
		printf("Icon shader failed to initialize!\n");
		return;
	}

	lightIconTexture = new Texture("Icons/Light.png");
	if (!lightIconTexture->LoadTextureA()) {
		printf("Failed to load Light.png icon!\n");
		delete lightIconTexture;
		lightIconTexture = nullptr;
		return;
	}

	CreateIconMesh();
}

void SceneManager::CreateIconMesh()
{
	unsigned int indices[] = {
		0, 2, 1,
		1, 2, 3
	};

	GLfloat vertices[] = {
		// x     y      z     u     v     nx    ny    nz    tx    ty    tz    bx    by    bz
		-0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		 0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		-0.5f,  0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		 0.5f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
	};

	iconMesh = new Mesh();
	iconMesh->CreateMesh(vertices, indices, 56, 6);
}

void SceneManager::RenderIcons(glm::mat4 projection, glm::mat4 view)
{
	if (!iconMesh || !lightIconTexture || iconShader.GetShaderID() == 0) return;

	// Clear any previous errors to isolate the issue
	while (glGetError() != GL_NO_ERROR);

	iconShader.UseShader();
	
	GLuint projLoc = iconShader.GetProjectionLocation();
	GLuint viewLoc = iconShader.GetViewLocation();
	
	if (projLoc != (GLuint)-1) glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
	if (viewLoc != (GLuint)-1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
	
	GLuint worldPosLoc = glGetUniformLocation(iconShader.GetShaderID(), "worldPos");
	GLuint iconSizeLoc = glGetUniformLocation(iconShader.GetShaderID(), "iconSize");
	GLuint textureLoc = glGetUniformLocation(iconShader.GetShaderID(), "theTexture");
	GLuint iconColorLoc = glGetUniformLocation(iconShader.GetShaderID(), "iconColor");
	
	// Force unit 0 specifically for icons to avoid unit 1 mismatch from Texture::UseTexture
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, lightIconTexture->GetTextureID()); // Bypass UseTexture() hardcoded unit
	
	if (textureLoc != (GLuint)-1) glUniform1i(textureLoc, 0); 
	if (iconSizeLoc != (GLuint)-1) glUniform1f(iconSizeLoc, 0.5f); 

	for (auto* light : lights)
	{
		if (!light) continue;

		glm::vec3* pos = light->GetPositionPtr();
		if (pos)
		{
			if (worldPosLoc != (GLuint)-1) glUniform3f(worldPosLoc, pos->x, pos->y, pos->z);
			
			glm::vec3* color = light->GetColorPtr();
			if (iconColorLoc != (GLuint)-1 && color) glUniform3f(iconColorLoc, color->x, color->y, color->z);
			
			iconMesh->RenderMesh();
		}
	}

	glUseProgram(0);
	
	// Final check for errors
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		printf("OpenGL error in RenderIcons: 0x%x\n", err);
	}
}

void SceneManager::Clear()
{
	for (auto* obj : objects)
	{
		delete obj;
	}
	objects.clear();
	
	for (auto* light : lights)
	{
		delete light;
	}
	lights.clear();
	
	selectedObjectIndex = -1;
	selectedLightIndex = -1;
}
