#include "InteriorGenNode.h"
#include "InteriorDecorators.h"
#include "GameObject.h"
#include "Mesh.h"
#include "Material.h"
#include "Texture.h"
#include "TextureLayer.h"
#include "PrimitiveGenerator.h"
#include "AssetManager.h"

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

InteriorGenNode::InteriorGenNode(NodeGraph& graph)
{
	id = graph.NextNodeId();
	title = "Interior Gen";

	Pin plotsIn(graph.NextPinId(), PinDataType::TransformList, "Plots");
	inputs.push_back(plotsIn);

	Pin bedIn(graph.NextPinId(), PinDataType::Mesh, "Bed Model");
	Pin deskIn(graph.NextPinId(), PinDataType::Mesh, "Desk Model");
	Pin tvIn(graph.NextPinId(), PinDataType::Mesh, "TV Model");
	Pin stoveIn(graph.NextPinId(), PinDataType::Mesh, "Stove Model");
	Pin fridgeIn(graph.NextPinId(), PinDataType::Mesh, "Fridge Model");
	Pin sinkIn(graph.NextPinId(), PinDataType::Mesh, "Sink Model");
	inputs.push_back(bedIn);
	inputs.push_back(deskIn);
	inputs.push_back(tvIn);
	inputs.push_back(stoveIn);
	inputs.push_back(fridgeIn);
	inputs.push_back(sinkIn);
}

// =====================================================================
// UI
// =====================================================================

void InteriorGenNode::RenderContent(SceneManager* scene)
{
	ImGui::Text("Interior Generation");
	ImGui::Separator();

	ImGui::DragFloat("Floor Height", &floorHeight, 0.1f, 1.5f, 6.0f, "%.1f");
	ImGui::DragFloat("Wall Thickness", &wallThickness, 0.01f, 0.05f, 0.5f, "%.2f");
	ImGui::DragFloat("Min Room Area", &minRoomArea, 0.5f, 2.0f, 30.0f, "%.1f");
	ImGui::DragFloat("Door Width", &doorWidth, 0.05f, 0.6f, 2.0f, "%.2f");
	ImGui::DragFloat("Hallway Width", &hallwayWidth, 0.1f, 1.0f, 5.0f, "%.1f");
	ImGui::DragFloat("Wall Inset", &wallInset, 0.1f, 0.0f, 5.0f, "%.1f");
	ImGui::DragInt("Seed", &seed, 1, 0, 9999);
	ImGui::Checkbox("Generate Furniture", &generateFurniture);
	ImGui::Checkbox("Draw Interior Walls", &generateWalls);
	ImGui::Checkbox("Draw Ceilings", &generateCeiling);

	ImGui::Separator();
	ImGui::Text("Room Layout Configuration");
	ImGui::DragInt("Bedrooms", &numBedrooms, 0.1f, 0, 10);
	ImGui::DragInt("Kitchens", &numKitchens, 0.1f, 0, 5);
	ImGui::DragInt("Bathrooms", &numBathrooms, 0.1f, 0, 5);
}

// =====================================================================
// Serialization
// =====================================================================

json InteriorGenNode::Serialize() const
{
	json j = GraphNode::Serialize();
	j["floorHeight"]       = floorHeight;
	j["wallThickness"]     = wallThickness;
	j["floorThick"]        = floorThick;
	j["minRoomArea"]       = minRoomArea;
	j["doorWidth"]         = doorWidth;
	j["doorHeight"]        = doorHeight;
	j["hallwayWidth"]      = hallwayWidth;
	j["wallInset"]         = wallInset;
	j["seed"]              = seed;
	j["generateFurniture"] = generateFurniture;
	j["numBedrooms"]       = numBedrooms;
	j["numKitchens"]       = numKitchens;
	j["numBathrooms"]      = numBathrooms;
	j["generateWalls"]     = generateWalls;
	j["generateCeiling"]   = generateCeiling;
	return j;
}

void InteriorGenNode::Deserialize(const json& j)
{
	GraphNode::Deserialize(j);
	floorHeight       = j.value("floorHeight", 3.0f);
	wallThickness     = j.value("wallThickness", 0.15f);
	floorThick        = j.value("floorThick", 0.1f);
	minRoomArea       = j.value("minRoomArea", 6.0f);
	doorWidth         = j.value("doorWidth", 1.0f);
	doorHeight        = j.value("doorHeight", 2.4f);
	hallwayWidth      = j.value("hallwayWidth", 2.0f);
	wallInset         = j.value("wallInset", 0.5f);
	seed              = j.value("seed", 42);
	generateFurniture = j.value("generateFurniture", false);
	numBedrooms       = j.value("numBedrooms", 2);
	numKitchens       = j.value("numKitchens", 1);
	numBathrooms      = j.value("numBathrooms", 1);
	generateWalls     = j.value("generateWalls", true);
	generateCeiling   = j.value("generateCeiling", false);
}

