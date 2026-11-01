#include "Procedural/InteriorGenNode.h"
#include "Nodes/NodeAssets.h"
#include "Nodes/NodeRandom.h"
#include <cctype>
#include <set>
#include "Procedural/InteriorDecorators.h"
#include "Scene/GameObject.h"
#include "Rendering/Mesh.h"
#include "Rendering/Material.h"
#include "Rendering/Texture.h"
#include "Rendering/TextureLayer.h"
#include "Rendering/PrimitiveGenerator.h"
#include "Core/AssetManager.h"
#include "Core/ServiceLocator.h"
#include <iostream>

#include <thread>
#include <future>
#include <map>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <fstream>

// Bounding box/color helpers removed. All furniture generation has been simplified to modular empty rooms.


// =====================================================================
// Interior texture paths
// =====================================================================
static const char* INTERIOR_TEXTURES[] = {
	"Assets/Textures/buildings/concrete.jpg",     // MAT_DRYWALL  (200) - reuse concrete as wall paint
	"Assets/Textures/Tile/tile_diffuse.png",      // MAT_FLOOR_TILE (201)
	"Assets/Textures/buildings/concrete.jpg",     // MAT_CARPET   (202)
	"Assets/Textures/buildings/roof_shingles.jpg", // MAT_WOOD     (203)
	"Assets/Textures/buildings/metal_building.jpg",// MAT_METAL    (204)
	"Assets/Textures/buildings/concrete.jpg",     // MAT_FABRIC   (205)
	"Assets/Textures/buildings/concrete.jpg",     // MAT_GLASS    (206)
	"Assets/Textures/buildings/concrete.jpg",     // MAT_CONCRETE (207)
	"Assets/Textures/buildings/concrete.jpg",     // MAT_CEILING  (208)
};

static const char* GetInteriorTexture(int matKey)
{
	int idx = matKey - MAT_DRYWALL;
	if (idx >= 0 && idx < 9) return INTERIOR_TEXTURES[idx];
	return "Assets/Textures/buildings/concrete.jpg";
}

static float GetInteriorTiling(int matKey)
{
	switch (matKey)
	{
		case MAT_DRYWALL:    return 2.0f;
		case MAT_FLOOR_TILE: return 4.0f;
		case MAT_CARPET:     return 3.0f;
		case MAT_WOOD:       return 2.0f;
		case MAT_METAL:      return 1.0f;
		case MAT_FABRIC:     return 1.0f;
		case MAT_GLASS:      return 1.0f;
		case MAT_CONCRETE:   return 5.0f;
		case MAT_CEILING:    return 3.0f;
		default:             return 1.0f;
	}
}

static std::string GetInteriorBatchName(int matKey)
{
	switch (matKey)
	{
		case MAT_DRYWALL:    return "Interior_Walls";
		case MAT_FLOOR_TILE: return "Interior_Tile";
		case MAT_CARPET:     return "Interior_Carpet";
		case MAT_WOOD:       return "Interior_Wood";
		case MAT_METAL:      return "Interior_Metal";
		case MAT_FABRIC:     return "Interior_Fabric";
		case MAT_GLASS:      return "Interior_Glass";
		case MAT_CONCRETE:   return "Interior_Concrete";
		case MAT_CEILING:    return "Interior_Ceiling";
		default:             return "Interior_Misc_" + std::to_string(matKey);
	}
}

// =====================================================================
// Construction
// =====================================================================

InteriorGenNode::InteriorGenNode(NodeGraph& graph) : GraphNode(graph)
{
	seed = NodeRandom::TrueRandomSeed();
	AddInput(PinType::Points, "Plots", "plots");
	AddInput(PinType::Objects, "Furniture", "furniture", true);

	AddFloat("floorHeight", "Floor height", &floorHeight, 0.1f, 1.5f, 6.0f, "%.1f");
	AddFloat("wallThickness", "Wall thickness", &wallThickness, 0.01f, 0.05f, 0.5f, "%.2f");
	AddFloat("doorWidth", "Door width", &doorWidth, 0.05f, 0.6f, 2.0f, "%.2f");
	AddFloat("doorHeight", "Door height", &doorHeight, 0.05f, 1.5f, 3.5f, "%.2f");
	AddFloat("hallwayWidth", "Hallway width", &hallwayWidth, 0.1f, 1.0f, 5.0f, "%.1f");
	AddFloat("wallInset", "Wall inset", &wallInset, 0.1f, 0.0f, 5.0f, "%.1f");
	AddFloat("singleWidth", "Width", &singleWidth, 0.5f, 4.0f, 200.0f, "%.1f").Section("Without plots")
		.ShowIf([this] { return !owner->IsPinLinked(inputs[0].id); }).Tip("Footprint of the one building generated when no plots are linked");
	AddFloat("singleDepth", "Depth", &singleDepth, 0.5f, 4.0f, 200.0f, "%.1f")
		.ShowIf([this] { return !owner->IsPinLinked(inputs[0].id); });
	AddInt("numBedrooms", "Bedrooms", &numBedrooms, 0.1f, 0, 10).Section("Rooms");
	AddInt("numBathrooms", "Bathrooms", &numBathrooms, 0.1f, 0, 10);
	AddInt("numKitchens", "Kitchens", &numKitchens, 0.1f, 0, 5);
	AddInt("numLivingRooms", "Living rooms", &numLivingRooms, 0.1f, 0, 5);
	AddBool("generateWalls", "Interior walls", &generateWalls).Section("Build");
	AddBool("generateCeiling", "Ceilings", &generateCeiling);
	AddBool("generateFurniture", "Placeholder furniture", &generateFurniture)
		.Tip("Furnish rooms even without furniture objects (simple placeholder geometry)");
	AddSeed("seed", "Seed", &seed);
}

const std::vector<std::pair<std::string, std::string>>& InteriorGenNode::Categories()
{
	static const std::vector<std::pair<std::string, std::string>> categories = {
		{ "bed", "Bed" }, { "desk", "Desk" }, { "tv", "TV" }, { "stove", "Stove" }, { "fridge", "Fridge" },
		{ "sink", "Sink" }, { "toilet", "Toilet" }, { "bathtub", "Bathtub" }, { "sofa", "Sofa" },
		{ "coffee_table", "Coffee table" }, { "tv_stand", "TV stand" },
	};
	return categories;
}

std::vector<std::string> InteriorGenNode::LegacyInputKeys() const
{
	std::vector<std::string> keys = { "plots" };
	for (const auto& c : Categories()) keys.push_back("furniture:" + c.first);
	return keys;
}

void InteriorGenNode::SaveState(json& j) const
{
	if (!furnitureOverrides.empty()) j["furnitureOverrides"] = furnitureOverrides;
	j["floorThick"] = floorThick;
}

void InteriorGenNode::LoadState(const json& j)
{
	furnitureOverrides.clear();
	if (j.contains("furnitureOverrides") && j["furnitureOverrides"].is_object())
		for (auto it = j["furnitureOverrides"].begin(); it != j["furnitureOverrides"].end(); ++it)
			furnitureOverrides[it.key()] = it.value().get<std::vector<std::string>>();
	floorThick = j.value("floorThick", floorThick);
	// At least one room
	if (numBedrooms + numBathrooms + numKitchens + numLivingRooms <= 0) numBedrooms = 1;
}

namespace {
	std::string LowerCase(std::string s)
	{
		for (auto& c : s) c = (char)std::tolower((unsigned char)c);
		return s;
	}

	// The category an object's name (or label) suggests: the longest keyword it contains
	std::string GuessCategory(const std::string& text)
	{
		static const std::vector<std::pair<std::string, std::vector<std::string>>> keywords = {
			{ "bed", { "bed" } },
			{ "desk", { "desk", "table" } },
			{ "tv", { "tv", "television", "monitor" } },
			{ "stove", { "stove", "oven", "cooker" } },
			{ "fridge", { "fridge", "refrigerator" } },
			{ "sink", { "sink", "basin" } },
			{ "toilet", { "toilet", "wc" } },
			{ "bathtub", { "bathtub", "bath", "tub", "shower" } },
			{ "sofa", { "sofa", "couch" } },
			{ "coffee_table", { "coffee", "coffee_table", "glass_table", "side_table" } },
			{ "tv_stand", { "tv_stand", "tvstand", "stand", "cabinet", "tumba", "dresser" } },
		};
		std::string lower = LowerCase(text);
		std::string best;
		size_t bestLen = 0;
		for (const auto& [category, words] : keywords)
			for (const auto& w : words)
				if (w.size() > bestLen && lower.find(w) != std::string::npos) { best = category; bestLen = w.size(); }
		return best;
	}
}

