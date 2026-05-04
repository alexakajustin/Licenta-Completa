#include "SceneSerializer.h"
#include "Camera.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "LightObject.h"
#include "DirectionalLight.h"
#include "PointLight.h"
#include "SpotLight.h"
#include "Texture.h"
#include "Material.h"
#include "TextureLayer.h"
#include "PrimitiveGenerator.h"
#include "AssetManager.h"
#include "CommonValues.h"

#include "External Libs/nlohmann/json.hpp"

#include <fstream>
#include <iostream>
#include <Windows.h>
#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")

using json = nlohmann::json;

// =====================================================================
// Win32 Native File Dialogs
// =====================================================================

std::string SceneSerializer::OpenFileDialog(const char* filter, const char* title)
{
	OPENFILENAMEA ofn;
	char szFile[MAX_PATH] = "";
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.lpstrTitle = title;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

	if (GetOpenFileNameA(&ofn))
	{
		return std::string(szFile);
	}
	return "";
}

std::string SceneSerializer::SaveFileDialog(const char* filter, const char* title)
{
	OPENFILENAMEA ofn;
	char szFile[MAX_PATH] = "";
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.lpstrTitle = title;
	ofn.lpstrDefExt = "json";
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

	if (GetSaveFileNameA(&ofn))
	{
		return std::string(szFile);
	}
	return "";
}

// =====================================================================
// Helper: safe texture path extraction
// =====================================================================

static std::string GetTexturePath(Texture* tex)
{
	if (!tex) return "";
	const char* loc = tex->GetFileLocation();
	if (!loc || loc[0] == '\0') return "";
	return std::string(loc);
}

// =====================================================================
// Save Scene
// =====================================================================