// =====================================================================
// MakeWallBox — UV-scaled cube primitive (same pattern as BuildingGenNode)
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
// SubdivideFloor — Recursive BSP-style room splitting
// =====================================================================

void InteriorGenNode::SubdivideFloor(
	BuildingInterior& interior,
	glm::vec3 floorMin, glm::vec3 floorMax,
	int floorIndex, bool isCommercial,
	std::mt19937& rng,
	int targetRooms) const
{
	float width  = floorMax.x - floorMin.x;
	float depth  = floorMax.z - floorMin.z;
	float area   = width * depth;

	// Base case: we reached the exact number of rooms requested for this partition
	if (targetRooms <= 1 || (width < 2.5f && depth < 2.5f))
	{
		InteriorRoom room;
		room.minBounds = floorMin;
		room.maxBounds = floorMax;
		room.floorIndex = floorIndex;

		// Check if this room touches the building exterior (for window assignment)
		float eps = wallThickness * 2.0f;
		room.hasExteriorWindow =
			(std::abs(floorMin.x - interior.footprintMin.x) < eps) ||
			(std::abs(floorMax.x - interior.footprintMax.x) < eps) ||
			(std::abs(floorMin.z - interior.footprintMin.z) < eps) ||
			(std::abs(floorMax.z - interior.footprintMax.z) < eps);

		interior.rooms.push_back(room);
		return;
	}

	// Split along the longer dimension
	bool splitX = (width > depth);
	std::uniform_real_distribution<float> splitDist(0.35f, 0.65f);
	float splitFrac = splitDist(rng);

	// Proportionally distribute the target rooms to the two halves based on area split
	int leftRooms = std::max(1, (int)std::round(targetRooms * splitFrac));
	int rightRooms = std::max(1, targetRooms - leftRooms);

	// Edge case balancing
	if (leftRooms + rightRooms != targetRooms) {
		rightRooms = targetRooms - leftRooms;
	}
	if (rightRooms < 1 && targetRooms > 1) {
		rightRooms = 1;
		leftRooms = targetRooms - 1;
	}
	if (leftRooms < 1 && targetRooms > 1) {
		leftRooms = 1;
		rightRooms = targetRooms - 1;
	}

	if (splitX)
	{
		float splitPos = floorMin.x + width * splitFrac;

		// Create the wall
		InteriorWall wall;
		wall.minBounds = glm::vec3(splitPos - wallThickness * 0.5f, floorMin.y, floorMin.z);
		wall.maxBounds = glm::vec3(splitPos + wallThickness * 0.5f, floorMax.y, floorMax.z);
		interior.walls.push_back(wall);

		// Recurse into both halves
		glm::vec3 leftMax = glm::vec3(splitPos - wallThickness * 0.5f, floorMax.y, floorMax.z);
		glm::vec3 rightMin = glm::vec3(splitPos + wallThickness * 0.5f, floorMin.y, floorMin.z);

		SubdivideFloor(interior, floorMin, leftMax, floorIndex, isCommercial, rng, leftRooms);
		SubdivideFloor(interior, rightMin, floorMax, floorIndex, isCommercial, rng, rightRooms);
	}
	else
	{
		float splitPos = floorMin.z + depth * splitFrac;

		InteriorWall wall;
		wall.minBounds = glm::vec3(floorMin.x, floorMin.y, splitPos - wallThickness * 0.5f);
		wall.maxBounds = glm::vec3(floorMax.x, floorMax.y, splitPos + wallThickness * 0.5f);
		interior.walls.push_back(wall);

		glm::vec3 frontMax = glm::vec3(floorMax.x, floorMax.y, splitPos - wallThickness * 0.5f);
		glm::vec3 backMin = glm::vec3(floorMin.x, floorMin.y, splitPos + wallThickness * 0.5f);

		SubdivideFloor(interior, floorMin, frontMax, floorIndex, isCommercial, rng, leftRooms);
		SubdivideFloor(interior, backMin, floorMax, floorIndex, isCommercial, rng, rightRooms);
	}
}

