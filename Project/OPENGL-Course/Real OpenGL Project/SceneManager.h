#pragma once

#include <vector>
#include <string>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "GameObject.h"
#include "LightObject.h"
#include "Shader.h"

class SceneManager
{
public:
	SceneManager();
	~SceneManager();

	// Object management
	void AddObject(GameObject* obj);
	void RemoveObject(const std::string& name);
	GameObject* FindObject(const std::string& name);
	std::vector<GameObject*>& GetObjects() { return objects; }

	// Light management
	void AddLight(LightObject* light);
	std::vector<LightObject*>& GetLights() { return lights; }

	// Selection (negative = object, positive = light, 0 = none)
	void SetSelectedIndex(int index) { 
		selectedObjectIndex = index; 
		if (index >= 0) selectedLightIndex = -1; // Clear light selection
	}
	int GetSelectedIndex() const { return selectedObjectIndex; }
	
	void SetSelectedLightIndex(int index) { 
		selectedLightIndex = index; 
		if (index >= 0) selectedObjectIndex = -1; // Clear object selection
	}
	int GetSelectedLightIndex() const { return selectedLightIndex; }

	// Initialize picking (call after OpenGL context is ready)
	void InitPicking(int width, int height);

	// Color picking - renders scene to pick buffer, returns object index at mouse pos
	int PickObject(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view);

	// Rendering
	void RenderAll(GLuint uniformModel, GLuint uniformSpecularIntensity, GLuint uniformShininess);

	// ImGui interface
	void RenderImGui();

	// Clear all objects
	void Clear();

private:
	std::vector<GameObject*> objects;
	std::vector<LightObject*> lights;
	int selectedObjectIndex;
	int selectedLightIndex;

	// Color picking resources
	GLuint pickingFBO;
	GLuint pickingTexture;
	GLuint pickingDepth;
	Shader pickingShader;
	int pickWidth, pickHeight;
	bool pickingInitialized;
};
