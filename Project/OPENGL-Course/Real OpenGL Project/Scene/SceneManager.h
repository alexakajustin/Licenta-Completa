#pragma once

#include <vector>
#include <string>
#include <filesystem>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <map>

#include "imgui.h"
#include "Scene/GameObject.h"
#include "Lighting/LightObject.h"
#include "Rendering/Shader.h"
#include "Rendering/Texture.h"
#include "Core/Frustum.h"
#include "Nodes/NodeGraph.h"
#include "Rendering/InstancedGroup.h"
#include "Rendering/WaterFoamMap.h"
#include "Rendering/Impostor.h"
#include "Scene/UndoManager.h"
#include "Editor/TransformGizmo.h"

class Player;
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
	void GetAllObjects(std::vector<GameObject*>& outList);
	Player* FindPlayer();
	std::vector<GameObject*>& GetObjects() { return objects; }

	// ========== Light Management ==========
	void AddLight(LightObject* light);
	std::vector<LightObject*>& GetLights() { return lights; }

	// ========== Selection ==========
	void ClearSelection() { 
		selectedObjectIndices.clear(); 
		selectedLightIndices.clear(); 
		AbortGizmoDrag();
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
		glm::vec4 clipPlane = glm::vec4(0, 0, 0, 1), glm::mat4 shadowTransform = glm::mat4(1.0f), const GraphicsSettings* gs = nullptr,
		float customCullDistance = -1.0f);
	void RenderIcons(glm::mat4 projection, glm::mat4 view);

	// ========== GPU-Driven Instanced Groups ==========
	void AddInstancedGroup(InstancedGroup* group);
	void RemoveInstancedGroup(const std::string& name);
	void ClearInstancedGroups();
	std::vector<InstancedGroup*>& GetInstancedGroups() { return instancedGroups; }
	// Once per frame before the shadow passes: tree impostors (bakes the missing ones, Impostor.h)
	void UpdateImpostors(const GraphicsSettings* gs) { impostors.Update(instancedGroups, gs); }
	const WaterFoamMap& GetWaterFoamMap() const { return waterFoam; }
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
	int GetActiveDragAxis() const { return activeDragAxis; } // Non-zero while a gizmo handle is dragged
	void HandleMousePress(int button, int action, float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, glm::vec3 cameraPos, float viewportWidth = 0.0f, float viewportHeight = 0.0f);
	// snapInvert: Ctrl held (snapping on when it is off, and off when on)
	void HandleMouseMove(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, float viewportWidth = 0.0f, float viewportHeight = 0.0f, bool snapInvert = false);

	// Transform gizmo (Editor/TransformGizmo.h): mode, space, pivot and snapping live in it
	TransformGizmo& GetGizmo() { return gizmo; }
	// Camera and selection -> gizmo; once per frame before hover tests and drawing
	void SyncGizmo(const glm::mat4& view, const glm::mat4& projection, glm::vec2 viewportSize);
	void UpdateGizmoHover(float mouseX, float mouseY, bool inViewport);
	void DrawGizmo(ImDrawList* drawList, glm::vec2 viewportScreenPos, const glm::mat4& view, const glm::mat4& projection, glm::vec2 viewportSize);
	void CancelGizmoDrag(); // Esc / right click during a drag: everything back where it was
	void AbortGizmoDrag();  // The selection changed under the drag: stop without touching anything
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
	void DeleteGameObject(GameObject* obj);
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
	NodeGraph& GetNodeGraph() { return *graphTabs[activeTabIndex].graph; }

	// ========== Multi-Tab Graph Management ==========
	struct GraphTab {
		std::string name;
		std::unique_ptr<NodeGraph> graph;
	};

	std::vector<GraphTab>& GetGraphTabs() { return graphTabs; }
	const std::vector<GraphTab>& GetGraphTabs() const { return graphTabs; }
	int GetActiveTabIndex() const { return activeTabIndex; }
	void SetActiveTabIndex(int index);

	void AddGraphTab(const std::string& name);
	void RemoveGraphTab(int index);
	void RenameGraphTab(int index, const std::string& newName);
	void ClearGraphs();
	void EnsureDefaultTab();

	void ExecutePipeline(Texture* defaultTex, Material* defaultMat, std::function<void(float, float, const std::string&)> progressCallback = nullptr);
	GraphNode* FindNodeInAllGraphs(int nodeId, NodeGraph** outGraph = nullptr);
	void NotifyAllGraphsObjectRenamed(const std::string& oldName, const std::string& newName);
	std::shared_ptr<int>& GetSharedNextId() { return sharedNextId; }

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

	// Occlusion Culling Access
	GLuint GetHiZTexture() const { return hizTexture; }
	// Depth of this frame's planar water reflection (0 = none): the lake adds the fog between
	// the water and what it mirrors
	void SetReflectionDepthTexture(GLuint texture) { reflectionDepthTexture = texture; }
	// Height the planar water reflection is rendered for (rivers use it where they reach that height)
	void SetReflectionPlaneHeight(float height) { reflectionPlaneHeight = height; }
	GLuint GetHiZDebugTexture() const { return hizDebugTexture; }
	int GetHiZWidth() const { return hizWidth; }
	int GetHiZHeight() const { return hizHeight; }
	void GenerateHiZDebug(float nearPlane, float farPlane);

	// Exclude a specific object from rendering (e.g. self during reflection map capture)
	void SetExcludeObject(GameObject* obj) { excludeObject = obj; }
	GameObject* GetExcludeObject() const { return excludeObject; }


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
	GLuint waterDepthCopy = 0;   // Opaque depth copy sampled by water while it writes depth
	GLuint reflectionDepthTexture = 0;
	float reflectionPlaneHeight = -1.0e9f;
	int waterDepthCopyW = 0, waterDepthCopyH = 0;
	WaterFoamMap waterFoam;       // Foam around objects standing in the water + river wakes
	ImpostorSystem impostors;     // Distant trees as cards baked from their full LOD0
	GLuint hizDebugTexture = 0;
	int hizWidth = 0;
	int hizHeight = 0;
	int hizMipCount = 1;
	Shader* hizComputeShader = nullptr;
	Shader* hizCopyShader = nullptr;
	Shader* hizDebugShader = nullptr;

	// CPU-Side Hi-Z Occlusion Culling (legacy PBO readback — kept for compatibility)
	GLuint hizPBO[2] = { 0, 0 };
	int currentPBO = 0;
	std::vector<float> cpuHiZMap;
	int cpuHiZWidth = 0;
	int cpuHiZHeight = 0;
	glm::mat4 prevViewProj = glm::mat4(1.0f);

	// GPU-Driven Object Occlusion Culling (NVIDIA-style compute shader)
	Shader* objectCullShader = nullptr;
	GLuint objectBoundsSSBO = 0;           // Per-object AABB data uploaded each frame
	GLuint objectVisibilitySSBO[2] = {0,0}; // Double-buffered: [writeFrame], [readFrame]
	int objectCullWriteIdx = 0;            // Which SSBO to write to this frame
	std::vector<uint32_t> cpuObjectVisibility; // CPU readback from previous frame
	int lastObjectCullCount = 0;           // Number of objects tested last dispatch
	bool objectCullReady = false;          // True after first frame (readback available)
	void DispatchObjectCull(const glm::mat4& viewProj, const glm::mat4& projection, const glm::vec3& cameraPos,
		int screenWidth, int screenHeight, const std::vector<GameObject*>& cullList);

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
	Mesh* debugSphereMesh;
	Mesh* debugCubeMesh;
	void CreateIconMesh();

	// Flat-color shader (bounds and culling debug draws)
	Shader gizmoShader;

	// Transform gizmo and the selection's state when its drag started
	TransformGizmo gizmo;
	int activeDragAxis = 0;
	bool isBoxSelecting = false;
	struct GizmoObjectStart {
		GameObject* object;
		glm::mat4 world;          // World matrix at the start
		glm::mat4 parent;         // What the local transform is relative to (GameObject::GetWorldMatrix)
		glm::vec3 position, rotation, scale; // Local
	};
	struct GizmoLightStart { LightObject* light; glm::vec3 position; bool hasDirection; glm::vec3 direction; };
	struct GizmoInstanceStart { InstancedGroup* group; int index; glm::vec4 positionAndScale; glm::vec4 rotationAndFlags; };
	std::vector<GizmoObjectStart> gizmoObjects;
	std::vector<GizmoLightStart> gizmoLights;
	std::vector<GizmoInstanceStart> gizmoInstances;
	bool gizmoChanged = false;
	void BeginGizmoDrag();
	void ApplyGizmoDrag();
	void EndGizmoDrag();
	void RestoreGizmoStart();
	
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

	// Helper: the gizmo's pivot and orientation for the current selection
	bool GetGizmoTarget(glm::vec3& pivot, glm::mat3& orientation, bool& canRotate, bool& canScale);

	float renderDistanceMultiplier = 1.0f;
	float shadowDistanceMultiplier = 1.0f;

	std::vector<GraphTab> graphTabs;
	int activeTabIndex = 0;
	std::shared_ptr<int> sharedNextId;
	UndoManager undoManager;

	GraphicsSettings* graphicsSettings = nullptr;

	GameObject* excludeObject = nullptr;
};
