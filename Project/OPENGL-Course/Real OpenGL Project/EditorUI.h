#pragma once

#include <string>
#include <glm/glm.hpp>
#include <GL/glew.h>

#include "imgui.h"
#include "Shader.h"
#include "Mesh.h"
#include "Texture.h"

class SceneManager;
class Camera;
class GameObject;

class EditorUI
{
public:
	EditorUI();
	~EditorUI();

	// Draw all editor panels (hierarchy + inspector)
	void Render(SceneManager& scene, const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos, GLuint sceneTextureID, Camera* camera = nullptr);

private:
	void RenderHierarchy(SceneManager& scene, int bufferHeight, Camera* camera = nullptr);
	void RenderHierarchyRecursive(SceneManager& scene, GameObject* obj, int index, Camera* camera);
	void RenderInspector(SceneManager& scene, int bufferWidth, int bufferHeight);
	void RenderViewport(SceneManager& scene, const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos, GLuint textureID);

	// Helper: Unity-style Vector3 input 
	static void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float speed = 0.1f);

	// Helper: handle ASSET_PATH drag-drop (DRY — used by hierarchy, inspector, and viewport)
	static void HandleAssetDrop(SceneManager& scene, glm::vec3 spawnPos = glm::vec3(0.0f));

	struct WindowState {
		bool isHierarchyOpen = true;
		bool isInspectorOpen = true;
		bool isViewportOpen = true;
	} windowState;

	// Material preview sphere
	void InitMaterialPreview();
	void RenderMaterialPreview(float specular, float shininess, glm::vec3 color, Texture* diffuse = nullptr, Texture* normal = nullptr);
	GLuint previewFBO = 0;
	GLuint previewTexture = 0;
	GLuint previewDepth = 0;
	Shader previewShader;
	Mesh* previewSphere = nullptr;
	bool previewInitialized = false;
	static const int PREVIEW_SIZE = 128;
};