std::map<std::string, std::vector<GameObject*>> InteriorGenNode::ResolveFurniture(NodeContext& ctx)
{
	std::map<std::string, std::vector<GameObject*>> result;
	std::set<std::string> assigned;
	auto add = [&](const std::string& category, GameObject* obj) {
		auto& list = result[category];
		if (std::find(list.begin(), list.end(), obj) == list.end()) list.push_back(obj);
		assigned.insert(obj->GetName());
	};

	// 1. The table on the node
	for (const auto& [category, names] : furnitureOverrides)
		for (const auto& name : names) {
			GameObject* obj = ctx.scene.FindObject(name);
			if (obj) add(category, obj);
			else ctx.Warn("Furniture '" + name + "' (" + category + ") is not in the scene");
		}

	// 2. The linked objects: by their label, else by their name
	for (const auto& e : In("furniture").Objects()) {
		GameObject* obj = e.object ? e.object : ctx.scene.FindObject(e.name);
		if (!obj || assigned.count(e.name)) continue;
		std::string category = e.label.empty() ? std::string() : GuessCategory(e.label);
		if (category.empty()) category = GuessCategory(e.name);
		if (category.empty()) { ctx.Warn("No furniture category for '" + e.name + "': pick it in the table (or give it a label)"); continue; }
		add(category, obj);
	}

	lastAssignment.clear();
	for (const auto& [category, objs] : result)
		for (auto* o : objs) lastAssignment[category].push_back(o->GetName());
	return result;
}

void InteriorGenNode::RenderContent(SceneManager* scene)
{
	DrawParams(scene);

	ImGui::SeparatorText("Furniture");
	ImGui::TextDisabled("Linked objects are sorted by name;");
	ImGui::TextDisabled("pick (or drop) them per category here");
	float width = ImGui::CalcItemWidth();
	for (const auto& [key, label] : Categories()) {
		ImGui::PushID(key.c_str());
		auto over = furnitureOverrides.find(key);
		std::string current = over != furnitureOverrides.end() && !over->second.empty() ? over->second[0] : std::string("(auto)");
		if (over != furnitureOverrides.end() && over->second.size() > 1) current += " +" + std::to_string(over->second.size() - 1);
		ImGui::SetNextItemWidth(width);
		if (ImGui::BeginCombo("##obj", current.c_str(), ImGuiComboFlags_HeightLarge)) {
			if (ImGui::Selectable("(auto)", over == furnitureOverrides.end())) furnitureOverrides.erase(key);
			if (scene) {
				std::vector<GameObject*> all;
				scene->GetAllObjects(all);
				for (int i = 0; i < (int)all.size(); i++) {
					ImGui::PushID(i);
					if (ImGui::Selectable(all[i]->GetName().c_str(), all[i]->GetName() == current)) furnitureOverrides[key] = { all[i]->GetName() };
					ImGui::PopID();
				}
			}
			ImGui::EndCombo();
		}
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT_INDEX")) {
				int index = *(const int*)payload->Data;
				if (scene && index >= 0 && index < (int)scene->GetObjects().size()) {
					auto& list = furnitureOverrides[key];
					std::string name = scene->GetObjects()[index]->GetName();
					if (std::find(list.begin(), list.end(), name) == list.end()) list.push_back(name);
				}
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label.c_str());
		auto used = lastAssignment.find(key);
		if (used != lastAssignment.end() && !used->second.empty()) {
			ImGui::SameLine();
			if (used->second.size() == 1) ImGui::TextDisabled("= %s", used->second[0].c_str());
			else ImGui::TextDisabled("= %d variants", (int)used->second.size());
			if (ImGui::IsItemHovered()) {
				std::string tip;
				for (auto& n : used->second) tip += n + "\n";
				ImGui::SetTooltip("%s", tip.c_str());
			}
		}
		ImGui::PopID();
	}
}

// =====================================================================
// MakeWallBox â€” UV-scaled cube primitive (same pattern as BuildingGenNode)
// =====================================================================

MeshData InteriorGenNode::MakeWallBox(glm::mat4 plotMat, glm::vec3 center, glm::vec3 halfExtents, float uvScale) const
{
	static const MeshData baseCube = PrimitiveGenerator::GetCubeData();
	MeshData cube = baseCube;

	float sizeX = halfExtents.x * 2.0f;
	float sizeY = halfExtents.y * 2.0f;
	float sizeZ = halfExtents.z * 2.0f;

	for (int i = 0; i < cube.GetVertexCount(); i++)
	{
		float nx = cube.vertices[i * 14 + 5];
		float ny = cube.vertices[i * 14 + 6];
		float nz = cube.vertices[i * 14 + 7];

		if (std::abs(nx) > 0.5f) {
			cube.vertices[i * 14 + 3] *= sizeZ * uvScale;
			cube.vertices[i * 14 + 4] *= sizeY * uvScale;
		}
		else if (std::abs(nz) > 0.5f) {
			cube.vertices[i * 14 + 3] *= sizeX * uvScale;
			cube.vertices[i * 14 + 4] *= sizeY * uvScale;
		}
		else if (std::abs(ny) > 0.5f) {
			cube.vertices[i * 14 + 3] *= sizeX * uvScale;
			cube.vertices[i * 14 + 4] *= sizeZ * uvScale;
		}
	}

	glm::mat4 xform = glm::translate(plotMat, center);
	xform = glm::scale(xform, halfExtents * 2.0f);
	cube.TransformBy(xform);
	return cube;
}

// =====================================================================
// ComputeRoomSize â€” Determine ideal room dimensions from furniture
// =====================================================================

glm::vec2 InteriorGenNode::ComputeRoomSize(RoomType type, const FurnitureSpecs& f) const
{
	float padding = (type == RoomType::Bathroom) ? 0.8f : 1.5f; // Walking space around furniture
	float doorClearance = 1.2f; // Space for door swing

	switch (type)
	{
	case RoomType::Bedroom:
	{
		float bedLen = std::max(f.bed.size.x, f.bed.size.z);
		float bedW = std::min(f.bed.size.x, f.bed.size.z);
		float w = std::max(bedLen + padding + 0.5f, 3.5f);
		float d = std::max(bedW + f.desk.size.z + padding + doorClearance, 3.2f);
		return glm::vec2(w, d);
	}
	case RoomType::Bathroom:
	{
		float bathtubLen = std::max(f.bathtub.size.x, f.bathtub.size.z);
		float bathtubW = std::min(f.bathtub.size.x, f.bathtub.size.z);
		float w = std::max(std::max(bathtubLen, f.toilet.size.x) + padding, 2.5f);
		float d = std::max(std::max(bathtubW, f.sink.size.z) + padding + doorClearance, 2.0f);
		return glm::vec2(w, d);
	}
	case RoomType::Kitchen:
	{
		float applianceRow = f.stove.size.x + f.sink.size.x + f.fridge.size.x + 0.3f * 2.0f;
		float w = std::max(applianceRow + padding, 3.5f);
		float d = std::max(f.fridge.size.z + padding + doorClearance + 1.0f, 3.0f);
		return glm::vec2(w, d);
	}
	case RoomType::Lobby: // Living Room
	{
		float sofaLen = std::max(f.sofa.size.x, f.sofa.size.z);
		float w = std::max(sofaLen + f.coffeeTable.size.x + padding + 0.5f, 4.0f);
		float d = std::max(f.sofa.size.z + f.tvStand.size.z + padding + doorClearance, 3.5f);
		return glm::vec2(w, d);
	}
	default:
		return glm::vec2(3.0f, 3.0f);
	}
}

// =====================================================================
// AssembleFloorplan â€” Pack rooms along a central hallway
// =====================================================================

