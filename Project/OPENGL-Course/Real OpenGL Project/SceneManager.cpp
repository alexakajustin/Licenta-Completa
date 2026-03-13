#include "SceneManager.h"
#include "PrimitiveGenerator.h"
#include <iostream>
#include <GLFW/glfw3.h>
#include <algorithm>

// =====================================================================
// Constructor / Destructor
// =====================================================================

SceneManager::SceneManager()
	: pickingFBO(0), pickingTexture(0), pickingDepth(0),
	  pickWidth(0), pickHeight(0), pickingInitialized(false),
	  lightIconTexture(nullptr), iconMesh(nullptr), gizmoArrowModel(nullptr), gizmoTorusModel(nullptr)
{
}

SceneManager::~SceneManager()
{
	Clear();
	
	if (pickingFBO) glDeleteFramebuffers(1, &pickingFBO);
	if (pickingTexture) glDeleteTextures(1, &pickingTexture);
	if (pickingDepth) glDeleteRenderbuffers(1, &pickingDepth);

	if (gizmoArrowModel) { gizmoArrowModel->ClearModel(); delete gizmoArrowModel; }
	if (gizmoTorusModel) { gizmoTorusModel->ClearModel(); delete gizmoTorusModel; }
}

// =====================================================================
// Object & Light Management
// =====================================================================

void SceneManager::AddObject(GameObject* obj)
{
	if (obj) objects.push_back(obj);
}

void SceneManager::RemoveObject(const std::string& name)
{
	for (auto it = objects.begin(); it != objects.end(); ++it)
	{
		if ((*it)->GetName() == name)
		{
			int index = (int)(it - objects.begin());
			delete *it;
			objects.erase(it);

			// Update selection indices
			std::vector<int> newSelection;
			for (int selIdx : selectedObjectIndices) {
				if (selIdx == index) continue;
				if (selIdx > index) newSelection.push_back(selIdx - 1);
				else newSelection.push_back(selIdx);
			}
			selectedObjectIndices = newSelection;
			
			if (selectedObjectIndices.empty()) activeDragAxis = 0;
			return;
		}
	}
}

GameObject* SceneManager::FindObject(const std::string& name)
{
	for (auto* obj : objects)
	{
		if (obj->GetName() == name) return obj;
	}
	return nullptr;
}

void SceneManager::RenderAll(GLint uniformModel, GLint uniformSpecularIntensity, GLint uniformShininess, GLint uniformMaterialColor, GLint uniformUseNormalMap, GLint uniformUseDiffuseTexture)
{
	for (auto* obj : objects)
	{
		obj->Render(uniformModel, uniformSpecularIntensity, uniformShininess, uniformMaterialColor, uniformUseNormalMap, uniformUseDiffuseTexture);
	}
}

void SceneManager::AddLight(LightObject* light)
{
	if (light) lights.push_back(light);
}

void SceneManager::Clear()
{
	for (auto* obj : objects) delete obj;
	objects.clear();
	
	for (auto* light : lights) delete light;
	lights.clear();
	
	selectedObjectIndices.clear();
	selectedLightIndices.clear();
}

// =====================================================================
// DRY Helpers
// =====================================================================

glm::mat4 SceneManager::GetSelectedRotationMatrix() const
{
	glm::mat4 rot(1.0f);
	int sel = GetSelectedIndex();
	if (sel != -1) {
		glm::vec3 r = objects[sel]->GetTransform().GetRotation();
		rot = glm::rotate(rot, glm::radians(r.y), glm::vec3(0.0f, 1.0f, 0.0f));
		rot = glm::rotate(rot, glm::radians(r.x), glm::vec3(1.0f, 0.0f, 0.0f));
		rot = glm::rotate(rot, glm::radians(r.z), glm::vec3(0.0f, 0.0f, 1.0f));
	}
	return rot;
}

