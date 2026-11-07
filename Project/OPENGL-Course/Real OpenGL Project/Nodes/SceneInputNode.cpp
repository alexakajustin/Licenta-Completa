#include "Nodes/SceneInputNode.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "Rendering/PrimitiveGenerator.h"
#include "Rendering/Texture.h"
#include "Rendering/Material.h"
#include "Rendering/Model.h"
#include "Core/AssetManager.h"
#include "Core/ServiceLocator.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <functional>

SceneInputNode::SceneInputNode(NodeGraph& graph) : GraphNode(graph)
{
	AddOutput(PinType::Mesh, "Mesh", "mesh");
	AddOutput(PinType::Objects, "Object", "object");
	paramPins = false;
	AddObjects("selectedName", "Object", &selected, true).Tip("Pick an object, or drag one here from the hierarchy");
	AddBool("forceOriginalPrimitive", "Original geometry", &forceOriginalPrimitive)
		.Tip("Planes, cubes and spheres are read as fresh primitives, not as the mesh a graph last\n"
			"generated into them (no feedback loops when a graph writes back into its own input)");
	AddInt("planeCells", "Plane resolution", &planeCells, 4.0f, 2, 2048)
		.Tip("Cells along each side of a plane read as original geometry: the grid of a terrain built\n"
			"on it (512 on a 2000 m plane: 3.9 m cells). Each doubling makes four times the vertices,\n"
			"and the nodes working on them take about four times as long")
		.ShowIf([this] { return forceOriginalPrimitive && (cachedPrimitiveType == "Plane" || GetSelectedName().find("Plane") != std::string::npos); });
}

SceneInputNode::~SceneInputNode()
{
	CleanupFallbacks();
}

void SceneInputNode::CleanupFallbacks()
{
	delete fallbackTexture; fallbackTexture = nullptr;
	delete fallbackNormalMap; fallbackNormalMap = nullptr;
	delete fallbackMaterial; fallbackMaterial = nullptr;
	for (auto* t : fallbackLayersTextures) delete t;
	fallbackLayersTextures.clear();
	for (auto* n : fallbackLayersNormals) delete n;
	fallbackLayersNormals.clear();
}

void SceneInputNode::SaveState(json& j) const
{
	j["cachedPrimitiveType"] = cachedPrimitiveType;
	j["cachedModelPath"] = cachedModelPath;
	j["cachedPosition"] = { cachedPosition.x, cachedPosition.y, cachedPosition.z };
	j["cachedRotation"] = { cachedRotation.x, cachedRotation.y, cachedRotation.z };
	j["cachedScale"] = { cachedScale.x, cachedScale.y, cachedScale.z };
	j["cachedTexturePath"] = cachedTexturePath;
	j["cachedNormalMapPath"] = cachedNormalMapPath;
	json layers = json::array();
	for (const auto& layer : cachedTextureLayers) {
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
		layers.push_back(lj);
	}
	j["cachedTextureLayers"] = layers;
	j["hasCachedMaterial"] = hasCachedMaterial;
	if (hasCachedMaterial) {
		j["cachedMatSpecular"] = cachedMatSpecular;
		j["cachedMatShininess"] = cachedMatShininess;
		j["cachedMatColor"] = { cachedMatColor.x, cachedMatColor.y, cachedMatColor.z };
		j["cachedMatTiling"] = { cachedMatTiling.x, cachedMatTiling.y };
		j["cachedMatOffset"] = { cachedMatOffset.x, cachedMatOffset.y };
	}
}