void InteriorGenNode::AssembleFloorplan(
	BuildingInterior& interior,
	glm::vec3 origin, float floorY, float ceilY,
	std::mt19937& rng,
	const FurnitureSpecs& furniture) const
{
	// 1. Build the room list with types and ideal sizes
	struct RoomSpec {
		RoomType type;
		float width;
		float depth;
	};

	std::vector<RoomSpec> specs;
	for (int i = 0; i < numLivingRooms; i++)
	{
		auto sz = ComputeRoomSize(RoomType::Lobby, furniture);
		specs.push_back({ RoomType::Lobby, sz.x, sz.y });
	}
	for (int i = 0; i < numKitchens; i++)
	{
		auto sz = ComputeRoomSize(RoomType::Kitchen, furniture);
		specs.push_back({ RoomType::Kitchen, sz.x, sz.y });
	}
	for (int i = 0; i < numBedrooms; i++)
	{
		auto sz = ComputeRoomSize(RoomType::Bedroom, furniture);
		specs.push_back({ RoomType::Bedroom, sz.x, sz.y });
	}
	for (int i = 0; i < numBathrooms; i++)
	{
		auto sz = ComputeRoomSize(RoomType::Bathroom, furniture);
		specs.push_back({ RoomType::Bathroom, sz.x, sz.y });
	}

	if (specs.empty()) return;

	// 2. Split rooms into two rows (top and bottom of hallway)
	std::vector<RoomSpec> topRow, bottomRow;
	for (size_t i = 0; i < specs.size(); i++)
	{
		if (i % 2 == 0)
			topRow.push_back(specs[i]);
		else
			bottomRow.push_back(specs[i]);
	}

	// 3. Compute row widths and max depths
	float wt = wallThickness;
	float hallW = hallwayWidth;

	float topTotalWidth = 0.0f;
	float topMaxDepth = 0.0f;
	for (const auto& r : topRow)
	{
		topTotalWidth += r.width + wt;
		topMaxDepth = std::max(topMaxDepth, r.depth);
	}
	if (!topRow.empty()) topTotalWidth -= wt; // No trailing wall

	float bottomTotalWidth = 0.0f;
	float bottomMaxDepth = 0.0f;
	for (const auto& r : bottomRow)
	{
		bottomTotalWidth += r.width + wt;
		bottomMaxDepth = std::max(bottomMaxDepth, r.depth);
	}
	if (!bottomRow.empty()) bottomTotalWidth -= wt;

	float maxTotalWidth = std::max(topTotalWidth, bottomTotalWidth);

	float totalWidth = maxTotalWidth + wt * 2.0f;
	bool needsHallway = (specs.size() > 1);
	float totalDepth = topMaxDepth + bottomMaxDepth + wt * 2.0f;
	if (needsHallway) totalDepth += hallW + wt * 2.0f;

	// 4. Set building footprint from computed dimensions
	float halfW = totalWidth * 0.5f;
	float halfD = totalDepth * 0.5f;
	interior.footprintMin = glm::vec3(origin.x - halfW, origin.y, origin.z - halfD);
	interior.footprintMax = glm::vec3(origin.x + halfW, ceilY, origin.z + halfD);

	// Helper lambdas
	auto AddRoom = [&](glm::vec3 minB, glm::vec3 maxB, RoomType t) {
		InteriorRoom r;
		r.minBounds = minB;
		r.maxBounds = maxB;
		r.type = t;
		r.floorIndex = 0;
		// For irregular shapes, any room might have exterior walls so we'll just leave this as footprint check 
		// although true exterior windows might need better checking later
		float eps = wt * 2.0f;
		r.hasExteriorWindow =
			(std::abs(minB.x - interior.footprintMin.x) < eps) ||
			(std::abs(maxB.x - interior.footprintMax.x) < eps) ||
			(std::abs(minB.z - interior.footprintMin.z) < eps) ||
			(std::abs(maxB.z - interior.footprintMax.z) < eps);
		interior.rooms.push_back(r);
	};

	auto AddWall = [&](glm::vec3 minB, glm::vec3 maxB) {
		InteriorWall w;
		w.minBounds = minB;
		w.maxBounds = maxB;
		interior.walls.push_back(w);
	};

	// 5. Place rooms
	float startX = interior.footprintMin.x + wt;

	if (needsHallway)
	{
		// Hallway boundaries
		float hallMinZ = origin.z - hallW * 0.5f;
		float hallMaxZ = origin.z + hallW * 0.5f;

		// Add hallway room
		AddRoom(
			glm::vec3(interior.footprintMin.x + wt, floorY, hallMinZ),
			glm::vec3(interior.footprintMax.x - wt, ceilY, hallMaxZ),
			RoomType::Corridor
		);

		// Hallway walls (top and bottom boundaries)
		AddWall(glm::vec3(interior.footprintMin.x, floorY, hallMaxZ),
				glm::vec3(interior.footprintMax.x, ceilY, hallMaxZ + wt));
		AddWall(glm::vec3(interior.footprintMin.x, floorY, hallMinZ - wt),
				glm::vec3(interior.footprintMax.x, ceilY, hallMinZ));

		// Hallway caps (left and right exterior)
		AddWall(glm::vec3(interior.footprintMin.x, floorY, hallMinZ),
				glm::vec3(interior.footprintMin.x + wt, ceilY, hallMaxZ));
		AddWall(glm::vec3(interior.footprintMax.x - wt, floorY, hallMinZ),
				glm::vec3(interior.footprintMax.x, ceilY, hallMaxZ));

		// Place top row (above hallway, +Z)
		float curX = startX;
		float topMinZ = hallMaxZ + wt;

		if (!topRow.empty()) {
			// Far North exterior wall
			AddWall(glm::vec3(startX - wt, floorY, topMinZ + topMaxDepth),
					glm::vec3(startX + topTotalWidth + wt, ceilY, topMinZ + topMaxDepth + wt));
		}

		for (size_t i = 0; i < topRow.size(); i++)
		{
			float roomW = topRow[i].width;
			float roomD = topMaxDepth;

			AddRoom(
				glm::vec3(curX, floorY, topMinZ),
				glm::vec3(curX + roomW, ceilY, topMinZ + roomD),
				topRow[i].type
			);

			// Left exterior cap
			if (i == 0) {
				AddWall(glm::vec3(startX - wt, floorY, topMinZ),
						glm::vec3(startX, ceilY, topMinZ + roomD));
			}

			// Shared interior wall OR right exterior cap
			AddWall(glm::vec3(curX + roomW, floorY, topMinZ),
					glm::vec3(curX + roomW + wt, ceilY, topMinZ + roomD));

			curX += roomW + wt;
		}

		// Place bottom row (below hallway, -Z)
		curX = startX;
		float bottomMaxZ = hallMinZ - wt;

		if (!bottomRow.empty()) {
			// Far South exterior wall
			AddWall(glm::vec3(startX - wt, floorY, bottomMaxZ - bottomMaxDepth - wt),
					glm::vec3(startX + bottomTotalWidth + wt, ceilY, bottomMaxZ - bottomMaxDepth));
		}

		for (size_t i = 0; i < bottomRow.size(); i++)
		{
			float roomW = bottomRow[i].width;
			float roomD = bottomMaxDepth;

			AddRoom(
				glm::vec3(curX, floorY, bottomMaxZ - roomD),
				glm::vec3(curX + roomW, ceilY, bottomMaxZ),
				bottomRow[i].type
			);

			// Left exterior cap
			if (i == 0) {
				AddWall(glm::vec3(startX - wt, floorY, bottomMaxZ - roomD),
						glm::vec3(startX, ceilY, bottomMaxZ));
			}

			// Shared interior wall OR right exterior cap
			AddWall(glm::vec3(curX + roomW, floorY, bottomMaxZ - roomD),
					glm::vec3(curX + roomW + wt, ceilY, bottomMaxZ));

			curX += roomW + wt;
		}
	}
	else
	{
		// Single room — no hallway needed
		float roomW = specs[0].width;
		float roomD = specs[0].depth;
		float halfRW = roomW * 0.5f;
		float halfRD = roomD * 0.5f;

		interior.footprintMin = glm::vec3(origin.x - halfRW - wt, origin.y, origin.z - halfRD - wt);
		interior.footprintMax = glm::vec3(origin.x + halfRW + wt, ceilY, origin.z + halfRD + wt);

		AddRoom(
			glm::vec3(origin.x - halfRW, floorY, origin.z - halfRD),
			glm::vec3(origin.x + halfRW, ceilY, origin.z + halfRD),
			specs[0].type
		);

		// 4 perimeter walls
		AddWall(glm::vec3(interior.footprintMin.x, floorY, interior.footprintMin.z),
				glm::vec3(interior.footprintMax.x, ceilY, interior.footprintMin.z + wt)); // South
		AddWall(glm::vec3(interior.footprintMin.x, floorY, interior.footprintMax.z - wt),
				glm::vec3(interior.footprintMax.x, ceilY, interior.footprintMax.z)); // North
		AddWall(glm::vec3(interior.footprintMin.x, floorY, interior.footprintMin.z + wt),
				glm::vec3(interior.footprintMin.x + wt, ceilY, interior.footprintMax.z - wt)); // West
		AddWall(glm::vec3(interior.footprintMax.x - wt, floorY, interior.footprintMin.z + wt),
				glm::vec3(interior.footprintMax.x, ceilY, interior.footprintMax.z - wt)); // East
	}
}

// =====================================================================
// PlaceDoors â€” Insert doors between adjacent rooms
// =====================================================================

