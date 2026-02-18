#include "SceneManager.h"
#include "PrimitiveGenerator.h"
#include <iostream>
#include <GLFW/glfw3.h>

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
	  lightIconTexture(nullptr), iconMesh(nullptr), gizmoArrowModel(nullptr), gizmoTorusModel(nullptr),
	  currentAssetPath("Assets")
{
}

SceneManager::~SceneManager()
{
	Clear();
	
	// Cleanup picking resources
	if (pickingFBO) glDeleteFramebuffers(1, &pickingFBO);
	if (pickingTexture) glDeleteTextures(1, &pickingTexture);
	if (pickingDepth) glDeleteRenderbuffers(1, &pickingDepth);

	// Cleanup gizmo
	if (gizmoArrowModel) {
		gizmoArrowModel->ClearModel();
		delete gizmoArrowModel;
	}
	if (gizmoTorusModel) {
		gizmoTorusModel->ClearModel();
		delete gizmoTorusModel;
	}

	// Cleanup asset icons
	for (auto const& [key, val] : assetTextureCache) {
		if (val) {
			val->ClearTexture();
			delete val;
		}
	}
	if (folderIconSlot) { folderIconSlot->ClearTexture(); delete folderIconSlot; }
	if (modelIconSlot) { modelIconSlot->ClearTexture(); delete modelIconSlot; }
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

int SceneManager::PickObject(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, glm::vec3 cameraPos)
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
		glDisable(GL_CULL_FACE); // Billboard quads need culling disabled (matches RenderIcons)
		glDisable(GL_DEPTH_TEST); // Icons should always be pickable when visible
		glUniform1i(isBillboardLoc, 1);
		glUniform1f(iconSizeLoc, 0.7f); // Slightly larger picking area for better feel

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

	// Render gizmo tori for picking (FIRST, so arrows can be on top)
	if (gizmoTorusModel && (selectedObjectIndex != -1 || selectedLightIndex != -1))
	{
		glUniform1i(isBillboardLoc, 0);
		glDisable(GL_DEPTH_TEST);

		glm::vec3 gizmoPos(0.0f);
		glm::mat4 objRot(1.0f);
		if (selectedObjectIndex != -1) {
			gizmoPos = objects[selectedObjectIndex]->GetTransform().GetPosition();
			glm::vec3 r = objects[selectedObjectIndex]->GetTransform().GetRotation();
			objRot = glm::rotate(objRot, glm::radians(r.y), glm::vec3(0.0f, 1.0f, 0.0f));
			objRot = glm::rotate(objRot, glm::radians(r.x), glm::vec3(1.0f, 0.0f, 0.0f));
			objRot = glm::rotate(objRot, glm::radians(r.z), glm::vec3(0.0f, 0.0f, 1.0f));
		}
		else {
			glm::vec3* lightPos = lights[selectedLightIndex]->GetPositionPtr();
			if (lightPos) gizmoPos = *lightPos;
		}

		glDisable(GL_CULL_FACE); // Gizmos should be pickable from any angle
		float dist = glm::length(gizmoPos - cameraPos);
		float torusPickScale = dist * 0.1f * 0.6f; // Reduced from 1.3f

		// X Rotation (20004)
		glUniform3f(colorLoc, (20004 & 0xFF) / 255.0f, ((20004 >> 8) & 0xFF) / 255.0f, ((20004 >> 16) & 0xFF) / 255.0f);
		glm::mat4 rotX = glm::translate(glm::mat4(1.0f), gizmoPos);
		rotX = rotX * objRot;
		rotX = glm::rotate(rotX, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		rotX = glm::scale(rotX, glm::vec3(torusPickScale));
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(rotX));
		gizmoTorusModel->RenderModel(0);

		// Y Rotation (20005)
		glUniform3f(colorLoc, (20005 & 0xFF) / 255.0f, ((20005 >> 8) & 0xFF) / 255.0f, ((20005 >> 16) & 0xFF) / 255.0f);
		glm::mat4 rotY = glm::translate(glm::mat4(1.0f), gizmoPos);
		rotY = rotY * objRot;
		rotY = glm::scale(rotY, glm::vec3(torusPickScale));
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(rotY));
		gizmoTorusModel->RenderModel(0);

		// Z Rotation (20006)
		glUniform3f(colorLoc, (20006 & 0xFF) / 255.0f, ((20006 >> 8) & 0xFF) / 255.0f, ((20006 >> 16) & 0xFF) / 255.0f);
		glm::mat4 rotZ = glm::translate(glm::mat4(1.0f), gizmoPos);
		rotZ = rotZ * objRot;
		rotZ = glm::rotate(rotZ, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		rotZ = glm::scale(rotZ, glm::vec3(torusPickScale));
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(rotZ));
		gizmoTorusModel->RenderModel(0);
	}

	// Render gizmo arrows for picking (SECOND, so they take priority for the pixel)
	if (gizmoArrowModel && (selectedObjectIndex != -1 || selectedLightIndex != -1))
	{
		glUniform1i(isBillboardLoc, 0);
		glDisable(GL_DEPTH_TEST);

		glm::vec3 gizmoPos(0.0f);
		if (selectedObjectIndex != -1) gizmoPos = objects[selectedObjectIndex]->GetTransform().GetPosition();
		else {
			glm::vec3* lightPos = lights[selectedLightIndex]->GetPositionPtr();
			if (lightPos) gizmoPos = *lightPos;
			else {
				// Cleanup state before return
				glEnable(GL_DEPTH_TEST);
				glEnable(GL_CULL_FACE);
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				return 0;
			}
		}

		float dist = glm::length(gizmoPos - cameraPos);
		float scaleFactor = dist * 0.1f; 

		// Calculate object rotation matrix
		glm::mat4 objRot(1.0f);
		if (selectedObjectIndex != -1) {
			glm::vec3 r = objects[selectedObjectIndex]->GetTransform().GetRotation();
			objRot = glm::rotate(objRot, glm::radians(r.y), glm::vec3(0.0f, 1.0f, 0.0f));
			objRot = glm::rotate(objRot, glm::radians(r.x), glm::vec3(1.0f, 0.0f, 0.0f));
			objRot = glm::rotate(objRot, glm::radians(r.z), glm::vec3(0.0f, 0.0f, 1.0f));
		}

		// X Axis (20001)
		glUniform3f(colorLoc, (20001 & 0xFF) / 255.0f, ((20001 >> 8) & 0xFF) / 255.0f, ((20001 >> 16) & 0xFF) / 255.0f);
		glm::mat4 modelX = glm::translate(glm::mat4(1.0f), gizmoPos);
		modelX = modelX * objRot;
		modelX = glm::rotate(modelX, glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		modelX = glm::scale(modelX, glm::vec3(scaleFactor));
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(modelX));
		gizmoArrowModel->RenderModel(0);

		// Y Axis (20002)
		glUniform3f(colorLoc, (20002 & 0xFF) / 255.0f, ((20002 >> 8) & 0xFF) / 255.0f, ((20002 >> 16) & 0xFF) / 255.0f);
		glm::mat4 modelY = glm::translate(glm::mat4(1.0f), gizmoPos);
		modelY = modelY * objRot;
		modelY = glm::scale(modelY, glm::vec3(scaleFactor));
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(modelY));
		gizmoArrowModel->RenderModel(0);

		// Z Axis (20003)
		glUniform3f(colorLoc, (20003 & 0xFF) / 255.0f, ((20003 >> 8) & 0xFF) / 255.0f, ((20003 >> 16) & 0xFF) / 255.0f);
		glm::mat4 modelZ = glm::translate(glm::mat4(1.0f), gizmoPos);
		modelZ = modelZ * objRot;
		modelZ = glm::rotate(modelZ, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		modelZ = glm::scale(modelZ, glm::vec3(scaleFactor));
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(modelZ));
		gizmoArrowModel->RenderModel(0);
	}

	// Restore GL state after icon rendering
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);

	// Read pixel at mouse position
	glFlush();
	glFinish();

	// Map window-space mouse coordinates to framebuffer-space (handles DPI scaling/window resizing)
	int windowWidth, windowHeight;
	glfwGetWindowSize(glfwGetCurrentContext(), &windowWidth, &windowHeight);
	float scaleX = (float)pickWidth / (float)windowWidth;
	float scaleY = (float)pickHeight / (float)windowHeight;

	unsigned char pixel[3];
	int readX = (int)(mouseX * scaleX);
	int readY = pickHeight - (int)(mouseY * scaleY); // Flip Y correctly in buffer space
	glReadPixels(readX, readY, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);

	// Convert color back to index
	int pickedID = pixel[0] + pixel[1] * 256 + pixel[2] * 256 * 256;

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// Handle selection logic based on the ID
	if (pickedID > 0 && pickedID <= (int)objects.size())
	{
		selectedObjectIndex = pickedID - 1;
		selectedLightIndex = -1;
	}
	else if (pickedID >= 10000 && pickedID < 10000 + (int)lights.size())
	{
		selectedLightIndex = pickedID - 10000;
		selectedObjectIndex = -1;
	}
	else if (pickedID < 20000) // If we didn't hit a gizmo, clear selection
	{
		// But don't clear if we're clicking on UI or something else handled externally
		// For now, keep the original behavior but only if it's not a gizmo
		selectedObjectIndex = -1;
		selectedLightIndex = -1;
	}

	return pickedID;
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

	// Delete key - delete selected object or light
	if (ImGui::IsKeyPressed(ImGuiKey_Delete))
	{
		if (selectedObjectIndex >= 0 && selectedObjectIndex < (int)objects.size() && selectedLightIndex < 0)
		{
			delete objects[selectedObjectIndex];
			objects.erase(objects.begin() + selectedObjectIndex);
			selectedObjectIndex = -1;
		}
		else if (selectedLightIndex >= 0 && selectedLightIndex < (int)lights.size() && selectedObjectIndex < 0)
		{
			DeleteLight(selectedLightIndex);
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

	RenderAssetBrowser();
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
			globalPointLights[idx] = PointLight(1024, 1024, 0.01f, 100.0f, 1.0f, 1.0f, 1.0f, 0.1f, 0.8f, 0.0f, 5.0f, 0.0f, 1.0f, 0.02f, 0.01f);
			
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

void SceneManager::DeleteLight(int index)
{
	if (index < 0 || index >= (int)lights.size()) return;

	LightObject* light = lights[index];
	LightType type = light->GetLightType();

	// Never delete the directional light (Sun)
	if (type == LightType::Directional) return;

	// Remove from the global light array by compacting it and fix remaining pointers
	if (type == LightType::Point && globalPointLights && globalPointLightCount) {
		PointLight* ptr = light->GetPointLight();
		int arrayIdx = (int)(ptr - globalPointLights);
		if (arrayIdx >= 0 && arrayIdx < (int)*globalPointLightCount) {
			for (unsigned int j = arrayIdx; j < *globalPointLightCount - 1; j++) {
				globalPointLights[j] = globalPointLights[j + 1];
			}
			(*globalPointLightCount)--;

			// Update pointers in remaining LightObjects that pointed past the deleted slot
			for (auto* lo : lights) {
				if (lo == light) continue;
				if (lo->GetLightType() == LightType::Point && lo->GetPointLight() > ptr) {
					lo->SetPointLight(lo->GetPointLight() - 1);
				}
			}
		}
	}
	else if (type == LightType::Spot && globalSpotLights && globalSpotLightCount) {
		SpotLight* ptr = light->GetSpotLight();
		int arrayIdx = (int)(ptr - globalSpotLights);
		if (arrayIdx >= 0 && arrayIdx < (int)*globalSpotLightCount) {
			for (unsigned int j = arrayIdx; j < *globalSpotLightCount - 1; j++) {
				globalSpotLights[j] = globalSpotLights[j + 1];
			}
			(*globalSpotLightCount)--;

			// Update pointers in remaining LightObjects that pointed past the deleted slot
			for (auto* lo : lights) {
				if (lo == light) continue;
				if (lo->GetLightType() == LightType::Spot && lo->GetSpotLight() > ptr) {
					lo->SetSpotLight(lo->GetSpotLight() - 1);
				}
			}
		}
	}

	// Remove from lights vector and delete the wrapper
	delete light;
	lights.erase(lights.begin() + index);
	selectedLightIndex = -1;
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

void SceneManager::InitGizmo()
{
	gizmoShader.CreateFromFiles("Shaders/gizmo.vert", "Shaders/gizmo.frag");
	
	if (gizmoShader.GetShaderID() == 0) {
		printf("Gizmo shader failed to initialize!\n");
		return;
	}

	gizmoArrowModel = new Model();
	gizmoArrowModel->LoadModel("Utils/arrow.obj");

	gizmoTorusModel = new Model();
	gizmoTorusModel->LoadModel("Utils/torus.obj");
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
	
	// Disable face culling for billboards (they may face either direction)
	glDisable(GL_CULL_FACE);
	
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
	
	// Re-enable face culling after billboard rendering
	glEnable(GL_CULL_FACE);
	
	// Final check for errors
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		printf("OpenGL error in RenderIcons: 0x%x\n", err);
	}
}

void SceneManager::RenderGizmo(glm::mat4 projection, glm::mat4 view, glm::vec3 cameraPos)
{
	// Only render if something is selected
	if (selectedObjectIndex == -1 && selectedLightIndex == -1) return;
	if (!gizmoArrowModel || gizmoShader.GetShaderID() == 0) return;

	glm::vec3 gizmoPos(0.0f);
	if (selectedObjectIndex != -1) {
		gizmoPos = objects[selectedObjectIndex]->GetTransform().GetPosition();
	} else {
		glm::vec3* lightPos = lights[selectedLightIndex]->GetPositionPtr();
		if (lightPos) gizmoPos = *lightPos;
		else return; // Don't render gizmo for directional lights with no position
	}

	// Dynamic scale based on distance to camera to maintain constant screen size
	float dist = glm::length(gizmoPos - cameraPos);
	float scaleFactor = dist * 0.1f; // Scaled down for better visual balance
	glm::vec3 scale(scaleFactor);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	gizmoShader.UseShader();
	
	GLuint projLoc = gizmoShader.GetProjectionLocation();
	GLuint viewLoc = gizmoShader.GetViewLocation();
	GLuint modelLoc = gizmoShader.GetModelLocation();
	GLuint colorLoc = glGetUniformLocation(gizmoShader.GetShaderID(), "gizmoColor");

	glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));

	// Calculate object rotation matrix
	glm::mat4 objRot(1.0f);
	if (selectedObjectIndex != -1) {
		glm::vec3 r = objects[selectedObjectIndex]->GetTransform().GetRotation();
		objRot = glm::rotate(objRot, glm::radians(r.y), glm::vec3(0.0f, 1.0f, 0.0f));
		objRot = glm::rotate(objRot, glm::radians(r.x), glm::vec3(1.0f, 0.0f, 0.0f));
		objRot = glm::rotate(objRot, glm::radians(r.z), glm::vec3(0.0f, 0.0f, 1.0f));
	}

	// --- Rotation Tori (Draw FIRST) ---
	float torusScaleFactor = dist * 0.1f * 0.6f; // Reduced from 1.3f
	glm::vec3 torusScale(torusScaleFactor);

	// X Rotation (Red) - YZ plane
	glm::mat4 rotX = glm::translate(glm::mat4(1.0f), gizmoPos);
	rotX = rotX * objRot;
	rotX = glm::rotate(rotX, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	rotX = glm::scale(rotX, torusScale);
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(rotX));
	if (activeDragAxis == 20004) glUniform3f(colorLoc, 1.0f, 1.0f, 0.0f); // Gold/Yellow when active
	else glUniform3f(colorLoc, 1.0f, 0.0f, 0.0f);
	gizmoTorusModel->RenderModel(0);

	// Y Rotation (Green) - XZ plane
	glm::mat4 rotY = glm::translate(glm::mat4(1.0f), gizmoPos);
	rotY = rotY * objRot;
	rotY = glm::scale(rotY, torusScale);
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(rotY));
	if (activeDragAxis == 20005) glUniform3f(colorLoc, 1.0f, 1.0f, 0.0f);
	else glUniform3f(colorLoc, 0.0f, 1.0f, 0.0f);
	gizmoTorusModel->RenderModel(0);

	// Z Rotation (Blue) - XY plane
	glm::mat4 rotZ = glm::translate(glm::mat4(1.0f), gizmoPos);
	rotZ = rotZ * objRot;
	rotZ = glm::rotate(rotZ, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	rotZ = glm::scale(rotZ, torusScale);
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(rotZ));
	if (activeDragAxis == 20006) glUniform3f(colorLoc, 1.0f, 1.0f, 0.0f);
	else glUniform3f(colorLoc, 0.0f, 0.0f, 1.0f);
	gizmoTorusModel->RenderModel(0);

	// --- Move Arrows (Draw SECOND to be on top) ---
	// Y Axis (Green)
	glm::mat4 modelY = glm::translate(glm::mat4(1.0f), gizmoPos);
	modelY = modelY * objRot;
	modelY = glm::scale(modelY, scale);
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(modelY));
	if (activeDragAxis == 20002) glUniform3f(colorLoc, 1.0f, 1.0f, 0.0f);
	else glUniform3f(colorLoc, 0.0f, 1.0f, 0.0f);
	gizmoArrowModel->RenderModel(0);

	// X Axis (Red) - Rotate -90 degrees around Z
	glm::mat4 modelX = glm::translate(glm::mat4(1.0f), gizmoPos);
	modelX = modelX * objRot;
	modelX = glm::rotate(modelX, glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	modelX = glm::scale(modelX, scale);
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(modelX));
	if (activeDragAxis == 20001) glUniform3f(colorLoc, 1.0f, 1.0f, 0.0f);
	else glUniform3f(colorLoc, 1.0f, 0.0f, 0.0f);
	gizmoArrowModel->RenderModel(0);

	// Z Axis (Blue) - Rotate +90 degrees around X
	glm::mat4 modelZ = glm::translate(glm::mat4(1.0f), gizmoPos);
	modelZ = modelZ * objRot;
	modelZ = glm::rotate(modelZ, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	modelZ = glm::scale(modelZ, scale);
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(modelZ));
	if (activeDragAxis == 20003) glUniform3f(colorLoc, 1.0f, 1.0f, 0.0f);
	else glUniform3f(colorLoc, 0.0f, 0.0f, 1.0f);
	gizmoArrowModel->RenderModel(0);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glUseProgram(0);
}