bool SceneManager::GetGizmoPosition(glm::vec3& outPos) const
{
	int selObj = GetSelectedIndex();
	if (selObj != -1) {
		outPos = objects[selObj]->GetTransform().GetPosition();
		return true;
	}
	
	int selLight = GetSelectedLightIndex();
	if (selLight != -1) {
		glm::vec3* lightPos = lights[selLight]->GetPositionPtr();
		if (lightPos) { outPos = *lightPos; return true; }
	}
	return false;
}

std::string SceneManager::GetSelectedName() const
{
	int obj = GetSelectedIndex();
	int light = GetSelectedLightIndex();
	
	if (obj != -1 && obj < (int)objects.size())
		return objects[obj]->GetName();
	else if (light != -1 && light < (int)lights.size())
		return lights[light]->GetName();
	return "None";
}

// ========== Selection Implementation ==========

void SceneManager::SetSelectedIndex(int index, bool multiSelect, bool rangeSelect)
{
	if (!multiSelect && !rangeSelect) {
		selectedObjectIndices.clear();
		selectedLightIndices.clear();
	}

	if (index < 0) return;
	selectedLightIndices.clear(); // Cannot select both lights and objects in multi-select for now

	if (rangeSelect && !selectedObjectIndices.empty()) {
		int start = selectedObjectIndices.back();
		int end = index;
		if (start > end) std::swap(start, end);
		
		for (int i = start; i <= end; i++) {
			if (!IsObjectSelected(i)) selectedObjectIndices.push_back(i);
		}
	} else if (multiSelect) {
		auto it = std::find(selectedObjectIndices.begin(), selectedObjectIndices.end(), index);
		if (it != selectedObjectIndices.end()) {
			selectedObjectIndices.erase(it);
		} else {
			selectedObjectIndices.push_back(index);
		}
	} else {
		selectedObjectIndices.push_back(index);
	}
	activeDragAxis = 0;
}

int SceneManager::GetSelectedIndex() const 
{ 
	return selectedObjectIndices.empty() ? -1 : selectedObjectIndices.back(); 
}

bool SceneManager::IsObjectSelected(int index) const 
{
	return std::find(selectedObjectIndices.begin(), selectedObjectIndices.end(), index) != selectedObjectIndices.end();
}

void SceneManager::SetSelectedLightIndex(int index, bool multiSelect, bool rangeSelect)
{
	if (!multiSelect && !rangeSelect) {
		selectedObjectIndices.clear();
		selectedLightIndices.clear();
	}

	if (index < 0) return;
	selectedObjectIndices.clear();

	if (rangeSelect && !selectedLightIndices.empty()) {
		int start = selectedLightIndices.back();
		int end = index;
		if (start > end) std::swap(start, end);
		for (int i = start; i <= end; i++) {
			if (!IsLightSelected(i)) selectedLightIndices.push_back(i);
		}
	} else if (multiSelect) {
		auto it = std::find(selectedLightIndices.begin(), selectedLightIndices.end(), index);
		if (it != selectedLightIndices.end()) {
			selectedLightIndices.erase(it);
		} else {
			selectedLightIndices.push_back(index);
		}
	} else {
		selectedLightIndices.push_back(index);
	}
	activeDragAxis = 0;
}

int SceneManager::GetSelectedLightIndex() const 
{ 
	return selectedLightIndices.empty() ? -1 : selectedLightIndices.back(); 
}

bool SceneManager::IsLightSelected(int index) const 
{
	return std::find(selectedLightIndices.begin(), selectedLightIndices.end(), index) != selectedLightIndices.end();
}

// =====================================================================
// Creation / Deletion
// =====================================================================

void SceneManager::CreateGameObject(const std::string& type)
{
	GameObject* newObj = new GameObject(type + " " + std::to_string(objects.size()));
	
	if (type == "Plane") newObj->SetMesh(PrimitiveGenerator::CreatePlane());
	else if (type == "Cube") newObj->SetMesh(PrimitiveGenerator::CreateCube());
	else if (type == "Sphere") newObj->SetMesh(PrimitiveGenerator::CreateSphere());

	objects.push_back(newObj);
	SetSelectedIndex((int)objects.size() - 1);
}

