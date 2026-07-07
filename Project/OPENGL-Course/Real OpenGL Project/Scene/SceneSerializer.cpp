#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <commdlg.h>

#include "Scene/SceneSerializer.h"
#include "Core/Camera.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "Scene/BoxCollider.h"
#include "Scene/MeshCollider.h"
#include "Scene/CapsuleCollider.h"
#include "Scene/RigidBody.h"
#include "Scene/Player.h"
#include "Scene/Planet.h"
#include "Lighting/LightObject.h"
#include "Lighting/DirectionalLight.h"
#include "Lighting/PointLight.h"
#include "Lighting/SpotLight.h"
#include "Rendering/Texture.h"
#include "Rendering/Material.h"
#include "Rendering/TextureLayer.h"
#include "Rendering/PrimitiveGenerator.h"
#include "Core/AssetManager.h"
#include "Core/ServiceLocator.h"
#include "CommonValues.h"

#include "External Libs/nlohmann/json.hpp"

#include <fstream>
#include <iostream>
#include <set>
#include <thread>

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

	// ========== Serialize Node Graphs (Multi-Tab Pipeline) ==========
	{
		json graphsJson;
		graphsJson["nextId"] = *scene.GetSharedNextId();
		graphsJson["activeTabIndex"] = scene.GetActiveTabIndex();

		json tabsArray = json::array();
		for (const auto& tab : scene.GetGraphTabs()) {
			json tabJson;
			tabJson["name"] = tab.name;
			tabJson["graph"] = tab.graph->Serialize();
			tabsArray.push_back(tabJson);
		}
		graphsJson["tabs"] = tabsArray;
		j["nodeGraphs"] = graphsJson;
	}

	// ========== Serialize Objects (The "Results") ==========
	std::map<GameObject*, int> ptrToSavedIndex;
	int indexCounter = 0;
	for (auto* obj : scene.GetObjects()) {
		ptrToSavedIndex[obj] = indexCounter++;
	}

	json objectsArray = json::array();
	for (auto* obj : scene.GetObjects())
	{
		json objJson;
		objJson["name"] = obj->GetName();
		objJson["primitiveType"] = obj->GetPrimitiveType();
		objJson["modelPath"] = obj->GetModelSourcePath();
		objJson["isVisible"] = obj->GetVisible();

		// Transform
		const Transform& t = obj->GetTransform();
		objJson["transform"]["position"] = { t.GetPosition().x, t.GetPosition().y, t.GetPosition().z };
		objJson["transform"]["rotation"] = { t.GetRotation().x, t.GetRotation().y, t.GetRotation().z };
		objJson["transform"]["scale"] = { t.GetScale().x, t.GetScale().y, t.GetScale().z };

		// Planet Specific
		if (obj->GetPrimitiveType() == "Planet") {
			Planet* planet = dynamic_cast<Planet*>(obj);
			if (planet) {
				objJson["planet"]["subdivisions"] = planet->GetParams().subdivisions;
				objJson["planet"]["seed"] = planet->GetParams().seed;
				objJson["planet"]["radius"] = planet->GetParams().radius;
			}
		}

		// Hierarchy
		objJson["parent"] = obj->GetParent() ? obj->GetParent()->GetName() : "";
		if (obj->GetParent() && ptrToSavedIndex.count(obj->GetParent())) {
			objJson["parentIndex"] = ptrToSavedIndex[obj->GetParent()];
		} else {
			objJson["parentIndex"] = -1;
		}
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
				if (mat->GetShader()->HasTessellation()) {
					objJson["material"]["shader_tcs"] = mat->GetShader()->GetTCSPath();
					objJson["material"]["shader_tes"] = mat->GetShader()->GetTESPath();
				}
			}

			// Save all dynamic properties
			for (auto const& [name, val] : mat->GetFloats()) objJson["material"][name] = val;
			for (auto const& [name, val] : mat->GetInts())   objJson["material"][name] = val;
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

		// Components
		const auto& components = obj->GetComponents();
		if (!components.empty()) {
			json compsArray = json::array();
			for (const auto& comp : components) {
				json cJson;
				cJson["type"] = comp->GetName();
				if (comp->GetName() == "BoxCollider") {
					BoxCollider* bc = dynamic_cast<BoxCollider*>(comp.get());
					if (bc) {
						cJson["size"] = { bc->size.x, bc->size.y, bc->size.z };
						cJson["offset"] = { bc->offset.x, bc->offset.y, bc->offset.z };
						cJson["isTrigger"] = bc->isTrigger;
					}
				} else if (comp->GetName() == "CapsuleCollider") {
					CapsuleCollider* cc = dynamic_cast<CapsuleCollider*>(comp.get());
					if (cc) {
						cJson["height"] = cc->height;
						cJson["radius"] = cc->radius;
					}
				} else if (comp->GetName() == "Player") {
					Player* p = dynamic_cast<Player*>(comp.get());
					if (p) {
						cJson["moveSpeed"] = p->GetMoveSpeed();
						cJson["turnSpeed"] = p->GetTurnSpeed();
						cJson["eyeHeight"] = p->GetEyeHeight();
						cJson["jumpForce"] = p->GetJumpForce();
						cJson["gravity"] = p->GetGravity();
					}
				} else if (comp->GetName() == "RigidBody") {
					RigidBody* rb = dynamic_cast<RigidBody*>(comp.get());
					if (rb) {
						cJson["bodyType"] = static_cast<int>(rb->GetType());
						cJson["mass"] = rb->GetMass();
						cJson["friction"] = rb->GetFriction();
						cJson["restitution"] = rb->GetRestitution();
						cJson["lockRotation"] = rb->GetLockRotation();
					}
				}
				compsArray.push_back(cJson);
			}
			objJson["components"] = compsArray;
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
	pointLightCount = 0;
	spotLightCount = 0;

	// ========== Pre-load Models ==========
	if (j.contains("objects"))
	{
		if (progressCallback) progressCallback(15.0f, 0.0f, "Preparing Assets...");

		// Pass 0: Collect unique model paths to avoid redundant overhead
		std::set<std::string> uniquePaths;
		for (auto& objJson : j["objects"]) {
			std::string path = objJson.value("modelPath", "");
			if (!path.empty()) uniquePaths.insert(path);
		}

		// Pass 0.5: Trigger background loading
		for (const auto& path : uniquePaths) {
			ServiceLocator::GetAssetManager()->GetModel(path);
		}
		
		// Responsive Wait: Process GPU uploads while keeping UI alive
		size_t initialTasks = ServiceLocator::GetAssetManager()->GetActiveTasksCount();
		while (ServiceLocator::GetAssetManager()->GetActiveTasksCount() > 0)
		{
			ServiceLocator::GetAssetManager()->Update(); // Process ready GPU uploads
			
			if (progressCallback) {
				size_t remaining = ServiceLocator::GetAssetManager()->GetActiveTasksCount();
				float progress = 15.0f + (initialTasks > 0 ? (1.0f - (float)remaining / initialTasks) * 15.0f : 15.0f);
				progressCallback(progress, 0.0f, "Uploading GPU Assets (" + std::to_string(remaining) + " left)...");
			}
			
			// Small sleep to avoid pegged CPU if no tasks are ready for GPU yet
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	// ========== Load Objects ==========
	if (j.contains("objects"))
	{
		if (progressCallback) progressCallback(40.0f, 0.0f, "Instantiating Objects (Parallel)...");
		
		int objCount = (int)j["objects"].size();
		std::vector<GameObject*> loadedObjects(objCount, nullptr);

		// Pass 1: Parallel instantiation using all available cores
		const int chunkSize = 100;
		std::vector<std::future<void>> futures;
		
		for (int i = 0; i < objCount; i += chunkSize)
		{
			int end = (std::min)(i + chunkSize, objCount);
			futures.push_back(std::async(std::launch::async, [i, end, &j, &loadedObjects]() {
				for (int k = i; k < end; k++) {
					auto& objJson = j["objects"][k];
					std::string name = objJson.value("name", "Object");
					std::string primType = objJson.value("primitiveType", "");
					
					GameObject* obj = nullptr;
					if (primType == "Planet") obj = new Planet(name);
					else obj = new GameObject(name);
					
					loadedObjects[k] = obj;
				}
			}));
		}
		for (auto& f : futures) f.wait();

		if (progressCallback) progressCallback(55.0f, 0.0f, "Finalizing Objects...");

		// Pass 1.5: Set properties (non-parallel for safety)
		int objIndex = 0;
		std::map<std::string, Texture*> localTextureCache;
		std::map<std::string, Material*> localMaterialCache;

		for (int k = 0; k < objCount; k++)
		{
			auto& objJson = j["objects"][k];
			objIndex++;
			std::string name = objJson.value("name", "Object");
			GameObject* obj = loadedObjects[k];
			if (!obj) continue;

			std::string primType = objJson.value("primitiveType", "");
			std::string modelPath = objJson.value("modelPath", "");
			obj->SetVisible(objJson.value("isVisible", true));

			// Recreate mesh based on primitiveType
			if (primType == "Plane")       obj->SetMesh(PrimitiveGenerator::CreatePlane());
			else if (primType == "Cube")   obj->SetMesh(PrimitiveGenerator::CreateCube());
			else if (primType == "Sphere") obj->SetMesh(PrimitiveGenerator::CreateSphere());

			obj->SetPrimitiveType(primType);

			// Planet initialization
			if (primType == "Planet") {
				Planet* planet = dynamic_cast<Planet*>(obj);
				if (planet && objJson.contains("planet")) {
					auto& pJson = objJson["planet"];
					PlanetParams p = planet->GetParams();
					p.subdivisions = pJson.value("subdivisions", 6);
					p.seed = pJson.value("seed", 12345);
					p.radius = pJson.value("radius", 100.0f);
					planet->SetParams(p);
					planet->Generate(); // Rebuild with loaded subdivisions/radius
				}
			}

			if (!modelPath.empty())
			{
				Model* model = ServiceLocator::GetAssetManager()->GetModel(modelPath);
				if (model && model->GetMeshCount() == 1) {
					obj->SetModel(model);
				}
				obj->SetModelSourcePath(modelPath);
			}

			// Load custom binary baked mesh
			if (objJson.contains("customMeshPath"))
			{
				std::string meshFileName = objJson["customMeshPath"].get<std::string>();
				std::string meshFilePath = (std::filesystem::path(filePath).parent_path() / meshFileName).string();
				MeshData bakedData;
				if (bakedData.LoadFromBinary(meshFilePath)) {
					obj->SetMesh(bakedData.ToMesh());
					obj->SetCPUMeshData(bakedData);
				}
			}

			// Transform
			if (objJson.contains("transform"))
			{
				auto& t = objJson["transform"];
				if (t.contains("position")) {
					auto& p = t["position"];
					obj->GetTransform().SetPosition(glm::vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>()));
				}
				if (t.contains("rotation")) {
					auto& r = t["rotation"];
					obj->GetTransform().SetRotation(glm::vec3(r[0].get<float>(), r[1].get<float>(), r[2].get<float>()));
				}
				if (t.contains("scale")) {
					auto& s = t["scale"];
					obj->GetTransform().SetScale(glm::vec3(s[0].get<float>(), s[1].get<float>(), s[2].get<float>()));
				}
			}

			// Texture Loading (Optimized with Cache)
			std::string texPath = objJson.value("texturePath", "");
			if (!texPath.empty())
			{
				if (localTextureCache.count(texPath)) {
					obj->SetTexture(localTextureCache[texPath]);
				} else {
					Texture* tex = new Texture(texPath.c_str());
					if (tex->LoadTextureA()) {
						obj->SetTexture(tex);
						localTextureCache[texPath] = tex;
					} else { delete tex; }
				}
			}

			// Normal Map (Optimized with Cache)
			std::string normPath = objJson.value("normalMapPath", "");
			if (!normPath.empty())
			{
				if (localTextureCache.count(normPath)) {
					obj->SetNormalMap(localTextureCache[normPath]);
				} else {
					Texture* norm = new Texture(normPath.c_str());
					if (norm->LoadTextureA()) {
						obj->SetNormalMap(norm);
						localTextureCache[normPath] = norm;
					} else { delete norm; }
				}
			}

			// Material Loading (Shared Cache)
			if (objJson.contains("material"))
			{
				auto& matJson = objJson["material"];
				// Simple caching: If it's a modular mesh or has a common material key, reuse it
				std::string matKey = matJson.dump(); // Use full JSON as key for perfect matching
				
				if (localMaterialCache.count(matKey)) {
					obj->SetMaterial(localMaterialCache[matKey]);
				} else {
					Material* mat = new Material();
					mat->SetShader(scene.GetMainShader()); // Default

					if (matJson.contains("shader_vert") && matJson.contains("shader_frag")) {
						std::string vPath = matJson["shader_vert"].get<std::string>();
						std::string fPath = matJson["shader_frag"].get<std::string>();
						if (!vPath.empty() && !fPath.empty()) {
							Shader* customShader = new Shader();
							if (matJson.contains("shader_tcs") && matJson.contains("shader_tes")) {
								std::string tcsPath = matJson["shader_tcs"].get<std::string>();
								std::string tesPath = matJson["shader_tes"].get<std::string>();
								customShader->CreateFromFiles(vPath.c_str(), tcsPath.c_str(), tesPath.c_str(), fPath.c_str());
							} else {
								customShader->CreateFromFiles(vPath.c_str(), fPath.c_str());
							}
							mat->SetShader(customShader);
						}
					}

					for (auto it = matJson.begin(); it != matJson.end(); ++it) {
						std::string key = it.key();
						if (key.find("shader") != std::string::npos) continue;

						if (it.value().is_number()) {
							float val = it.value().get<float>();
							
							// Smart type-matching: check the shader's expected type
							if (mat->GetShader()) {
								auto const& props = mat->GetShader()->GetUniformProperties();
								if (props.count(key)) {
									if (props.at(key).type == Shader::UniformType::Int) {
										mat->SetInt(key, (int)val);
										continue;
									}
								}
							}
							mat->SetFloat(key, val);
						}
						else if (it.value().is_string()) {
							std::string path = it.value().get<std::string>();
							// ... (rest of texture loading logic)
							if (localTextureCache.count(path)) {
								mat->SetTexture(key, localTextureCache[path]);
							} else {
								Texture* t = new Texture(path.c_str());
								if (t->LoadTextureA()) {
									mat->SetTexture(key, t);
									localTextureCache[path] = t;
								} else delete t;
							}
						}
						else if (it.value().is_array()) {
							if (it.value().size() == 2) mat->SetVec2(key, glm::vec2(it.value()[0], it.value()[1]));
							else if (it.value().size() == 3) mat->SetVec3(key, glm::vec3(it.value()[0], it.value()[1], it.value()[2]));
							else if (it.value().size() == 4) mat->SetVec4(key, glm::vec4(it.value()[0], it.value()[1], it.value()[2], it.value()[3]));
						}
					}
					obj->SetMaterial(mat);
					localMaterialCache[matKey] = mat;
				}
			}

			// Texture Layers (Optimized with Cache)
			if (objJson.contains("textureLayers"))
			{
				for (const auto& layerJson : objJson["textureLayers"])
				{
					TextureLayer layer;
					std::string tp = layerJson.value("texturePath", "");
					std::string np = layerJson.value("normalMapPath", "");
					std::string dp = layerJson.value("displacementMapPath", "");

					if (!tp.empty()) {
						if (localTextureCache.count(tp)) layer.texture = localTextureCache[tp];
						else {
							Texture* t = new Texture(tp.c_str());
							if (t->LoadTextureA()) { layer.texture = t; localTextureCache[tp] = t; }
							else delete t;
						}
					}
					if (!np.empty()) {
						if (localTextureCache.count(np)) layer.normalMap = localTextureCache[np];
						else {
							Texture* t = new Texture(np.c_str());
							if (t->LoadTextureA()) { layer.normalMap = t; localTextureCache[np] = t; }
							else delete t;
						}
					}
					if (!dp.empty()) {
						if (localTextureCache.count(dp)) layer.displacementMap = localTextureCache[dp];
						else {
							Texture* t = new Texture(dp.c_str());
							if (t->LoadTextureGrayscale()) { layer.displacementMap = t; localTextureCache[dp] = t; }
							else delete t;
						}
					}

					layer.texturePath = tp;
					layer.normalMapPath = np;
					layer.displacementMapPath = dp;
					layer.blendMode = (LayerBlendMode)layerJson.value("blendMode", 0);
					layer.opacity = layerJson.value("opacity", 1.0f);
					layer.tiling = layerJson.value("tiling", 1.0f);
					layer.heightMin = layerJson.value("heightMin", 0.0f);
					layer.heightMax = layerJson.value("heightMax", 100.0f);
					layer.slopeMin = layerJson.value("slopeMin", 0.0f);
					layer.slopeMax = layerJson.value("slopeMax", 0.5f);
					layer.invert = layerJson.value("invert", false);
					layer.displacementScale = layerJson.value("displacementScale", 0.05f);
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

			// Components
			if (objJson.contains("components")) {
				for (const auto& cJson : objJson["components"]) {
					std::string type = cJson.value("type", "");
					if (type == "BoxCollider") {
						BoxCollider* bc = obj->AddComponent<BoxCollider>();
						if (cJson.contains("size")) {
							auto& s = cJson["size"];
							bc->size = glm::vec3(s[0].get<float>(), s[1].get<float>(), s[2].get<float>());
						}
						if (cJson.contains("offset")) {
							auto& o = cJson["offset"];
							bc->offset = glm::vec3(o[0].get<float>(), o[1].get<float>(), o[2].get<float>());
						}
						bc->isTrigger = cJson.value("isTrigger", false);
					} else if (type == "CapsuleCollider") {
						CapsuleCollider* cc = obj->AddComponent<CapsuleCollider>();
						cc->height = cJson.value("height", 1.7f);
						cc->radius = cJson.value("radius", 0.5f);
					} else if (type == "MeshCollider") {
						obj->AddComponent<MeshCollider>();
					} else if (type == "Player") {
						Player* p = obj->AddComponent<Player>();
						p->SetMoveSpeed(cJson.value("moveSpeed", 5.0f));
						p->SetTurnSpeed(cJson.value("turnSpeed", 50.0f));
						p->SetEyeHeight(cJson.value("eyeHeight", 1.7f));
						p->SetJumpForce(cJson.value("jumpForce", 5.0f));
						p->SetGravity(cJson.value("gravity", 9.8f));
					} else if (type == "RigidBody") {
						RigidBody* rb = obj->AddComponent<RigidBody>();
						rb->SetType(static_cast<RigidBody::BodyType>(cJson.value("bodyType", static_cast<int>(RigidBody::BodyType::Dynamic))));
						rb->SetMass(cJson.value("mass", 1.0f));
						rb->SetFriction(cJson.value("friction", 0.2f));
						rb->SetRestitution(cJson.value("restitution", 0.0f));
						rb->SetLockRotation(cJson.value("lockRotation", false));
					}
				}
			}

			// Culling Settings
			// Legacy culling settings ignored

			scene.AddObject(obj);
		}

		// Second pass: resolve parent-child relationships and re-link modular meshes
		auto& objects = scene.GetObjects();
		// Pass 2: Hierarchy and Mesh Re-linking (O(1) lookup)
		std::map<Model*, std::map<std::string, size_t>> modelMeshIndexCache;
		for (int i = 0; i < (int)objects.size(); i++)
		{
			auto& objJson = j["objects"][i];
			int parentIndex = objJson.value("parentIndex", -1);
			std::string parentName = objJson.value("parent", "");
			if (parentName.empty()) parentName = objJson.value("parentName", ""); // Handle both naming conventions

			GameObject* parent = nullptr;
			if (parentIndex >= 0 && parentIndex < objCount) {
				parent = loadedObjects[parentIndex];
			} else if (!parentName.empty()) {
				// Fallback to name search for backward compatibility with old saves
				for (auto* o : loadedObjects) {
					if (o && o->GetName() == parentName) { parent = o; break; }
				}
			}

			if (parent && parent != loadedObjects[i])
			{
				parent->AddChild(loadedObjects[i]);

				// MODULAR RE-LINKING: If this is a child of a modular model, re-attach its specific mesh
				std::string sourcePath = parent->GetModelSourcePath();
				if (!sourcePath.empty() && !loadedObjects[i]->GetMesh() && !loadedObjects[i]->GetModel())
				{
					Model* parentModel = ServiceLocator::GetAssetManager()->GetModel(sourcePath);
					if (parentModel) 
					{
						// Optimize mesh lookup by indexing the model once
						if (modelMeshIndexCache.find(parentModel) == modelMeshIndexCache.end()) {
							auto& indexMap = modelMeshIndexCache[parentModel];
							for (size_t m = 0; m < parentModel->GetMeshCount(); m++) {
								indexMap[parentModel->GetMeshNames()[m]] = m;
							}
						}

						std::string targetName = loadedObjects[i]->GetName();
						size_t suffixPos = targetName.find(" (");
						if (suffixPos != std::string::npos) targetName = targetName.substr(0, suffixPos);

						auto& indexMap = modelMeshIndexCache[parentModel];
						size_t meshIdx = size_t(-1);

						if (indexMap.count(targetName)) {
							meshIdx = indexMap[targetName];
						} else {
							// Fallback to "Mesh_X" check
							if (targetName.find("Mesh_") == 0) {
								try { meshIdx = std::stoull(targetName.substr(5)); } catch (...) {}
							}
						}

						if (meshIdx < parentModel->GetMeshCount()) {
							loadedObjects[i]->SetMesh(parentModel->GetMesh(meshIdx));
							
							// Inherit textures/layers if missing
							if (loadedObjects[i]->GetTextureLayers().empty()) {
								unsigned int matIdx = parentModel->GetMaterialIndex((unsigned int)meshIdx);
								Texture* diffuse = parentModel->GetTexture(matIdx);
								Texture* normal = parentModel->GetNormalMap(matIdx);
								if (diffuse || normal) {
									TextureLayer layer;
									layer.texture = diffuse;
									layer.normalMap = normal;
									layer.texturePath = diffuse ? diffuse->GetFileLocation() : "";
									layer.normalMapPath = normal ? normal->GetFileLocation() : "";
									loadedObjects[i]->AddTextureLayer(layer);
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
					scene.AddInstancedGroup(group);
				}
			}
			else
			{
				printf("[SceneSerializer] Warning: Could not reconstruct InstancedGroup '%s', missing source object '%s'\n", name.c_str(), sourceObjName.c_str());
			}
		}
	}

	// ========== Load Node Graphs (Multi-Tab Pipeline) ==========
	bool hasGraphs = false;

	if (j.contains("nodeGraphs"))
	{
		// New multi-tab format
		if (progressCallback) progressCallback(40.0f, 0.0f, "Restoring Node Graphs...");

		const auto& graphsJson = j["nodeGraphs"];
		int nextId = graphsJson.value("nextId", 1);
		*scene.GetSharedNextId() = nextId;

		scene.GetGraphTabs().clear();
		int loadedActiveTab = graphsJson.value("activeTabIndex", 0);

		if (graphsJson.contains("tabs"))
		{
			for (const auto& tabJson : graphsJson["tabs"])
			{
				std::string tabName = tabJson.value("name", "Unnamed");
				scene.AddGraphTab(tabName);
				auto& tab = scene.GetGraphTabs().back();
				if (tabJson.contains("graph"))
					tab.graph->Deserialize(tabJson["graph"], scene);
			}
		}

		scene.EnsureDefaultTab();
		scene.SetActiveTabIndex(loadedActiveTab);
		hasGraphs = true;
	}
	else if (j.contains("nodeGraph"))
	{
		// Legacy single-graph format — import into default tab
		if (progressCallback) progressCallback(40.0f, 0.0f, "Restoring Node Graph (Legacy)...");

		scene.GetGraphTabs().clear();
		scene.AddGraphTab("Main");
		scene.SetActiveTabIndex(0);
		scene.GetNodeGraph().Deserialize(j["nodeGraph"], scene);
		hasGraphs = true;
		printf("[SceneSerializer] Legacy single-graph imported into 'Main' tab.\n");
	}

	if (hasGraphs)
	{
		if (progressCallback) progressCallback(45.0f, 0.0f, "Waiting for Assets...");

		// Wait for all assets to finish loading before auto-execution.
		// Otherwise, models used by the scatter nodes will be empty, and instances will not spawn.
		ServiceLocator::GetAssetManager()->WaitForAll();

		if (progressCallback) progressCallback(50.0f, 0.0f, "Executing Generation Pipeline...");

		// AUTO-EXECUTE: Recreate the millions of objects across all tabs
		scene.ExecutePipeline(defaultTexture, defaultMaterial, progressCallback);
		printf("[SceneSerializer] Node Graphs restored and auto-executed (%d tabs).\n", (int)scene.GetGraphTabs().size());
	}

	printf("[SceneSerializer] Scene loaded from: %s (%d objects, %d lights)\n",
		filePath.c_str(), (int)scene.GetObjects().size(), (int)scene.GetLights().size());

	return true;
}

// =====================================================================
// In-Memory Snapshots (for Undo/Redo)
// =====================================================================

std::string SceneSerializer::SnapshotObject(GameObject* obj)
{
	json j;
	if (!obj) return j.dump();

	j["name"] = obj->GetName();

	// Component assignments
	j["has_model"] = (obj->GetModel() != nullptr);
	if (obj->GetModel()) j["model_path"] = obj->GetModel()->GetPath();
	
	j["has_texture"] = (obj->GetTexture() != nullptr);
	if (obj->GetTexture()) j["texture_path"] = obj->GetTexture()->GetFileLocation();
	
	j["has_normal"] = (obj->GetNormalMap() != nullptr);
	if (obj->GetNormalMap()) j["normal_path"] = obj->GetNormalMap()->GetFileLocation();
	
	Material* mat = obj->GetMaterial();
	j["has_material"] = (mat != nullptr);
	if (mat) {
		if (!mat->GetPath().empty()) {
			j["material_path"] = mat->GetPath();
		} else if (mat->GetShader()) {
			j["material_shader_v"] = mat->GetShader()->GetVertexPath();
			j["material_shader_f"] = mat->GetShader()->GetFragmentPath();
		}
	}

	// Transform
	const Transform& t = obj->GetTransform();
	j["pos"] = { t.GetPosition().x, t.GetPosition().y, t.GetPosition().z };
	j["rot"] = { t.GetRotation().x, t.GetRotation().y, t.GetRotation().z };
	j["scl"] = { t.GetScale().x, t.GetScale().y, t.GetScale().z };

	// Tessellation
	j["tess_enabled"] = obj->GetUseTessellation();
	j["tess_level"] = obj->GetTessLevel();
	j["tess_dist"] = obj->GetTessDistance();
	j["tess_disp_scale"] = obj->GetTessDisplacementScale();
	j["tess_disp_bias"] = obj->GetTessDisplacementBias();

	// Material (value properties only — no textures/shaders)
	mat = obj->GetMaterial();
	if (mat) {
		json mj;
		for (auto const& [name, val] : mat->GetFloats()) mj["f"][name] = val;
		for (auto const& [name, val] : mat->GetInts())   mj["i"][name] = val;
		for (auto const& [name, val] : mat->GetVec2s())  mj["v2"][name] = { val.x, val.y };
		for (auto const& [name, val] : mat->GetVec3s())  mj["v3"][name] = { val.x, val.y, val.z };
		for (auto const& [name, val] : mat->GetVec4s())  mj["v4"][name] = { val.x, val.y, val.z, val.w };
		j["mat"] = mj;
	}

	// Texture layer value properties (no texture pointers)
	if (!obj->GetTextureLayers().empty()) {
		json layers = json::array();
		for (const auto& layer : obj->GetTextureLayers()) {
			json lj;
			lj["blend"] = (int)layer.blendMode;
			lj["opacity"] = layer.opacity;
			lj["tiling"] = layer.tiling;
			lj["hMin"] = layer.heightMin;
			lj["hMax"] = layer.heightMax;
			lj["sMin"] = layer.slopeMin;
			lj["sMax"] = layer.slopeMax;
			lj["invert"] = layer.invert;
			lj["dispScale"] = layer.displacementScale;
			lj["tex"] = layer.texturePath;
			lj["norm"] = layer.normalMapPath;
			lj["disp"] = layer.displacementMapPath;
			layers.push_back(lj);
		}
		j["layers"] = layers;
	}

	// Planet params
	Planet* planet = dynamic_cast<Planet*>(obj);
	if (planet) {
		PlanetParams p = planet->GetParams();
		j["planet_sub"] = p.subdivisions;
		j["planet_seed"] = (int)p.seed;
		j["planet_radius"] = p.radius;
	}

	return j.dump();
}

void SceneSerializer::RestoreObject(GameObject* obj, const std::string& jsonStr, SceneManager* scene)
{
	if (!obj || jsonStr.empty()) return;
	json j = json::parse(jsonStr);

	// Name
	if (j.contains("name")) obj->SetName(j["name"].get<std::string>());

	// Model assignment
	if (j.contains("has_model") && !j["has_model"].get<bool>()) {
		obj->SetModel(nullptr);
	} else if (j.contains("model_path")) {
		std::string path = j["model_path"].get<std::string>();
		if (!path.empty() && (!obj->GetModel() || obj->GetModel()->GetPath() != path)) {
			obj->SetModel(ServiceLocator::GetAssetManager()->GetModel(path));
		}
	}

	// Texture assignments
	if (j.contains("has_texture") && !j["has_texture"].get<bool>()) {
		obj->SetTexture(nullptr);
	} else if (j.contains("texture_path")) {
		std::string path = j["texture_path"].get<std::string>();
		if (!path.empty() && (!obj->GetTexture() || std::string(obj->GetTexture()->GetFileLocation()) != path)) {
			Texture* tex = new Texture(path.c_str());
			if (tex->LoadTextureA()) obj->SetTexture(tex); else delete tex;
		}
	}

	if (j.contains("has_normal") && !j["has_normal"].get<bool>()) {
		obj->SetNormalMap(nullptr);
	} else if (j.contains("normal_path")) {
		std::string path = j["normal_path"].get<std::string>();
		if (!path.empty() && (!obj->GetNormalMap() || std::string(obj->GetNormalMap()->GetFileLocation()) != path)) {
			Texture* tex = new Texture(path.c_str());
			if (tex->LoadTextureA()) obj->SetNormalMap(tex); else delete tex;
		}
	}

	// Material assignment
	if (j.contains("has_material") && !j["has_material"].get<bool>()) {
		obj->SetMaterial(nullptr);
	} else {
		if (j.contains("material_path")) {
			std::string path = j["material_path"].get<std::string>();
			if (!path.empty() && (!obj->GetMaterial() || obj->GetMaterial()->GetPath() != path)) {
				obj->SetMaterial(Material::LoadFromFile(path));
			}
		} else if (j.contains("material_shader_v") && j.contains("material_shader_f")) {
			std::string v = j["material_shader_v"];
			std::string f = j["material_shader_f"];
			Material* currentMat = obj->GetMaterial();
			
			// Only replace the material if the shader paths are actually different
			if (!currentMat || !currentMat->GetShader() || 
				currentMat->GetShader()->GetVertexPath() != v || 
				currentMat->GetShader()->GetFragmentPath() != f) 
			{
				Material* newMat = new Material();
				Shader* s = new Shader();
				s->CreateFromFiles(v.c_str(), f.c_str());
				newMat->SetShader(s);
				obj->SetMaterial(newMat);
			}
		}
	}

	// Transform
	if (j.contains("pos")) {
		auto& p = j["pos"];
		obj->GetTransform().SetPosition(glm::vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>()));
	}
	if (j.contains("rot")) {
		auto& r = j["rot"];
		obj->GetTransform().SetRotation(glm::vec3(r[0].get<float>(), r[1].get<float>(), r[2].get<float>()));
	}
	if (j.contains("scl")) {
		auto& s = j["scl"];
		obj->GetTransform().SetScale(glm::vec3(s[0].get<float>(), s[1].get<float>(), s[2].get<float>()));
	}
	obj->SetDirty();

	// Tessellation
	if (j.contains("tess_enabled")) obj->SetUseTessellation(j["tess_enabled"].get<bool>());
	if (j.contains("tess_level")) obj->SetTessLevel(j["tess_level"].get<float>());
	if (j.contains("tess_dist")) obj->SetTessDistance(j["tess_dist"].get<float>());
	if (j.contains("tess_disp_scale")) obj->SetTessDisplacementScale(j["tess_disp_scale"].get<float>());
	if (j.contains("tess_disp_bias")) obj->SetTessDisplacementBias(j["tess_disp_bias"].get<float>());

	// Material values
	Material* mat = obj->GetMaterial();
	if (mat && j.contains("mat")) {
		auto& mj = j["mat"];
		if (mj.contains("f")) for (auto it = mj["f"].begin(); it != mj["f"].end(); ++it) mat->SetFloat(it.key(), it.value().get<float>());
		if (mj.contains("i")) for (auto it = mj["i"].begin(); it != mj["i"].end(); ++it) mat->SetInt(it.key(), it.value().get<int>());
		if (mj.contains("v2")) for (auto it = mj["v2"].begin(); it != mj["v2"].end(); ++it) mat->SetVec2(it.key(), glm::vec2(it.value()[0], it.value()[1]));
		if (mj.contains("v3")) for (auto it = mj["v3"].begin(); it != mj["v3"].end(); ++it) mat->SetVec3(it.key(), glm::vec3(it.value()[0], it.value()[1], it.value()[2]));
		if (mj.contains("v4")) for (auto it = mj["v4"].begin(); it != mj["v4"].end(); ++it) mat->SetVec4(it.key(), glm::vec4(it.value()[0], it.value()[1], it.value()[2], it.value()[3]));
	}

	// Texture layer values
	if (j.contains("layers")) {
		auto& layers = obj->GetTextureLayers();
		auto& jLayers = j["layers"];
		layers.resize(jLayers.size());
		for (int i = 0; i < (int)jLayers.size(); i++) {
			auto& lj = jLayers[i];
			if (lj.contains("blend")) layers[i].blendMode = (LayerBlendMode)lj["blend"].get<int>();
			if (lj.contains("opacity")) layers[i].opacity = lj["opacity"].get<float>();
			if (lj.contains("tiling")) layers[i].tiling = lj["tiling"].get<float>();
			if (lj.contains("hMin")) layers[i].heightMin = lj["hMin"].get<float>();
			if (lj.contains("hMax")) layers[i].heightMax = lj["hMax"].get<float>();
			if (lj.contains("sMin")) layers[i].slopeMin = lj["sMin"].get<float>();
			if (lj.contains("sMax")) layers[i].slopeMax = lj["sMax"].get<float>();
			if (lj.contains("invert")) layers[i].invert = lj["invert"].get<bool>();
			if (lj.contains("dispScale")) layers[i].displacementScale = lj["dispScale"].get<float>();
			
			if (lj.contains("tex")) {
				std::string tPath = lj["tex"].get<std::string>();
				if (!tPath.empty() && layers[i].texturePath != tPath) {
					Texture* tex = new Texture(tPath.c_str());
					if (tex->LoadTextureA()) { layers[i].texture = tex; layers[i].texturePath = tPath; } else delete tex;
				}
			}
			if (lj.contains("norm")) {
				std::string nPath = lj["norm"].get<std::string>();
				if (!nPath.empty() && layers[i].normalMapPath != nPath) {
					Texture* tex = new Texture(nPath.c_str());
					if (tex->LoadTextureA()) { layers[i].normalMap = tex; layers[i].normalMapPath = nPath; } else delete tex;
				}
				else if (nPath.empty()) { layers[i].normalMap = nullptr; layers[i].normalMapPath = ""; }
			}
			if (lj.contains("disp")) {
				std::string dPath = lj["disp"].get<std::string>();
				if (!dPath.empty() && layers[i].displacementMapPath != dPath) {
					Texture* tex = new Texture(dPath.c_str());
					if (tex->LoadTextureGrayscale()) { layers[i].displacementMap = tex; layers[i].displacementMapPath = dPath; } else delete tex;
				}
				else if (dPath.empty()) { layers[i].displacementMap = nullptr; layers[i].displacementMapPath = ""; }
			}
		}
	}

	// Planet
	Planet* planet = dynamic_cast<Planet*>(obj);
	if (planet && j.contains("planet_sub")) {
		PlanetParams p = planet->GetParams();
		p.subdivisions = j["planet_sub"].get<int>();
		p.seed = (unsigned int)j["planet_seed"].get<int>();
		p.radius = j["planet_radius"].get<float>();
		planet->SetParams(p);
		planet->Generate();
		planet->UpdateUniforms();
	}
}

std::string SceneSerializer::SnapshotLight(LightObject* light)
{
	json j;
	if (!light) return j.dump();

	j["name"] = light->GetName();
	j["type"] = (int)light->GetLightType();

	if (light->GetColorPtr()) {
		glm::vec3& c = *light->GetColorPtr();
		j["color"] = { c.x, c.y, c.z };
	}
	if (light->GetAmbientIntensityPtr()) j["ambient"] = *light->GetAmbientIntensityPtr();
	if (light->GetDiffuseIntensityPtr()) j["diffuse"] = *light->GetDiffuseIntensityPtr();
	if (light->GetPositionPtr()) {
		glm::vec3& p = *light->GetPositionPtr();
		j["pos"] = { p.x, p.y, p.z };
	}
	if (light->GetDirectionPtr()) {
		glm::vec3& d = *light->GetDirectionPtr();
		j["dir"] = { d.x, d.y, d.z };
	}
	if (light->GetPitchPtr()) j["pitch"] = *light->GetPitchPtr();
	if (light->GetYawPtr()) j["yaw"] = *light->GetYawPtr();
	if (light->GetConstantPtr()) j["constant"] = *light->GetConstantPtr();
	if (light->GetLinearPtr()) j["linear"] = *light->GetLinearPtr();
	if (light->GetExponentPtr()) j["exponent"] = *light->GetExponentPtr();
	if (light->GetSpotEdgePtr()) j["spotEdge"] = *light->GetSpotEdgePtr();

	return j.dump();
}

void SceneSerializer::RestoreLight(LightObject* light, const std::string& jsonStr)
{
	if (!light || jsonStr.empty()) return;
	json j = json::parse(jsonStr);

	if (j.contains("name")) light->SetName(j["name"].get<std::string>());

	if (j.contains("color") && light->GetColorPtr()) {
		auto& c = j["color"];
		*light->GetColorPtr() = glm::vec3(c[0].get<float>(), c[1].get<float>(), c[2].get<float>());
	}
	if (j.contains("ambient") && light->GetAmbientIntensityPtr()) *light->GetAmbientIntensityPtr() = j["ambient"].get<float>();
	if (j.contains("diffuse") && light->GetDiffuseIntensityPtr()) *light->GetDiffuseIntensityPtr() = j["diffuse"].get<float>();
	if (j.contains("pos") && light->GetPositionPtr()) {
		auto& p = j["pos"];
		*light->GetPositionPtr() = glm::vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
	}
	if (j.contains("dir") && light->GetDirectionPtr()) {
		auto& d = j["dir"];
		*light->GetDirectionPtr() = glm::vec3(d[0].get<float>(), d[1].get<float>(), d[2].get<float>());
	}
	if (j.contains("pitch") && light->GetPitchPtr()) *light->GetPitchPtr() = j["pitch"].get<float>();
	if (j.contains("yaw") && light->GetYawPtr()) *light->GetYawPtr() = j["yaw"].get<float>();
	if (j.contains("constant") && light->GetConstantPtr()) *light->GetConstantPtr() = j["constant"].get<float>();
	if (j.contains("linear") && light->GetLinearPtr()) *light->GetLinearPtr() = j["linear"].get<float>();
	if (j.contains("exponent") && light->GetExponentPtr()) *light->GetExponentPtr() = j["exponent"].get<float>();
	if (j.contains("spotEdge") && light->GetSpotEdgePtr()) *light->GetSpotEdgePtr() = j["spotEdge"].get<float>();

	// For directional lights, update direction vector from restored pitch/yaw
	if (light->GetLightType() == LightType::Directional && light->GetDirectionalLight()) {
		light->GetDirectionalLight()->UpdateDirectionFromEuler();
	}
}