// =====================================================================
// AssignRoomTypes — Classify rooms based on size, position, adjacency
// =====================================================================

void InteriorGenNode::AssignRoomTypes(
	BuildingInterior& interior,
	bool isCommercial,
	std::mt19937& rng) const
{
	if (interior.rooms.empty()) return;

	// Reset all room types to Corridor initially
	for (auto& room : interior.rooms)
	{
		room.type = RoomType::Corridor;
	}

	// 1. Assign Lobby to the largest room (or first floor room)
	// Sort rooms by area descending to find the largest room
	std::vector<InteriorRoom*> sortedRooms;
	for (auto& room : interior.rooms)
	{
		sortedRooms.push_back(&room);
	}

	std::sort(sortedRooms.begin(), sortedRooms.end(), [](const InteriorRoom* a, const InteriorRoom* b) {
		return a->GetArea() > b->GetArea();
	});

	// The largest room gets assigned as Lobby/Livingroom
	if (!sortedRooms.empty())
	{
		sortedRooms[0]->type = RoomType::Lobby;
	}

	// We will allocate remaining rooms based on user preferences.
	std::vector<InteriorRoom*> remainingRooms;
	for (size_t i = 1; i < sortedRooms.size(); i++)
	{
		remainingRooms.push_back(sortedRooms[i]);
	}

	// If it's commercial, everything just becomes Office
	if (isCommercial)
	{
		for (auto* room : remainingRooms)
		{
			room->type = RoomType::Office;
		}
		return;
	}

	// 2. Assign Bathrooms: bathrooms are usually the smallest remaining rooms!
	std::sort(remainingRooms.begin(), remainingRooms.end(), [](const InteriorRoom* a, const InteriorRoom* b) {
		return a->GetArea() < b->GetArea();
	});

	int assignedBathrooms = 0;
	size_t index = 0;
	while (assignedBathrooms < numBathrooms && index < remainingRooms.size())
	{
		remainingRooms[index]->type = RoomType::Bathroom;
		assignedBathrooms++;
		index++;
	}

	// Remove assigned bathrooms from remaining list
	remainingRooms.erase(remainingRooms.begin(), remainingRooms.begin() + assignedBathrooms);

	// 3. Assign Kitchens: kitchens are medium-large, so let's sort remaining rooms descending (largest first)
	std::sort(remainingRooms.begin(), remainingRooms.end(), [](const InteriorRoom* a, const InteriorRoom* b) {
		return a->GetArea() > b->GetArea();
	});

	int assignedKitchens = 0;
	index = 0;
	while (assignedKitchens < numKitchens && index < remainingRooms.size())
	{
		remainingRooms[index]->type = RoomType::Kitchen;
		assignedKitchens++;
		index++;
	}

	// Remove assigned kitchens from remaining list
	remainingRooms.erase(remainingRooms.begin(), remainingRooms.begin() + assignedKitchens);

	// 4. Assign Bedrooms: up to numBedrooms
	int assignedBedrooms = 0;
	index = 0;
	while (assignedBedrooms < numBedrooms && index < remainingRooms.size())
	{
		remainingRooms[index]->type = RoomType::Bedroom;
		assignedBedrooms++;
		index++;
	}

	// Remove assigned bedrooms
	remainingRooms.erase(remainingRooms.begin(), remainingRooms.begin() + assignedBedrooms);

	// 5. Remaining rooms: any leftovers become beautiful corridors or study offices!
	std::uniform_real_distribution<float> prob(0.0f, 1.0f);
	for (auto* room : remainingRooms)
	{
		float r = prob(rng);
		if (r < 0.4f) room->type = RoomType::Closet;
		else if (r < 0.7f) room->type = RoomType::Office;
		else room->type = RoomType::Corridor;
	}
}

// =====================================================================
// PlaceDoors — Insert doors between adjacent rooms
// =====================================================================

