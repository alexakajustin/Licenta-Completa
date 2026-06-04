#include "Nodes/SceneInputNode.h"
#include "Rendering/PrimitiveGenerator.h"
#include "Core/AssetManager.h"
#include "Core/ServiceLocator.h"
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
	std::vector<GameObject*> allObjects;
	scene->GetAllObjects(allObjects);

	if (ImGui::BeginCombo("Object", selectedName.c_str()))
	{
		for (int i = 0; i < (int)allObjects.size(); i++)
		{
			bool isSelected = (selectedName == allObjects[i]->GetName());
			ImGui::PushID(i);
			if (ImGui::Selectable(allObjects[i]->GetName().c_str(), isSelected))
			{
				selectedIndex = i;
				selectedName = allObjects[i]->GetName();
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

	GameObject* obj = scene.FindObject(selectedName);
	MeshData data;
	bool found = false;

	if (obj)
	{
		outputs[0].data.sourceObject = obj;
		outputs[0].data.sourceObjectName = selectedName;

		// We will recursively collect all mesh data from obj and its descendants.
		// To bake local transforms relative to the root object, we transform descendant vertices
		// from descendant space to root space. To do this, we transform by the descendant's world matrix
		// and then by the inverse of the root's world position translation matrix (to keep the vertices
		// relative to the root's position, while baking root's rotation/scale).
		glm::vec3 rootWorldPos = glm::vec3(obj->GetWorldMatrix()[3]);
		glm::mat4 rootWorldPosInv = glm::translate(glm::mat4(1.0f), -rootWorldPos);

		bool collectedAny = false;

		std::function<void(GameObject*)> collectMesh = [&](GameObject* current) {
			if (!current) return;

			MeshData currentData;
			bool currentFound = false;

			// 1. ALWAYS prefer the displaced/custom mesh if the object has one
			if (current->HasCustomMesh() && !forceOriginalPrimitive)
			{
				currentData = current->GetCPUMeshData();
				currentFound = true;
			}
			// 2. Otherwise generate the primitive base
			else if (current->GetPrimitiveType() == "Plane") { currentData = PrimitiveGenerator::GetPlaneData(512, 512); currentFound = true; }
			else if (current->GetPrimitiveType() == "Sphere") { currentData = PrimitiveGenerator::GetSphereData(); currentFound = true; }
			else if (current->GetPrimitiveType() == "Cube") { currentData = PrimitiveGenerator::GetCubeData(); currentFound = true; }
			// 3. Extract from Model if available (clean asset geometry)
			else if (current->GetModel() && !current->GetModel()->GetMeshDataList().empty())
			{
				const auto& meshes = current->GetModel()->GetMeshDataList();
				for (const auto& m : meshes)
				{
					int baseIdx = (int)currentData.vertices.size() / 14;
					currentData.vertices.insert(currentData.vertices.end(), m.vertices.begin(), m.vertices.end());
					for (unsigned int idx : m.indices)
					{
						currentData.indices.push_back(idx + baseIdx);
					}
				}
				if (!currentData.vertices.empty()) currentFound = true;
			}
			// 4. Fallback: If it has a model source path, try to load it from the AssetManager
			else if (!current->GetModelSourcePath().empty())
			{
				Model* m = ServiceLocator::GetAssetManager()->GetModel(current->GetModelSourcePath());
				if (m)
				{
					const auto& meshes = m->GetMeshDataList();
					for (const auto& meshData : meshes)
					{
						int baseIdx = (int)currentData.vertices.size() / 14;
						currentData.vertices.insert(currentData.vertices.end(), meshData.vertices.begin(), meshData.vertices.end());
						for (unsigned int idx : meshData.indices)
						{
							currentData.indices.push_back(idx + baseIdx);
						}
					}
					if (!currentData.vertices.empty()) currentFound = true;
				}
			}

			// 5. Final fallback: if no primitive/model was resolved, use the custom mesh if available
			if (!currentFound && current->HasCustomMesh())
			{
				currentData = current->GetCPUMeshData();
				currentFound = true;
			}

			if (currentFound && !currentData.vertices.empty())
			{
				// T = rootWorldPosInv * current->GetWorldMatrix()
				glm::mat4 localToRoot = rootWorldPosInv * current->GetWorldMatrix();
				glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(localToRoot)));

				int startIdx = (int)data.vertices.size() / 14;

				// Transform vertices
				for (size_t v = 0; v < currentData.vertices.size(); v += 14)
				{
					// Position
					glm::vec4 pos(currentData.vertices[v + 0], currentData.vertices[v + 1], currentData.vertices[v + 2], 1.0f);
					pos = localToRoot * pos;

					// Normal
					glm::vec3 norm(currentData.vertices[v + 5], currentData.vertices[v + 6], currentData.vertices[v + 7]);
					if (glm::length(norm) > 0.001f) {
						norm = glm::normalize(normalMatrix * norm);
					}

					// Tangent
					glm::vec3 tang(currentData.vertices[v + 8], currentData.vertices[v + 9], currentData.vertices[v + 10]);
					if (glm::length(tang) > 0.001f) {
						tang = glm::normalize(normalMatrix * tang);
					}

					// Bitangent
					glm::vec3 bitang(currentData.vertices[v + 11], currentData.vertices[v + 12], currentData.vertices[v + 13]);
					if (glm::length(bitang) > 0.001f) {
						bitang = glm::normalize(normalMatrix * bitang);
					}

					data.AddVertex(pos.x, pos.y, pos.z,
								   currentData.vertices[v + 3], currentData.vertices[v + 4], // UV
								   norm.x, norm.y, norm.z,
								   tang.x, tang.y, tang.z,
								   bitang.x, bitang.y, bitang.z);
				}

				// Append indices
				for (unsigned int idx : currentData.indices)
				{
					data.indices.push_back(idx + startIdx);
				}

				collectedAny = true;
			}

			// Recurse children
			for (auto* child : current->GetChildren())
			{
				collectMesh(child);
			}
		};

		collectMesh(obj);

		if (collectedAny) found = true;

		// Even if no mesh was found (empty container), we still "found" the object itself 
		// for hierarchy and transform propagation (essential for modular trees).
		if (obj) found = true;

		// Update cache if successfully found!
		if (found)
		{
			cachedPrimitiveType = obj->GetPrimitiveType();
			cachedModelPath = obj->GetModelSourcePath();
			cachedPosition = glm::vec3(obj->GetWorldMatrix()[3]);
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
			Model* m = ServiceLocator::GetAssetManager()->GetModel(cachedModelPath);
			// We MUST wait for the model to finish loading before trying to extract its meshes
			ServiceLocator::GetAssetManager()->WaitForAll();
			
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
		// Only transform using cachedScale / cachedRotation if the object was deleted (obj is null)
		// because the recursive collectMesh call above already bakes the relative transforms
		// of the root and child GameObjects.
		if (!obj) {
			glm::vec3 scale = cachedScale;
			glm::vec3 rotation = cachedRotation;

			// Transform only POSITIONS using rotation and scale of the parent/source object.
			// Normals, tangents, and bitangents are left in local space — the vertex shader
			// already applies transpose(inverse(model)) for normals, and pipeline nodes
			// (PerlinNoise, Erosion, River) recompute normals from displaced positions.
			if (rotation != glm::vec3(0.0f) || scale != glm::vec3(1.0f))
			{
				glm::mat4 rotMatrix(1.0f);
				rotMatrix = glm::rotate(rotMatrix, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
				rotMatrix = glm::rotate(rotMatrix, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
				rotMatrix = glm::rotate(rotMatrix, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

				glm::mat4 transformMatrix = glm::scale(rotMatrix, scale);

				for (size_t v = 0; v < data.vertices.size(); v += 14)
				{
					// Position (0, 1, 2)
					glm::vec4 pos(data.vertices[v + 0], data.vertices[v + 1], data.vertices[v + 2], 1.0f);
					pos = transformMatrix * pos;
					data.vertices[v + 0] = pos.x;
					data.vertices[v + 1] = pos.y;
					data.vertices[v + 2] = pos.z;
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
		// We propagate the original scale and rotation here. The OutputNode will
		// perform inverse transformation (un-bake) before writing/uploading, so
		// we can keep the GameObject's actual transform scale/rotation in the scene
		// without experiencing the double-scaling bug.
		TransformData t;
		t.position = obj ? glm::vec3(obj->GetWorldMatrix()[3]) : cachedPosition;
		t.rotation = obj ? obj->GetTransform().GetRotation() : cachedRotation;
		t.scale = obj ? obj->GetTransform().GetScale() : cachedScale;
		outputs[0].data.transforms.push_back(t);
	}
}
