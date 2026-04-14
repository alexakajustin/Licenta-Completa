#pragma once

#include <string>
#include <functional>

class SceneManager;
class DirectionalLight;
class PointLight;
class SpotLight;
class Texture;
class Material;

using SceneProgressCallback = std::function<void(float, float, const std::string&)>;

class SceneSerializer
{
public:
	static bool SaveScene(const std::string& filePath, SceneManager& scene);

	static bool LoadScene(const std::string& filePath, SceneManager& scene,
		DirectionalLight& mainLight,
		PointLight* pointLights, unsigned int& pointLightCount,
		SpotLight* spotLights, unsigned int& spotLightCount,
		Texture* defaultTexture, Material* defaultMaterial,
		SceneProgressCallback progressCallback = nullptr);

	// Native Win32 file dialogs
	static std::string OpenFileDialog(
		const char* filter = "Scene Files (*.json)\0*.json\0All Files (*.*)\0*.*\0",
		const char* title = "Open Scene");

	static std::string SaveFileDialog(
		const char* filter = "Scene Files (*.json)\0*.json\0All Files (*.*)\0*.*\0",
		const char* title = "Save Scene");
};