void InteriorGenNode::PlaceDoors(
	BuildingInterior& interior,
	std::mt19937& rng) const
{
	std::uniform_real_distribution<float> posDist(0.3f, 0.7f);

	for (const auto& wall : interior.walls)
	{
		float wallW = wall.maxBounds.x - wall.minBounds.x;
		float wallD = wall.maxBounds.z - wall.minBounds.z;

		bool thinInX = (wallW < wallD * 0.5f); // wall is thin in X → runs along Z

		InteriorDoor door;
		door.width = doorWidth;
		door.height = doorHeight;
		door.runsAlongX = !thinInX;

		float t = posDist(rng);

		if (thinInX)
		{
			// Wall runs along Z, door opening spans Z
			float doorZ = wall.minBounds.z + (wallD - doorWidth) * t + doorWidth * 0.5f;
			door.position = glm::vec3(
				(wall.minBounds.x + wall.maxBounds.x) * 0.5f,
				wall.minBounds.y,
				doorZ
			);
		}
		else
		{
			// Wall runs along X, door opening spans X
			float doorX = wall.minBounds.x + (wallW - doorWidth) * t + doorWidth * 0.5f;
			door.position = glm::vec3(
				doorX,
				wall.minBounds.y,
				(wall.minBounds.z + wall.maxBounds.z) * 0.5f
			);
		}

		// Basic hinge-swing collision check: alternate hinge sides
		door.hingeOnLeft = (interior.doors.size() % 2 == 0);
		door.isOpen = true;

		interior.doors.push_back(door);
	}
}

// =====================================================================
// GenerateBuildingInterior — Full pipeline for one building
// =====================================================================

BuildingInterior InteriorGenNode::GenerateBuildingInterior(
	const TransformData& plot, std::mt19937& rng) const
{
	BuildingInterior interior;

	float plotW = plot.scale.x;
	float plotD = plot.scale.z;
	bool isCommercial = (plot.scale.y > 1.5f);

	float currentInset = isCommercial ? wallInset : std::max(wallInset, 4.0f);
	float bW = (plotW - currentInset * 2.0f) * 0.5f;
	float bD = (plotD - currentInset * 2.0f) * 0.5f;

	if (bW < 1.5f || bD < 1.5f) return interior; // too small

	interior.isCommercial = isCommercial;
	interior.floorHeight = floorHeight;
	interior.wallThickness = wallThickness;
	interior.floorThickness = floorThick;

	// Building footprint in world space
	interior.footprintMin = glm::vec3(plot.position.x - bW, plot.position.y, plot.position.z - bD);
	interior.footprintMax = glm::vec3(plot.position.x + bW, 0.0f, plot.position.z + bD);

	// Determine number of floors (Locked to exactly 1 floor as requested)
	interior.numFloors = 1;
	interior.footprintMax.y = plot.position.y + interior.numFloors * floorHeight;

	// Subdivide each floor
	for (int f = 0; f < interior.numFloors; f++)
	{
		float floorY = plot.position.y + f * floorHeight + floorThick;
		float ceilY  = plot.position.y + (f + 1) * floorHeight;

		glm::vec3 floorMin(interior.footprintMin.x + wallThickness,
		                   floorY,
		                   interior.footprintMin.z + wallThickness);
		glm::vec3 floorMax(interior.footprintMax.x - wallThickness,
		                   ceilY,
		                   interior.footprintMax.z - wallThickness);

		int totalRequested = 1 + numBedrooms + numKitchens + numBathrooms;
		SubdivideFloor(interior, floorMin, floorMax, f, isCommercial, rng, totalRequested);
	}

	// Assign room functions
	AssignRoomTypes(interior, isCommercial, rng);

	// Place doors in walls
	PlaceDoors(interior, rng);

	return interior;
}

