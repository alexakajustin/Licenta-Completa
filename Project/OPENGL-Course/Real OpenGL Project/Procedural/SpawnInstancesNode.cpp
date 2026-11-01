#include "Procedural/SpawnInstancesNode.h"
#include "Nodes/NodeRandom.h"
#include "Nodes/SceneInputNode.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "Rendering/Model.h"
#include "Rendering/Mesh.h"
#include "Rendering/Texture.h"
#include "Rendering/Material.h"
#include "Core/AssetManager.h"
#include "Core/ServiceLocator.h"
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <future>
#include <limits>
#include <thread>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>

SpawnInstancesNode::SpawnInstancesNode(NodeGraph& graph) : GraphNode(graph), seed(NodeRandom::TrueRandomSeed())
{
	AddInput(PinType::Points, "Points", "points", true);
	AddInput(PinType::Objects, "Objects", "objects", true);
	AddBool("alignToNormal", "Align to normal", &alignToNormal).Tip("Tilt each instance to the surface normal of its point");
	AddSeed("seed", "Seed", &seed).Tip("Which object each point gets, when there are several (by their weights)");
}

void SpawnInstancesNode::SaveState(json& j) const
{
	if (legacyScale) j["legacyScale"] = true;
	json volumes = json::array();
	for (const auto& v : deletionVolumes)
		volumes.push_back({ { "x", v.position.x }, { "y", v.position.y }, { "z", v.position.z }, { "radius", v.radius } });
	j["deletionVolumes"] = volumes;
}

void SpawnInstancesNode::LoadState(const json& j)
{
	legacyScale = j.value("legacyScale", false);
	deletionVolumes.clear();
	if (j.contains("deletionVolumes"))
		for (const auto& v : j["deletionVolumes"])
			deletionVolumes.push_back({ glm::vec3(v.value("x", 0.0f), v.value("y", 0.0f), v.value("z", 0.0f)), v.value("radius", 1.0f) });
}

void SpawnInstancesNode::RenderContent(SceneManager* scene)
{
	DrawParams(scene);
	if (!deletionVolumes.empty()) {
		ImGui::TextDisabled("%d erased areas", (int)deletionVolumes.size());
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Instances erased in the viewport stay erased when the graph runs again");
		ImGui::SameLine();
		if (ImGui::SmallButton("Restore")) deletionVolumes.clear();
	}
	if (legacyScale) {
		ImGui::TextDisabled("Legacy scale");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Saved before the node overhaul: single-mesh objects are instanced at their own\n"
				"scale squared (as they always were here). Fix: their scale counts once (they grow).");
		ImGui::SameLine();
		if (ImGui::SmallButton("Fix")) legacyScale = false;
	}
}

Mesh* SpawnInstancesNode::MeshCache::Get(const MeshData& data)
{
	auto it = meshes.find(&data);
	if (it != meshes.end()) return it->second;
	Mesh* mesh = data.ToMesh(0);
	meshes[&data] = mesh;
	return mesh;
}

// Instances erased in the viewport: spheres that remove every point inside them
void SpawnInstancesNode::ApplyDeletionVolumes(TransformList& points) const
{
	if (deletionVolumes.empty() || points.empty()) return;
	float minX = std::numeric_limits<float>::max(), minZ = minX;
	float maxX = std::numeric_limits<float>::lowest(), maxZ = maxX;
	float maxRadius = 0.0f;
	for (const auto& v : deletionVolumes) {
		minX = std::min(minX, v.position.x - v.radius); maxX = std::max(maxX, v.position.x + v.radius);
		minZ = std::min(minZ, v.position.z - v.radius); maxZ = std::max(maxZ, v.position.z + v.radius);
		maxRadius = std::max(maxRadius, v.radius);
	}
	float cell = std::max(maxRadius * 2.0f, 10.0f);
	int gw = std::max(1, (int)std::ceil((maxX - minX) / cell));
	int gh = std::max(1, (int)std::ceil((maxZ - minZ) / cell));
	std::vector<std::vector<int>> grid((size_t)gw * gh);
	for (int i = 0; i < (int)deletionVolumes.size(); i++) {
		const auto& v = deletionVolumes[i];
		int x0 = std::max(0, (int)std::floor((v.position.x - v.radius - minX) / cell)), x1 = std::min(gw - 1, (int)std::floor((v.position.x + v.radius - minX) / cell));
		int z0 = std::max(0, (int)std::floor((v.position.z - v.radius - minZ) / cell)), z1 = std::min(gh - 1, (int)std::floor((v.position.z + v.radius - minZ) / cell));
		for (int z = z0; z <= z1; z++)
			for (int x = x0; x <= x1; x++) grid[(size_t)z * gw + x].push_back(i);
	}
	auto erased = [&](const TransformData& t) {
		if (t.position.x < minX || t.position.x > maxX || t.position.z < minZ || t.position.z > maxZ) return false;
		int cx = std::clamp((int)std::floor((t.position.x - minX) / cell), 0, gw - 1);
		int cz = std::clamp((int)std::floor((t.position.z - minZ) / cell), 0, gh - 1);
		for (int i : grid[(size_t)cz * gw + cx]) {
			const auto& v = deletionVolumes[i];
			if (glm::distance2(t.position, v.position) <= v.radius * v.radius) return true;
		}
		return false;
	};
	points.erase(std::remove_if(points.begin(), points.end(), erased), points.end());
}

