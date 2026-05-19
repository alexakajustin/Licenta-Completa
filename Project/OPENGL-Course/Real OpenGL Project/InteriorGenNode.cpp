#include "InteriorGenNode.h"
#include "InteriorDecorators.h"
#include "GameObject.h"
#include "Mesh.h"
#include "Material.h"
#include "Texture.h"
#include "TextureLayer.h"
#include "PrimitiveGenerator.h"
#include "AssetManager.h"
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
	Pin toiletIn(graph.NextPinId(), PinDataType::Mesh, "Toilet Model");
	Pin bathtubIn(graph.NextPinId(), PinDataType::Mesh, "Bathtub Model");
	Pin sofaIn(graph.NextPinId(), PinDataType::Mesh, "Sofa Model");
	Pin coffeeTableIn(graph.NextPinId(), PinDataType::Mesh, "Coffee Table Model");
	Pin tvStandIn(graph.NextPinId(), PinDataType::Mesh, "TV Stand Model");
	inputs.push_back(bedIn);
	inputs.push_back(deskIn);
	inputs.push_back(tvIn);
	inputs.push_back(stoveIn);
	inputs.push_back(fridgeIn);
	inputs.push_back(sinkIn);
	inputs.push_back(toiletIn);
	inputs.push_back(bathtubIn);
	inputs.push_back(sofaIn);
	inputs.push_back(coffeeTableIn);
	inputs.push_back(tvStandIn);
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
	ImGui::DragFloat("Door Width", &doorWidth, 0.05f, 0.6f, 2.0f, "%.2f");
	ImGui::DragFloat("Door Height", &doorHeight, 0.05f, 1.5f, 3.5f, "%.2f");
	ImGui::DragFloat("Hallway Width", &hallwayWidth, 0.1f, 1.0f, 5.0f, "%.1f");
	ImGui::DragFloat("Wall Inset", &wallInset, 0.1f, 0.0f, 5.0f, "%.1f");
	ImGui::DragInt("Seed", &seed, 1, 0, 9999);
	ImGui::Checkbox("Generate Furniture", &generateFurniture);
	ImGui::Checkbox("Draw Interior Walls", &generateWalls);
	ImGui::Checkbox("Draw Ceilings", &generateCeiling);

	ImGui::Separator();
	ImGui::Text("Room Layout Configuration");
	ImGui::DragInt("Target Rooms", &numRooms, 0.1f, 1, 15);
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
	j["numRooms"]          = numRooms;
	j["minRoomSize"]       = minRoomSize;
	j["maxRoomSize"]       = maxRoomSize;
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
	doorWidth         = j.value("doorWidth", 0.8f);
	doorHeight        = j.value("doorHeight", 2.1f);
	hallwayWidth      = j.value("hallwayWidth", 2.0f);
	wallInset         = j.value("wallInset", 0.5f);
	seed              = j.value("seed", 42);
	generateFurniture = j.value("generateFurniture", false);
	numRooms          = j.value("numRooms", 4);
	minRoomSize       = j.value("minRoomSize", 3.0f);
	maxRoomSize       = j.value("maxRoomSize", 6.0f);
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
	// Helper to add room with exterior window flag check
	auto AddRoomDirect = [&](glm::vec3 minB, glm::vec3 maxB, RoomType t) {
		InteriorRoom r;
		r.minBounds = minB;
		r.maxBounds = maxB;
		r.type = t;
		r.floorIndex = floorIndex;

		float eps = wallThickness * 2.0f;
		r.hasExteriorWindow =
			(std::abs(minB.x - interior.footprintMin.x) < eps) ||
			(std::abs(maxB.x - interior.footprintMax.x) < eps) ||
			(std::abs(minB.z - interior.footprintMin.z) < eps) ||
			(std::abs(maxB.z - interior.footprintMax.z) < eps);
		interior.rooms.push_back(r);
	};

	// Helper to add wall
	auto AddWallDirect = [&](glm::vec3 minB, glm::vec3 maxB) {
		InteriorWall w;
		w.minBounds = minB;
		w.maxBounds = maxB;
		interior.walls.push_back(w);
	};

	struct SplitRoom {
		glm::vec3 minB;
		glm::vec3 maxB;
		bool isHallway = false;
	};

	std::vector<SplitRoom> to_split;
	to_split.push_back({ floorMin, floorMax, false });
	std::vector<SplitRoom> finalRooms;
	bool hasCorridor = false;

	float hallWidth = isCommercial ? 2.2f : 1.8f;
	float minHallSize = minRoomSize * 2.0f + hallWidth + wallThickness * 2.0f;

	while (!to_split.empty())
	{
		// Find the largest room in the queue to split first (keeps room proportions balanced!)
		auto largestIt = to_split.begin();
		float maxArea = -1.0f;
		for (auto it = to_split.begin(); it != to_split.end(); ++it)
		{
			float area = (it->maxB.x - it->minB.x) * (it->maxB.z - it->minB.z);
			if (area > maxArea)
			{
				maxArea = area;
				largestIt = it;
			}
		}

		SplitRoom current = *largestIt;
		to_split.erase(largestIt);

		float w = current.maxB.x - current.minB.x;
		float d = current.maxB.z - current.minB.z;

		// Decide if we have enough space to split along X or Z
		bool canSplitX = (w >= minRoomSize * 2.0f + wallThickness);
		bool canSplitZ = (d >= minRoomSize * 2.0f + wallThickness);

		bool canSplit = (canSplitX || canSplitZ);
		// Split only if we have not reached the target rooms limit
		bool shouldSplit = ((int)(finalRooms.size() + to_split.size() + 1) < targetRooms);

		if (!canSplit || !shouldSplit)
		{
			finalRooms.push_back(current);
			continue;
		}

		// Decide split axis based on aspect ratio or pick randomly based on rng seed
		bool splitX = true;
		if (canSplitX && canSplitZ)
		{
			if (w > 1.25f * d) {
				splitX = true;
			} else if (d > 1.25f * w) {
				splitX = false;
			} else {
				std::uniform_int_distribution<int> axisDist(0, 1);
				splitX = (axisDist(rng) == 0);
			}
		}
		else
		{
			splitX = canSplitX;
		}

		// Check if we should do a hallway split
		bool makeHallway = false;
		float currentHallWidth = hallWidth;
		float useMinRoomSize = minRoomSize;
		if (!isCommercial && !hasCorridor && ((int)(finalRooms.size() + to_split.size() + 2) <= targetRooms))
		{
			float sizeVal = splitX ? w : d;
			float standardMinHallSize = minRoomSize * 2.0f + hallWidth + wallThickness * 2.0f;
			float compactMinHallSize = 2.2f * 2.0f + 1.2f + wallThickness * 2.0f;

			if (sizeVal >= standardMinHallSize)
			{
				makeHallway = true;
				currentHallWidth = hallWidth;
				useMinRoomSize = minRoomSize;
			}
			else if (sizeVal >= compactMinHallSize)
			{
				makeHallway = true;
				currentHallWidth = 1.2f;
				useMinRoomSize = 2.2f;
			}
		}

		// Decide random split position
		float minPos = splitX ? (current.minB.x + useMinRoomSize) : (current.minB.z + useMinRoomSize);
		float maxPos = splitX ? (current.maxB.x - useMinRoomSize) : (current.maxB.z - useMinRoomSize);

		// Adjust the random split range for hallway splits
		if (makeHallway)
		{
			minPos = splitX ? (current.minB.x + useMinRoomSize + currentHallWidth * 0.5f) : (current.minB.z + useMinRoomSize + currentHallWidth * 0.5f);
			maxPos = splitX ? (current.maxB.x - useMinRoomSize - currentHallWidth * 0.5f) : (current.maxB.z - useMinRoomSize - currentHallWidth * 0.5f);
		}

		if (minPos >= maxPos)
		{
			finalRooms.push_back(current);
			continue;
		}

		std::uniform_real_distribution<float> posDist(minPos, maxPos);
		float splitVal = posDist(rng);

		if (makeHallway)
		{
			hasCorridor = true;
			float hMin = splitVal - currentHallWidth * 0.5f;
			float hMax = splitVal + currentHallWidth * 0.5f;

			if (splitX)
			{
				AddWallDirect(glm::vec3(hMin - wallThickness * 0.5f, current.minB.y, current.minB.z),
							  glm::vec3(hMin + wallThickness * 0.5f, current.maxB.y, current.maxB.z));
				AddWallDirect(glm::vec3(hMax - wallThickness * 0.5f, current.minB.y, current.minB.z),
							  glm::vec3(hMax + wallThickness * 0.5f, current.maxB.y, current.maxB.z));

				to_split.push_back({ current.minB, glm::vec3(hMin - wallThickness * 0.5f, current.maxB.y, current.maxB.z), false });
				finalRooms.push_back({ glm::vec3(hMin + wallThickness * 0.5f, current.minB.y, current.minB.z),
									   glm::vec3(hMax - wallThickness * 0.5f, current.maxB.y, current.maxB.z), true });
				to_split.push_back({ glm::vec3(hMax + wallThickness * 0.5f, current.minB.y, current.minB.z), current.maxB, false });
			}
			else
			{
				AddWallDirect(glm::vec3(current.minB.x, current.minB.y, hMin - wallThickness * 0.5f),
							  glm::vec3(current.maxB.x, current.maxB.y, hMin + wallThickness * 0.5f));
				AddWallDirect(glm::vec3(current.minB.x, current.minB.y, hMax - wallThickness * 0.5f),
							  glm::vec3(current.maxB.x, current.maxB.y, hMax + wallThickness * 0.5f));

				to_split.push_back({ current.minB, glm::vec3(current.maxB.x, current.maxB.y, hMin - wallThickness * 0.5f), false });
				finalRooms.push_back({ glm::vec3(current.minB.x, current.minB.y, hMin + wallThickness * 0.5f),
									   glm::vec3(current.maxB.x, current.maxB.y, hMax - wallThickness * 0.5f), true });
				to_split.push_back({ glm::vec3(current.minB.x, current.minB.y, hMax + wallThickness * 0.5f), current.maxB, false });
			}
		}
		else
		{
			// Standard split: place one wall, creating two rooms
			if (splitX)
			{
				AddWallDirect(glm::vec3(splitVal - wallThickness * 0.5f, current.minB.y, current.minB.z),
							  glm::vec3(splitVal + wallThickness * 0.5f, current.maxB.y, current.maxB.z));

				to_split.push_back({ current.minB, glm::vec3(splitVal - wallThickness * 0.5f, current.maxB.y, current.maxB.z), false });
				to_split.push_back({ glm::vec3(splitVal + wallThickness * 0.5f, current.minB.y, current.minB.z), current.maxB, false });
			}
			else
			{
				AddWallDirect(glm::vec3(current.minB.x, current.minB.y, splitVal - wallThickness * 0.5f),
							  glm::vec3(current.maxB.x, current.maxB.y, splitVal + wallThickness * 0.5f));

				to_split.push_back({ current.minB, glm::vec3(current.maxB.x, current.maxB.y, splitVal - wallThickness * 0.5f), false });
				to_split.push_back({ glm::vec3(current.minB.x, current.minB.y, splitVal + wallThickness * 0.5f), current.maxB, false });
			}
		}
	}

	for (const auto& r : finalRooms)
	{
		AddRoomDirect(r.minB, r.maxB, r.isHallway ? RoomType::Corridor : RoomType::Office);
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

	// Gather only the assignable rooms (those created as RoomType::Office)
	std::vector<InteriorRoom*> sortedRooms;
	for (auto& room : interior.rooms)
	{
		if (room.type == RoomType::Office)
		{
			sortedRooms.push_back(&room);
		}
	}

	// If there are no assignable rooms, we're done
	if (sortedRooms.empty()) return;

	// If it's commercial, all assignable rooms remain RoomType::Office
	if (isCommercial)
	{
		return;
	}

	// Sort assignable rooms by area descending (largest first)
	std::sort(sortedRooms.begin(), sortedRooms.end(), [](const InteriorRoom* a, const InteriorRoom* b) {
		return a->GetArea() > b->GetArea();
	});

	if (sortedRooms.size() == 1)
	{
		sortedRooms[0]->type = RoomType::Lobby; // Studio: Living room/bedroom
	}
	else if (sortedRooms.size() == 2)
	{
		sortedRooms[0]->type = RoomType::Lobby;     // Living Room
		sortedRooms[1]->type = RoomType::Bathroom;  // Bathroom
	}
	else if (sortedRooms.size() == 3)
	{
		sortedRooms[0]->type = RoomType::Lobby;     // Living Room / Kitchen combo
		sortedRooms[1]->type = RoomType::Bedroom;   // Bedroom
		sortedRooms[2]->type = RoomType::Bathroom;  // Bathroom
	}
	else
	{
		// 4 or more rooms
		sortedRooms[0]->type = RoomType::Lobby;     // Living Room (largest)
		sortedRooms[1]->type = RoomType::Kitchen;   // Kitchen (second largest)

		// Smallest room(s) become Bathroom
		size_t numBathrooms = std::max<size_t>(1, sortedRooms.size() / 4);
		for (size_t i = 0; i < numBathrooms; i++)
		{
			size_t idx = sortedRooms.size() - 1 - i;
			sortedRooms[idx]->type = RoomType::Bathroom;
		}

		// Middle rooms become Bedroom or Office
		std::uniform_real_distribution<float> prob(0.0f, 1.0f);
		for (size_t i = 2; i < sortedRooms.size() - numBathrooms; i++)
		{
			if (prob(rng) < 0.75f)
			{
				sortedRooms[i]->type = RoomType::Bedroom;
			}
			else
			{
				sortedRooms[i]->type = RoomType::Office;
			}
		}
	}

	// Build adjacency list for topological validation
	size_t n = interior.rooms.size();
	std::vector<std::vector<size_t>> adj(n);
	float wThick = interior.wallThickness;
	for (size_t i = 0; i < n; i++)
	{
		for (size_t j = i + 1; j < n; j++)
		{
			const auto& r1 = interior.rooms[i];
			const auto& r2 = interior.rooms[j];

			float overlapX = std::min(r1.maxBounds.x, r2.maxBounds.x) - std::max(r1.minBounds.x, r2.minBounds.x);
			float overlapZ = std::min(r1.maxBounds.z, r2.maxBounds.z) - std::max(r1.minBounds.z, r2.minBounds.z);

			bool isAdjacent = false;
			if (overlapZ > 0.4f && (std::abs(r1.maxBounds.x - r2.minBounds.x) < wThick * 2.0f + 0.1f ||
								   std::abs(r1.minBounds.x - r2.maxBounds.x) < wThick * 2.0f + 0.1f)) {
				isAdjacent = true;
			}
			if (overlapX > 0.4f && (std::abs(r1.maxBounds.z - r2.minBounds.z) < wThick * 2.0f + 0.1f ||
								   std::abs(r1.minBounds.z - r2.maxBounds.z) < wThick * 2.0f + 0.1f)) {
				isAdjacent = true;
			}

			if (isAdjacent) {
				adj[i].push_back(j);
				adj[j].push_back(i);
			}
		}
	}

	// Validate private-to-public adjacency. If a private room (Bedroom, Office, Bathroom)
	// has NO neighbors of type Lobby, Corridor, or Kitchen (or in the case of a Bathroom, Bedroom for ensuite),
	// we change either it or one of its neighbors to a Corridor or Lobby to make it reachable without bedroom-to-bedroom doors.
	for (int pass = 0; pass < 3; pass++)
	{
		for (size_t i = 0; i < n; i++)
		{
			auto& room = interior.rooms[i];
			if (room.type == RoomType::Bedroom || room.type == RoomType::Office || room.type == RoomType::Bathroom)
			{
				bool hasPublicNeighbor = false;
				for (size_t neighborIdx : adj[i])
				{
					RoomType nt = interior.rooms[neighborIdx].type;
					if (nt == RoomType::Lobby || nt == RoomType::Corridor || nt == RoomType::Kitchen)
					{
						hasPublicNeighbor = true;
						break;
					}
					if (room.type == RoomType::Bathroom && nt == RoomType::Bedroom)
					{
						hasPublicNeighbor = true;
						break;
					}
				}

				if (!hasPublicNeighbor)
				{
					// Resolve the isolation: convert this room to a hallway (Corridor) or extension of the Lobby.
					if (room.GetArea() < 12.0f)
					{
						room.type = RoomType::Corridor;
					}
					else
					{
						room.type = RoomType::Lobby;
					}
				}
			}
		}
	}
}