bool SceneSerializer::SaveScene(const std::string& filePath, SceneManager& scene, Camera* camera)
{
	json j;
	j["version"] = 1;

	// ========== Serialize Camera ==========
	if (camera)
	{
		json cam;
		cam["position"] = { camera->getCameraPosition().x, camera->getCameraPosition().y, camera->getCameraPosition().z };
		cam["yaw"] = camera->getYaw();
		cam["pitch"] = camera->getPitch();
		cam["front"] = { camera->getCameraDirection().x, camera->getCameraDirection().y, camera->getCameraDirection().z };
		j["camera"] = cam;
	}

	// ========== Serialize Node Graph (The "Recipe") ==========
	json graphJson = scene.GetNodeGraph().Serialize();
	j["nodeGraph"] = graphJson;

	// ========== Serialize Objects (The "Results") ==========
	json objectsArray = json::array();
	for (auto* obj : scene.GetObjects())
	{
		// SMART FILTER: Previously we skipped objects managed by the Node Graph entirely.
		// However, users may want to tweak material parameters or visual properties of 
		// spawned objects (like Rivers) in the Inspector. We now save these objects to JSON,
		// but we still skip saving their massive binary mesh data (see below) because
		// the graph will regenerate the geometry on load anyway.
		GameObject* root = obj;
		while (root->GetParent()) root = root->GetParent();
		if (!root->GetSaveInScene()) continue; // Skip massive procedural hierarchies
		if (!obj->GetSaveInScene()) continue;

		json objJson;
		objJson["name"] = obj->GetName();
		objJson["primitiveType"] = obj->GetPrimitiveType();
		objJson["modelPath"] = obj->GetModelSourcePath();

		// Transform
		const Transform& t = obj->GetTransform();
		objJson["transform"]["position"] = { t.GetPosition().x, t.GetPosition().y, t.GetPosition().z };
		objJson["transform"]["rotation"] = { t.GetRotation().x, t.GetRotation().y, t.GetRotation().z };
		objJson["transform"]["scale"] = { t.GetScale().x, t.GetScale().y, t.GetScale().z };

		// Hierarchy
		objJson["parent"] = obj->GetParent() ? obj->GetParent()->GetName() : "";
		objJson["inheritScale"] = obj->GetInheritScale();

		// Textures
		objJson["texturePath"] = GetTexturePath(obj->GetTexture());
		objJson["normalMapPath"] = GetTexturePath(obj->GetNormalMap());

		// Material
		if (obj->GetMaterial())
		{
			Material* mat = obj->GetMaterial();
			if (mat->GetShader()) {
				objJson["material"]["shader_vert"] = mat->GetShader()->GetVertexPath();
				objJson["material"]["shader_frag"] = mat->GetShader()->GetFragmentPath();
			}

			// Save all dynamic properties
			for (auto const& [name, val] : mat->GetFloats()) objJson["material"][name] = val;
			for (auto const& [name, val] : mat->GetVec2s())  objJson["material"][name] = { val.x, val.y };
			for (auto const& [name, val] : mat->GetVec3s())  objJson["material"][name] = { val.x, val.y, val.z };
			for (auto const& [name, val] : mat->GetVec4s())  objJson["material"][name] = { val.x, val.y, val.z, val.w };
			for (auto const& [name, path] : mat->GetTexturePaths()) objJson["material"][name] = path;
		}

		// SAVE CUSTOM MESH CACHE (.mesh binary sidecar)
		// We save custom vertex data to a separate binary file to avoid JSON bloat.
		// This acts as a 'visual cache' so the scene loads instantly with correct meshes,
		// even before the procedural graph finishes its re-execution.
		if (obj->HasCustomMesh())
		{
			std::filesystem::path scenePath(filePath);
			std::string sceneStem = scenePath.stem().string();
			std::string objName = obj->GetName();
			
			// Sanitize object name for filesystem
			for (char& c : objName) if (c == ' ' || c == '\\' || c == '/' || c == ':' || c == '<' || c == '>' || c == '|' || c == '*' || c == '?') c = '_';
			
			std::string meshFileName = sceneStem + "_" + objName + ".mesh";
			std::string meshFilePath = (scenePath.parent_path() / meshFileName).string();
			
			if (obj->GetCPUMeshData().SaveToBinary(meshFilePath)) {
				objJson["customMeshPath"] = meshFileName;
			}
		}

		// Texture Layers
		if (!obj->GetTextureLayers().empty())
		{
			json layersArray = json::array();
			for (const auto& layer : obj->GetTextureLayers())
			{
				json layerJson;
				layerJson["texturePath"] = layer.texturePath;
				layerJson["normalMapPath"] = layer.normalMapPath;
				layerJson["blendMode"] = (int)layer.blendMode;
				layerJson["opacity"] = layer.opacity;
				layerJson["tiling"] = layer.tiling;
				layerJson["heightMin"] = layer.heightMin;
				layerJson["heightMax"] = layer.heightMax;
				layerJson["slopeMin"] = layer.slopeMin;
				layerJson["slopeMax"] = layer.slopeMax;
				layerJson["invert"] = layer.invert;
				layerJson["displacementMapPath"] = layer.displacementMapPath;
				layerJson["displacementScale"] = layer.displacementScale;
				layersArray.push_back(layerJson);
			}
			objJson["textureLayers"] = layersArray;
		}

		// GPU Tessellation
		if (obj->GetUseTessellation()) {
			objJson["tessellation"]["enabled"] = true;
			objJson["tessellation"]["level"] = obj->GetTessLevel();
			objJson["tessellation"]["distance"] = obj->GetTessDistance();
			objJson["tessellation"]["displacementScale"] = obj->GetTessDisplacementScale();
			objJson["tessellation"]["displacementBias"] = obj->GetTessDisplacementBias();
		}

		objectsArray.push_back(objJson);
	}
	j["objects"] = objectsArray;

	// ========== Serialize Lights ==========
	json lightsArray = json::array();
	for (auto* light : scene.GetLights())
	{
		json lightJson;
		lightJson["name"] = light->GetName();

		std::string typeStr;
		switch (light->GetLightType())
		{
		case LightType::Directional: typeStr = "Directional"; break;
		case LightType::Point:       typeStr = "Point"; break;
		case LightType::Spot:        typeStr = "Spot"; break;
		}
		lightJson["type"] = typeStr;

		// Color
		glm::vec3* color = light->GetColorPtr();
		if (color) lightJson["color"] = { color->x, color->y, color->z };

		// Intensities
		float* ambient = light->GetAmbientIntensityPtr();
		if (ambient) lightJson["ambientIntensity"] = *ambient;

		float* diffuse = light->GetDiffuseIntensityPtr();
		if (diffuse) lightJson["diffuseIntensity"] = *diffuse;

		// Position (Point/Spot only)
		glm::vec3* pos = light->GetPositionPtr();
		if (pos) lightJson["position"] = { pos->x, pos->y, pos->z };

		// Direction (Directional/Spot only)
		glm::vec3* dir = light->GetDirectionPtr();
		if (dir) lightJson["direction"] = { dir->x, dir->y, dir->z };

		// Attenuation (Point/Spot only)
		float* constant = light->GetConstantPtr();
		float* linear = light->GetLinearPtr();
		float* exponent = light->GetExponentPtr();
		if (constant) lightJson["constant"] = *constant;
		if (linear) lightJson["linear"] = *linear;
		if (exponent) lightJson["exponent"] = *exponent;

		lightsArray.push_back(lightJson);
	}
	j["lights"] = lightsArray;

	// ========== Serialize Instanced Groups ==========
	json instancedGroupsArray = json::array();
	for (auto* group : scene.GetInstancedGroups())
	{
		json groupJson;
		groupJson["name"] = group->GetName();
		groupJson["sourceObjectName"] = group->GetSourceObjectName();
		groupJson["maxDrawDistance"] = group->GetMaxDrawDistance();
		groupJson["shadowDistance"] = group->GetShadowDistance();

		// Save the cpuInstances to a binary file
		std::filesystem::path scenePath(filePath);
		std::string sceneStem = scenePath.stem().string();
		std::string safeGroupName = group->GetName();
		for (char& c : safeGroupName) if (c == ' ' || c == '\\' || c == '/' || c == ':' || c == '<' || c == '>' || c == '|' || c == '*' || c == '?') c = '_';
		std::string instFileName = sceneStem + "_" + safeGroupName + ".inst";
		std::string instFilePath = (scenePath.parent_path() / instFileName).string();

		// Write binary instance data
		std::ofstream instFile(instFilePath, std::ios::binary);
		if (instFile.is_open())
		{
			uint32_t count = (uint32_t)group->cpuInstances.size();
			instFile.write((const char*)&count, sizeof(uint32_t));
			if (count > 0) {
				instFile.write((const char*)group->cpuInstances.data(), count * sizeof(InstancedGroup::PackedInstance));
			}
			instFile.close();
			groupJson["instanceDataPath"] = instFileName;
		}

		instancedGroupsArray.push_back(groupJson);
	}
	j["instancedGroups"] = instancedGroupsArray;

	// ========== Write to File ==========
	std::ofstream file(filePath);
	if (!file.is_open())
	{
		printf("[SceneSerializer] ERROR: Could not open file for writing: %s\n", filePath.c_str());
		return false;
	}
	file << j.dump(4);
	file.close();

	printf("[SceneSerializer] Scene saved to: %s\n", filePath.c_str());
	return true;
}

