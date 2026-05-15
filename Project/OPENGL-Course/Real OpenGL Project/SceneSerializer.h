#pragma once

#include <string>
#include <functional>

class SceneManager;
class DirectionalLight;
class PointLight;
class SpotLight;
class Texture;
class Material;
class Camera;
class GameObject;
class LightObject;

using SceneProgressCallback = std::function<void(float, float, const std::string&)>;

class SceneSerializer
{
public:
	static bool SaveScene(const std::string& filePath, SceneManager& scene, Camera* camera = nullptr);

	static bool LoadScene(const std::string& filePath, SceneManager& scene,
		DirectionalLight& mainLight,
		PointLight* pointLights, unsigned int& pointLightCount,
		SpotLight* spotLights, unsigned int& spotLightCount,
		Texture* defaultTexture, Material* defaultMaterial,
		Camera* camera = nullptr,
		SceneProgressCallback progressCallback = nullptr);

	// =====================================================================
	// In-Memory Snapshots (for Undo/Redo — no disk I/O)
	// Returns/accepts compact JSON strings to avoid exposing nlohmann in header
	// =====================================================================
	
	static std::string SnapshotObject(GameObject* obj);
	static void RestoreObject(GameObject* obj, const std::string& jsonStr, SceneManager* scene = nullptr);

	static std::string SnapshotLight(LightObject* light);
	static void RestoreLight(LightObject* light, const std::string& jsonStr);

	// Native Win32 file dialogs
	static std::string OpenFileDialog(
		const char* filter = "Scene Files (*.json)\0*.json\0All Files (*.*)\0*.*\0",
		const char* title = "Open Scene");

	static std::string SaveFileDialog(
		const char* filter = "Scene Files (*.json)\0*.json\0All Files (*.*)\0*.*\0",
		const char* title = "Save Scene");
};