// =====================================================================
// BuildStructuralMesh — Generate floor/ceiling/wall geometry
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
			glm::vec3 center = (wall.minBounds + wall.maxBounds) * 0.5f;
			glm::vec3 half = (wall.maxBounds - wall.minBounds) * 0.5f;
			meshBuckets[MAT_DRYWALL].Append(MakeWallBox(plotMat, center, half, 2.0f));
		}

		// Outside walls (Perimeter NSEW)
		float wThick = interior.wallThickness;
		float minX = interior.footprintMin.x;
		float maxX = interior.footprintMax.x;
		float minZ = interior.footprintMin.z;
		float maxZ = interior.footprintMax.z;

		for (int f = 0; f < interior.numFloors; f++)
		{
			float floorY = interior.footprintMin.y + f * interior.floorHeight + interior.floorThickness;
			float ceilY  = interior.footprintMin.y + (f + 1) * interior.floorHeight;
			float wallH  = (ceilY - floorY) * 0.5f;
			float centerY = (floorY + ceilY) * 0.5f;

			// North Wall (at maxZ)
			meshBuckets[MAT_DRYWALL].Append(MakeWallBox(plotMat, 
				glm::vec3((minX + maxX) * 0.5f, centerY, maxZ - wThick * 0.5f),
				glm::vec3((maxX - minX) * 0.5f, wallH, wThick * 0.5f),
				2.0f));

			// South Wall (at minZ)
			meshBuckets[MAT_DRYWALL].Append(MakeWallBox(plotMat, 
				glm::vec3((minX + maxX) * 0.5f, centerY, minZ + wThick * 0.5f),
				glm::vec3((maxX - minX) * 0.5f, wallH, wThick * 0.5f),
				2.0f));

			// East Wall (at maxX)
			meshBuckets[MAT_DRYWALL].Append(MakeWallBox(plotMat, 
				glm::vec3(maxX - wThick * 0.5f, centerY, (minZ + maxZ) * 0.5f),
				glm::vec3(wThick * 0.5f, wallH, (maxZ - minZ) * 0.5f),
				2.0f));

			// West Wall (at minX)
			meshBuckets[MAT_DRYWALL].Append(MakeWallBox(plotMat, 
				glm::vec3(minX + wThick * 0.5f, centerY, (minZ + maxZ) * 0.5f),
				glm::vec3(wThick * 0.5f, wallH, (maxZ - minZ) * 0.5f),
				2.0f));
		}
	}
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
	obj->GetWorldBounds(minB, maxB);
	
	// Restore transform
	obj->SetParent(oldParent);
	obj->GetTransform().SetPosition(oldPos);
	obj->GetTransform().SetRotation(oldRot);
	obj->GetTransform().SetScale(oldScl);
	obj->SetDirty();
	
	if (minB.x > 1e9f || maxB.x < -1e9f) {
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
	
	// Query the recursive world bounds which include all children and relative transforms
	glm::vec3 minB, maxB;
	obj->GetWorldBounds(minB, maxB);

	// Fallback check in case the object has invalid/infinite bounds
	if (minB.x > 1e9f || maxB.x < -1e9f) {
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
// Execute — Main entry point
// =====================================================================

void InteriorGenNode::Execute(SceneManager& scene, NodeProgressCallback progress)
{
	if (progress) progress(0.0f, "Reading plot data...");

	// Retrieve modular bedroom mesh inputs from pins
	GameObject* bedSrcObj = (inputs.size() > 1 && inputs[1].data.type == PinDataType::Mesh) ? inputs[1].data.sourceObject : nullptr;
	GameObject* deskSrcObj = (inputs.size() > 2 && inputs[2].data.type == PinDataType::Mesh) ? inputs[2].data.sourceObject : nullptr;
	GameObject* tvSrcObj = (inputs.size() > 3 && inputs[3].data.type == PinDataType::Mesh) ? inputs[3].data.sourceObject : nullptr;
	GameObject* stoveSrcObj = (inputs.size() > 4 && inputs[4].data.type == PinDataType::Mesh) ? inputs[4].data.sourceObject : nullptr;
	GameObject* fridgeSrcObj = (inputs.size() > 5 && inputs[5].data.type == PinDataType::Mesh) ? inputs[5].data.sourceObject : nullptr;
	GameObject* sinkSrcObj = (inputs.size() > 6 && inputs[6].data.type == PinDataType::Mesh) ? inputs[6].data.sourceObject : nullptr;

	glm::vec3 bedSize = GetObjectAABBSize(bedSrcObj, glm::vec3(1.6f, 0.8f, 2.0f));
	glm::vec3 deskSize = GetObjectAABBSize(deskSrcObj, glm::vec3(1.2f, 0.75f, 0.6f));
	glm::vec3 tvSize = GetObjectAABBSize(tvSrcObj, glm::vec3(0.9f, 0.6f, 0.2f));
	glm::vec3 stoveSize = GetObjectAABBSize(stoveSrcObj, glm::vec3(0.8f, 0.9f, 0.6f));
	glm::vec3 fridgeSize = GetObjectAABBSize(fridgeSrcObj, glm::vec3(0.8f, 1.8f, 0.7f));
	glm::vec3 sinkSize = GetObjectAABBSize(sinkSrcObj, glm::vec3(0.9f, 0.9f, 0.6f));

	if (bedSrcObj) printf("[InteriorGenNode] Connected Bed: %s (Size: %.2f x %.2f x %.2f)\n", bedSrcObj->GetName().c_str(), bedSize.x, bedSize.y, bedSize.z);
	if (deskSrcObj) printf("[InteriorGenNode] Connected Desk: %s (Size: %.2f x %.2f x %.2f)\n", deskSrcObj->GetName().c_str(), deskSize.x, deskSize.y, deskSize.z);
	if (tvSrcObj) printf("[InteriorGenNode] Connected TV: %s (Size: %.2f x %.2f x %.2f)\n", tvSrcObj->GetName().c_str(), tvSize.x, tvSize.y, tvSize.z);
	if (stoveSrcObj) printf("[InteriorGenNode] Connected Stove: %s (Size: %.2f x %.2f x %.2f)\n", stoveSrcObj->GetName().c_str(), stoveSize.x, stoveSize.y, stoveSize.z);
	if (fridgeSrcObj) printf("[InteriorGenNode] Connected Fridge: %s (Size: %.2f x %.2f x %.2f)\n", fridgeSrcObj->GetName().c_str(), fridgeSize.x, fridgeSize.y, fridgeSize.z);
	if (sinkSrcObj) printf("[InteriorGenNode] Connected Sink: %s (Size: %.2f x %.2f x %.2f)\n", sinkSrcObj->GetName().c_str(), sinkSize.x, sinkSize.y, sinkSize.z);

	// Write debugging information to a file in the workspace
	{
		std::ofstream f("C:\\Users\\Justin\\Desktop\\Licenta-Completa\\debug_interior.txt", std::ios::trunc);
		if (f.is_open()) {
			f << "=== Interior Gen Debug ===\n";
			if (bedSrcObj) {
				f << "Bed: " << bedSrcObj->GetName() << "\n";
				glm::vec3 s = bedSrcObj->GetTransform().GetScale();
				glm::vec3 r = bedSrcObj->GetTransform().GetRotation();
				glm::vec3 p = bedSrcObj->GetTransform().GetPosition();
				f << "  Source Transform: Pos(" << p.x << ", " << p.y << ", " << p.z 
				  << ") Rot(" << r.x << ", " << r.y << ", " << r.z 
				  << ") Scl(" << s.x << ", " << s.y << ", " << s.z << ")\n";
				f << "  Calculated AABB Size: " << bedSize.x << " x " << bedSize.y << " x " << bedSize.z << "\n";
			} else {
				f << "Bed: None connected\n";
			}
			if (deskSrcObj) {
				f << "Desk: " << deskSrcObj->GetName() << "\n";
				f << "  Calculated AABB Size: " << deskSize.x << " x " << deskSize.y << " x " << deskSize.z << "\n";
			}
			f.close();
		}
	}

	TransformList plots;
	if (inputs.empty() || inputs[0].data.type != PinDataType::TransformList || inputs[0].data.transforms.empty())
	{
		TransformData defaultPlot;
		defaultPlot.position = glm::vec3(0.0f, 0.0f, 0.0f);
		defaultPlot.rotation = glm::vec3(0.0f);
		defaultPlot.scale = glm::vec3(singleWidth, 1.0f, singleDepth);
		plots.push_back(defaultPlot);
	}
	else
	{
		plots = inputs[0].data.transforms;
	}

	if (progress) progress(5.0f, "Generating interiors...");

	// Phase 1: Generate interior data for all buildings (multi-threaded)
	std::vector<BuildingInterior> interiors(plots.size());
	int numThreads = std::thread::hardware_concurrency();
	if (numThreads == 0) numThreads = 8;

	std::vector<std::thread> threads;
	size_t chunkSize = (plots.size() + numThreads - 1) / numThreads;

	for (int t = 0; t < numThreads; t++)
	{
		size_t startIdx = t * chunkSize;
		size_t endIdx = std::min(startIdx + chunkSize, plots.size());
		if (startIdx >= plots.size()) break;

		threads.emplace_back([this, startIdx, endIdx, &plots, &interiors]() {
			for (size_t i = startIdx; i < endIdx; i++)
			{
				std::mt19937 localRng(seed + (int)i + 7919); // different seed offset than BuildingGenNode
				interiors[i] = GenerateBuildingInterior(plots[i], localRng);
			}
		});
	}

	for (auto& t : threads) t.join();

	if (progress) progress(40.0f, "Building structural meshes...");

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
		if (generateFurniture || bedSrcObj || deskSrcObj || tvSrcObj || stoveSrcObj || fridgeSrcObj || sinkSrcObj)
		{
			std::mt19937 decorRng(seed + (int)i + 13337);
			for (const auto& room : interior.rooms)
			{
				auto decorator = CreateDecoratorForRoom(room.type);
				if (decorator)
				{
					decorator->Decorate(meshBuckets, interior.props, room, decorRng, floorHeight, bedSize, deskSize, tvSize, stoveSize, fridgeSize, sinkSize);
				}
			}
		}

		if (progress)
		{
			float pct = 40.0f + 40.0f * ((float)(i + 1) / interiors.size());
			progress(pct, "Processing building " + std::to_string(i + 1) + "/" + std::to_string(interiors.size()));
		}
	}

	if (progress) progress(85.0f, "Uploading to GPU...");

	// Phase 3: Upload batched meshes as GameObjects
	std::string prefix = "Interior_" + std::to_string(id) + "_";
	std::string rootName = "City_Interiors_" + std::to_string(id);

	GameObject* root = scene.FindObject(rootName);
	if (!root)
	{
		root = new GameObject(rootName);
		root->SetSaveInScene(false);
		scene.AddObject(root);
	}
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

	std::map<int, Texture*> texCache;
	int batchCount = 0;

	for (auto& [matKey, mergedMesh] : meshBuckets)
	{
		if (mergedMesh.GetVertexCount() == 0) continue;

		std::string batchName = prefix + GetInteriorBatchName(matKey);
		GameObject* batchObj = scene.FindObject(batchName);
		if (!batchObj)
		{
			batchObj = new GameObject(batchName);
			scene.AddObject(batchObj);
		}

		batchObj->GetTransform().SetPosition(glm::vec3(0.0f));
		batchObj->GetTransform().SetRotation(glm::vec3(0.0f));
		batchObj->GetTransform().SetScale(glm::vec3(1.0f));
		batchObj->SetParent(root);
		batchObj->SetMesh(mergedMesh.ToMesh());
		batchObj->SetCPUMeshData(mergedMesh);

		// Texture
		while (batchObj->GetTextureLayers().size() > 0) batchObj->RemoveTextureLayer(0);

		const char* texPath = GetInteriorTexture(matKey);
		if (texCache.find(matKey) == texCache.end())
		{
			texCache[matKey] = new Texture(texPath);
			texCache[matKey]->LoadTexture();
		}

		TextureLayer layer;
		layer.texturePath = texPath;
		layer.texture = texCache[matKey];
		layer.blendMode = LayerBlendMode::Normal;
		layer.opacity = 1.0f;
		layer.tiling = GetInteriorTiling(matKey);
		batchObj->AddTextureLayer(layer);

		batchCount++;
	}

	// Phase 3b: Instantiate high-fidelity props as GameObjects
	if (generateFurniture || bedSrcObj || deskSrcObj || tvSrcObj || stoveSrcObj || fridgeSrcObj || sinkSrcObj)
	{
		int propId = 0;
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

			for (const auto& prop : interior.props)
			{
				GameObject* sourceObj = nullptr;
				if (prop.category == "bed") sourceObj = bedSrcObj;
				else if (prop.category == "desk") sourceObj = deskSrcObj;
				else if (prop.category == "tv") sourceObj = tvSrcObj;
				else if (prop.category == "stove") sourceObj = stoveSrcObj;
				else if (prop.category == "fridge") sourceObj = fridgeSrcObj;
				else if (prop.category == "sink") sourceObj = sinkSrcObj;

				if (sourceObj)
				{
					// 1. Create a placement wrapper GameObject
					std::string wrapName = prefix + prop.category + "_" + std::to_string(propId++) + "_placement";
					GameObject* wrapObj = new GameObject(wrapName);
					
					// Set its transform to the clean procedural placement parameters
					wrapObj->GetTransform().SetPosition(prop.position);
					wrapObj->GetTransform().SetRotation(prop.rotation);
					wrapObj->GetTransform().SetScale(prop.scale);
					
					// Parent placement wrapper to building root!
					wrapObj->SetParent(bldRoot);
					scene.AddObject(wrapObj);

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

	if (progress) progress(100.0f, "Interiors complete!");

	int totalRooms = 0;
	for (const auto& [k, m] : meshBuckets)
		totalRooms += m.GetTriangleCount();

	printf("[InteriorGenNode] Generated interiors for %d buildings -> %d material batches, %d total triangles\n",
		(int)plots.size(), batchCount, totalRooms);
}