// =====================================================================
// Load Scene
// =====================================================================

bool SceneSerializer::LoadScene(const std::string& filePath, SceneManager& scene,
	DirectionalLight& mainLight,
	PointLight* pointLights, unsigned int& pointLightCount,
	SpotLight* spotLights, unsigned int& spotLightCount,
	Texture* defaultTexture, Material* defaultMaterial,
	Camera* camera,
	SceneProgressCallback progressCallback)
{
	if (progressCallback) progressCallback(5.0f, 0.0f, "Opening Scene File...");

	std::ifstream file(filePath);
	if (!file.is_open())
	{
		printf("[SceneSerializer] ERROR: Could not open file for reading: %s\n", filePath.c_str());
		return false;
	}

	if (progressCallback) progressCallback(10.0f, 0.0f, "Parsing JSON Data...");

	json j;
	try
	{
		file >> j;
	}
	catch (const std::exception& e)
	{
		printf("[SceneSerializer] ERROR: Failed to parse JSON: %s\n", e.what());
		return false;
	}
	file.close();

	// ========== Load Camera ==========
	if (camera && j.contains("camera"))
	{
		auto& cam = j["camera"];
		if (cam.contains("position"))
		{
			auto& p = cam["position"];
			camera->setCameraPosition(glm::vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>()));
		}
		if (cam.contains("yaw")) camera->setYaw(cam["yaw"].get<float>());
		if (cam.contains("pitch")) camera->setPitch(cam["pitch"].get<float>());
		
		// Re-calculate front vector if possible (assuming Camera has a method or handles it via yaw/pitch)
		camera->update(); 
	}

	if (progressCallback) progressCallback(15.0f, 0.0f, "Clearing Current Scene...");

	// ========== Clear existing scene ==========
	scene.Clear();
	scene.GetNodeGraph().Clear();
	pointLightCount = 0;
	spotLightCount = 0;

	// ========== Pre-load Models ==========
	if (j.contains("objects"))
	{
		// First pass: Pre-load all models into the AssetManager
		for (auto& objJson : j["objects"])
		{
			std::string modelPath = objJson.value("modelPath", "");
			if (!modelPath.empty())
			{
				AssetManager::Get().GetModel(modelPath);
			}
		}
		
		// CRITICAL: Wait for all models to finish CPU AND GPU loading (buffers uploaded)
		AssetManager::Get().WaitForAll();
	}

	// ========== Load Objects ==========
	if (j.contains("objects"))
	{
		int objCount = (int)j["objects"].size();
		int objIndex = 0;

		for (auto& objJson : j["objects"])
		{
			objIndex++;
			if (progressCallback) {
				float pct = 15.0f + ((float)objIndex / (float)objCount) * 15.0f; // 15% -> 30%
				progressCallback(pct, 0.0f, "Loading Object: " + objJson.value("name", "Object"));
			}

			std::string name = objJson.value("name", "Object");
			std::string primType = objJson.value("primitiveType", "");
			std::string modelPath = objJson.value("modelPath", "");

			GameObject* obj = new GameObject(name);

			// Recreate mesh based on primitiveType
			if (primType == "Plane")       obj->SetMesh(PrimitiveGenerator::CreatePlane());
			else if (primType == "Cube")   obj->SetMesh(PrimitiveGenerator::CreateCube());
			else if (primType == "Sphere") obj->SetMesh(PrimitiveGenerator::CreateSphere());

			obj->SetPrimitiveType(primType);

			if (!modelPath.empty())
			{
				Model* model = AssetManager::Get().GetModel(modelPath);
				
				// CRITICAL: If the model has multiple meshes (like Sponza), we DON'T assign it to the root.
				// This prevents rendering the whole model twice and allows clicking children.
				if (model && model->GetMeshCount() == 1) {
					obj->SetModel(model);
				}
				obj->SetModelSourcePath(modelPath);
			}

			// Load custom binary baked mesh if the old graph was cleared
			if (objJson.contains("customMeshPath"))
			{
				std::string meshFileName = objJson["customMeshPath"].get<std::string>();
				std::string meshFilePath = (std::filesystem::path(filePath).parent_path() / meshFileName).string();
				
				MeshData bakedData;
				if (bakedData.LoadFromBinary(meshFilePath)) {
					Mesh* newMesh = bakedData.ToMesh();
					obj->SetMesh(newMesh);
					obj->SetCPUMeshData(bakedData);
					printf("[SceneSerializer] Successfully loaded baked custom mesh: %s\n", meshFileName.c_str());
				} else {
					printf("[SceneSerializer] Warning: Could not find baked mesh sidecar: %s\n", meshFilePath.c_str());
				}
			}

			// Transform
			if (objJson.contains("transform"))
			{
				auto& t = objJson["transform"];
				if (t.contains("position"))
				{
					auto& p = t["position"];
					obj->GetTransform().SetPosition(glm::vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>()));
				}
				if (t.contains("rotation"))
				{
					auto& r = t["rotation"];
					obj->GetTransform().SetRotation(glm::vec3(r[0].get<float>(), r[1].get<float>(), r[2].get<float>()));
				}
				if (t.contains("scale"))
				{
					auto& s = t["scale"];
					obj->GetTransform().SetScale(glm::vec3(s[0].get<float>(), s[1].get<float>(), s[2].get<float>()));
				}
			}

			// Hierarchy settings (parent is resolved in a second pass)
			obj->SetInheritScale(objJson.value("inheritScale", true));

			// Texture
			std::string texPath = objJson.value("texturePath", "");
			if (!texPath.empty())
			{
				Texture* tex = new Texture(texPath.c_str());
				if (tex->LoadTextureA())
				{
					obj->SetTexture(tex);
				}
				else
				{
					printf("[SceneSerializer] Warning: Failed to load texture: %s\n", texPath.c_str());
					delete tex;
				}
			}

			// Normal map
			std::string normPath = objJson.value("normalMapPath", "");
			if (!normPath.empty())
			{
				Texture* norm = new Texture(normPath.c_str());
				if (norm->LoadTextureA())
				{
					obj->SetNormalMap(norm);
				}
				else
				{
					printf("[SceneSerializer] Warning: Failed to load normal map: %s\n", normPath.c_str());
					delete norm;
				}
			}

			// Material
			if (objJson.contains("material"))
			{
				auto& matJson = objJson["material"];
				Material* mat = new Material();

				// Load all properties
				for (auto it = matJson.begin(); it != matJson.end(); ++it) {
					if (it.value().is_number_float() || it.value().is_number_integer()) {
						mat->SetFloat(it.key(), it.value().get<float>());
					}
					else if (it.value().is_string()) {
						if (it.key() != "shader_vert" && it.key() != "shader_frag") {
							mat->SetTextureParam(it.key(), it.value().get<std::string>());
						}
					}
					else if (it.value().is_array() && it.value().size() == 2) {
						mat->SetVec2(it.key(), glm::vec2(it.value()[0], it.value()[1]));
					}
					else if (it.value().is_array() && it.value().size() == 3) {
						mat->SetVec3(it.key(), glm::vec3(it.value()[0], it.value()[1], it.value()[2]));
					}
					else if (it.value().is_array() && it.value().size() == 4) {
						mat->SetVec4(it.key(), glm::vec4(it.value()[0], it.value()[1], it.value()[2], it.value()[3]));
					}
				}

				// Handle shader loading
				if (matJson.contains("shader_vert") && matJson.contains("shader_frag")) {
					std::string v = matJson["shader_vert"];
					std::string f = matJson["shader_frag"];
					// We need a way to load/cache this shader. 
					// For now, we'll set it to mainShader if it matches, 
					// or we'll need a ShaderManager.
					if (v == "Assets/Shaders/shader.vert" || v == "Shaders/shader.vert") mat->SetShader(scene.GetMainShader());
					else {
						// Create a new shader for this material? 
						// This could lead to duplicate shaders.
						Shader* s = new Shader();
						s->CreateFromFiles(v.c_str(), f.c_str());
						mat->SetShader(s);
					}
				} else {
					mat->SetShader(scene.GetMainShader());
				}

				obj->SetMaterial(mat);
			}

			// Texture Layers
			if (objJson.contains("textureLayers"))
			{
				for (const auto& layerJson : objJson["textureLayers"])
				{
					TextureLayer layer;
					layer.texturePath = layerJson.value("texturePath", "");
					layer.normalMapPath = layerJson.value("normalMapPath", "");
					layer.blendMode = (LayerBlendMode)layerJson.value("blendMode", 0);
					layer.opacity = layerJson.value("opacity", 1.0f);
					layer.tiling = layerJson.value("tiling", 1.0f);
					layer.heightMin = layerJson.value("heightMin", 0.0f);
					layer.heightMax = layerJson.value("heightMax", 100.0f);
					layer.slopeMin = layerJson.value("slopeMin", 0.0f);
					layer.slopeMax = layerJson.value("slopeMax", 0.5f);
					layer.invert = layerJson.value("invert", false);
					layer.displacementMapPath = layerJson.value("displacementMapPath", "");
					layer.displacementScale = layerJson.value("displacementScale", 0.05f);

					if (!layer.texturePath.empty())
					{
						Texture* tex = new Texture(layer.texturePath.c_str());
						if (tex->LoadTextureA()) {
							layer.texture = tex;
						} else {
							printf("[SceneSerializer] Warning: Failed to load layer texture: %s\n", layer.texturePath.c_str());
							delete tex;
						}
					}

					if (!layer.normalMapPath.empty())
					{
						Texture* norm = new Texture(layer.normalMapPath.c_str());
						if (norm->LoadTextureA()) {
							layer.normalMap = norm;
						} else {
							printf("[SceneSerializer] Warning: Failed to load layer normal map: %s\n", layer.normalMapPath.c_str());
							delete norm;
						}
					}

					if (!layer.displacementMapPath.empty())
					{
						Texture* disp = new Texture(layer.displacementMapPath.c_str());
						if (disp->LoadTextureGrayscale()) {
							layer.displacementMap = disp;
						} else {
							printf("[SceneSerializer] Warning: Failed to load layer displacement map: %s\n", layer.displacementMapPath.c_str());
							delete disp;
						}
					}

					obj->AddTextureLayer(layer);
				}
			}

			// GPU Tessellation
			if (objJson.contains("tessellation")) {
				auto& tessJson = objJson["tessellation"];
				obj->SetUseTessellation(tessJson.value("enabled", false));
				obj->SetTessLevel(tessJson.value("level", 8.0f));
				obj->SetTessDistance(tessJson.value("distance", 50.0f));
				obj->SetTessDisplacementScale(tessJson.value("displacementScale", 1.0f));
				obj->SetTessDisplacementBias(tessJson.value("displacementBias", -0.5f));
			}

			scene.AddObject(obj);
		}

		// Second pass: resolve parent-child relationships and re-link modular meshes
		auto& objects = scene.GetObjects();
		// Final pass: Re-link modular hierarchies and update transforms
		// Ensure any models requested during the main loop (if any were missed) are also finished
		AssetManager::Get().WaitForAll();

		for (size_t i = 0; i < objects.size(); i++)
		{
			std::string parentName = j["objects"][i].value("parent", "");
			if (!parentName.empty())
			{
				GameObject* parent = scene.FindObject(parentName);
				if (parent && parent != objects[i])
				{
					parent->AddChild(objects[i]); // Use raw parenting to preserve local transforms from JSON

					// MODULAR FIX: If this is a child of a model-based root, re-link the mesh
					if (!parent->GetModelSourcePath().empty() && !objects[i]->GetMesh() && !objects[i]->GetModel())
					{
						Model* parentModel = AssetManager::Get().GetModel(parent->GetModelSourcePath());
						if (parentModel) {
							// Find mesh matching this object's name (stripping suffixes like " (Foliage)")
							std::string targetName = objects[i]->GetName();
							size_t suffixPos = targetName.find(" (");
							if (suffixPos != std::string::npos) targetName = targetName.substr(0, suffixPos);

							for (size_t m = 0; m < parentModel->GetMeshCount(); m++) {
								bool nameMatch = (parentModel->GetMeshNames()[m] == targetName);
								bool indexMatch = (!nameMatch && targetName == ("Mesh_" + std::to_string(m)));

								if (nameMatch || indexMatch) {
									objects[i]->SetMesh(parentModel->GetMesh(m));
									
									// UNIFIED FIX: If the child has no layers, pull them from the model's material
									if (objects[i]->GetTextureLayers().empty()) {
										unsigned int matIdx = parentModel->GetMaterialIndex((unsigned int)m);
										Texture* diffuse = parentModel->GetTexture(matIdx);
										Texture* normal = parentModel->GetNormalMap(matIdx);
										
										if (diffuse || normal) {
											TextureLayer layer;
											layer.texture = diffuse;
											layer.normalMap = normal;
											layer.texturePath = diffuse ? diffuse->GetFileLocation() : "";
											layer.normalMapPath = normal ? normal->GetFileLocation() : "";
											objects[i]->AddTextureLayer(layer);
										}
									}
									break;
								}
							}
						}
					}
				}
			}
		}
	}

	if (progressCallback) progressCallback(35.0f, 0.0f, "Loading Scene Lights...");

	// ========== Load Lights ==========
	if (j.contains("lights"))
	{
		pointLightCount = 0;
		spotLightCount = 0;

		for (auto& lightJson : j["lights"])
		{
			std::string name = lightJson.value("name", "Light");
			std::string type = lightJson.value("type", "Point");

			glm::vec3 color(1.0f);
			if (lightJson.contains("color"))
			{
				auto& c = lightJson["color"];
				color = glm::vec3(c[0].get<float>(), c[1].get<float>(), c[2].get<float>());
			}
			float ambient = lightJson.value("ambientIntensity", 0.1f);
			float diffuse = lightJson.value("diffuseIntensity", 0.8f);

			if (type == "Directional")
			{
				glm::vec3 dir(0.0f, -1.0f, 0.0f);
				if (lightJson.contains("direction"))
				{
					auto& d = lightJson["direction"];
					dir = glm::vec3(d[0].get<float>(), d[1].get<float>(), d[2].get<float>());
				}

				// Modify the existing directional light in Application
				*mainLight.GetColourPtr() = color;
				*mainLight.GetAmbientIntensityPtr() = ambient;
				*mainLight.GetDiffuseIntensityPtr() = diffuse;
				mainLight.SetDirection(dir);

				// Recreate the wrapper
				LightObject* lightObj = new LightObject(name, &mainLight);
				scene.AddLight(lightObj);
			}
			else if (type == "Point")
			{
				if (pointLightCount < MAX_POINT_LIGHTS)
				{
					glm::vec3 pos(0.0f, 5.0f, 0.0f);
					if (lightJson.contains("position"))
					{
						auto& p = lightJson["position"];
						pos = glm::vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
					}

					float con = lightJson.value("constant", 1.0f);
					float lin = lightJson.value("linear", 0.02f);
					float exp = lightJson.value("exponent", 0.01f);

					pointLights[pointLightCount] = PointLight(
						1024, 1024, 0.01f, 100.0f,
						color.r, color.g, color.b,
						ambient, diffuse,
						pos.x, pos.y, pos.z,
						con, lin, exp
					);

					LightObject* lightObj = new LightObject(name, &pointLights[pointLightCount]);
					scene.AddLight(lightObj);
					pointLightCount++;
				}
			}
			else if (type == "Spot")
			{
				if (spotLightCount < MAX_SPOT_LIGHTS)
				{
					glm::vec3 pos(0.0f, 5.0f, 0.0f);
					if (lightJson.contains("position"))
					{
						auto& p = lightJson["position"];
						pos = glm::vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
					}

					glm::vec3 dir(0.0f, -1.0f, 0.0f);
					if (lightJson.contains("direction"))
					{
						auto& d = lightJson["direction"];
						dir = glm::vec3(d[0].get<float>(), d[1].get<float>(), d[2].get<float>());
					}

					float con = lightJson.value("constant", 1.0f);
					float lin = lightJson.value("linear", 0.02f);
					float exp = lightJson.value("exponent", 0.01f);

					spotLights[spotLightCount] = SpotLight(
						1024, 1024, 0.01f, 100.0f,
						color.r, color.g, color.b,
						ambient, diffuse,
						pos.x, pos.y, pos.z,
						dir.x, dir.y, dir.z,
						con, lin, exp,
						20.0f  // Default edge angle
					);

					LightObject* lightObj = new LightObject(name, &spotLights[spotLightCount]);
					scene.AddLight(lightObj);
					spotLightCount++;
				}
			}
		}
	}

	if (progressCallback) progressCallback(38.0f, 0.0f, "Loading Instanced Groups...");

	// ========== Load Instanced Groups ==========
	if (j.contains("instancedGroups"))
	{
		for (auto& groupJson : j["instancedGroups"])
		{
			std::string name = groupJson.value("name", "InstancedGroup");
			std::string sourceObjName = groupJson.value("sourceObjectName", "");
			float maxDraw = groupJson.value("maxDrawDistance", 200.0f);
			float shadowDist = groupJson.value("shadowDistance", 30.0f);

			std::string instFileName = groupJson.value("instanceDataPath", "");
			if (instFileName.empty()) continue;

			std::string instFilePath = (std::filesystem::path(filePath).parent_path() / instFileName).string();
			std::ifstream instFile(instFilePath, std::ios::binary);
			if (!instFile.is_open())
			{
				printf("[SceneSerializer] Warning: Could not find instance data file %s\n", instFilePath.c_str());
				continue;
			}

			uint32_t count = 0;
			instFile.read((char*)&count, sizeof(uint32_t));
			std::vector<InstancedGroup::PackedInstance> instances(count);
			if (count > 0) {
				instFile.read((char*)instances.data(), count * sizeof(InstancedGroup::PackedInstance));
			}
			instFile.close();

			// Reconstruct setup by finding the source object
			GameObject* sourceObj = scene.FindObject(sourceObjName);
			if (!sourceObj && !sourceObjName.empty()) {
				// Fallback to searching by prefix if sourceObjName is a sub-mesh name
				for (auto* obj : scene.GetObjects()) {
					std::string objName = obj->GetName();
					size_t suffixPos = objName.find(" (");
					if (suffixPos != std::string::npos) objName = objName.substr(0, suffixPos);
					
					if (objName == sourceObjName) {
						sourceObj = obj;
						break;
					}
				}
			}

			if (sourceObj && sourceObj->GetMesh())
			{
				Mesh* mesh = sourceObj->GetMesh();
				Material* mat = sourceObj->GetMaterial() ? sourceObj->GetMaterial() : defaultMaterial;
				Texture* tex = sourceObj->GetTexture() ? sourceObj->GetTexture() : defaultTexture;
				Texture* norm = sourceObj->GetNormalMap();
				auto layers = sourceObj->GetTextureLayers();
				
				InstancedGroup* group = new InstancedGroup(name);
				group->SetSourceObjectName(sourceObjName);
				group->Setup(mesh, instances, mat, tex, norm, layers);
				group->SetMaxDrawDistance(maxDraw);
				group->SetShadowDistance(shadowDist);
				scene.AddInstancedGroup(group);
			}
			else if (sourceObj && sourceObj->GetModel())
			{
				Model* model = sourceObj->GetModel();
				
				// Extract sub-mesh index from group name (e.g., "_0", "_1")
				int mIdx = 0;
				size_t lastUnderscore = name.find_last_of('_');
				if (lastUnderscore != std::string::npos && lastUnderscore < name.length() - 1) {
					try {
						mIdx = std::stoi(name.substr(lastUnderscore + 1));
					} catch (...) {
						mIdx = 0;
					}
				}

				if (mIdx >= 0 && mIdx < model->GetMeshCount()) {
					Mesh* mesh = model->GetMesh(mIdx);
					unsigned int matIdx = model->GetMaterialIndex(mIdx);
					Material* mat = sourceObj->GetMaterial() ? sourceObj->GetMaterial() : defaultMaterial;
					Texture* tex = sourceObj->GetTexture() ? sourceObj->GetTexture() : model->GetTexture(matIdx);
					if (!tex) tex = defaultTexture;
					Texture* norm = sourceObj->GetNormalMap() ? sourceObj->GetNormalMap() : model->GetNormalMap(matIdx);
					auto layers = sourceObj->GetTextureLayers();

					InstancedGroup* group = new InstancedGroup(name);
					group->SetSourceObjectName(sourceObjName);
					group->Setup(mesh, instances, mat, tex, norm, layers);
					group->SetMaxDrawDistance(maxDraw);
					group->SetShadowDistance(shadowDist);
					scene.AddInstancedGroup(group);
				}
			}
			else
			{
				printf("[SceneSerializer] Warning: Could not reconstruct InstancedGroup '%s', missing source object '%s'\n", name.c_str(), sourceObjName.c_str());
			}
		}
	}

	// ========== Load Node Graph (The "Recipe") ==========
	if (j.contains("nodeGraph"))
	{
		if (progressCallback) progressCallback(40.0f, 0.0f, "Restoring Node Graph...");

		scene.GetNodeGraph().Deserialize(j["nodeGraph"], scene);
		
		if (progressCallback) progressCallback(45.0f, 0.0f, "Waiting for Assets...");

		// Wait for all assets to finish loading before auto-execution.
		// Otherwise, models used by the scatter nodes will be empty, and instances will not spawn.
		AssetManager::Get().WaitForAll();

		if (progressCallback) progressCallback(50.0f, 0.0f, "Executing Generation Pipeline...");

		// AUTO-EXECUTE: Recreate the millions of objects
		scene.GetNodeGraph().Execute(scene, defaultTexture, defaultMaterial, progressCallback);
		printf("[SceneSerializer] Node Graph restored and auto-executed.\n");
	}

	printf("[SceneSerializer] Scene loaded from: %s (%d objects, %d lights)\n",
		filePath.c_str(), (int)scene.GetObjects().size(), (int)scene.GetLights().size());

	return true;
}