void SceneManager::HandleMousePress(int button, int action, float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, glm::vec3 cameraPos)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT) {
		if (action == GLFW_PRESS) {
			int pickedID = PickObject(mouseX, mouseY, projection, view, cameraPos);
			printf("Picked ID: %d\n", pickedID);
			
			if (pickedID >= 20001 && pickedID <= 20003) {
				activeDragAxis = pickedID;
				printf("Gizmo Drag START: Axis %d\n", activeDragAxis);
				
				// Calculate initial drag anchors
				if (selectedObjectIndex != -1) dragInitialObjectPos = objects[selectedObjectIndex]->GetTransform().GetPosition();
				else if (selectedLightIndex != -1) dragInitialObjectPos = *lights[selectedLightIndex]->GetPositionPtr();
				
				// Calculate local axes based on object rotation
				glm::mat4 objRot(1.0f);
				if (selectedObjectIndex != -1) {
					glm::vec3 r = objects[selectedObjectIndex]->GetTransform().GetRotation();
					objRot = glm::rotate(objRot, glm::radians(r.y), glm::vec3(0.0f, 1.0f, 0.0f));
					objRot = glm::rotate(objRot, glm::radians(r.x), glm::vec3(1.0f, 0.0f, 0.0f));
					objRot = glm::rotate(objRot, glm::radians(r.z), glm::vec3(0.0f, 0.0f, 1.0f));
				}

				glm::vec3 localX = glm::vec3(objRot * glm::vec4(1, 0, 0, 0));
				glm::vec3 localY = glm::vec3(objRot * glm::vec4(0, 1, 0, 0));
				glm::vec3 localZ = glm::vec3(objRot * glm::vec4(0, 0, 1, 0));

				// Pick the best plane for dragging
				glm::vec3 cameraFront = glm::normalize(glm::vec3(glm::inverse(view)[2])); 
				glm::vec3 planeNorm1, planeNorm2;
				
				if (activeDragAxis == 20001) { planeNorm1 = localY; planeNorm2 = localZ; }
				else if (activeDragAxis == 20002) { planeNorm1 = localX; planeNorm2 = localZ; }
				else { planeNorm1 = localX; planeNorm2 = localY; }
				
				if (std::abs(glm::dot(planeNorm1, cameraFront)) > std::abs(glm::dot(planeNorm2, cameraFront)))
					dragPlaneNormal = planeNorm1;
				else
					dragPlaneNormal = planeNorm2;
					
				glm::vec3 rayOrigin = glm::vec3(glm::inverse(view)[3]);
				glm::vec3 rayDir = GetMouseRay(mouseX, mouseY, projection, view);
				
				RayPlaneIntersection(rayOrigin, rayDir, dragInitialObjectPos, dragPlaneNormal, dragInitialIntersectPos);
			}
			else if (pickedID >= 20004 && pickedID <= 20006) {
				activeDragAxis = pickedID;
				printf("Gizmo Rotation START: Axis %d\n", activeDragAxis);

				if (selectedObjectIndex != -1) {
					dragInitialObjectRot = objects[selectedObjectIndex]->GetTransform().GetRotation();
					glm::vec3 gizmoPos = objects[selectedObjectIndex]->GetTransform().GetPosition();

					// Calculate screen center of gizmo for seamless rotation
					glm::vec4 ndc = projection * view * glm::vec4(gizmoPos, 1.0f);
					if (ndc.w != 0) ndc /= ndc.w;

					int windowWidth, windowHeight;
					glfwGetWindowSize(glfwGetCurrentContext(), &windowWidth, &windowHeight);
					float centerX = (ndc.x + 1.0f) * 0.5f * windowWidth;
					float centerY = (1.0f - ndc.y) * 0.5f * windowHeight;

					dragInitialAngle = atan2(mouseY - centerY, mouseX - centerX);
				}
				else if (selectedLightIndex != -1) dragInitialObjectRot = glm::vec3(0.0f); 

				dragInitialMousePos = glm::vec2(mouseX, mouseY);
			}
		} else if (action == GLFW_RELEASE) {
			if (activeDragAxis != 0) printf("Gizmo Drag END\n");
			activeDragAxis = 0;
		}
	}
}