void SceneManager::InstantiateModel(const std::filesystem::path& path, glm::vec3 spawnPos)
{
	std::string name = path.stem().string();
	GameObject* newObj = new GameObject(name + " " + std::to_string(objects.size()));
	newObj->GetTransform().SetPosition(spawnPos);
	Model* model = new Model();
	model->LoadModel(path.string());
	newObj->SetModel(model);

	objects.push_back(newObj);
	SetSelectedIndex((int)objects.size() - 1);
	
	printf("Instantiated model: %s\n", path.string().c_str());
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
			SetSelectedLightIndex((int)lights.size() - 1);
		}
	} else if (type == LightType::Spot) {
		if (globalSpotLights && globalSpotLightCount && *globalSpotLightCount < MAX_SPOT_LIGHTS) {
			unsigned int idx = *globalSpotLightCount;
			globalSpotLights[idx] = SpotLight(1024, 1024, 0.01f, 100.0f, 1.0f, 1.0f, 1.0f, 0.1f, 1.0f, 0.0f, 5.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.02f, 0.01f, 20.0f);
			
			LightObject* newLightObj = new LightObject("Spot Light " + std::to_string(lights.size()), &globalSpotLights[idx]);
			lights.push_back(newLightObj);
			(*globalSpotLightCount)++;
			SetSelectedLightIndex((int)lights.size() - 1);
		}
	}
}

void SceneManager::DeleteLight(int index)
{
	if (index < 0 || index >= (int)lights.size()) return;

	LightObject* light = lights[index];
	LightType type = light->GetLightType();

	if (type == LightType::Directional) return;

	if (type == LightType::Point && globalPointLights && globalPointLightCount) {
		PointLight* ptr = light->GetPointLight();
		int arrayIdx = (int)(ptr - globalPointLights);
		if (arrayIdx >= 0 && arrayIdx < (int)*globalPointLightCount) {
			for (unsigned int j = arrayIdx; j < *globalPointLightCount - 1; j++)
				globalPointLights[j] = globalPointLights[j + 1];
			(*globalPointLightCount)--;

			for (auto* lo : lights) {
				if (lo == light) continue;
				if (lo->GetLightType() == LightType::Point && lo->GetPointLight() > ptr)
					lo->SetPointLight(lo->GetPointLight() - 1);
			}
		}
	}
	else if (type == LightType::Spot && globalSpotLights && globalSpotLightCount) {
		SpotLight* ptr = light->GetSpotLight();
		int arrayIdx = (int)(ptr - globalSpotLights);
		if (arrayIdx >= 0 && arrayIdx < (int)*globalSpotLightCount) {
			for (unsigned int j = arrayIdx; j < *globalSpotLightCount - 1; j++)
				globalSpotLights[j] = globalSpotLights[j + 1];
			(*globalSpotLightCount)--;

			for (auto* lo : lights) {
				if (lo == light) continue;
				if (lo->GetLightType() == LightType::Spot && lo->GetSpotLight() > ptr)
					lo->SetSpotLight(lo->GetSpotLight() - 1);
			}
		}
	}

	delete light;
	lights.erase(lights.begin() + index);
	selectedLightIndices.clear();
}

// =====================================================================
// Picking System
// =====================================================================

static glm::vec3 EncodeID(int id)
{
	return glm::vec3(
		((id & 0x0000FF) >> 0) / 255.0f,
		((id & 0x00FF00) >> 8) / 255.0f,
		((id & 0xFF0000) >> 16) / 255.0f
	);
}

static int DecodeID(unsigned char pixel[3])
{
	return pixel[0] + pixel[1] * 256 + pixel[2] * 256 * 256;
}

