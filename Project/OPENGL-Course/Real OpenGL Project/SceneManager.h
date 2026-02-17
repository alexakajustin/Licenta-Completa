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
	void InitIcons();

	// Color picking - renders scene to pick buffer, returns object index at mouse pos
	int PickObject(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, glm::vec3 cameraPos);

	// Rendering
	void RenderAll(GLuint uniformModel, GLuint uniformSpecularIntensity, GLuint uniformShininess, GLuint uniformUseNormalMap);
	void RenderIcons(glm::mat4 projection, glm::mat4 view);
	void RenderGizmo(glm::mat4 projection, glm::mat4 view, glm::vec3 cameraPos);

	// ImGui interface
	void RenderImGui();

	// Creation methods
	void CreateGameObject(const std::string& type);
	void CreateLight(LightType type);

	// Gizmo methods
	void InitGizmo();
	void HandleMousePress(int button, int action, float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, glm::vec3 cameraPos);
	void HandleMouseMove(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view);

	// Deletion
	void DeleteLight(int index);

	// Sync with global lighting system
	void SetLightArrays(PointLight* pLights, unsigned int* pCount, SpotLight* sLights, unsigned int* sCount) {
		globalPointLights = pLights;
		globalPointLightCount = pCount;
		globalSpotLights = sLights;
		globalSpotLightCount = sCount;
	}

	// Default resources
	void SetDefaultResources(Texture* tex, Material* mat) { defaultTexture = tex; defaultMaterial = mat; }

	// Clear all objects
	void Clear();

private:
	std::vector<GameObject*> objects;
	std::vector<LightObject*> lights;
	int selectedObjectIndex;
	int selectedLightIndex;

	Texture* defaultTexture = nullptr;
	Material* defaultMaterial = nullptr;

	struct WindowState {
		bool isHierarchyOpen = true;
		bool isInspectorOpen = true;
	} windowState;

	// Global light state pointers
	PointLight* globalPointLights = nullptr;
	unsigned int* globalPointLightCount = nullptr;
	SpotLight* globalSpotLights = nullptr;
	unsigned int* globalSpotLightCount = nullptr;

	// Color picking resources
	GLuint pickingFBO;
	GLuint pickingTexture;
	GLuint pickingDepth;
	Shader pickingShader;
	int pickWidth, pickHeight;
	bool pickingInitialized;

	// Icon resources
	Shader iconShader;
	Texture* lightIconTexture;
	Mesh* iconMesh;
	void CreateIconMesh();

	// Gizmo resources
	Shader gizmoShader;
	Model* gizmoArrowModel = nullptr;
	Model* gizmoTorusModel = nullptr;

	// Gizmo dragging state
	int activeDragAxis = 0; // 0=None, 20001=X, 20002=Y, 20003=Z (Move), 20004=X, 20005=Y, 20006=Z (Rot)
	glm::vec3 dragInitialObjectPos;
	glm::vec3 dragInitialObjectRot;
	glm::vec2 dragInitialMousePos;
	float dragInitialAngle; // For seamless rotation
	glm::vec3 dragInitialIntersectPos;
	glm::vec3 dragPlaneNormal;

	// Ray-Plane math helpers
	glm::vec3 GetMouseRay(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view);
	bool RayPlaneIntersection(glm::vec3 rayOrigin, glm::vec3 rayDir, glm::vec3 planePoint, glm::vec3 planeNormal, glm::vec3& intersectPoint);
};