void SceneManager::HandleMouseMove(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view)
{
	if (activeDragAxis == 0) return;
	// printf("Dragging axis %d...\n", activeDragAxis);

	glm::vec3 rayOrigin = glm::vec3(glm::inverse(view)[3]);
	glm::vec3 rayDir = GetMouseRay(mouseX, mouseY, projection, view);
	
	glm::vec3 currentIntersect;
	if (activeDragAxis >= 20001 && activeDragAxis <= 20003 && RayPlaneIntersection(rayOrigin, rayDir, dragInitialObjectPos, dragPlaneNormal, currentIntersect)) {
		glm::vec3 delta = currentIntersect - dragInitialIntersectPos;
		
		glm::mat4 objRot(1.0f);
		if (selectedObjectIndex != -1) {
			glm::vec3 r = objects[selectedObjectIndex]->GetTransform().GetRotation();
			objRot = glm::rotate(objRot, glm::radians(r.y), glm::vec3(0.0f, 1.0f, 0.0f));
			objRot = glm::rotate(objRot, glm::radians(r.x), glm::vec3(1.0f, 0.0f, 0.0f));
			objRot = glm::rotate(objRot, glm::radians(r.z), glm::vec3(0.0f, 0.0f, 1.0f));
		}

		glm::vec3 axis(0.0f);
		if (activeDragAxis == 20001) axis = glm::vec3(objRot * glm::vec4(1, 0, 0, 0));
		else if (activeDragAxis == 20002) axis = glm::vec3(objRot * glm::vec4(0, 1, 0, 0));
		else if (activeDragAxis == 20003) axis = glm::vec3(objRot * glm::vec4(0, 0, 1, 0));
		
		float movement = glm::dot(delta, axis);
		glm::vec3 newPos = dragInitialObjectPos + axis * movement;
		
		if (selectedObjectIndex != -1) objects[selectedObjectIndex]->GetTransform().SetPosition(newPos);
		else if (selectedLightIndex != -1) lights[selectedLightIndex]->SetPosition(newPos);
	}
	else if (activeDragAxis >= 20004 && activeDragAxis <= 20006) {
		if (selectedObjectIndex == -1) return;

		glm::vec3 gizmoPos = objects[selectedObjectIndex]->GetTransform().GetPosition();
		glm::vec4 ndc = projection * view * glm::vec4(gizmoPos, 1.0f);
		if (ndc.w != 0) ndc /= ndc.w;

		int windowWidth, windowHeight;
		glfwGetWindowSize(glfwGetCurrentContext(), &windowWidth, &windowHeight);
		float centerX = (ndc.x + 1.0f) * 0.5f * windowWidth;
		float centerY = (1.0f - ndc.y) * 0.5f * windowHeight;

		float currentAngle = atan2(mouseY - centerY, mouseX - centerX);
		float deltaAngle = -glm::degrees(currentAngle - dragInitialAngle); // Negated to fix inversion

		glm::vec3 rotationDelta(0.0f);
		if (activeDragAxis == 20004) rotationDelta.x = deltaAngle;
		else if (activeDragAxis == 20005) rotationDelta.y = deltaAngle;
		else if (activeDragAxis == 20006) rotationDelta.z = deltaAngle;

		objects[selectedObjectIndex]->GetTransform().SetRotation(dragInitialObjectRot + rotationDelta);
	}
}

