#include "SceneSerializer.h"
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

bool SceneSerializer::SaveScene(const std::string& filePath, SceneManager& scene)
{
	json j;
	j["version"] = 1;

	// ========== Serialize Node Graph (The "Recipe") ==========
	json graphJson = scene.GetNodeGraph().Serialize();
	j["nodeGraph"] = graphJson;

	// ========== Serialize Objects (The "Results") ==========
	json objectsArray = json::array();
	json assetsArray = json::array();
	std::map<MeshData*, int> meshToAssetId;

	for (auto* obj : scene.GetObjects())
	{
		// SMART FILTER: Skip objects managed by the Node Graph
		// They will be regenerated on load via NodeGraph::Execute()
		if (scene.GetNodeGraph().IsObjectGenerated(obj->GetName()))
			continue;

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
			objJson["material"]["specular"] = mat->GetSpecularIntensity();
			objJson["material"]["shininess"] = mat->GetShininess();
			objJson["material"]["color"] = { mat->GetColor().x, mat->GetColor().y, mat->GetColor().z };
			objJson["material"]["tiling"] = { mat->GetTiling().x, mat->GetTiling().y };
			objJson["material"]["offset"] = { mat->GetOffset().x, mat->GetOffset().y };
		}

		// Custom Mesh Data (with deduplication)
		// SMART FILTER: Do not save custom mesh data if the node graph actively modifies it!
		// Proceeding with saving here creates massive file bloat (saving millions of generated vertices)
		// and introduces the risk of double-processing on load. The graph recalculates this anyway!
		if (obj->HasCustomMesh() && !scene.GetNodeGraph().IsObjectMeshModified(obj->GetName()))
		{
			const MeshData& data = obj->GetCPUMeshData();
			// Since MeshData is now shared via shared_ptr, we can check pointers
			MeshData* dataPtr = const_cast<MeshData*>(&data);

			if (meshToAssetId.find(dataPtr) == meshToAssetId.end())
			{
				int newId = (int)assetsArray.size();
				json assetJson;
				assetJson["vertices"] = data.vertices;
				assetJson["indices"] = data.indices;
				assetsArray.push_back(assetJson);
				meshToAssetId[dataPtr] = newId;
			}
			objJson["meshAssetId"] = meshToAssetId[dataPtr];
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
				layersArray.push_back(layerJson);
			}
			objJson["textureLayers"] = layersArray;
		}

		objectsArray.push_back(objJson);
	}
	j["objects"] = objectsArray;
	j["assets"] = assetsArray;

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
	Texture* defaultTexture, Material* defaultMaterial)
{
	std::ifstream file(filePath);
	if (!file.is_open())
	{
		printf("[SceneSerializer] ERROR: Could not open file for reading: %s\n", filePath.c_str());
		return false;
	}

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

	// ========== Clear existing scene ==========
	scene.Clear();
	scene.GetNodeGraph().Clear();
	pointLightCount = 0;
	spotLightCount = 0;

	// ========== Load Asset Library ==========
	std::vector<std::shared_ptr<MeshData>> assetLibrary;
	if (j.contains("assets"))
	{
		for (const auto& aj : j["assets"])
		{
			auto data = std::make_shared<MeshData>();
			data->vertices = aj["vertices"].get<std::vector<GLfloat>>();
			data->indices = aj["indices"].get<std::vector<unsigned int>>();
			assetLibrary.push_back(data);
		}
	}

	// ========== Load Objects ==========
	if (j.contains("objects"))
	{
		for (auto& objJson : j["objects"])
		{
			std::string name = objJson.value("name", "Object");
			std::string primType = objJson.value("primitiveType", "");
			std::string modelPath = objJson.value("modelPath", "");

			GameObject* obj = new GameObject(name);

			// Recreate mesh based on primitiveType
			if (primType == "Plane")       obj->SetMesh(PrimitiveGenerator::CreatePlane());
			else if (primType == "Cube")   obj->SetMesh(PrimitiveGenerator::CreateCube());
			else if (primType == "Sphere") obj->SetMesh(PrimitiveGenerator::CreateSphere());

			obj->SetPrimitiveType(primType);

			// Recreate model from path (through AssetManager for caching)
			if (!modelPath.empty())
			{
				Model* model = AssetManager::Get().GetModel(modelPath);
				obj->SetModel(model);
				obj->SetModelSourcePath(modelPath);
			}

			// Mesh Asset override (Deduplication)
			if (objJson.contains("meshAssetId"))
			{
				int assetId = objJson["meshAssetId"];
				if (assetId >= 0 && assetId < (int)assetLibrary.size())
				{
					auto data = assetLibrary[assetId];
					Mesh* newMesh = data->ToMesh();
					if (newMesh)
					{
						obj->SetMesh(newMesh);
						obj->SetCPUMeshData(data);
					}
				}
			}
			// Legacy Custom Mesh support
			else if (objJson.contains("customMesh"))
			{
				auto& meshJson = objJson["customMesh"];
				if (meshJson.contains("vertices") && meshJson.contains("indices"))
				{
					auto data = std::make_shared<MeshData>();
					data->vertices = meshJson["vertices"].get<std::vector<GLfloat>>();
					data->indices = meshJson["indices"].get<std::vector<unsigned int>>();

					Mesh* newMesh = data->ToMesh();
					if (newMesh)
					{
						obj->SetMesh(newMesh);
						obj->SetCPUMeshData(data);
					}
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
				float spec = matJson.value("specular", 0.5f);
				float shine = matJson.value("shininess", 32.0f);

				glm::vec3 color(1.0f);
				if (matJson.contains("color"))
				{
					auto& c = matJson["color"];
					color = glm::vec3(c[0].get<float>(), c[1].get<float>(), c[2].get<float>());
				}

				Material* mat = new Material(spec, shine, color);

				if (matJson.contains("tiling"))
				{
					auto& t = matJson["tiling"];
					mat->SetTiling(glm::vec2(t[0].get<float>(), t[1].get<float>()));
				}
				if (matJson.contains("offset"))
				{
					auto& o = matJson["offset"];
					mat->SetOffset(glm::vec2(o[0].get<float>(), o[1].get<float>()));
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

					obj->AddTextureLayer(layer);
				}
			}

			scene.AddObject(obj);
		}

		// Second pass: resolve parent-child relationships
		auto& objects = scene.GetObjects();
		for (int i = 0; i < (int)j["objects"].size() && i < (int)objects.size(); i++)
		{
			std::string parentName = j["objects"][i].value("parent", "");
			if (!parentName.empty())
			{
				GameObject* parent = scene.FindObject(parentName);
				if (parent && parent != objects[i])
				{
					objects[i]->SetParent(parent);
				}
			}
		}
	}

	// ========== Load Lights ==========
	if (j.contains("lights"))
	{
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
				*mainLight.GetDirectionPtr() = dir;

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

	// ========== Load Node Graph (The "Recipe") ==========
	if (j.contains("nodeGraph"))
	{
		scene.GetNodeGraph().Deserialize(j["nodeGraph"], scene);
		
		// Wait for all assets to finish loading before auto-execution.
		// Otherwise, models used by the scatter nodes will be empty, and instances will not spawn.
		AssetManager::Get().WaitForAll();

		// AUTO-EXECUTE: Recreate the millions of objects
		scene.GetNodeGraph().Execute(scene, defaultTexture, defaultMaterial);
		printf("[SceneSerializer] Node Graph restored and auto-executed.\n");
	}

	printf("[SceneSerializer] Scene loaded from: %s (%d objects, %d lights)\n",
		filePath.c_str(), (int)scene.GetObjects().size(), (int)scene.GetLights().size());

	return true;
}