void SceneManager::InitPicking(int width, int height)
{
	pickWidth = width;
	pickHeight = height;

	glGenFramebuffers(1, &pickingFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, pickingFBO);

	glGenTextures(1, &pickingTexture);
	glBindTexture(GL_TEXTURE_2D, pickingTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pickingTexture, 0);

	glGenRenderbuffers(1, &pickingDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, pickingDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, pickingDepth);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		printf("Picking framebuffer not complete!\n");

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	pickingShader.CreateFromFiles("Shaders/picking.vert", "Shaders/picking.frag");
	pickingInitialized = true;
}

int SceneManager::PickObject(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, glm::vec3 cameraPos, float viewportWidth, float viewportHeight)
{
	if (!pickingInitialized) return -1;

	glBindFramebuffer(GL_FRAMEBUFFER, pickingFBO);
	// We still render picking to the full internal buffer size (pickWidth/Height)
	glViewport(0, 0, pickWidth, pickHeight);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	pickingShader.UseShader();

	glUniformMatrix4fv(pickingShader.GetProjectionLocation(), 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(pickingShader.GetViewLocation(), 1, GL_FALSE, glm::value_ptr(view));

	GLint modelLoc = pickingShader.GetModelLocation();
	GLint colorLoc = glGetUniformLocation(pickingShader.GetShaderID(), "pickingColor");
	GLint isBillboardLoc = glGetUniformLocation(pickingShader.GetShaderID(), "isBillboard");
	GLint worldPosLoc = glGetUniformLocation(pickingShader.GetShaderID(), "worldPos");
	GLint iconSizeLoc = glGetUniformLocation(pickingShader.GetShaderID(), "iconSize");

	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE); 
	glEnable(GL_CULL_FACE);

	// Objects
	for (int i = 0; i < (int)objects.size(); i++)
	{
		glm::vec3 color = EncodeID(i + 1);
		glUniform3f(colorLoc, color.r, color.g, color.b);
		glm::mat4 modelMatrix = objects[i]->GetTransform().GetModelMatrix();
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(modelMatrix));
		if (objects[i]->GetModel()) objects[i]->GetModel()->RenderModel(0, 0);
		else if (objects[i]->GetMesh()) objects[i]->GetMesh()->RenderMesh();
	}

	glClear(GL_DEPTH_BUFFER_BIT);

	// Light icons
	if (iconMesh)
	{
		glDisable(GL_CULL_FACE);
		glUniform1i(isBillboardLoc, 1);
		glUniform1f(iconSizeLoc, 0.7f);
		for (int i = 0; i < (int)lights.size(); i++)
		{
			glm::vec3 color = EncodeID(i + 10000);
			glUniform3f(colorLoc, color.r, color.g, color.b);
			glm::vec3* pos = lights[i]->GetPositionPtr();
			if (pos) {
				glUniform3f(worldPosLoc, pos->x, pos->y, pos->z);
				iconMesh->RenderMesh();
			}
		}
	}

	// Gizmo picking
	glm::vec3 gizmoPos;
	if (GetGizmoPosition(gizmoPos))
	{
		glm::mat4 objRot = GetSelectedRotationMatrix();
		float dist = glm::length(gizmoPos - cameraPos);
		glUniform1i(isBillboardLoc, 0);
		glDisable(GL_CULL_FACE);

		if (gizmoArrowModel) {
			float arrowScale = dist * 0.1f;
			struct { int id; glm::mat4 extraRot; } arrows[] = {
				{ 20001, glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0,0,1)) },
				{ 20002, glm::mat4(1.0f) },
				{ 20003, glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1,0,0)) }
			};
			for (auto& a : arrows) {
				glm::vec3 color = EncodeID(a.id);
				glUniform3f(colorLoc, color.r, color.g, color.b);
				glm::mat4 m = glm::translate(glm::mat4(1.0f), gizmoPos) * a.extraRot;
				m = glm::scale(m, glm::vec3(arrowScale));
				glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
				gizmoArrowModel->RenderModel(0, 0);
			}
		}

		if (gizmoTorusModel) {
			float torusScale = dist * 0.1f * 0.6f;
			struct { int id; glm::mat4 extraRot; } tori[] = {
				{ 20004, glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0,0,1)) },
				{ 20005, glm::mat4(1.0f) },
				{ 20006, glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1,0,0)) }
			};
			for (auto& t : tori) {
				glm::vec3 color = EncodeID(t.id);
				glUniform3f(colorLoc, color.r, color.g, color.b);
				glm::mat4 m = glm::translate(glm::mat4(1.0f), gizmoPos) * objRot * t.extraRot;
				m = glm::scale(m, glm::vec3(torusScale));
				glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
				gizmoTorusModel->RenderModel(0, 0);
			}
		}
	}

	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glFlush();
	glFinish();

	// Scale mouse coordinates from viewport window space to picking FBO space
	float scaleX = (float)pickWidth / viewportWidth;
	float scaleY = (float)pickHeight / viewportHeight;

	unsigned char pixel[3];
	int readX = (int)(mouseX * scaleX);
	int readY = pickHeight - (int)(mouseY * scaleY);
	
	glReadPixels(readX, readY, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);
	int pickedID = DecodeID(pixel);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (pickedID > 0 && pickedID <= (int)objects.size()) {
		SetSelectedIndex(pickedID - 1);
	}
	else if (pickedID >= 10000 && pickedID < 10000 + (int)lights.size()) {
		SetSelectedLightIndex(pickedID - 10000);
	}
	else if (pickedID < 20000) {
		ClearSelection();
	}

	return pickedID;
}