void SpawnInstancesNode::Execute(NodeContext& ctx)
{
	const NodeValue& pointsIn = In("points");
	const ObjectList& objectsIn = In("objects").Objects();
	if (pointsIn.Points().empty()) { ctx.Info("no points"); return; }
	if (objectsIn.empty()) { ctx.Warn("No objects to place: link a Scene Object (or Scene Objects) into Objects"); return; }

	// The objects, merged by name, with their weights
	struct Prototype { std::string name; GameObject* object; float weight; };
	std::vector<Prototype> prototypes;
	for (const auto& e : objectsIn) {
		GameObject* obj = e.object ? e.object : ctx.scene.FindObject(e.name);
		if (!obj) { ctx.Warn("Object '" + e.name + "' not found"); continue; }
		auto it = std::find_if(prototypes.begin(), prototypes.end(), [&](const Prototype& p) { return p.name == e.name; });
		if (it != prototypes.end()) it->weight += std::max(e.weight, 0.0f);
		else prototypes.push_back({ e.name, obj, std::max(e.weight, 0.0f) });
	}
	if (prototypes.empty()) return;

	TransformList points = pointsIn.Points();
	ApplyDeletionVolumes(points);

	// Each point picks an object by weight, hashed from its position: the same object every run,
	// whatever else changes upstream
	std::vector<std::vector<size_t>> subsets(prototypes.size());
	if (prototypes.size() == 1) {
		subsets[0].resize(points.size());
		for (size_t i = 0; i < points.size(); i++) subsets[0][i] = i;
	} else {
		float total = 0.0f;
		for (auto& p : prototypes) total += p.weight;
		if (total <= 0.0f) { for (auto& p : prototypes) p.weight = 1.0f; total = (float)prototypes.size(); }
		auto bits = [](float f) { uint32_t u; std::memcpy(&u, &f, sizeof(u)); return (uint64_t)u; };
		for (size_t i = 0; i < points.size(); i++) {
			const glm::vec3& p = points[i].position;
			uint64_t key = bits(p.x) * 0x9E3779B97F4A7C15ull ^ bits(p.y) * 0xC2B2AE3D27D4EB4Full ^ bits(p.z) * 0x165667B19E3779F9ull;
			float u = NodeRandom::Float01((uint32_t)seed, key, 0x53504157u) * total;
			size_t k = 0;
			for (; k + 1 < prototypes.size(); k++) { if (u < prototypes[k].weight) break; u -= prototypes[k].weight; }
			subsets[k].push_back(i);
		}
	}

	MeshCache cache;
	size_t placed = 0;
	int groups = 0;
	for (size_t k = 0; k < prototypes.size(); k++) {
		const Prototype& proto = prototypes[k];
		const auto& subset = subsets[k];
		if (subset.empty()) continue;
		glm::vec3 objScale = proto.object->GetTransform().GetScale();

		// Packed per-instance data, in parallel
		std::vector<InstancedGroup::PackedInstance> packed(subset.size());
		unsigned threadCount = std::max(1u, std::min(std::thread::hardware_concurrency(), (unsigned)(subset.size() / 4096 + 1)));
		size_t perThread = (subset.size() + threadCount - 1) / threadCount;
		std::vector<std::future<void>> jobs;
		for (unsigned t = 0; t < threadCount; t++) {
			size_t start = t * perThread, end = std::min(subset.size(), start + perThread);
			if (start >= end) break;
			jobs.push_back(std::async(std::launch::async, [&, start, end]() {
				for (size_t i = start; i < end; i++) {
					const TransformData& t = points[subset[i]];
					// The object's own scale, once (uniform instances: averaged)
					glm::vec3 s = t.scale * objScale;
					float scale = (s.x + s.y + s.z) / 3.0f;
					glm::vec3 euler = t.rotation;
					if (alignToNormal && glm::length(t.normal) > 0.001f) {
						glm::vec3 up(0.0f, 1.0f, 0.0f);
						if (std::abs(glm::dot(up, t.normal)) < 0.999f) {
							glm::vec3 axis = glm::normalize(glm::cross(up, t.normal));
							float angle = std::acos(glm::clamp(glm::dot(up, t.normal), -1.0f, 1.0f));
							euler.x += glm::degrees(angle * axis.x);
							euler.z += glm::degrees(angle * axis.z);
						}
					}
					packed[i].positionAndScale = glm::vec4(t.position, scale);
					packed[i].rotationAndFlags = glm::vec4(euler, 0.0f);
				}
			}));
		}
		for (auto& j : jobs) j.get();

		ctx.Progress(100.0f * (k + 1) / prototypes.size(), "Instancing " + proto.name + "...");
		groups += BuildGroups(ctx, proto.object, proto.name, packed, cache);
		placed += packed.size();
	}

	char buf[96];
	snprintf(buf, sizeof(buf), "%zu instances, %d groups", placed, groups);
	ctx.Info(buf);
}