void InteriorGenNode::PlaceDoors(
	BuildingInterior& interior,
	std::mt19937& rng) const
{
	interior.doors.clear();
	if (interior.rooms.size() <= 1) return;

	float wThick = interior.wallThickness;
	float eps = 0.05f;

	struct DoorEdge {
		size_t roomA;
		size_t roomB;
		glm::vec3 doorPos;
		bool runsAlongX;
		float overlapLen;
		int priority; // 0 = Corridor/Lobby, 1 = Kitchen, 2 = Other, 3 = Small Overlap Fallback
		bool isForbidden;
	};

	std::vector<DoorEdge> edges;

	// Helper to gather adjacencies with a given minimum overlap
	auto FindEdges = [&](float minOverlap, int prVal) {
		for (size_t i = 0; i < interior.rooms.size(); i++)
		{
			for (size_t j = i + 1; j < interior.rooms.size(); j++)
			{
				const auto& room = interior.rooms[i];
				const auto& other = interior.rooms[j];

				// Check X adjacency (wall runs along Z, door spans Z)
				float overlapZMin = std::max(room.minBounds.z, other.minBounds.z);
				float overlapZMax = std::min(room.maxBounds.z, other.maxBounds.z);
				float overlapZ = overlapZMax - overlapZMin;
				if (overlapZ > minOverlap)
				{
					if (std::abs(room.maxBounds.x - other.minBounds.x) < wThick * 2.0f + 0.1f ||
						std::abs(room.minBounds.x - other.maxBounds.x) < wThick * 2.0f + 0.1f)
					{
						// Prevent duplicates
						bool exists = false;
						for (const auto& e : edges) {
							if (e.roomA == i && e.roomB == j && !e.runsAlongX) {
								exists = true;
								break;
							}
						}
						if (!exists) {
							DoorEdge edge;
							edge.roomA = i;
							edge.roomB = j;
							float midZ = (overlapZMin + overlapZMax) * 0.5f;
							float L = overlapZ;
							float W = doorWidth;
							if (L > W + 0.4f) {
								std::uniform_int_distribution<int> sideDist(0, 1);
								if (sideDist(rng) == 0) {
									midZ = overlapZMin + W * 0.5f + 0.15f;
								} else {
									midZ = overlapZMax - W * 0.5f - 0.15f;
								}
							}
							float wallX = std::abs(room.maxBounds.x - other.minBounds.x) < wThick * 2.0f + 0.1f ? (room.maxBounds.x + other.minBounds.x) * 0.5f : (room.minBounds.x + other.maxBounds.x) * 0.5f;
							edge.doorPos = glm::vec3(wallX, room.minBounds.y, midZ);
							edge.runsAlongX = false;
							edge.overlapLen = overlapZ;

							if (prVal < 3) {
								if (room.type == RoomType::Corridor || room.type == RoomType::Lobby ||
									other.type == RoomType::Corridor || other.type == RoomType::Lobby)
									edge.priority = 0;
								else if (room.type == RoomType::Kitchen || other.type == RoomType::Kitchen)
									edge.priority = 1;
								else
									edge.priority = 2;
							} else {
								edge.priority = 3;
							}

							edge.isForbidden = (room.type == RoomType::Bedroom && other.type == RoomType::Bedroom) ||
											   (room.type == RoomType::Bedroom && other.type == RoomType::Office) ||
											   (room.type == RoomType::Office && other.type == RoomType::Bedroom) ||
											   (room.type == RoomType::Bathroom && other.type == RoomType::Bathroom);

							edges.push_back(edge);
						}
					}
				}

				// Check Z adjacency (wall runs along X, door spans X)
				float overlapXMin = std::max(room.minBounds.x, other.minBounds.x);
				float overlapXMax = std::min(room.maxBounds.x, other.maxBounds.x);
				float overlapX = overlapXMax - overlapXMin;
				if (overlapX > minOverlap)
				{
					if (std::abs(room.maxBounds.z - other.minBounds.z) < wThick * 2.0f + 0.1f ||
						std::abs(room.minBounds.z - other.maxBounds.z) < wThick * 2.0f + 0.1f)
					{
						// Prevent duplicates
						bool exists = false;
						for (const auto& e : edges) {
							if (e.roomA == i && e.roomB == j && e.runsAlongX) {
								exists = true;
								break;
							}
						}
						if (!exists) {
							DoorEdge edge;
							edge.roomA = i;
							edge.roomB = j;
							float midX = (overlapXMin + overlapXMax) * 0.5f;
							float L = overlapX;
							float W = doorWidth;
							if (L > W + 0.4f) {
								std::uniform_int_distribution<int> sideDist(0, 1);
								if (sideDist(rng) == 0) {
									midX = overlapXMin + W * 0.5f + 0.15f;
								} else {
									midX = overlapXMax - W * 0.5f - 0.15f;
								}
							}
							float wallZ = std::abs(room.maxBounds.z - other.minBounds.z) < wThick * 2.0f + 0.1f ? (room.maxBounds.z + other.minBounds.z) * 0.5f : (room.minBounds.z + other.maxBounds.z) * 0.5f;
							edge.doorPos = glm::vec3(midX, room.minBounds.y, wallZ);
							edge.runsAlongX = true;
							edge.overlapLen = overlapX;

							if (prVal < 3) {
								if (room.type == RoomType::Corridor || room.type == RoomType::Lobby ||
									other.type == RoomType::Corridor || other.type == RoomType::Lobby)
									edge.priority = 0;
								else if (room.type == RoomType::Kitchen || other.type == RoomType::Kitchen)
									edge.priority = 1;
								else
									edge.priority = 2;
							} else {
								edge.priority = 3;
							}

							edge.isForbidden = (room.type == RoomType::Bedroom && other.type == RoomType::Bedroom) ||
											   (room.type == RoomType::Bedroom && other.type == RoomType::Office) ||
											   (room.type == RoomType::Office && other.type == RoomType::Bedroom) ||
											   (room.type == RoomType::Bathroom && other.type == RoomType::Bathroom);

							edges.push_back(edge);
						}
					}
				}
			}
		}
	};

	// 1. Gather normal edges first (with a generous overlap of 0.6 meters)
	FindEdges(0.6f, 0);

	// 2. Find any isolated rooms and gather fallback edges with smaller overlap (0.1 meters) for them
	for (size_t i = 0; i < interior.rooms.size(); i++)
	{
		bool hasEdge = false;
		for (const auto& e : edges) {
			if (e.roomA == i || e.roomB == i) {
				hasEdge = true;
				break;
			}
		}
		if (!hasEdge) {
			// Find tiny overlap edges for this specific room
			for (size_t j = 0; j < interior.rooms.size(); j++)
			{
				if (i == j) continue;
				const auto& room = interior.rooms[i];
				const auto& other = interior.rooms[j];

				float overlapZMin = std::max(room.minBounds.z, other.minBounds.z);
				float overlapZMax = std::min(room.maxBounds.z, other.maxBounds.z);
				float overlapZ = overlapZMax - overlapZMin;
				if (overlapZ > 0.1f)
				{
					if (std::abs(room.maxBounds.x - other.minBounds.x) < wThick * 2.0f + 0.1f ||
						std::abs(room.minBounds.x - other.maxBounds.x) < wThick * 2.0f + 0.1f)
					{
						DoorEdge edge;
						edge.roomA = std::min(i, j);
						edge.roomB = std::max(i, j);
						float midZ = (overlapZMin + overlapZMax) * 0.5f;
						float L = overlapZ;
						float W = doorWidth;
						if (L > W + 0.4f) {
							std::uniform_int_distribution<int> sideDist(0, 1);
							if (sideDist(rng) == 0) {
								midZ = overlapZMin + W * 0.5f + 0.15f;
							} else {
								midZ = overlapZMax - W * 0.5f - 0.15f;
							}
						}
						float wallX = std::abs(room.maxBounds.x - other.minBounds.x) < wThick * 2.0f + 0.1f ? (room.maxBounds.x + other.minBounds.x) * 0.5f : (room.minBounds.x + other.maxBounds.x) * 0.5f;
						edge.doorPos = glm::vec3(wallX, room.minBounds.y, midZ);
						edge.runsAlongX = false;
						edge.overlapLen = overlapZ;
						edge.priority = 3;
						edge.isForbidden = (room.type == RoomType::Bedroom && other.type == RoomType::Bedroom) ||
										   (room.type == RoomType::Bedroom && other.type == RoomType::Office) ||
										   (room.type == RoomType::Office && other.type == RoomType::Bedroom) ||
										   (room.type == RoomType::Bathroom && other.type == RoomType::Bathroom);
						edges.push_back(edge);
					}
				}

				float overlapXMin = std::max(room.minBounds.x, other.minBounds.x);
				float overlapXMax = std::min(room.maxBounds.x, other.maxBounds.x);
				float overlapX = overlapXMax - overlapXMin;
				if (overlapX > 0.1f)
				{
					if (std::abs(room.maxBounds.z - other.minBounds.z) < wThick * 2.0f + 0.1f ||
						std::abs(room.minBounds.z - other.maxBounds.z) < wThick * 2.0f + 0.1f)
					{
						DoorEdge edge;
						edge.roomA = std::min(i, j);
						edge.roomB = std::max(i, j);
						float midX = (overlapXMin + overlapXMax) * 0.5f;
						float L = overlapX;
						float W = doorWidth;
						if (L > W + 0.4f) {
							std::uniform_int_distribution<int> sideDist(0, 1);
							if (sideDist(rng) == 0) {
								midX = overlapXMin + W * 0.5f + 0.15f;
							} else {
								midX = overlapXMax - W * 0.5f - 0.15f;
							}
						}
						float wallZ = std::abs(room.maxBounds.z - other.minBounds.z) < wThick * 2.0f + 0.1f ? (room.maxBounds.z + other.minBounds.z) * 0.5f : (room.minBounds.z + other.maxBounds.z) * 0.5f;
						edge.doorPos = glm::vec3(midX, room.minBounds.y, wallZ);
						edge.runsAlongX = true;
						edge.overlapLen = overlapX;
						edge.priority = 3;
						edge.isForbidden = (room.type == RoomType::Bedroom && other.type == RoomType::Bedroom) ||
										   (room.type == RoomType::Bedroom && other.type == RoomType::Office) ||
										   (room.type == RoomType::Office && other.type == RoomType::Bedroom) ||
										   (room.type == RoomType::Bathroom && other.type == RoomType::Bathroom);
						edges.push_back(edge);
					}
				}
			}
		}
	}

	// DSU spanning tree helper class
	struct DSU {
		std::vector<int> parent;
		DSU(size_t n) {
			parent.resize(n);
			for (size_t i = 0; i < n; i++) parent[i] = (int)i;
		}
		int find(int i) {
			if (parent[i] == i)
				return i;
			return parent[i] = find(parent[i]);
		}
		bool union_set(int i, int j) {
			int rootI = find(i);
			int rootJ = find(j);
			if (rootI != rootJ) {
				parent[rootI] = rootJ;
				return true;
			}
			return false;
		}
	};

	DSU dsu(interior.rooms.size());
	std::vector<std::pair<size_t, size_t>> placedDoors;

	auto HasDoorBetween = [&](size_t a, size_t b) {
		size_t mn = std::min(a, b);
		size_t mx = std::max(a, b);
		for (const auto& p : placedDoors) {
			if (p.first == mn && p.second == mx) return true;
		}
		return false;
	};

	auto AddDoorDirect = [&](const DoorEdge& edge) {
		InteriorDoor door;
		door.width = doorWidth;
		door.height = doorHeight;
		door.runsAlongX = edge.runsAlongX;
		door.position = edge.doorPos;
		door.hingeOnLeft = (rng() % 2 == 0);
		door.isOpen = true;

		interior.doors.push_back(door);
		placedDoors.push_back({ std::min(edge.roomA, edge.roomB), std::max(edge.roomA, edge.roomB) });
	};

	// Sort edges: non-forbidden first, then priority (0, 1, 2, 3), then overlapLen descending
	std::sort(edges.begin(), edges.end(), [](const DoorEdge& a, const DoorEdge& b) {
		if (a.isForbidden != b.isForbidden)
			return !a.isForbidden; // non-forbidden first
		if (a.priority != b.priority)
			return a.priority < b.priority;
		return a.overlapLen > b.overlapLen;
	});

	// Phase 1: Connect all components using the spanning tree (first pass avoiding forbidden edges if possible)
	for (const auto& edge : edges)
	{
		if (edge.isForbidden) continue; // STRICTLY SKIP forbidden edges!

		if (dsu.union_set((int)edge.roomA, (int)edge.roomB))
		{
			AddDoorDirect(edge);
		}
	}

	// Phase 2: Add extra direct connections to Corridor/Lobby (priority 0) to ensure natural navigation flow
	for (const auto& edge : edges)
	{
		if (edge.isForbidden) continue; // STRICTLY SKIP forbidden edges!

		if (edge.priority == 0)
		{
			if (!HasDoorBetween(edge.roomA, edge.roomB))
			{
				AddDoorDirect(edge);
			}
		}
	}

	// DEBUG PRINT FOR DIAGNOSIS
	std::cout << "\n--- PlaceDoors Debug Info ---" << std::endl;
	std::cout << "Rooms count: " << interior.rooms.size() << std::endl;
	for (size_t i = 0; i < interior.rooms.size(); i++) {
		auto& r = interior.rooms[i];
		std::cout << "Room " << i << " (type=" << (int)r.type << "): min=(" 
				  << r.minBounds.x << "," << r.minBounds.z << "), max=(" 
				  << r.maxBounds.x << "," << r.maxBounds.z << ")" << std::endl;
	}
	std::cout << "Edges collected: " << edges.size() << std::endl;
	for (const auto& e : edges) {
		std::cout << "Edge " << e.roomA << " <-> " << e.roomB 
				  << " overlap=" << e.overlapLen << " priority=" << e.priority 
				  << " forbidden=" << e.isForbidden << " pos=("
				  << e.doorPos.x << "," << e.doorPos.z << ")" << std::endl;
	}
	std::cout << "Doors placed: " << interior.doors.size() << std::endl;
	for (const auto& d : interior.doors) {
		std::cout << "Door at (" << d.position.x << "," << d.position.z 
				  << ") axisX=" << d.runsAlongX << " W=" << d.width << std::endl;
	}
	for (size_t i = 0; i < interior.rooms.size(); i++) {
		int doorCount = 0;
		for (const auto& p : placedDoors) {
			if (p.first == i || p.second == i) doorCount++;
		}
		if (doorCount == 0) {
			std::cout << "WARNING: Room " << i << " has ZERO doors!" << std::endl;
		}
	}
	std::cout << "-----------------------------\n" << std::endl;
}