void SceneInputNode::LoadState(const json& j)
{
	auto vec3 = [&](const char* key, glm::vec3& out) {
		if (j.contains(key) && j[key].size() >= 3) out = glm::vec3(j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>());
	};
	auto vec2 = [&](const char* key, glm::vec2& out) {
		if (j.contains(key) && j[key].size() >= 2) out = glm::vec2(j[key][0].get<float>(), j[key][1].get<float>());
	};
	cachedPrimitiveType = j.value("cachedPrimitiveType", "");
	cachedModelPath = j.value("cachedModelPath", "");
	vec3("cachedPosition", cachedPosition);
	vec3("cachedRotation", cachedRotation);
	vec3("cachedScale", cachedScale);
	cachedTexturePath = j.value("cachedTexturePath", "");
	cachedNormalMapPath = j.value("cachedNormalMapPath", "");
	cachedTextureLayers.clear();
	if (j.contains("cachedTextureLayers")) {
		for (const auto& lj : j["cachedTextureLayers"]) {
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
	if (hasCachedMaterial) {
		cachedMatSpecular = j.value("cachedMatSpecular", 0.5f);
		cachedMatShininess = j.value("cachedMatShininess", 32.0f);
		vec3("cachedMatColor", cachedMatColor);
		vec2("cachedMatTiling", cachedMatTiling);
		vec2("cachedMatOffset", cachedMatOffset);
	}
}

namespace {
	void AppendMeshes(MeshData& into, const std::vector<MeshData>& meshes)
	{
		for (const auto& m : meshes) {
			unsigned int base = (unsigned int)(into.vertices.size() / 14);
			into.vertices.insert(into.vertices.end(), m.vertices.begin(), m.vertices.end());
			for (unsigned int idx : m.indices) into.indices.push_back(idx + base);
		}
	}
}

MeshValue ReadObjectMesh(GameObject* obj, bool originalPrimitives, int planeCells)
{
	planeCells = std::clamp(planeCells, 2, 2048);
	glm::vec3 rootWorldPos = glm::vec3(obj->GetWorldMatrix()[3]);
	glm::mat4 rootWorldPosInv = glm::translate(glm::mat4(1.0f), -rootWorldPos);
	MeshData data;

	std::function<void(GameObject*)> collect = [&](GameObject* current) {
		if (!current) return;
		MeshData part;
		bool found = false;
		if (current->HasCustomMesh() && !originalPrimitives) { part = current->GetCPUMeshData(); found = true; }
		else if (current->GetPrimitiveType() == "Plane") { part = PrimitiveGenerator::GetPlaneData(planeCells, planeCells); found = true; }
		else if (current->GetPrimitiveType() == "Sphere") { part = PrimitiveGenerator::GetSphereData(); found = true; }
		else if (current->GetPrimitiveType() == "Cube") { part = PrimitiveGenerator::GetCubeData(); found = true; }
		else if (current->GetModel() && !current->GetModel()->GetMeshDataList().empty()) {
			AppendMeshes(part, current->GetModel()->GetMeshDataList());
			found = !part.vertices.empty();
		}
		else if (!current->GetModelSourcePath().empty()) {
			if (Model* m = ServiceLocator::GetAssetManager()->GetModel(current->GetModelSourcePath())) {
				AppendMeshes(part, m->GetMeshDataList());
				found = !part.vertices.empty();
			}
		}
		if (!found && current->HasCustomMesh()) { part = current->GetCPUMeshData(); found = true; }

		if (found && !part.vertices.empty()) {
			glm::mat4 localToRoot = rootWorldPosInv * current->GetWorldMatrix();
			glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(localToRoot)));
			unsigned int start = (unsigned int)(data.vertices.size() / 14);
			data.vertices.reserve(data.vertices.size() + part.vertices.size());
			for (size_t v = 0; v < part.vertices.size(); v += 14) {
				glm::vec4 pos = localToRoot * glm::vec4(part.vertices[v], part.vertices[v + 1], part.vertices[v + 2], 1.0f);
				glm::vec3 n(part.vertices[v + 5], part.vertices[v + 6], part.vertices[v + 7]);
				glm::vec3 t(part.vertices[v + 8], part.vertices[v + 9], part.vertices[v + 10]);
				glm::vec3 b(part.vertices[v + 11], part.vertices[v + 12], part.vertices[v + 13]);
				if (glm::length(n) > 0.001f) n = glm::normalize(normalMatrix * n);
				if (glm::length(t) > 0.001f) t = glm::normalize(normalMatrix * t);
				if (glm::length(b) > 0.001f) b = glm::normalize(normalMatrix * b);
				data.AddVertex(pos.x, pos.y, pos.z, part.vertices[v + 3], part.vertices[v + 4], n.x, n.y, n.z, t.x, t.y, t.z, b.x, b.y, b.z);
			}
			for (unsigned int idx : part.indices) data.indices.push_back(idx + start);
		}
		for (auto* child : current->GetChildren()) collect(child);
	};
	collect(obj);

	MeshValue mesh;
	mesh.Set(std::move(data));
	mesh.origin = rootWorldPos;
	mesh.rotation = obj->GetTransform().GetRotation();
	mesh.scale = obj->GetTransform().GetScale();
	mesh.sourceObject = obj->GetName();
	mesh.look.material = obj->GetMaterial();
	mesh.look.texture = obj->GetTexture();
	mesh.look.normalMap = obj->GetNormalMap();
	mesh.look.layers = obj->GetTextureLayers();
	return mesh;
}

