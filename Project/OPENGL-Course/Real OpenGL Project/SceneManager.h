#pragma once

#include <vector>
#include <string>
#include <filesystem>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "GameObject.h"
#include "LightObject.h"
#include "Shader.h"
#include "Texture.h"

class SceneManager
{
public:
	SceneManager();
	~SceneManager();

	// ========== Object Management ==========
	void AddObject(GameObject* obj);
	void RemoveObject(const std::string& name);
	GameObject* FindObject(const std::string& name);
	std::vector<GameObject*>& GetObjects() { return objects; }

	// ========== Light Management ==========
	void AddLight(LightObject* light);
	std::vector<LightObject*>& GetLights() { return lights; }

	// ========== Selection ==========
	void ClearSelection() { selectedObjectIndices.clear(); selectedLightIndices.clear(); activeDragAxis = 0; }
	
	void SetSelectedIndex(int index, bool multiSelect = false, bool rangeSelect = false);
	int GetSelectedIndex() const; // Returns the "anchor" or last selected object
	const std::vector<int>& GetSelectedObjectIndices() const { return selectedObjectIndices; }
	bool IsObjectSelected(int index) const;

	void SetSelectedLightIndex(int index, bool multiSelect = false, bool rangeSelect = false);
	int GetSelectedLightIndex() const; // Returns the "anchor" or last selected light
	const std::vector<int>& GetSelectedLightIndices() const { return selectedLightIndices; }
	bool IsLightSelected(int index) const;

	std::string GetSelectedName() const;

	// ========== Rendering ==========
	void RenderAll(GLint uniformModel, GLint uniformSpecularIntensity, GLint uniformShininess, GLint uniformMaterialColor, GLint uniformUseNormalMap, GLint uniformUseDiffuseTexture);
	void RenderIcons(glm::mat4 projection, glm::mat4 view);
	void RenderGizmo(glm::mat4 projection, glm::mat4 view, glm::vec3 cameraPos);

	// ========== Picking & Gizmo ==========
	void InitPicking(int width, int height);
	void InitIcons();
	void InitGizmo();
	int PickObject(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, glm::vec3 cameraPos, float viewportWidth, float viewportHeight);
	int GetActiveDragAxis() const { return activeDragAxis; }
	void HandleMousePress(int button, int action, float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, glm::vec3 cameraPos, float viewportWidth = 0.0f, float viewportHeight = 0.0f);
	void HandleMouseMove(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, float viewportWidth = 0.0f, float viewportHeight = 0.0f);
	
	void DeleteSelectedObjects();
	void DeleteSelectedLights();

	// ========== Creation / Deletion ==========
	void CreateGameObject(const std::string& type);
	void InstantiateModel(const std::filesystem::path& path, glm::vec3 spawnPos = glm::vec3(0.0f));
	void DeleteGameObject(int index);
	void CreateLight(LightType type);
	void DeleteLight(int index);

	// ========== Configuration ==========
	void SetLightArrays(PointLight* pLights, unsigned int* pCount, SpotLight* sLights, unsigned int* sCount) {
		globalPointLights = pLights;
		globalPointLightCount = pCount;
		globalSpotLights = sLights;
		globalSpotLightCount = sCount;
	}
	void SetDefaultResources(Texture* tex, Material* mat) { defaultTexture = tex; defaultMaterial = mat; }

	// ========== Utilities (public for EditorUI viewport drop) ==========
	glm::vec3 GetMouseRay(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, float viewportWidth, float viewportHeight);
	bool RayPlaneIntersect(glm::vec3 rayOrigin, glm::vec3 rayDir, glm::vec3 planePoint, glm::vec3 planeNormal, glm::vec3& intersectPoint);

	// ========== Cleanup ==========
	void Clear();

	// Keep for asset browser backward compat
	void RefreshAssetList() {} // No-op, AssetBrowser handles this now

private:
	std::vector<GameObject*> objects;
	std::vector<LightObject*> lights;
	
	std::vector<int> selectedObjectIndices; // Ordered by selection time, last is primary
	std::vector<int> selectedLightIndices;

	Texture* defaultTexture = nullptr;
	Material* defaultMaterial = nullptr;

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
	int activeDragAxis = 0;
	glm::vec3 dragInitialObjectPos;
	glm::vec3 dragInitialObjectRot;
	glm::vec2 dragInitialMousePos;
	glm::vec3 dragInitialIntersectPos;
	glm::vec3 dragPlaneNormal;
	// Rotation-specific: ray-plane approach
	glm::vec3 dragRotationCenter;
	glm::vec3 dragInitialRotVec;  // Initial vector from center to plane intersection
	glm::vec3 dragRotationAxis;   // The world-space rotation axis

	// Helper: build rotation matrix from Euler angles (DRY — used by picking, gizmo, drag)
	glm::mat4 GetSelectedRotationMatrix() const;
	// Helper: get the gizmo position (from object or light)
	bool GetGizmoPosition(glm::vec3& outPos) const;
};