namespace {
	// "Trunk_LOD1" -> ("Trunk", 1); names without a LOD suffix are LOD0
	int SplitLodName(const std::string& name, std::string& baseName)
	{
		baseName = name;
		std::string upper = name;
		for (auto& c : upper) c = (char)std::toupper((unsigned char)c);
		size_t lodPos = upper.find("LOD");
		if (lodPos == std::string::npos) return 0;
		std::string suffix = upper.substr(lodPos + 3);
		if (!suffix.empty() && (suffix[0] == '_' || suffix[0] == ' ' || suffix[0] == '-')) suffix = suffix.substr(1);
		if (suffix.empty() || !std::isdigit((unsigned char)suffix[0])) return 0;
		size_t baseEnd = lodPos;
		if (baseEnd > 0 && (name[baseEnd - 1] == '_' || name[baseEnd - 1] == ' ' || name[baseEnd - 1] == '-')) baseEnd--;
		baseName = name.substr(0, baseEnd);
		return suffix[0] - '0';
	}

	// The geometry of an object without a multi-mesh model: its meshes and its children's, as stored
	void CollectRawMeshes(GameObject* obj, MeshData& into)
	{
		if (obj->GetModel() && !obj->GetModel()->GetMeshDataList().empty()) {
			for (const auto& md : obj->GetModel()->GetMeshDataList()) into.Append(md);
		} else if (obj->HasCustomMesh()) {
			into.Append(obj->GetCPUMeshData());
		}
		for (auto* child : obj->GetChildren()) CollectRawMeshes(child, into);
	}
}