// =====================================================================
// Icons
// =====================================================================

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
	unsigned int indices[] = { 0, 2, 1, 1, 2, 3 };

	GLfloat vertices[] = {
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

	while (glGetError() != GL_NO_ERROR);

	iconShader.UseShader();
	
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	
	GLint projLoc = iconShader.GetProjectionLocation();
	GLint viewLoc = iconShader.GetViewLocation();
	
	if (projLoc != -1) glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
	if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
	
	GLint worldPosLoc = glGetUniformLocation(iconShader.GetShaderID(), "worldPos");
	GLint iconSizeLoc = glGetUniformLocation(iconShader.GetShaderID(), "iconSize");
	GLint textureLoc = glGetUniformLocation(iconShader.GetShaderID(), "theTexture");
	GLint iconColorLoc = glGetUniformLocation(iconShader.GetShaderID(), "iconColor");
	
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, lightIconTexture->GetTextureID());
	
	if (textureLoc != (GLuint)-1) glUniform1i(textureLoc, 0); 
	if (iconSizeLoc != (GLuint)-1) glUniform1f(iconSizeLoc, 0.5f); 

	for (auto* light : lights)
	{
		if (!light) continue;

		glm::vec3* pos = light->GetPositionPtr();
		if (pos)
		{
			if (worldPosLoc != -1) glUniform3f(worldPosLoc, pos->x, pos->y, pos->z);
			
			glm::vec3* color = light->GetColorPtr();
			if (iconColorLoc != -1 && color) glUniform3f(iconColorLoc, color->x, color->y, color->z);
			
			iconMesh->RenderMesh();
		}
	}

	glUseProgram(0);
	glEnable(GL_CULL_FACE);
	
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		printf("OpenGL error in RenderIcons: 0x%x\n", err);
	}
}

// =====================================================================
// Gizmo
// =====================================================================

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

