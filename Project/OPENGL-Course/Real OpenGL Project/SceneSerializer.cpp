#include "SceneSerializer.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "LightObject.h"
#include "DirectionalLight.h"
#include "PointLight.h"
#include "SpotLight.h"
#include "Texture.h"
#include "Material.h"
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

	// ========== Serialize Objects ==========
	json objectsArray = json::array();
	for (auto* obj : scene.GetObjects())
	{
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
	pointLightCount = 0;
	spotLightCount = 0;

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

	printf("[SceneSerializer] Scene loaded from: %s (%d objects, %d lights)\n",
		filePath.c_str(), (int)scene.GetObjects().size(), (int)scene.GetLights().size());

	return true;
}