// =====================================================================
// PlaceDoors — Insert doors between adjacent rooms
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

		int totalRequested = numRooms;
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
	GameObject* toiletSrcObj = (inputs.size() > 7 && inputs[7].data.type == PinDataType::Mesh) ? inputs[7].data.sourceObject : nullptr;
	GameObject* bathtubSrcObj = (inputs.size() > 8 && inputs[8].data.type == PinDataType::Mesh) ? inputs[8].data.sourceObject : nullptr;
	GameObject* sofaSrcObj = (inputs.size() > 9 && inputs[9].data.type == PinDataType::Mesh) ? inputs[9].data.sourceObject : nullptr;
	GameObject* coffeeTableSrcObj = (inputs.size() > 10 && inputs[10].data.type == PinDataType::Mesh) ? inputs[10].data.sourceObject : nullptr;
	GameObject* tvStandSrcObj = (inputs.size() > 11 && inputs[11].data.type == PinDataType::Mesh) ? inputs[11].data.sourceObject : nullptr;

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

	if (bedSrcObj) printf("[InteriorGenNode] Connected Bed: %s (Size: %.2f x %.2f x %.2f)\n", bedSrcObj->GetName().c_str(), bedSize.x, bedSize.y, bedSize.z);
	if (deskSrcObj) printf("[InteriorGenNode] Connected Desk: %s (Size: %.2f x %.2f x %.2f)\n", deskSrcObj->GetName().c_str(), deskSize.x, deskSize.y, deskSize.z);
	if (tvSrcObj) printf("[InteriorGenNode] Connected TV: %s (Size: %.2f x %.2f x %.2f)\n", tvSrcObj->GetName().c_str(), tvSize.x, tvSize.y, tvSize.z);
	if (stoveSrcObj) printf("[InteriorGenNode] Connected Stove: %s (Size: %.2f x %.2f x %.2f)\n", stoveSrcObj->GetName().c_str(), stoveSize.x, stoveSize.y, stoveSize.z);
	if (fridgeSrcObj) printf("[InteriorGenNode] Connected Fridge: %s (Size: %.2f x %.2f x %.2f)\n", fridgeSrcObj->GetName().c_str(), fridgeSize.x, fridgeSize.y, fridgeSize.z);
	if (sinkSrcObj) printf("[InteriorGenNode] Connected Sink: %s (Size: %.2f x %.2f x %.2f)\n", sinkSrcObj->GetName().c_str(), sinkSize.x, sinkSize.y, sinkSize.z);
	if (toiletSrcObj) printf("[InteriorGenNode] Connected Toilet: %s (Size: %.2f x %.2f x %.2f)\n", toiletSrcObj->GetName().c_str(), toiletSize.x, toiletSize.y, toiletSize.z);
	if (bathtubSrcObj) printf("[InteriorGenNode] Connected Bathtub: %s (Size: %.2f x %.2f x %.2f)\n", bathtubSrcObj->GetName().c_str(), bathtubSize.x, bathtubSize.y, bathtubSize.z);
	if (sofaSrcObj) printf("[InteriorGenNode] Connected Sofa: %s (Size: %.2f x %.2f x %.2f)\n", sofaSrcObj->GetName().c_str(), sofaSize.x, sofaSize.y, sofaSize.z);
	if (coffeeTableSrcObj) printf("[InteriorGenNode] Connected Coffee Table: %s (Size: %.2f x %.2f x %.2f)\n", coffeeTableSrcObj->GetName().c_str(), coffeeTableSize.x, coffeeTableSize.y, coffeeTableSize.z);
	if (tvStandSrcObj) printf("[InteriorGenNode] Connected TV Stand: %s (Size: %.2f x %.2f x %.2f)\n", tvStandSrcObj->GetName().c_str(), tvStandSize.x, tvStandSize.y, tvStandSize.z);

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
			if (sinkSrcObj) {
				f << "Sink: " << sinkSrcObj->GetName() << "\n";
				f << "  Calculated AABB Size: " << sinkSize.x << " x " << sinkSize.y << " x " << sinkSize.z << "\n";
			}
			if (toiletSrcObj) {
				f << "Toilet: " << toiletSrcObj->GetName() << "\n";
				f << "  Calculated AABB Size: " << toiletSize.x << " x " << toiletSize.y << " x " << toiletSize.z << "\n";
			}
			if (bathtubSrcObj) {
				f << "Bathtub: " << bathtubSrcObj->GetName() << "\n";
				f << "  Calculated AABB Size: " << bathtubSize.x << " x " << bathtubSize.y << " x " << bathtubSize.z << "\n";
			}
			if (sofaSrcObj) {
				f << "Sofa: " << sofaSrcObj->GetName() << "\n";
				f << "  Calculated AABB Size: " << sofaSize.x << " x " << sofaSize.y << " x " << sofaSize.z << "\n";
			}
			if (coffeeTableSrcObj) {
				f << "Coffee Table: " << coffeeTableSrcObj->GetName() << "\n";
				f << "  Calculated AABB Size: " << coffeeTableSize.x << " x " << coffeeTableSize.y << " x " << coffeeTableSize.z << "\n";
			}
			if (tvStandSrcObj) {
				f << "TV Stand: " << tvStandSrcObj->GetName() << "\n";
				f << "  Calculated AABB Size: " << tvStandSize.x << " x " << tvStandSize.y << " x " << tvStandSize.z << "\n";
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
		if (generateFurniture || bedSrcObj || deskSrcObj || tvSrcObj || stoveSrcObj || fridgeSrcObj || sinkSrcObj || toiletSrcObj || bathtubSrcObj || sofaSrcObj || coffeeTableSrcObj || tvStandSrcObj)
		{
			std::mt19937 decorRng(seed + (int)i + 13337);
			for (const auto& room : interior.rooms)
			{
				auto decorator = CreateDecoratorForRoom(room.type);
				if (decorator)
				{
					decorator->Decorate(meshBuckets, interior.props, room, decorRng, floorHeight, bedSize, deskSize, tvSize, stoveSize, fridgeSize, sinkSize, toiletSize, bathtubSize, sofaSize, coffeeTableSize, tvStandSize, interior.isCommercial);
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
	if (generateFurniture || bedSrcObj || deskSrcObj || tvSrcObj || stoveSrcObj || fridgeSrcObj || sinkSrcObj || toiletSrcObj || bathtubSrcObj || sofaSrcObj || coffeeTableSrcObj || tvStandSrcObj)
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
				else if (prop.category == "toilet") sourceObj = toiletSrcObj;
				else if (prop.category == "bathtub") sourceObj = bathtubSrcObj;
				else if (prop.category == "washing_machine") sourceObj = toiletSrcObj; // reuse bathroom input
				else if (prop.category == "cabinet") sourceObj = tvStandSrcObj ? tvStandSrcObj : (deskSrcObj ? deskSrcObj : sinkSrcObj); // reuse cabinet/desk input instead of sink
				else if (prop.category == "couch") sourceObj = sofaSrcObj;
				else if (prop.category == "coffee_table") sourceObj = coffeeTableSrcObj;
				else if (prop.category == "tv_stand") sourceObj = tvStandSrcObj;

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

