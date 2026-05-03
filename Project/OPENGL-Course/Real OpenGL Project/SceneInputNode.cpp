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
			if (ImGui::Selectable(objects[i]->GetName().c_str(), isSelected))
			{
				selectedIndex = i;
				selectedName = objects[i]->GetName();
			}
		}
		ImGui::EndCombo();
	}
}

json SceneInputNode::Serialize() const
{
	json j = GraphNode::Serialize();
	j["selectedName"] = selectedName;
	
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
		
		// 1. Always prefer clean base geometry (Primitive or Model) so that the graph 
		//    evaluates deterministically from scratch. This prevents an infinite feedback loop
		//    where modifying nodes (like Perlin Noise) keep adding height to the already 
		//    deformed mesh on every execution!
		if (obj->GetPrimitiveType() == "Plane") { data = PrimitiveGenerator::GetPlaneData(512, 512); found = true; }
		else if (obj->GetPrimitiveType() == "Sphere") { data = PrimitiveGenerator::GetSphereData(); found = true; }
		else if (obj->GetPrimitiveType() == "Cube") { data = PrimitiveGenerator::GetCubeData(); found = true; }
		// 2. Extract from Model if available (clean asset geometry)
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
		// 3. Fallback to existing procedural/custom mesh data ONLY if no clean base exists.
		else if (obj->HasCustomMesh())
		{
			data = obj->GetCPUMeshData();
			found = true;
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
		TransformData t;
		t.position = obj ? obj->GetTransform().GetPosition() : cachedPosition;
		t.rotation = obj ? obj->GetTransform().GetRotation() : cachedRotation;
		t.scale = obj ? obj->GetTransform().GetScale() : cachedScale;
		outputs[0].data.transforms.push_back(t);
	}
}
