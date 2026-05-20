#include "SceneInputNode.h"
#include "PrimitiveGenerator.h"
#include "AssetManager.h"
#include "imgui.h"

SceneInputNode::SceneInputNode(NodeGraph& graph)
{
	id = graph.NextNodeId();
	title = "Scene Input";

	// No inputs
	// One output: Mesh
	Pin meshOut(graph.NextPinId(), PinDataType::Mesh, "Mesh");
	outputs.push_back(meshOut);
}

SceneInputNode::~SceneInputNode()
{
	CleanupFallbacks();
}

void SceneInputNode::CleanupFallbacks()
{
	if (fallbackTexture) { delete fallbackTexture; fallbackTexture = nullptr; }
	if (fallbackNormalMap) { delete fallbackNormalMap; fallbackNormalMap = nullptr; }
	if (fallbackMaterial) { delete fallbackMaterial; fallbackMaterial = nullptr; }

	for (auto* t : fallbackLayersTextures) if (t) delete t;
	fallbackLayersTextures.clear();

	for (auto* n : fallbackLayersNormals) if (n) delete n;
	fallbackLayersNormals.clear();
}

void SceneInputNode::RenderContent(SceneManager* scene)
{
	if (!scene) return;
	auto& objects = scene->GetObjects();

	if (ImGui::BeginCombo("Object", selectedName.c_str()))
	{
		for (int i = 0; i < (int)objects.size(); i++)
		{
			bool isSelected = (selectedIndex == i);
			ImGui::PushID(i);
			if (ImGui::Selectable(objects[i]->GetName().c_str(), isSelected))
			{
				selectedIndex = i;
				selectedName = objects[i]->GetName();
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}

	ImGui::Checkbox("Force Fresh Primitive", &forceOriginalPrimitive);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("If enabled, always generates fresh mesh data for Planes/Cubes/Spheres,\nignoring previous graph modifications to prevent feedback loops.");
}

json SceneInputNode::Serialize() const
{
	json j = GraphNode::Serialize();
	j["selectedName"] = selectedName;
	j["forceOriginalPrimitive"] = forceOriginalPrimitive;
	
	// Cache properties
	j["cachedPrimitiveType"] = cachedPrimitiveType;
	j["cachedModelPath"] = cachedModelPath;
	j["cachedPosition"] = { cachedPosition.x, cachedPosition.y, cachedPosition.z };
	j["cachedRotation"] = { cachedRotation.x, cachedRotation.y, cachedRotation.z };
	j["cachedScale"] = { cachedScale.x, cachedScale.y, cachedScale.z };

	j["cachedTexturePath"] = cachedTexturePath;
	j["cachedNormalMapPath"] = cachedNormalMapPath;
	
	// Cache Texture Layers
	json layersArray = json::array();
	for (const auto& layer : cachedTextureLayers)
	{
		json lj;
		lj["texturePath"] = layer.texturePath;
		lj["normalMapPath"] = layer.normalMapPath;
		lj["blendMode"] = (int)layer.blendMode;
		lj["opacity"] = layer.opacity;
		lj["tiling"] = layer.tiling;
		lj["heightMin"] = layer.heightMin;
		lj["heightMax"] = layer.heightMax;
		lj["slopeMin"] = layer.slopeMin;
		lj["slopeMax"] = layer.slopeMax;
		lj["invert"] = layer.invert;
		layersArray.push_back(lj);
	}
	j["cachedTextureLayers"] = layersArray;

	j["hasCachedMaterial"] = hasCachedMaterial;
	if (hasCachedMaterial)
	{
		j["cachedMatSpecular"] = cachedMatSpecular;
		j["cachedMatShininess"] = cachedMatShininess;
		j["cachedMatColor"] = { cachedMatColor.x, cachedMatColor.y, cachedMatColor.z };
		j["cachedMatTiling"] = { cachedMatTiling.x, cachedMatTiling.y };
		j["cachedMatOffset"] = { cachedMatOffset.x, cachedMatOffset.y };
	}

	return j;
}

void SceneInputNode::Deserialize(const json& j)
{
	GraphNode::Deserialize(j);
	selectedName = j.value("selectedName", "(none)");
	selectedIndex = -1; // Force re-resolution on first Execute
	forceOriginalPrimitive = j.value("forceOriginalPrimitive", true);

	cachedPrimitiveType = j.value("cachedPrimitiveType", "");
	cachedModelPath = j.value("cachedModelPath", "");
	
	if (j.contains("cachedPosition"))
	{
		auto& p = j["cachedPosition"];
		cachedPosition = glm::vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
	}
	if (j.contains("cachedRotation"))
	{
		auto& r = j["cachedRotation"];
		cachedRotation = glm::vec3(r[0].get<float>(), r[1].get<float>(), r[2].get<float>());
	}
	if (j.contains("cachedScale"))
	{
		auto& s = j["cachedScale"];
		cachedScale = glm::vec3(s[0].get<float>(), s[1].get<float>(), s[2].get<float>());
	}

	cachedTexturePath = j.value("cachedTexturePath", "");
	cachedNormalMapPath = j.value("cachedNormalMapPath", "");

	cachedTextureLayers.clear();
	if (j.contains("cachedTextureLayers"))
	{
		for (const auto& lj : j["cachedTextureLayers"])
		{
			TextureLayer layer;
			layer.texturePath = lj.value("texturePath", "");
			layer.normalMapPath = lj.value("normalMapPath", "");
			layer.blendMode = (LayerBlendMode)lj.value("blendMode", 0);
			layer.opacity = lj.value("opacity", 1.0f);
			layer.tiling = lj.value("tiling", 1.0f);
			layer.heightMin = lj.value("heightMin", 0.0f);
			layer.heightMax = lj.value("heightMax", 100.0f);
			layer.slopeMin = lj.value("slopeMin", 0.0f);
			layer.slopeMax = lj.value("slopeMax", 0.5f);
			layer.invert = lj.value("invert", false);
			cachedTextureLayers.push_back(layer);
		}
	}

	hasCachedMaterial = j.value("hasCachedMaterial", false);
	if (hasCachedMaterial)
	{
		cachedMatSpecular = j.value("cachedMatSpecular", 0.5f);
		cachedMatShininess = j.value("cachedMatShininess", 32.0f);
		if (j.contains("cachedMatColor")) {
			auto& c = j["cachedMatColor"];
			cachedMatColor = glm::vec3(c[0].get<float>(), c[1].get<float>(), c[2].get<float>());
		}
		if (j.contains("cachedMatTiling")) {
			auto& t = j["cachedMatTiling"];
			cachedMatTiling = glm::vec2(t[0].get<float>(), t[1].get<float>());
		}
		if (j.contains("cachedMatOffset")) {
			auto& o = j["cachedMatOffset"];
			cachedMatOffset = glm::vec2(o[0].get<float>(), o[1].get<float>());
		}
	}
}

void SceneInputNode::Execute(SceneManager& scene, NodeProgressCallback progress)
{
	outputs[0].data.Clear();
	outputs[0].data.type = PinDataType::Mesh;

	if (selectedName == "(none)") return;

	auto& objects = scene.GetObjects();
	GameObject* obj = nullptr;

	// Resolve index by name if needed (essential for load from file)
	if (selectedIndex < 0 || selectedIndex >= (int)objects.size() || objects[selectedIndex]->GetName() != selectedName)
	{
		selectedIndex = -1;
		for (int i = 0; i < (int)objects.size(); i++)
		{
			if (objects[i]->GetName() == selectedName)
			{
				selectedIndex = i;
				break;
			}
		}
	}

	MeshData data;
	bool found = false;

	if (selectedIndex >= 0 && selectedIndex < (int)objects.size())
	{
		obj = objects[selectedIndex];
		outputs[0].data.sourceObject = obj;
		outputs[0].data.sourceObjectName = selectedName;
		
		// 1. ALWAYS prefer the displaced/custom mesh if the object has one
		if (obj->HasCustomMesh() && !forceOriginalPrimitive)
		{
			data = obj->GetCPUMeshData();
			found = true;
		}
		// 2. Otherwise generate the primitive base
		else if (obj->GetPrimitiveType() == "Plane") { data = PrimitiveGenerator::GetPlaneData(512, 512); found = true; }
		else if (obj->GetPrimitiveType() == "Sphere") { data = PrimitiveGenerator::GetSphereData(); found = true; }
		else if (obj->GetPrimitiveType() == "Cube") { data = PrimitiveGenerator::GetCubeData(); found = true; }
		// 3. Extract from Model if available (clean asset geometry)
		else if (obj->GetModel() && !obj->GetModel()->GetMeshDataList().empty())
		{
			const auto& meshes = obj->GetModel()->GetMeshDataList();
			for (const auto& m : meshes)
			{
				int baseIdx = (int)data.vertices.size() / 14;
				data.vertices.insert(data.vertices.end(), m.vertices.begin(), m.vertices.end());
				for (unsigned int idx : m.indices)
				{
					data.indices.push_back(idx + baseIdx);
				}
			}
			if (!data.vertices.empty()) found = true;
		}

		// Even if no mesh was found (empty container), we still "found" the object itself 
		// for hierarchy and transform propagation (essential for modular trees).
		if (obj) found = true;

		// Update cache if successfully found!
		if (found)
		{
			cachedPrimitiveType = obj->GetPrimitiveType();
			cachedModelPath = obj->GetModelSourcePath();
			cachedPosition = obj->GetTransform().GetPosition();
			cachedRotation = obj->GetTransform().GetRotation();
			cachedScale = obj->GetTransform().GetScale();

			Texture* tex = obj->GetTexture();
			cachedTexturePath = (tex && tex->GetFileLocation()) ? tex->GetFileLocation() : "";
			
			Texture* norm = obj->GetNormalMap();
			cachedNormalMapPath = (norm && norm->GetFileLocation()) ? norm->GetFileLocation() : "";

			// Cache Layers
			cachedTextureLayers = obj->GetTextureLayers();

			Material* mat = obj->GetMaterial();
			if (mat) {
				hasCachedMaterial = true;
				cachedMatSpecular = mat->GetSpecularIntensity();
				cachedMatShininess = mat->GetShininess();
				cachedMatColor = mat->GetColorRGB();
				cachedMatTiling = mat->GetTiling();
				cachedMatOffset = mat->GetOffset();
			} else {
				hasCachedMaterial = false;
			}
		}
	}

	// FALLBACK MECHANISM: If the user deleted the object from the scene graph.
	if (!found && selectedName != "(none)")
	{
		// Try resolving using cached info
		if (cachedPrimitiveType == "Plane" || selectedName.find("Plane") != std::string::npos) { data = PrimitiveGenerator::GetPlaneData(512, 512); found = true; }
		else if (cachedPrimitiveType == "Sphere" || selectedName.find("Sphere") != std::string::npos) { data = PrimitiveGenerator::GetSphereData(); found = true; }
		else if (cachedPrimitiveType == "Cube" || selectedName.find("Cube") != std::string::npos) { data = PrimitiveGenerator::GetCubeData(); found = true; }
		// Fallback to loaded asset
		else if (!cachedModelPath.empty())
		{
			// The AssetManager might already have it loaded, or missing, but we still try
			Model* m = AssetManager::Get().GetModel(cachedModelPath);
			// We MUST wait for the model to finish loading before trying to extract its meshes
			AssetManager::Get().WaitForAll();
			
			const auto& meshes = m->GetMeshDataList();
			for (const auto& mesh : meshes)
			{
				int baseIdx = (int)data.vertices.size() / 14;
				data.vertices.insert(data.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
				for (unsigned int idx : mesh.indices) data.indices.push_back(idx + baseIdx);
			}
			if (!data.vertices.empty()) found = true;
		}
	}

	if (found) {
		glm::vec3 scale = obj ? obj->GetTransform().GetScale() : cachedScale;
		glm::vec3 rotation = obj ? obj->GetTransform().GetRotation() : cachedRotation;

		// Transform the mesh data vertices using rotation and scale of the parent/source object
		if (rotation != glm::vec3(0.0f) || scale != glm::vec3(1.0f))
		{
			glm::mat4 rotMatrix(1.0f);
			rotMatrix = glm::rotate(rotMatrix, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
			rotMatrix = glm::rotate(rotMatrix, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
			rotMatrix = glm::rotate(rotMatrix, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

			glm::mat4 transformMatrix = glm::scale(rotMatrix, scale);
			glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transformMatrix)));
			glm::mat3 dirMatrix = glm::mat3(transformMatrix);

			for (size_t v = 0; v < data.vertices.size(); v += 14)
			{
				// Position (0, 1, 2)
				glm::vec4 pos(data.vertices[v + 0], data.vertices[v + 1], data.vertices[v + 2], 1.0f);
				pos = transformMatrix * pos;
				data.vertices[v + 0] = pos.x;
				data.vertices[v + 1] = pos.y;
				data.vertices[v + 2] = pos.z;

				// Normal (3, 4, 5)
				glm::vec3 norm(data.vertices[v + 3], data.vertices[v + 4], data.vertices[v + 5]);
				if (glm::length(norm) > 0.0001f) {
					norm = glm::normalize(normalMatrix * norm);
					data.vertices[v + 3] = norm.x;
					data.vertices[v + 4] = norm.y;
					data.vertices[v + 5] = norm.z;
				}

				// Tangent (8, 9, 10)
				glm::vec3 tang(data.vertices[v + 8], data.vertices[v + 9], data.vertices[v + 10]);
				if (glm::length(tang) > 0.0001f) {
					tang = glm::normalize(dirMatrix * tang);
					data.vertices[v + 8] = tang.x;
					data.vertices[v + 9] = tang.y;
					data.vertices[v + 10] = tang.z;
				}

				// Bitangent (11, 12, 13)
				glm::vec3 bitang(data.vertices[v + 11], data.vertices[v + 12], data.vertices[v + 13]);
				if (glm::length(bitang) > 0.0001f) {
					bitang = glm::normalize(dirMatrix * bitang);
					data.vertices[v + 11] = bitang.x;
					data.vertices[v + 12] = bitang.y;
					data.vertices[v + 13] = bitang.z;
				}
			}
		}

		outputs[0].data.meshData = data;
		
		if (obj) {
			outputs[0].data.sourceMaterial = obj->GetMaterial();
			outputs[0].data.sourceTexture = obj->GetTexture();
			outputs[0].data.sourceNormalMap = obj->GetNormalMap();
			outputs[0].data.textureLayers = obj->GetTextureLayers();
		} else {
			// WE SURVIVED VIA FALLBACK: Recreate all visual properties from cache
			if (!fallbackTexture && !cachedTexturePath.empty()) {
				fallbackTexture = new Texture(cachedTexturePath.c_str());
				fallbackTexture->LoadTextureA();
			}
			if (!fallbackNormalMap && !cachedNormalMapPath.empty()) {
				fallbackNormalMap = new Texture(cachedNormalMapPath.c_str());
				fallbackNormalMap->LoadTextureA();
			}
			if (!fallbackMaterial && hasCachedMaterial) {
				fallbackMaterial = new Material(cachedMatSpecular, cachedMatShininess, cachedMatColor);
				fallbackMaterial->SetTiling(cachedMatTiling);
				fallbackMaterial->SetOffset(cachedMatOffset);
			}

			outputs[0].data.sourceMaterial = fallbackMaterial;
			outputs[0].data.sourceTexture = fallbackTexture;
			outputs[0].data.sourceNormalMap = fallbackNormalMap;

			// RECREATE LAYERS FROM CACHE
			std::vector<TextureLayer> restoredLayers = cachedTextureLayers;
			for (auto& layer : restoredLayers)
			{
				layer.texture = nullptr;
				layer.normalMap = nullptr;

				if (!layer.texturePath.empty())
				{
					Texture* tex = new Texture(layer.texturePath.c_str());
					if (tex->LoadTextureA()) {
						fallbackLayersTextures.push_back(tex);
						layer.texture = tex;
					} else {
						delete tex;
					}
				}

				if (!layer.normalMapPath.empty())
				{
					Texture* norm = new Texture(layer.normalMapPath.c_str());
					if (norm->LoadTextureA()) {
						fallbackLayersNormals.push_back(norm);
						layer.normalMap = norm;
					} else {
						delete norm;
					}
				}
			}
			outputs[0].data.textureLayers = restoredLayers;
		}

		// CLEAR FALLBACKS ON SUCCESSFUL NEW RUN (Switching objects, etc.)
		if (obj) CleanupFallbacks();

		// Propagate transform data so downstream nodes can handle scale/restore
		// IMPORTANT: Since we already baked rotation and scale into the vertex data above
		// (lines 304-351), we must propagate identity scale/rotation. Otherwise the
		// OutputNode handler will re-apply scale (e.g. 1000,1,1000) on already-scaled
		// vertices, causing the terrain to appear flat (double-scaling bug).
		TransformData t;
		t.position = obj ? obj->GetTransform().GetPosition() : cachedPosition;
		t.rotation = glm::vec3(0.0f); // Already baked into vertices
		t.scale = glm::vec3(1.0f);    // Already baked into vertices
		outputs[0].data.transforms.push_back(t);
	}
}