// One instanced group per part of the object (with its LOD meshes), or one for a single mesh
int SpawnInstancesNode::BuildGroups(NodeContext& ctx, GameObject* sourceObj, const std::string& objName,
	const std::vector<InstancedGroup::PackedInstance>& instances, MeshCache& cache)
{
	const std::string groupName = GroupName(objName);

	Model* model = nullptr;
	if (sourceObj->GetModel() && sourceObj->GetModel()->GetMeshCount() > 0) model = sourceObj->GetModel();
	if (!model && !sourceObj->GetModelSourcePath().empty()) model = ServiceLocator::GetAssetManager()->GetModel(sourceObj->GetModelSourcePath());
	if (!model)
		for (auto* child : sourceObj->GetChildren())
			if (child->GetModel() && child->GetModel()->GetMeshCount() > 0) { model = child->GetModel(); break; }

	// An empty object in the hierarchy stands for the instances (select it, delete it)
	ctx.Object("Scatter_Group_" + std::to_string(id) + "_" + objName);

	// The object's rotation counts for its instances (its scale is in the instance scale)
	const glm::vec3 rotation = sourceObj->GetTransform().GetRotation();
	glm::mat4 rotationMatrix(1.0f);
	rotationMatrix = glm::rotate(rotationMatrix, glm::radians(rotation.y), glm::vec3(0, 1, 0));
	rotationMatrix = glm::rotate(rotationMatrix, glm::radians(rotation.x), glm::vec3(1, 0, 0));
	rotationMatrix = glm::rotate(rotationMatrix, glm::radians(rotation.z), glm::vec3(0, 0, 1));
	const bool rotated = rotation != glm::vec3(0.0f);
	std::vector<std::unique_ptr<MeshData>> rotatedCopies;
	auto meshFor = [&](const MeshData& data) -> Mesh* {
		if (!rotated) return cache.Get(data);
		rotatedCopies.push_back(std::make_unique<MeshData>(data));
		rotatedCopies.back()->TransformBy(rotationMatrix);
		return rotatedCopies.back()->ToMesh(0);
	};

	if (model && model->GetMeshCount() > 1) {
		// Multi-mesh model (a tree): group its meshes by part, each part with its LOD meshes
		const auto& meshDataList = model->GetMeshDataList();
		const auto& meshNames = model->GetMeshNames();
		const auto& children = sourceObj->GetChildren();
		struct Part { size_t lod[3] = { SIZE_MAX, SIZE_MAX, SIZE_MAX }; };
		std::map<std::string, Part> parts;
		for (size_t m = 0; m < model->GetMeshCount(); m++) {
			std::string name = m < meshNames.size() ? meshNames[m] : ("Mesh_" + std::to_string(m));
			std::string baseName;
			int level = SplitLodName(name, baseName);
			if (level < 0 || level > 2) continue;
			std::string unique = baseName;
			for (int dup = 1; parts.count(unique) && parts[unique].lod[level] != SIZE_MAX; dup++) unique = baseName + "_" + std::to_string(dup);
			parts[unique].lod[level] = m;
		}

		int made = 0;
		for (auto const& [baseName, part] : parts) {
			size_t m = part.lod[0];
			if (m == SIZE_MAX || m >= meshDataList.size() || meshDataList[m].vertices.empty()) continue;

			// The child object of this part (its textures), by name without copy/LOD suffixes
			GameObject* matchingChild = nullptr;
			for (auto* child : children) {
				std::string childName = child->GetName();
				size_t copyPos = childName.find(" (");
				if (copyPos != std::string::npos) childName = childName.substr(0, copyPos);
				size_t lodPos = childName.find("_LOD");
				if (lodPos == std::string::npos) lodPos = childName.find(" LOD");
				if (lodPos == std::string::npos) lodPos = childName.find("-LOD");
				if (lodPos != std::string::npos) childName = childName.substr(0, lodPos);
				if (childName == baseName) { matchingChild = child; break; }
			}
			if (!matchingChild && m < children.size()) matchingChild = children[m];

			Texture* tex = nullptr;
			Texture* nrm = nullptr;
			Material* mat = nullptr;
			std::vector<TextureLayer> layers;
			if (matchingChild) {
				tex = matchingChild->GetTexture();
				nrm = matchingChild->GetNormalMap();
				mat = matchingChild->GetMaterial();
				layers = matchingChild->GetTextureLayers();
			}
			if (!tex) {
				unsigned int matIdx = model->GetMaterialIndex((unsigned int)m);
				tex = model->GetTexture(matIdx);
				nrm = model->GetNormalMap(matIdx);
			}
			if (!mat) mat = sourceObj->GetMaterial();
			if (layers.empty()) layers = sourceObj->GetTextureLayers();

			InstancedGroup* group = new InstancedGroup(groupName + "_" + baseName);
			group->SetSourceObjectName(matchingChild ? matchingChild->GetName() : objName);
			group->Setup(meshFor(meshDataList[m]), instances, mat, tex, nrm, layers);
			for (int lod = 1; lod <= 2; lod++)
				if (part.lod[lod] != SIZE_MAX && part.lod[lod] < meshDataList.size())
					group->SetLODMesh(lod, meshFor(meshDataList[part.lod[lod]]));
			ctx.AddGroup(group);
			made++;
		}
		return made;
	}

	// A single mesh (grass, a rock): one group
	MeshData single;
	if (legacyScale) {
		// As saved scenes always had it: the object's geometry with its rotation and scale baked in
		MeshValue baked = ReadObjectMesh(sourceObj, true);
		single = baked.Data();
	} else {
		CollectRawMeshes(sourceObj, single);
		if (rotated && !single.vertices.empty()) single.TransformBy(rotationMatrix);
	}
	if (single.vertices.empty() && model && !model->GetMeshDataList().empty()) {
		single = model->GetMeshDataList()[0];
		if (rotated && !legacyScale) single.TransformBy(rotationMatrix);
	}
	if (single.vertices.empty()) {
		ctx.Warn("'" + objName + "' has no mesh to instance");
		return 0;
	}

	Material* mat = sourceObj->GetMaterial();
	Texture* tex = sourceObj->GetTexture();
	Texture* nrm = sourceObj->GetNormalMap();
	std::vector<TextureLayer> layers = sourceObj->GetTextureLayers();
	if (!tex && model) {
		tex = model->GetTexture(model->GetMaterialIndex(0));
		nrm = model->GetNormalMap(model->GetMaterialIndex(0));
	}

	InstancedGroup* group = new InstancedGroup(groupName);
	group->SetSourceObjectName(objName);
	group->Setup(single.ToMesh(0), instances, mat, tex, nrm, layers);
	ctx.AddGroup(group);
	return 1;
}