// =====================================================================
// GenerateBuildingInterior â€” Full pipeline for one building
// =====================================================================

BuildingInterior InteriorGenNode::GenerateBuildingInterior(
	const TransformData& plot,
	std::mt19937& rng,
	const FurnitureSpecs& furniture) const
{
	BuildingInterior interior;

	interior.isCommercial = false;
	interior.floorHeight = floorHeight;
	interior.wallThickness = wallThickness;
	interior.floorThickness = floorThick;
	interior.numFloors = 1;

	float floorY = plot.position.y + floorThick;
	float ceilY  = plot.position.y + floorHeight;

	// Assemble the floorplan — this sets footprintMin/footprintMax from room sizes
	AssembleFloorplan(interior, plot.position, floorY, ceilY, rng, furniture);

	if (interior.rooms.empty()) return interior;

	// Place doors in walls
	PlaceDoors(interior, rng);

	return interior;
}

// =====================================================================
// BuildStructuralMesh â€” Generate floor/ceiling/wall geometry
// =====================================================================

void InteriorGenNode::BuildStructuralMesh(
	const BuildingInterior& interior,
	glm::mat4 plotMat,
	std::map<int, MeshData>& meshBuckets) const
{
	// Floor and ceiling slabs per room
	for (const auto& room : interior.rooms)
	{
		glm::vec3 size = room.GetSize();
		glm::vec3 center = room.GetCenter();

		// Floor slab based on room type
		int floorMat = MAT_CARPET;
		if (room.type == RoomType::Bathroom || room.type == RoomType::Kitchen) {
			floorMat = MAT_FLOOR_TILE; // light ceramic tiles
		} else if (room.type == RoomType::Bedroom || room.type == RoomType::Office) {
			floorMat = MAT_CARPET;     // warm grey carpet
		} else if (room.type == RoomType::Lobby || room.type == RoomType::Corridor) {
			floorMat = MAT_WOOD;       // elegant dark wood planks
		}

		// Expand floor size to cover the wall gaps if we are not generating interior walls
		glm::vec3 floorSize = size;
		if (!generateWalls)
		{
			floorSize.x += interior.wallThickness;
			floorSize.z += interior.wallThickness;
		}

		meshBuckets[floorMat].Append(MakeWallBox(plotMat, 
			glm::vec3(center.x, room.minBounds.y - interior.floorThickness * 0.5f, center.z),
			glm::vec3(floorSize.x * 0.5f, interior.floorThickness * 0.5f, floorSize.z * 0.5f),
			2.0f));

		// Ceiling slab
		if (generateCeiling)
		{
			meshBuckets[MAT_CEILING].Append(MakeWallBox(plotMat, 
				glm::vec3(center.x, room.maxBounds.y + interior.floorThickness * 0.5f, center.z),
				glm::vec3(size.x * 0.5f, interior.floorThickness * 0.5f, size.z * 0.5f),
				2.0f));
		}
	}

		// Interior walls
		if (generateWalls)
		{
			for (const auto& wall : interior.walls)
			{
				float wallW = wall.maxBounds.x - wall.minBounds.x;
				float wallD = wall.maxBounds.z - wall.minBounds.z;
				bool thinInX = (wallW < wallD);

				// Find all intersecting doors
				std::vector<const InteriorDoor*> intersectingDoors;
				float eps = 0.15f; // updated eps to comfortably match slightly larger wall thicknesses

				for (const auto& door : interior.doors)
				{
					if (thinInX)
					{
						// Wall runs along Z. Door should also run along Z (runsAlongX = false)
						if (!door.runsAlongX)
						{
							float wallXCenter = (wall.minBounds.x + wall.maxBounds.x) * 0.5f;
							if (std::abs(door.position.x - wallXCenter) < eps &&
								door.position.z >= wall.minBounds.z - eps &&
								door.position.z <= wall.maxBounds.z + eps)
							{
								intersectingDoors.push_back(&door);
							}
						}
					}
					else
					{
						// Wall runs along X. Door should also run along X (runsAlongX = true)
						if (door.runsAlongX)
						{
							float wallZCenter = (wall.minBounds.z + wall.maxBounds.z) * 0.5f;
							if (std::abs(door.position.z - wallZCenter) < eps &&
								door.position.x >= wall.minBounds.x - eps &&
								door.position.x <= wall.maxBounds.x + eps)
							{
								intersectingDoors.push_back(&door);
							}
						}
					}
				}

				if (!intersectingDoors.empty())
				{
					if (thinInX)
					{
						// Sort doors along Z axis ascending
						std::sort(intersectingDoors.begin(), intersectingDoors.end(), [](const InteriorDoor* a, const InteriorDoor* b) {
							return a->position.z < b->position.z;
						});

						float currentZ = wall.minBounds.z;
						for (const auto* door : intersectingDoors)
						{
							float doorHalfW = door->width * 0.5f;
							float zSplitLeft = door->position.z - doorHalfW;
							float zSplitRight = door->position.z + doorHalfW;

							// Left/Bottom piece
							if (zSplitLeft - currentZ > 0.05f)
							{
								glm::vec3 minB = glm::vec3(wall.minBounds.x, wall.minBounds.y, currentZ);
								glm::vec3 maxB = glm::vec3(wall.maxBounds.x, wall.maxBounds.y, zSplitLeft);
								glm::vec3 center = (minB + maxB) * 0.5f;
								glm::vec3 half = (maxB - minB) * 0.5f;
								meshBuckets[MAT_DRYWALL].Append(MakeWallBox(plotMat, center, half, 2.0f));
							}

							// Header piece above the door
							float headerMinY = door->position.y + door->height;
							if (wall.maxBounds.y - headerMinY > 0.05f)
							{
								glm::vec3 minB = glm::vec3(wall.minBounds.x, headerMinY, zSplitLeft);
								glm::vec3 maxB = glm::vec3(wall.maxBounds.x, wall.maxBounds.y, zSplitRight);
								glm::vec3 center = (minB + maxB) * 0.5f;
								glm::vec3 half = (maxB - minB) * 0.5f;
								meshBuckets[MAT_DRYWALL].Append(MakeWallBox(plotMat, center, half, 2.0f));
							}

							currentZ = zSplitRight;
						}

						// Right/Top piece
						if (wall.maxBounds.z - currentZ > 0.05f)
						{
							glm::vec3 minB = glm::vec3(wall.minBounds.x, wall.minBounds.y, currentZ);
							glm::vec3 maxB = wall.maxBounds;
							glm::vec3 center = (minB + maxB) * 0.5f;
							glm::vec3 half = (maxB - minB) * 0.5f;
							meshBuckets[MAT_DRYWALL].Append(MakeWallBox(plotMat, center, half, 2.0f));
						}
					}
					else
					{
						// Sort doors along X axis ascending
						std::sort(intersectingDoors.begin(), intersectingDoors.end(), [](const InteriorDoor* a, const InteriorDoor* b) {
							return a->position.x < b->position.x;
						});

						float currentX = wall.minBounds.x;
						for (const auto* door : intersectingDoors)
						{
							float doorHalfW = door->width * 0.5f;
							float xSplitLeft = door->position.x - doorHalfW;
							float xSplitRight = door->position.x + doorHalfW;

							// Left piece
							if (xSplitLeft - currentX > 0.05f)
							{
								glm::vec3 minB = glm::vec3(currentX, wall.minBounds.y, wall.minBounds.z);
								glm::vec3 maxB = glm::vec3(xSplitLeft, wall.maxBounds.y, wall.maxBounds.z);
								glm::vec3 center = (minB + maxB) * 0.5f;
								glm::vec3 half = (maxB - minB) * 0.5f;
								meshBuckets[MAT_DRYWALL].Append(MakeWallBox(plotMat, center, half, 2.0f));
							}

							// Header piece above the door
							float headerMinY = door->position.y + door->height;
							if (wall.maxBounds.y - headerMinY > 0.05f)
							{
								glm::vec3 minB = glm::vec3(xSplitLeft, headerMinY, wall.minBounds.z);
								glm::vec3 maxB = glm::vec3(xSplitRight, wall.maxBounds.y, wall.maxBounds.z);
								glm::vec3 center = (minB + maxB) * 0.5f;
								glm::vec3 half = (maxB - minB) * 0.5f;
								meshBuckets[MAT_DRYWALL].Append(MakeWallBox(plotMat, center, half, 2.0f));
							}

							currentX = xSplitRight;
						}

						// Right piece
						if (wall.maxBounds.x - currentX > 0.05f)
						{
							glm::vec3 minB = glm::vec3(currentX, wall.minBounds.y, wall.minBounds.z);
							glm::vec3 maxB = wall.maxBounds;
							glm::vec3 center = (minB + maxB) * 0.5f;
							glm::vec3 half = (maxB - minB) * 0.5f;
							meshBuckets[MAT_DRYWALL].Append(MakeWallBox(plotMat, center, half, 2.0f));
						}
					}
				}
				else
				{
					// No door, render solid wall
					glm::vec3 center = (wall.minBounds + wall.maxBounds) * 0.5f;
					glm::vec3 half = (wall.maxBounds - wall.minBounds) * 0.5f;
					meshBuckets[MAT_DRYWALL].Append(MakeWallBox(plotMat, center, half, 2.0f));
				}
			}


	}
}