glm::vec3 SceneManager::GetMouseRay(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view)
{
	float x = (2.0f * mouseX) / pickWidth - 1.0f;
	float y = 1.0f - (2.0f * mouseY) / pickHeight;
	
	glm::vec4 rayClip = glm::vec4(x, y, -1.0f, 1.0f);
	glm::vec4 rayEye = glm::inverse(projection) * rayClip;
	rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
	
	glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
	return glm::normalize(rayWorld);
}

bool SceneManager::RayPlaneIntersection(glm::vec3 rayOrigin, glm::vec3 rayDir, glm::vec3 planePoint, glm::vec3 planeNormal, glm::vec3& intersectPoint)
{
	float denom = glm::dot(planeNormal, rayDir);
	if (std::abs(denom) > 1e-6) {
		float t = glm::dot(planePoint - rayOrigin, planeNormal) / denom;
		if (t >= 0) {
			intersectPoint = rayOrigin + rayDir * t;
			return true;
		}
	}
	return false;
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

void SceneManager::LoadAssetIcons()
{
	if (!folderIconSlot) {
		folderIconSlot = new Texture("Assets/Textures/plain.png"); // Placeholder for folder
		folderIconSlot->LoadTextureA();
	}
	if (!modelIconSlot) {
		modelIconSlot = new Texture("Assets/Textures/plain.png"); // Placeholder for model
		modelIconSlot->LoadTextureA();
	}
}

void SceneManager::RefreshAssetList()
{
	currentAssets.clear();
	LoadAssetIcons();

	if (!std::filesystem::exists(currentAssetPath)) {
		currentAssetPath = "Assets";
		if (!std::filesystem::exists(currentAssetPath)) return;
	}

	for (auto const& entry : std::filesystem::directory_iterator(currentAssetPath))
	{
		AssetInfo info;
		info.name = entry.path().filename().string();
		info.path = entry.path();
		
		if (entry.is_directory()) {
			info.type = AssetType::Folder;
			info.thumbnail = folderIconSlot;
		}
		else {
			std::string ext = entry.path().extension().string();
			for (auto& c : ext) c = tolower(c);

			if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") {
				info.type = AssetType::Texture;
				// Try to get from cache or load
				std::string pStr = entry.path().string();
				if (assetTextureCache.count(pStr)) {
					info.thumbnail = assetTextureCache[pStr];
				}
				else {
					Texture* tex = new Texture(pStr.data());
					if (tex->LoadTexture()) {
						assetTextureCache[pStr] = tex;
						info.thumbnail = tex;
					}
					else {
						delete tex;
						info.thumbnail = nullptr;
					}
				}
			}
			else if (ext == ".obj" || ext == ".fbx" || ext == ".dae") {
				info.type = AssetType::Model;
				info.thumbnail = modelIconSlot;
			}
			else {
				info.type = AssetType::Other;
				info.thumbnail = nullptr;
			}
		}
		currentAssets.push_back(info);
	}
}