void SceneManager::RenderGizmo(glm::mat4 projection, glm::mat4 view, glm::vec3 cameraPos)
{
	glm::vec3 gizmoPos;
	if (!GetGizmoPosition(gizmoPos)) return;
	if (!gizmoArrowModel || gizmoShader.GetShaderID() == 0) return;

	float dist = glm::length(gizmoPos - cameraPos);
	float scaleFactor = dist * 0.1f; // Restored to 0.1f for better thickness
	float torusScaleFactor = dist * 0.1f * 0.6f;

	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	gizmoShader.UseShader();
	
	GLint projLoc = gizmoShader.GetProjectionLocation();
	GLint viewLoc = gizmoShader.GetViewLocation();
	GLint modelLoc = gizmoShader.GetModelLocation();
	GLint colorLoc = glGetUniformLocation(gizmoShader.GetShaderID(), "gizmoColor");

	glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));

	glm::mat4 objRot = GetSelectedRotationMatrix();

	// DRY: data-driven rendering for all 6 gizmo parts
	struct GizmoPart {
		Model* model;
		int axisID;
		glm::vec3 defaultColor;
		glm::mat4 extraRot;
		float scale;
	};

	// World-space translation arrows (ignore objRot)
	GizmoPart translationParts[] = {
		{ gizmoArrowModel, 20002, {0,1,0}, glm::mat4(1.0f), scaleFactor },
		{ gizmoArrowModel, 20001, {1,0,0}, glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0,0,1)), scaleFactor },
		{ gizmoArrowModel, 20003, {0,0,1}, glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1,0,0)), scaleFactor }
	};

	// Local-space rotation tori (use objRot)
	GizmoPart rotationParts[] = {
		{ gizmoTorusModel, 20004, {1,0,0}, glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0,0,1)), torusScaleFactor },
		{ gizmoTorusModel, 20005, {0,1,0}, glm::mat4(1.0f), torusScaleFactor },
		{ gizmoTorusModel, 20006, {0,0,1}, glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1,0,0)), torusScaleFactor }
	};

	// Draw order: Arrows FIRST, Tori SECOND (so tori overlap)
	for (auto& part : translationParts)
	{
		if (!part.model) continue;
		glm::mat4 m = glm::translate(glm::mat4(1.0f), gizmoPos) * part.extraRot; // No objRot
		m = glm::scale(m, glm::vec3(part.scale));
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));

		if (activeDragAxis == part.axisID) glUniform3f(colorLoc, 1.0f, 1.0f, 0.0f);
		else glUniform3f(colorLoc, part.defaultColor.r, part.defaultColor.g, part.defaultColor.b);
		part.model->RenderModel(0, 0);
	}

	for (auto& part : rotationParts)
	{
		if (!part.model) continue;
		glm::mat4 m = glm::translate(glm::mat4(1.0f), gizmoPos) * objRot * part.extraRot; // Uses objRot
		m = glm::scale(m, glm::vec3(part.scale));
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));

		if (activeDragAxis == part.axisID) glUniform3f(colorLoc, 1.0f, 1.0f, 0.0f);
		else glUniform3f(colorLoc, part.defaultColor.r, part.defaultColor.g, part.defaultColor.b);
		part.model->RenderModel(0, 0);
	}

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glUseProgram(0);
}

// =====================================================================
// Mouse/Gizmo Interaction
// =====================================================================