// Helper to find the precise visual world bounds of an object and its hierarchy, ignoring empty/dummy nodes
static bool GetVisualWorldBounds(GameObject* obj, glm::vec3& outMin, glm::vec3& outMax) {
	if (!obj) return false;

	glm::vec3 worldMin(1e10f), worldMax(-1e10f);
	bool hasAnyMesh = false;

	// 1. Check if this node has geometry
	if (obj->GetModel() || obj->GetMesh() || obj->HasCustomMesh()) {
		glm::vec3 localMin(0.0f), localMax(0.0f);
		if (obj->GetModel() && !obj->HasCustomMesh()) {
			localMin = obj->GetModel()->GetMinBound();
			localMax = obj->GetModel()->GetMaxBound();
		}
		else if (obj->HasCustomMesh()) {
			if (obj->GetCPUMeshData().GetVertexCount() > 0) {
				obj->GetCPUMeshData().GetBounds(localMin, localMax);
			}
		}
		else if (obj->GetMesh()) {
			obj->GetMesh()->GetBounds(localMin, localMax);
		}

		glm::mat4 world = obj->GetWorldMatrix();
		glm::vec3 corners[8] = {
			{localMin.x, localMin.y, localMin.z}, {localMax.x, localMin.y, localMin.z},
			{localMin.x, localMax.y, localMin.z}, {localMin.x, localMin.y, localMax.z},
			{localMax.x, localMax.y, localMin.z}, {localMax.x, localMin.y, localMax.z},
			{localMin.x, localMax.y, localMax.z}, {localMax.x, localMax.y, localMax.z},
		};

		for (int i = 0; i < 8; i++) {
			glm::vec3 worldCorner = glm::vec3(world * glm::vec4(corners[i], 1.0f));
			worldMin = glm::min(worldMin, worldCorner);
			worldMax = glm::max(worldMax, worldCorner);
		}
		hasAnyMesh = true;
	}

	// 2. Check children recursively
	for (auto* child : obj->GetChildren()) {
		glm::vec3 cMin, cMax;
		if (GetVisualWorldBounds(child, cMin, cMax)) {
			worldMin = glm::min(worldMin, cMin);
			worldMax = glm::max(worldMax, cMax);
			hasAnyMesh = true;
		}
	}

	if (hasAnyMesh) {
		outMin = worldMin;
		outMax = worldMax;
		return true;
	}
	return false;
}

// Helper to find the geometric center and scaled/rotated size of an object's bounds in its own local space.
// This is used to counteract FBX assets whose pivots are not at their geometric center (e.g. at the foot of the bed).
static void GetObjectLocalBoundsInfo(GameObject* obj, glm::vec3 scale, glm::vec3 euler, glm::vec3& outCenter, glm::vec3& outSize) {
	if (!obj) {
		outCenter = glm::vec3(0.0f);
		outSize = glm::vec3(1.0f);
		return;
	}
	
	// Temporarily save transform
	glm::vec3 oldPos = obj->GetTransform().GetPosition();
	glm::vec3 oldRot = obj->GetTransform().GetRotation();
	glm::vec3 oldScl = obj->GetTransform().GetScale();
	GameObject* oldParent = obj->GetParent();
	
	obj->SetParent(nullptr);
	obj->GetTransform().SetPosition(glm::vec3(0.0f));
	obj->GetTransform().SetRotation(glm::vec3(0.0f));
	obj->GetTransform().SetScale(glm::vec3(1.0f));
	obj->SetDirty(); // Forces bounds and matrix recomputation
	
	glm::vec3 minB, maxB;
	bool hasBounds = GetVisualWorldBounds(obj, minB, maxB);
	
	// Restore transform
	obj->SetParent(oldParent);
	obj->GetTransform().SetPosition(oldPos);
	obj->GetTransform().SetRotation(oldRot);
	obj->GetTransform().SetScale(oldScl);
	obj->SetDirty();
	
	if (!hasBounds || minB.x > 1e9f || maxB.x < -1e9f) {
		outCenter = glm::vec3(0.0f);
		outSize = glm::vec3(1.0f);
		return;
	}

	// 1. Unscaled, unrotated center
	outCenter = (minB + maxB) * 0.5f;

	// 2. Compute rotated and scaled AABB size
	glm::mat4 rMat(1.0f);
	rMat = glm::rotate(rMat, glm::radians(euler.y), glm::vec3(0.0f, 1.0f, 0.0f));
	rMat = glm::rotate(rMat, glm::radians(euler.x), glm::vec3(1.0f, 0.0f, 0.0f));
	rMat = glm::rotate(rMat, glm::radians(euler.z), glm::vec3(0.0f, 0.0f, 1.0f));

	glm::vec3 corners[8] = {
		{minB.x, minB.y, minB.z}, {maxB.x, minB.y, minB.z},
		{minB.x, maxB.y, minB.z}, {minB.x, minB.y, maxB.z},
		{maxB.x, maxB.y, minB.z}, {maxB.x, minB.y, maxB.z},
		{minB.x, maxB.y, maxB.z}, {maxB.x, maxB.y, maxB.z},
	};

	glm::vec3 rotMin(1e10f), rotMax(-1e10f);
	for (int i = 0; i < 8; i++) {
		glm::vec3 scaled = corners[i] * scale;
		glm::vec3 rotated = glm::vec3(rMat * glm::vec4(scaled, 1.0f));
		rotMin = glm::min(rotMin, rotated);
		rotMax = glm::max(rotMax, rotated);
	}
	outSize = rotMax - rotMin;
}