// The object is gone: rebuild its geometry and look from what was cached when it was last read
MeshValue SceneInputNode::RebuildFromCache()
{
	const std::string name = GetSelectedName();
	MeshData data;
	if (cachedPrimitiveType == "Plane" || name.find("Plane") != std::string::npos) {
		int cells = std::clamp(planeCells, 2, 2048);
		data = PrimitiveGenerator::GetPlaneData(cells, cells);
	}
	else if (cachedPrimitiveType == "Sphere" || name.find("Sphere") != std::string::npos) data = PrimitiveGenerator::GetSphereData();
	else if (cachedPrimitiveType == "Cube" || name.find("Cube") != std::string::npos) data = PrimitiveGenerator::GetCubeData();
	else if (!cachedModelPath.empty()) {
		Model* m = ServiceLocator::GetAssetManager()->GetModel(cachedModelPath);
		ServiceLocator::GetAssetManager()->WaitForAll();
		if (m) AppendMeshes(data, m->GetMeshDataList());
	}
	if (data.vertices.empty()) return MeshValue();

	// Positions only: the shaders transform normals, and terrain nodes recompute them
	if (cachedRotation != glm::vec3(0.0f) || cachedScale != glm::vec3(1.0f)) {
		glm::mat4 m(1.0f);
		m = glm::rotate(m, glm::radians(cachedRotation.y), glm::vec3(0, 1, 0));
		m = glm::rotate(m, glm::radians(cachedRotation.x), glm::vec3(1, 0, 0));
		m = glm::rotate(m, glm::radians(cachedRotation.z), glm::vec3(0, 0, 1));
		m = glm::scale(m, cachedScale);
		for (size_t v = 0; v < data.vertices.size(); v += 14) {
			glm::vec4 p = m * glm::vec4(data.vertices[v], data.vertices[v + 1], data.vertices[v + 2], 1.0f);
			data.vertices[v] = p.x; data.vertices[v + 1] = p.y; data.vertices[v + 2] = p.z;
		}
	}

	if (!fallbackTexture && !cachedTexturePath.empty()) { fallbackTexture = new Texture(cachedTexturePath.c_str()); fallbackTexture->LoadTextureA(); }
	if (!fallbackNormalMap && !cachedNormalMapPath.empty()) { fallbackNormalMap = new Texture(cachedNormalMapPath.c_str()); fallbackNormalMap->LoadTextureA(); }
	if (!fallbackMaterial && hasCachedMaterial) {
		fallbackMaterial = new Material(cachedMatSpecular, cachedMatShininess, cachedMatColor);
		fallbackMaterial->SetTiling(cachedMatTiling);
		fallbackMaterial->SetOffset(cachedMatOffset);
	}

	MeshValue mesh;
	mesh.Set(std::move(data));
	mesh.origin = cachedPosition;
	mesh.rotation = cachedRotation;
	mesh.scale = cachedScale;
	mesh.sourceObject = name;
	mesh.look.material = fallbackMaterial;
	mesh.look.texture = fallbackTexture;
	mesh.look.normalMap = fallbackNormalMap;
	std::vector<TextureLayer> layers = cachedTextureLayers;
	for (auto& layer : layers) {
		layer.texture = nullptr;
		layer.normalMap = nullptr;
		if (!layer.texturePath.empty()) {
			Texture* tex = new Texture(layer.texturePath.c_str());
			if (tex->LoadTextureA()) { fallbackLayersTextures.push_back(tex); layer.texture = tex; }
			else delete tex;
		}
		if (!layer.normalMapPath.empty()) {
			Texture* nrm = new Texture(layer.normalMapPath.c_str());
			if (nrm->LoadTextureA()) { fallbackLayersNormals.push_back(nrm); layer.normalMap = nrm; }
			else delete nrm;
		}
	}
	mesh.look.layers = std::move(layers);
	return mesh;
}

void SceneInputNode::Execute(NodeContext& ctx)
{
	if (selected.empty()) return;
	const std::string name = selected[0];
	GameObject* obj = ctx.scene.FindObject(name);

	if (obj) {
		ObjectEntry entry;
		entry.name = name;
		entry.object = obj;
		Out(1, NodeValue::FromObjects({ entry }));

		// Remember enough to rebuild the geometry if the object is deleted later
		cachedPrimitiveType = obj->GetPrimitiveType();
		cachedModelPath = obj->GetModelSourcePath();
		cachedPosition = glm::vec3(obj->GetWorldMatrix()[3]);
		cachedRotation = obj->GetTransform().GetRotation();
		cachedScale = obj->GetTransform().GetScale();
		Texture* tex = obj->GetTexture();
		cachedTexturePath = (tex && tex->GetFileLocation()) ? tex->GetFileLocation() : "";
		Texture* nrm = obj->GetNormalMap();
		cachedNormalMapPath = (nrm && nrm->GetFileLocation()) ? nrm->GetFileLocation() : "";
		cachedTextureLayers = obj->GetTextureLayers();
		if (Material* mat = obj->GetMaterial()) {
			hasCachedMaterial = true;
			cachedMatSpecular = mat->GetSpecularIntensity();
			cachedMatShininess = mat->GetShininess();
			cachedMatColor = mat->GetColorRGB();
			cachedMatTiling = mat->GetTiling();
			cachedMatOffset = mat->GetOffset();
		} else {
			hasCachedMaterial = false;
		}
		CleanupFallbacks();

		if (ctx.OutputUsed(0)) Out(0, NodeValue::FromMesh(ReadObjectMesh(obj, forceOriginalPrimitive, planeCells)));
		return;
	}

	ctx.Warn("Object '" + name + "' is not in the scene" + (ctx.OutputUsed(0) ? ": using the cached geometry" : ""));
	if (ctx.OutputUsed(0)) {
		MeshValue mesh = RebuildFromCache();
		if (!mesh.Empty()) Out(0, NodeValue::FromMesh(std::move(mesh)));
	}
}