void SceneManager::HandleMousePress(int button, int action, float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, glm::vec3 cameraPos, float viewportWidth, float viewportHeight)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
	{
		// Use specific viewport resolution for picking
		int pickedID = PickObject(mouseX, mouseY, projection, view, cameraPos, viewportWidth, viewportHeight);
			printf("[SceneManager] Picked ID: %d (Active Selection: %s)\n", pickedID, GetSelectedName().c_str());
			
			glm::vec3 cameraForward = -glm::normalize(glm::vec3(glm::inverse(view)[2]));
			glm::vec3 rayOrigin = glm::vec3(glm::inverse(view)[3]);
			glm::vec3 rayDir = GetMouseRay(mouseX, mouseY, projection, view, viewportWidth, viewportHeight);

			if (pickedID >= 20001 && pickedID <= 20003) {
				// === TRANSLATION ===
				activeDragAxis = pickedID;
				printf("Gizmo Drag START: Axis %d\n", activeDragAxis);
				
				GetGizmoPosition(dragInitialObjectPos);
				
				// World-space translation arrows: ignore objRot
				glm::vec3 axis(0.0f);
				if (activeDragAxis == 20001) axis = glm::vec3(1, 0, 0);
				else if (activeDragAxis == 20002) axis = glm::vec3(0, 1, 0);
				else axis = glm::vec3(0, 0, 1);

				// Best drag plane: contains the axis, faces the camera
				// plane normal = cross(axis, cross(cameraForward, axis))
				glm::vec3 crossCamAxis = glm::cross(cameraForward, axis);
				if (glm::length(crossCamAxis) < 1e-4f) {
					// Camera looking along the axis — use camera up as fallback
					glm::vec3 cameraUp = glm::normalize(glm::vec3(glm::inverse(view)[1]));
					crossCamAxis = glm::cross(cameraUp, axis);
				}
				dragPlaneNormal = glm::normalize(glm::cross(axis, crossCamAxis));
					
				RayPlaneIntersect(rayOrigin, rayDir, dragInitialObjectPos, dragPlaneNormal, dragInitialIntersectPos);
			}
			else if (pickedID >= 20004 && pickedID <= 20006) {
				// === ROTATION ===
				activeDragAxis = pickedID;
				printf("Gizmo Rotation START: Axis %d\n", activeDragAxis);

				int selObj = GetSelectedIndex();
				if (selObj != -1) {
					dragInitialObjectRot = objects[selObj]->GetTransform().GetRotation();
					dragRotationCenter = objects[selObj]->GetTransform().GetPosition();
				} else {
					int selLight = GetSelectedLightIndex();
					if (selLight != -1) {
						dragInitialObjectRot = glm::vec3(0.0f);
						glm::vec3* lp = lights[selLight]->GetPositionPtr();
						if (lp) dragRotationCenter = *lp;
					}
				}

				// Rotation axis in world space
				glm::mat4 objRot = GetSelectedRotationMatrix();
				if (activeDragAxis == 20004) dragRotationAxis = glm::vec3(objRot * glm::vec4(1, 0, 0, 0));
				else if (activeDragAxis == 20005) dragRotationAxis = glm::vec3(objRot * glm::vec4(0, 1, 0, 0));
				else dragRotationAxis = glm::vec3(objRot * glm::vec4(0, 0, 1, 0));
				dragRotationAxis = glm::normalize(dragRotationAxis);

				// Rotation plane: perpendicular to the rotation axis, through center
				dragPlaneNormal = dragRotationAxis;

				glm::vec3 hitPoint;
				if (RayPlaneIntersect(rayOrigin, rayDir, dragRotationCenter, dragPlaneNormal, hitPoint)) {
					dragInitialRotVec = glm::normalize(hitPoint - dragRotationCenter);
				} else {
					// Fallback: if ray is parallel to plane, use camera right
					dragInitialRotVec = glm::normalize(glm::vec3(glm::inverse(view)[0]));
				}

				dragInitialMousePos = glm::vec2(mouseX, mouseY);
			}
		}
		else if (action == GLFW_RELEASE) {
			if (activeDragAxis != 0) printf("Gizmo Drag END\n");
			activeDragAxis = 0;
		}
}
void SceneManager::HandleMouseMove(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, float viewportWidth, float viewportHeight)
{
	if (activeDragAxis == 0) return;

	// Use viewport dimensions for ray casting
	glm::vec3 rayOrigin = glm::vec3(glm::inverse(view)[3]);
	glm::vec3 rayDir = GetMouseRay(mouseX, mouseY, projection, view, viewportWidth, viewportHeight);
	
	if (activeDragAxis >= 20001 && activeDragAxis <= 20003) {
		// === TRANSLATION ===
		glm::vec3 currentIntersect;
		if (!RayPlaneIntersect(rayOrigin, rayDir, dragInitialObjectPos, dragPlaneNormal, currentIntersect))
			return;

		glm::vec3 delta = currentIntersect - dragInitialIntersectPos;
		
		// World-space translation arrows: ignore objRot
		glm::vec3 axis(0.0f);
		if (activeDragAxis == 20001) axis = glm::vec3(1, 0, 0);
		else if (activeDragAxis == 20002) axis = glm::vec3(0, 1, 0);
		else if (activeDragAxis == 20003) axis = glm::vec3(0, 0, 1);
		
		float movement = glm::dot(delta, axis);
		movement = glm::clamp(movement, -50.0f, 50.0f);

		glm::vec3 newPos = dragInitialObjectPos + axis * movement;
		
		int selObj = GetSelectedIndex();
		int selLight = GetSelectedLightIndex();
		if (selObj != -1) objects[selObj]->GetTransform().SetPosition(newPos);
		else if (selLight != -1) lights[selLight]->SetPosition(newPos);
	}
	else if (activeDragAxis >= 20004 && activeDragAxis <= 20006) {
		// === ROTATION ===
		int selObj = GetSelectedIndex();
		if (selObj == -1) return;

		glm::vec3 hitPoint;
		if (!RayPlaneIntersect(rayOrigin, rayDir, dragRotationCenter, dragPlaneNormal, hitPoint)) {
			// Fallback: use mouse delta for rotation when ray is parallel to plane
			float dx = mouseX - dragInitialMousePos.x;
			float deltaAngle = dx * 0.5f; // No clamp for smoother rotation

			glm::vec3 rotationDelta(0.0f);
			if (activeDragAxis == 20004) rotationDelta.x = deltaAngle;
			else if (activeDragAxis == 20005) rotationDelta.y = deltaAngle;
			else if (activeDragAxis == 20006) rotationDelta.z = deltaAngle;

			objects[selObj]->GetTransform().SetRotation(dragInitialObjectRot + rotationDelta);
			return;
		}

		glm::vec3 currentVec = hitPoint - dragRotationCenter;
		float len = glm::length(currentVec);
		if (len < 1e-6f) return;
		currentVec /= len;

		// Signed angle between initial and current vectors, relative to the rotation axis
		float dotVal = glm::clamp(glm::dot(dragInitialRotVec, currentVec), -1.0f, 1.0f);
		glm::vec3 crossVal = glm::cross(dragInitialRotVec, currentVec);
		float sign = glm::dot(crossVal, dragRotationAxis);
		float angleRad = atan2(glm::length(crossVal) * (sign >= 0 ? 1.0f : -1.0f), dotVal);
		float deltaAngle = glm::degrees(angleRad);

		glm::vec3 rotationDelta(0.0f);
		if (activeDragAxis == 20004) rotationDelta.x = deltaAngle;
		else if (activeDragAxis == 20005) rotationDelta.y = deltaAngle;
		else if (activeDragAxis == 20006) rotationDelta.z = deltaAngle;

		objects[selObj]->GetTransform().SetRotation(dragInitialObjectRot + rotationDelta);
	}
}

// =====================================================================
// Math Utilities
// =====================================================================

glm::vec3 SceneManager::GetMouseRay(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, float viewportWidth, float viewportHeight)
{
	// Map to NDC using specific viewport dimensions
	float x = (2.0f * mouseX) / viewportWidth - 1.0f;
	float y = 1.0f - (2.0f * mouseY) / viewportHeight;
	
	glm::vec4 rayClip = glm::vec4(x, y, -1.0f, 1.0f);
	glm::vec4 rayEye = glm::inverse(projection) * rayClip;
	rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
	
	glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
	return glm::normalize(rayWorld);
}

bool SceneManager::RayPlaneIntersect(glm::vec3 rayOrigin, glm::vec3 rayDir, glm::vec3 planePoint, glm::vec3 planeNormal, glm::vec3& intersectPoint)
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