// Helper to compute a GameObject's AABB size in parent/room space based on its recursively computed world bounds
static glm::vec3 GetObjectAABBSize(GameObject* obj, glm::vec3 defaultVal) {
	if (!obj) return defaultVal;
	
	// Query the recursive visual world bounds which ignore dummy nodes/pivot helpers
	glm::vec3 minB, maxB;
	if (!GetVisualWorldBounds(obj, minB, maxB)) {
		return defaultVal;
	}

	glm::vec3 size = maxB - minB;

	// Sanity checks to ensure we never return degenerate dimensions
	if (size.x < 0.01f) size.x = defaultVal.x;
	if (size.y < 0.01f) size.y = defaultVal.y;
	if (size.z < 0.01f) size.z = defaultVal.z;

	return size;
}

// =====================================================================
// Execute â€” Main entry point
// =====================================================================

void InteriorGenNode::Execute(NodeContext& ctx)
{
	SceneManager& scene = ctx.scene;
	auto progress = [&](float pct, const std::string& msg) { ctx.Progress(pct, msg); };
	progress(0.0f, "Reading plot data...");

	// Furniture: category -> objects. Rooms are planned around the largest object of a category;
	// each placement picks one of its objects
	auto furniture = ResolveFurniture(ctx);
	const bool furnished = generateFurniture || !furniture.empty();
	auto largest = [&](const char* category) -> GameObject* {
		auto it = furniture.find(category);
		if (it == furniture.end() || it->second.empty()) return nullptr;
		GameObject* best = nullptr;
		float bestVolume = -1.0f;
		for (GameObject* o : it->second) {
			glm::vec3 size = GetObjectAABBSize(o, glm::vec3(0.0f));
			if (size.x * size.y * size.z > bestVolume) { bestVolume = size.x * size.y * size.z; best = o; }
		}
		return best;
	};
	GameObject* bedSrcObj = largest("bed");
	GameObject* deskSrcObj = largest("desk");
	GameObject* tvSrcObj = largest("tv");
	GameObject* stoveSrcObj = largest("stove");
	GameObject* fridgeSrcObj = largest("fridge");
	GameObject* sinkSrcObj = largest("sink");
	GameObject* toiletSrcObj = largest("toilet");
	GameObject* bathtubSrcObj = largest("bathtub");
	GameObject* sofaSrcObj = largest("sofa");
	GameObject* coffeeTableSrcObj = largest("coffee_table");
	GameObject* tvStandSrcObj = largest("tv_stand");

	glm::vec3 bedSize = GetObjectAABBSize(bedSrcObj, glm::vec3(1.6f, 0.8f, 2.0f));
	glm::vec3 deskSize = GetObjectAABBSize(deskSrcObj, glm::vec3(1.2f, 0.75f, 0.6f));
	glm::vec3 tvSize = GetObjectAABBSize(tvSrcObj, glm::vec3(0.9f, 0.6f, 0.2f));
	glm::vec3 stoveSize = GetObjectAABBSize(stoveSrcObj, glm::vec3(0.8f, 0.9f, 0.6f));
	glm::vec3 fridgeSize = GetObjectAABBSize(fridgeSrcObj, glm::vec3(0.8f, 1.8f, 0.7f));
	glm::vec3 sinkSize = GetObjectAABBSize(sinkSrcObj, glm::vec3(0.9f, 0.9f, 0.6f));
	glm::vec3 toiletSize = GetObjectAABBSize(toiletSrcObj, glm::vec3(0.0f));
	glm::vec3 bathtubSize = GetObjectAABBSize(bathtubSrcObj, glm::vec3(0.0f));
	glm::vec3 sofaSize = GetObjectAABBSize(sofaSrcObj, glm::vec3(1.6f, 0.8f, 0.8f));
	glm::vec3 coffeeTableSize = GetObjectAABBSize(coffeeTableSrcObj, glm::vec3(1.0f, 0.45f, 0.7f));
	glm::vec3 tvStandSize = GetObjectAABBSize(tvStandSrcObj, glm::vec3(1.2f, 0.5f, 0.6f));

	TransformList plots;
	if (In("plots").Points().empty())
	{
		TransformData defaultPlot;
		defaultPlot.position = glm::vec3(0.0f, 0.0f, 0.0f);
		defaultPlot.rotation = glm::vec3(0.0f);
		defaultPlot.scale = glm::vec3(singleWidth, 1.0f, singleDepth);
		plots.push_back(defaultPlot);
	}
	else
	{
		plots = In("plots").Points();
	}

	progress(5.0f, "Generating interiors...");

	// Phase 1: Generate interior data for all buildings (multi-threaded)
	std::vector<BuildingInterior> interiors(plots.size());
	int numThreads = std::thread::hardware_concurrency();
	if (numThreads == 0) numThreads = 8;

	std::vector<std::thread> threads;
	size_t chunkSize = (plots.size() + numThreads - 1) / numThreads;

	// Build furniture sizes struct for the pipeline
	FurnitureSpecs furnitureSpecs;
	furnitureSpecs.bed = { bedSize, bedSrcObj ? bedSrcObj->GetModelSourcePath() : "" };
	furnitureSpecs.desk = { deskSize, deskSrcObj ? deskSrcObj->GetModelSourcePath() : "" };
	furnitureSpecs.tv = { tvSize, tvSrcObj ? tvSrcObj->GetModelSourcePath() : "" };
	furnitureSpecs.stove = { stoveSize, stoveSrcObj ? stoveSrcObj->GetModelSourcePath() : "" };
	furnitureSpecs.fridge = { fridgeSize, fridgeSrcObj ? fridgeSrcObj->GetModelSourcePath() : "" };
	furnitureSpecs.sink = { sinkSize, sinkSrcObj ? sinkSrcObj->GetModelSourcePath() : "" };
	furnitureSpecs.toilet = { toiletSize, toiletSrcObj ? toiletSrcObj->GetModelSourcePath() : "" };
	furnitureSpecs.bathtub = { bathtubSize, bathtubSrcObj ? bathtubSrcObj->GetModelSourcePath() : "" };
	furnitureSpecs.sofa = { sofaSize, sofaSrcObj ? sofaSrcObj->GetModelSourcePath() : "" };
	furnitureSpecs.coffeeTable = { coffeeTableSize, coffeeTableSrcObj ? coffeeTableSrcObj->GetModelSourcePath() : "" };
	furnitureSpecs.tvStand = { tvStandSize, tvStandSrcObj ? tvStandSrcObj->GetModelSourcePath() : "" };

	for (int t = 0; t < numThreads; t++)
	{
		size_t startIdx = t * chunkSize;
		size_t endIdx = std::min(startIdx + chunkSize, plots.size());
		if (startIdx >= plots.size()) break;

		threads.emplace_back([this, startIdx, endIdx, &plots, &interiors, furnitureSpecs]() {
			for (size_t i = startIdx; i < endIdx; i++)
			{
				std::mt19937 localRng(seed + (int)i + 7919);
				interiors[i] = GenerateBuildingInterior(plots[i], localRng, furnitureSpecs);
			}
		});
	}

	for (auto& t : threads) t.join();

	progress(40.0f, "Building structural meshes...");

	// Phase 2: Build structural + furniture meshes into material buckets
	std::map<int, MeshData> meshBuckets;

	for (size_t i = 0; i < interiors.size(); i++)
	{
		auto& interior = interiors[i];
		if (interior.rooms.empty()) continue;

		glm::mat4 plotMat(1.0f);
		plotMat = glm::translate(plotMat, plots[i].position);
		plotMat = glm::rotate(plotMat, glm::radians(plots[i].rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		plotMat = glm::rotate(plotMat, glm::radians(plots[i].rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		plotMat = glm::rotate(plotMat, glm::radians(plots[i].rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		// Structural geometry (floors, ceilings, walls)
		BuildStructuralMesh(interior, plotMat, meshBuckets);

		// Furniture decoration
		if (furnished)
		{
			std::mt19937 decorRng(seed + (int)i + 13337);
			for (const auto& room : interior.rooms)
			{
				auto decorator = CreateDecoratorForRoom(room.type);
				if (decorator)
				{
					decorator->Decorate(meshBuckets, interior.props, room, interior.doors, decorRng, floorHeight, furnitureSpecs, interior.isCommercial);
				}
			}
		}

		{
			float pct = 40.0f + 40.0f * ((float)(i + 1) / interiors.size());
			progress(pct, "Processing building " + std::to_string(i + 1) + "/" + std::to_string(interiors.size()));
		}
	}

	progress(85.0f, "Uploading to GPU...");

	// Phase 3: Upload batched meshes as GameObjects
	std::string prefix = "Interior_" + std::to_string(id) + "_";
	std::string rootName = "City_Interiors_" + std::to_string(id);

	GameObject* root = ctx.Object(rootName);
	root->SetSaveInScene(false);
	root->GetTransform().SetPosition(glm::vec3(0.0f));
	root->GetTransform().SetRotation(glm::vec3(0.0f));
	root->GetTransform().SetScale(glm::vec3(1.0f));

	// Clean up old children
	{
		std::vector<std::string> oldChildren;
		for (auto* child : root->GetChildren())
			oldChildren.push_back(child->GetName());
		for (auto& name : oldChildren)
			scene.RemoveObject(name);
	}

	int batchCount = 0;

	for (auto& [matKey, mergedMesh] : meshBuckets)
	{
		if (mergedMesh.GetVertexCount() == 0) continue;

		std::string batchName = prefix + GetInteriorBatchName(matKey);
		GameObject* batchObj = ctx.Object(batchName);

		batchObj->GetTransform().SetPosition(glm::vec3(0.0f));
		batchObj->GetTransform().SetRotation(glm::vec3(0.0f));
		batchObj->GetTransform().SetScale(glm::vec3(1.0f));
		batchObj->SetParent(root);
		batchObj->SetMesh(mergedMesh.ToMesh());
		batchObj->SetCPUMeshData(mergedMesh);

		// Texture
		while (batchObj->GetTextureLayers().size() > 0) batchObj->RemoveTextureLayer(0);

		const char* texPath = GetInteriorTexture(matKey);

		TextureLayer layer;
		layer.texturePath = texPath;
		layer.texture = NodeAssets::GetTexture(texPath);
		layer.blendMode = LayerBlendMode::Normal;
		layer.opacity = 1.0f;
		layer.tiling = GetInteriorTiling(matKey);
		batchObj->AddTextureLayer(layer);

		batchCount++;
	}

	// Phase 3b: Instantiate high-fidelity props as GameObjects
	int propId = 0;
	size_t roomCount = 0;
	for (const auto& interior : interiors) roomCount += interior.rooms.size();
	if (furnished)
	{
		for (size_t i = 0; i < interiors.size(); i++)
		{
			auto& interior = interiors[i];

			// Create a building root wrapper specifically to hold all placement wrappers for this building.
			// This matches the plot's world position and rotation without any scaling!
			std::string bldRootName = prefix + "BuildingRoot_" + std::to_string(i);
			GameObject* bldRoot = new GameObject(bldRootName);
			bldRoot->GetTransform().SetPosition(plots[i].position);
			bldRoot->GetTransform().SetRotation(plots[i].rotation);
			bldRoot->GetTransform().SetScale(glm::vec3(1.0f));
			bldRoot->SetParent(root);
			scene.AddObject(bldRoot);
			ctx.Claim(bldRootName);

			for (const auto& prop : interior.props)
			{
				// The prop's category (washing machines and cabinets borrow others'), then one of its objects
				std::string category = prop.category;
				if (category == "washing_machine") category = "toilet";
				else if (category == "couch") category = "sofa";
				else if (category == "cabinet") category = furniture.count("tv_stand") ? "tv_stand" : (furniture.count("desk") ? "desk" : "sink");
				GameObject* sourceObj = nullptr;
				auto variants = furniture.find(category);
				if (variants != furniture.end() && !variants->second.empty()) {
					size_t pick = (size_t)NodeRandom::Mix(NodeRandom::Mix((uint64_t)seed, (uint64_t)i), (uint64_t)propId) % variants->second.size();
					sourceObj = variants->second[pick];
				}

				if (prop.category == "debug_fail_bathtub")
				{
					GameObject* gizmo = new GameObject(prefix + "DEBUG_FAILED_BATHTUB");
					gizmo->SetParent(bldRoot);
					
					gizmo->GetTransform().SetPosition(prop.position);
					gizmo->GetTransform().SetScale(glm::vec3(0.5f));
					gizmo->SetMesh(PrimitiveGenerator::CreateCube());

					Material* redMat = new Material();
					redMat->SetColor(glm::vec4(1.0f, 0.0f, 0.0f, 0.8f));
					gizmo->SetMaterial(redMat);

					scene.AddObject(gizmo);
					ctx.Claim(gizmo->GetName());
					continue;
				}

				if (sourceObj)
				{
					// 1. Create a placement wrapper GameObject
					std::string wrapName = prefix + prop.category + "_" + std::to_string(propId++) + "_placement";
					GameObject* wrapObj = new GameObject(wrapName);
					
					// Parent placement wrapper to building root FIRST!
					wrapObj->SetParent(bldRoot);
					
					// Set its transform to the clean procedural placement parameters
					wrapObj->GetTransform().SetPosition(prop.position);
					wrapObj->GetTransform().SetRotation(prop.rotation);
					wrapObj->GetTransform().SetScale(prop.scale);
					
					scene.AddObject(wrapObj);
					ctx.Claim(wrapName);

					// 2. Clone the source object
					GameObject* propObj = sourceObj->Clone(wrapName + "_model");
					
					// Parent it to the placement wrapper
					propObj->SetParent(wrapObj);
					
					// Overwrite local transform to maintain exact source rotation and scale corrections without any matrix extraction skew!
					// Calculate the offset required to center the mesh, since FBX pivots are often not perfectly centered.
					glm::vec3 scale = sourceObj->GetTransform().GetScale();
					glm::vec3 euler = sourceObj->GetTransform().GetRotation();
					
					glm::mat4 rMat(1.0f);
					rMat = glm::rotate(rMat, glm::radians(euler.y), glm::vec3(0.0f, 1.0f, 0.0f));
					rMat = glm::rotate(rMat, glm::radians(euler.x), glm::vec3(1.0f, 0.0f, 0.0f));
					rMat = glm::rotate(rMat, glm::radians(euler.z), glm::vec3(0.0f, 0.0f, 1.0f));

					glm::vec3 localCenter, localRotSize;
					GetObjectLocalBoundsInfo(sourceObj, scale, euler, localCenter, localRotSize);
					
					// 1. Shift the object so its geometric center is precisely at (0,0,0) in the wrapper
					glm::vec3 pivotCorrection = -glm::vec3(rMat * glm::vec4(localCenter * scale, 1.0f));
					
					// 2. We want the X and Z axes centered, but we want the Y axis to rest on the floor.
					// Since the center is at 0, the bottom of the bounds is at -localRotSize.y / 2.
					// So we push it UP by localRotSize.y / 2 to perfectly align the bottom to Y=0!
					pivotCorrection.y += localRotSize.y * 0.5f;

					propObj->GetTransform().SetPosition(pivotCorrection);
					propObj->GetTransform().SetRotation(euler);
					propObj->GetTransform().SetScale(scale);
					propObj->SetDirty();

					// Recursively register all cloned GameObjects to the scene so they are rendered and cleaned up correctly!
					std::function<void(GameObject*)> registerRecursive = [&](GameObject* obj) {
						scene.AddObject(obj);
						ctx.Claim(obj->GetName());
						for (auto* child : obj->GetChildren()) {
							registerRecursive(child);
						}
					};
					registerRecursive(propObj);
				}
			}
		}
	}

	// Phase 4: Free CPU data
	interiors.clear();
	interiors.shrink_to_fit();

	progress(100.0f, "Interiors complete!");

	int totalRooms = 0;
	for (const auto& [k, m] : meshBuckets)
		totalRooms += m.GetTriangleCount();

	printf("[InteriorGenNode] Generated interiors for %d buildings -> %d material batches, %d total triangles\n",
		(int)plots.size(), batchCount, totalRooms);
	ctx.Info(std::to_string(plots.size()) + (plots.size() == 1 ? " building, " : " buildings, ") + std::to_string(roomCount) +
		" rooms, " + std::to_string(propId) + " props");
}

