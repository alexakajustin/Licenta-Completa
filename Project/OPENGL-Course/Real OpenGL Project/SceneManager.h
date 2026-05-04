#pragma once

#include <vector>
#include <string>
#include <filesystem>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <map>

#include "imgui.h"
#include "GameObject.h"
#include "LightObject.h"
#include "Shader.h"
#include "Texture.h"
#include "Frustum.h"
#include "NodeGraph.h"
#include "InstancedGroup.h"
#include "UndoManager.h"

struct GraphicsSettings;

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
	void ClearSelection() { 
		selectedObjectIndices.clear(); 
		selectedLightIndices.clear(); 
		activeDragAxis = 0; 
		for (auto* group : instancedGroups) if (group) group->ClearSelection();
	}
	
	void SetSelectedIndex(int index, bool multiSelect = false, bool rangeSelect = false);
	int GetSelectedIndex() const; // Returns the "anchor" or last selected object
	const std::vector<int>& GetSelectedObjectIndices() const { return selectedObjectIndices; }
	bool IsObjectSelected(int index) const;

	void SetSelectedLightIndex(int index, bool multiSelect = false, bool rangeSelect = false);
	int GetSelectedLightIndex() const; // Returns the "anchor" or last selected light
	const std::vector<int>& GetSelectedLightIndices() const { return selectedLightIndices; }
	bool IsLightSelected(int index) const;

	std::string GetSelectedName() const;

	void RenderAll(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos,
		class DirectionalLight* mainLight, class PointLight* pointLights, unsigned int pointLightCount,
		class SpotLight* spotLights, unsigned int spotLightCount,
		float time = 0.0f, const Frustum* frustum = nullptr, Shader* overrideShader = nullptr,
		float screenWidth = 0.0f, float screenHeight = 0.0f, class Renderer* renderer = nullptr, GLuint sceneDepthTexture = 0, GLuint reflectionTexture = 0, GLuint refractionTexture = 0,
		glm::vec4 clipPlane = glm::vec4(0, 0, 0, 1), glm::mat4 shadowTransform = glm::mat4(1.0f), const GraphicsSettings* gs = nullptr);
	void RenderIcons(glm::mat4 projection, glm::mat4 view);
	void RenderGizmo(glm::mat4 projection, glm::mat4 view, glm::vec3 cameraPos);

	// ========== GPU-Driven Instanced Groups ==========
	void AddInstancedGroup(InstancedGroup* group);
	void RemoveInstancedGroup(const std::string& name);
	void ClearInstancedGroups();
	std::vector<InstancedGroup*>& GetInstancedGroups() { return instancedGroups; }
	void SetCullShader(Shader* s) { cullShader = s; }
	void SetInstancedRenderShader(Shader* s) { instancedRenderShader = s; }
	void SetRenderDistanceMultiplier(float m) { renderDistanceMultiplier = m; }
	void SetShadowDistanceMultiplier(float m) { shadowDistanceMultiplier = m; }
	float GetRenderDistanceMultiplier() const { return renderDistanceMultiplier; }
	float GetShadowDistanceMultiplier() const { return shadowDistanceMultiplier; }
	
	void SetGraphicsSettings(GraphicsSettings* gs) { graphicsSettings = gs; }

	// ========== Picking & Gizmo ==========
	void InitPicking(int width, int height);
	void InitIcons();
	void InitGizmo();
	int PickObject(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, glm::vec3 cameraPos, float viewportWidth, float viewportHeight);
	int GetActiveDragAxis() const { return activeDragAxis; }
	void HandleMousePress(int button, int action, float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, glm::vec3 cameraPos, float viewportWidth = 0.0f, float viewportHeight = 0.0f);
	void HandleMouseMove(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, float viewportWidth = 0.0f, float viewportHeight = 0.0f);
	void BoxSelect(glm::vec2 rectMin, glm::vec2 rectMax, const glm::mat4& projection, const glm::mat4& view, float viewportWidth, float viewportHeight, bool additive = false, GLuint depthFBO = 0);
	void SetBoxSelecting(bool val) { isBoxSelecting = val; }
	bool GetBoxSelecting() const { return isBoxSelecting; }
	
	void DeleteSelectedObjects();
	void DeleteSelectedLights();
	
	void CopySelected();
	void Paste();

	// ========== Creation / Deletion ==========
	void CreateGameObject(const std::string& type, glm::vec3 spawnPos = glm::vec3(0.0f));
	void InstantiateModel(const std::filesystem::path& path, glm::vec3 spawnPos = glm::vec3(0.0f));
	void DeleteGameObject(int index);
	void CreateLight(LightType type, glm::vec3 spawnPos = glm::vec3(0.0f));
	void DeleteLight(int index);

	// ========== Configuration ==========
	void SetLightArrays(PointLight* pLights, unsigned int* pCount, SpotLight* sLights, unsigned int* sCount) {
		globalPointLights = pLights;
		globalPointLightCount = pCount;
		globalSpotLights = sLights;
		globalSpotLightCount = sCount;
	}
	void SetDefaultResources(Texture* tex, Material* mat) { defaultTexture = tex; defaultMaterial = mat; }
	void SetMainShader(Shader* s) { mainShader = s; }
	Shader* GetMainShader() const { return mainShader; }
	NodeGraph& GetNodeGraph() { return nodeGraph; }

	// ========== Undo/Redo ==========
	UndoManager& GetUndoManager() { return undoManager; }

	// Low-level helpers for undo actions (no memory management, no undo recording)
	void InsertObjectAt(GameObject* obj, int index);
	void RemoveObjectRaw(int index); // Removes from vector WITHOUT deleting memory

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
	Shader* mainShader = nullptr;

	// GPU-Driven instanced groups
	std::vector<InstancedGroup*> instancedGroups;
	Shader* cullShader = nullptr;
	Shader* instancedRenderShader = nullptr;

	// Hi-Z Occlusion Culling
	GLuint hizTexture = 0;
	int hizWidth = 0;
	int hizHeight = 0;
	int hizMipCount = 1;
	Shader* hizComputeShader = nullptr;
	Shader* hizCopyShader = nullptr;

	void GenerateHiZMap(int screenWidth, int screenHeight, GLuint sceneDepthTexture);

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
	bool isBoxSelecting = false;
	
	struct InitialState {
		glm::vec3 position; // World position
		glm::vec3 rotation; // Euler angles
	};
	std::map<GameObject*, InitialState> dragInitialObjectStates;
	std::map<LightObject*, glm::vec3> dragInitialLightPositions;

	glm::vec3 dragInitialObjectPos;
	glm::vec3 dragInitialObjectRot;
	glm::vec2 dragInitialMousePos;
	glm::vec3 dragInitialIntersectPos;
	glm::vec3 dragPlaneNormal;
	// Rotation-specific: ray-plane approach
	glm::vec3 dragRotationCenter;
	glm::vec3 dragInitialRotVec;  // Initial vector from center to plane intersection
	glm::vec3 dragRotationAxis;   // The world-space rotation axis
	
	// Clipboard
	struct LightClipboardEntry {
		LightType type;
		std::string name;
		glm::vec3 color;
		float ambientIntensity;
		float diffuseIntensity;
		glm::vec3 position;
		glm::vec3 direction;
		float constant, linear, exponent;
		float edge;
	};
	std::vector<GameObject*> clipboardObjects;
	std::vector<LightClipboardEntry> clipboardLights;
	void ClearClipboard();

	// Helper: build rotation matrix from Euler angles (DRY — used by picking, gizmo, drag)
	glm::mat4 GetSelectedRotationMatrix() const;
	// Helper: get the gizmo position (from object or light)
	bool GetGizmoPosition(glm::vec3& outPos) const;

	float renderDistanceMultiplier = 1.0f;
	float shadowDistanceMultiplier = 1.0f;

	NodeGraph nodeGraph;
	UndoManager undoManager;

	GraphicsSettings* graphicsSettings = nullptr;
};