void SceneManager::RenderAssetBrowser()
{
	int bufferWidth, bufferHeight;
	glfwGetFramebufferSize(glfwGetCurrentContext(), &bufferWidth, &bufferHeight);
	ImGui::SetNextWindowPos(ImVec2(300, 450), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2((float)bufferWidth - 300.0f, 500.0f), ImGuiCond_FirstUseEver);

	ImGui::Begin("Project", &windowState.isAssetBrowserOpen);

	if (ImGui::Button("Refresh")) {
		RefreshAssetList();
	}
	ImGui::SameLine();
	if (ImGui::Button("..")) {
		if (currentAssetPath.has_parent_path() && currentAssetPath != "Assets") {
			currentAssetPath = currentAssetPath.parent_path();
			RefreshAssetList();
		}
	}
	ImGui::SameLine();
	ImGui::Text("Path: %s", currentAssetPath.string().c_str());

	ImGui::Separator();

	float cellSize = 100.0f;
	float padding = 16.0f;
	float panelWidth = ImGui::GetContentRegionAvail().x;
	int columnCount = (int)(panelWidth / (cellSize + padding));
	if (columnCount < 1) columnCount = 1;

	ImGui::Columns(columnCount, 0, false);

	for (int i = 0; i < (int)currentAssets.size(); i++)
	{
		ImGui::PushID(i);
		
		ImVec4 tint = ImVec4(1, 1, 1, 1);
		if (currentAssets[i].type == AssetType::Folder) tint = ImVec4(1, 0.8f, 0.4f, 1); // Yellowish for folders

		ImVec2 startPos = ImGui::GetCursorPos();
		bool isSelected = (currentAssets[i].path == selectedAssetPath);

		// Use a selectable for the entire area (image + text)
		if (ImGui::Selectable("##selectable", isSelected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(cellSize, cellSize + 40))) {
			selectedAssetPath = currentAssets[i].path;
			
			if (ImGui::IsMouseDoubleClicked(0)) {
				if (currentAssets[i].type == AssetType::Folder) {
					currentAssetPath = currentAssets[i].path;
					RefreshAssetList();
					ImGui::PopID();
					break; 
				}
			}
		}

		// Draw the content on top of the selectable
		ImGui::SetCursorPos(startPos);
		ImGui::BeginGroup();
		
		if (currentAssets[i].thumbnail) {
			ImGui::ImageWithBg((ImTextureID)(intptr_t)currentAssets[i].thumbnail->GetTextureID(), ImVec2(cellSize, cellSize), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tint);
		}
		else {
			ImGui::Button("??", ImVec2(cellSize, cellSize));
		}

		ImGui::TextWrapped("%s", currentAssets[i].name.c_str());
		ImGui::EndGroup();
		
		ImGui::NextColumn();
		ImGui::PopID();
	}

	ImGui::Columns(1);

	ImGui::End();
}
