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
	ImGui::DragInt("Target Rooms", &numRooms, 0.1f, 1, 15);
	ImGui::DragFloat("Min Room Size", &minRoomSize, 0.05f, 1.5f, 10.0f, "%.2f");
	ImGui::DragFloat("Max Room Size", &maxRoomSize, 0.05f, 2.0f, 20.0f, "%.2f");
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
	doorWidth         = j.value("doorWidth", 1.0f);
	doorHeight        = j.value("doorHeight", 2.4f);
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
	float width  = floorMax.x - floorMin.x;
	float depth  = floorMax.z - floorMin.z;

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

	// Decide layout style randomly based on size and room count
	float hallWidth = isCommercial ? 2.2f : 1.8f;
	float minSizeWithHall = minRoomSize * 2.0f + hallWidth + wallThickness * 2.0f;

	bool canCorridor = (targetRooms >= 3 && (width >= minSizeWithHall || depth >= minSizeWithHall));
	bool canWhirl = (!isCommercial && targetRooms >= 5 && width >= minRoomSize * 2.0f + wallThickness && depth >= minRoomSize * 2.0f + wallThickness);

	enum class LayoutStyle { None, Corridor, Whirl };
	std::vector<LayoutStyle> options;
	options.push_back(LayoutStyle::None); // Fallback is always an option

	if (canCorridor) options.push_back(LayoutStyle::Corridor);
	if (canWhirl) options.push_back(LayoutStyle::Whirl);

	std::uniform_int_distribution<size_t> optDist(0, options.size() - 1);
	LayoutStyle selectedStyle = options[optDist(rng)];

	// If targetRooms is high, force a structured layout if available
	if (targetRooms >= 4 && selectedStyle == LayoutStyle::None && options.size() > 1) {
		std::uniform_int_distribution<size_t> forceDist(1, options.size() - 1);
		selectedStyle = options[forceDist(rng)];
	}

	if (selectedStyle == LayoutStyle::Corridor)
	{
		// Randomized axis: if both work, pick randomly. Otherwise pick the one that works.
		bool canHallX = (width >= minSizeWithHall);
		bool canHallZ = (depth >= minSizeWithHall);
		bool hallAlongX = true;
		if (canHallX && canHallZ) {
			std::uniform_int_distribution<int> axisDist(0, 1);
			hallAlongX = (axisDist(rng) == 0);
		} else if (canHallX) {
			hallAlongX = true;
		} else {
			hallAlongX = false;
		}

		if (hallAlongX)
		{
			// Hallway runs along X (splits Z)
			// Random hallway center between 35% and 65% of depth
			std::uniform_real_distribution<float> centerDist(floorMin.z + minRoomSize + hallWidth * 0.5f, floorMax.z - minRoomSize - hallWidth * 0.5f);
			float zCenter = centerDist(rng);

			glm::vec3 hallMin = glm::vec3(floorMin.x, floorMin.y, zCenter - hallWidth * 0.5f);
			glm::vec3 hallMax = glm::vec3(floorMax.x, floorMax.y, zCenter + hallWidth * 0.5f);
			AddRoomDirect(hallMin, hallMax, RoomType::Corridor);

			// Longitudinal walls bounding the hallway
			AddWallDirect(glm::vec3(floorMin.x, floorMin.y, zCenter - hallWidth * 0.5f - wallThickness * 0.5f),
						  glm::vec3(floorMax.x, floorMax.y, zCenter - hallWidth * 0.5f + wallThickness * 0.5f));
			AddWallDirect(glm::vec3(floorMin.x, floorMin.y, zCenter + hallWidth * 0.5f - wallThickness * 0.5f),
						  glm::vec3(floorMax.x, floorMax.y, zCenter + hallWidth * 0.5f + wallThickness * 0.5f));

			// Allocate target rooms: corridor is 1, remaining N-1 are split randomly between Side A and Side B
			int remaining = targetRooms - 1;
			int roomsA = 1;
			int roomsB = 1;
			if (remaining > 2) {
				std::uniform_int_distribution<int> splitDist(1, remaining - 1);
				roomsA = splitDist(rng);
				roomsB = remaining - roomsA;
			} else if (remaining == 2) {
				roomsA = 1;
				roomsB = 1;
			}

			// Side A: Bottom / South
			glm::vec3 sideAMin = floorMin;
			glm::vec3 sideAMax = glm::vec3(floorMax.x, floorMax.y, zCenter - hallWidth * 0.5f - wallThickness * 0.5f);
			
			// Subdivide Side A with randomized room sizes
			std::vector<float> splitsA;
			splitsA.push_back(sideAMin.x);
			if (roomsA > 1) {
				std::vector<float> points;
				for (int i = 0; i < roomsA - 1; ++i) {
					float minP = sideAMin.x + (i + 1) * minRoomSize + i * wallThickness;
					float maxP = sideAMax.x - (roomsA - 1 - i) * minRoomSize - (roomsA - 1 - i) * wallThickness;
					if (minP < maxP) {
						std::uniform_real_distribution<float> pDist(minP, maxP);
						points.push_back(pDist(rng));
					} else {
						points.push_back(sideAMin.x + (sideAMax.x - sideAMin.x) * (float)(i + 1) / roomsA);
					}
				}
				std::sort(points.begin(), points.end());
				for (float p : points) splitsA.push_back(p);
			}
			splitsA.push_back(sideAMax.x);

			for (int i = 0; i < roomsA; i++)
			{
				float xStart = splitsA[i];
				float xEnd = splitsA[i + 1];

				if (i < roomsA - 1)
				{
					AddWallDirect(glm::vec3(xEnd - wallThickness * 0.5f, sideAMin.y, sideAMin.z),
								  glm::vec3(xEnd + wallThickness * 0.5f, sideAMax.y, sideAMax.z));
				}

				glm::vec3 rMin = glm::vec3(xStart + (i > 0 ? wallThickness * 0.5f : 0.0f), sideAMin.y, sideAMin.z);
				glm::vec3 rMax = glm::vec3(xEnd - (i < roomsA - 1 ? wallThickness * 0.5f : 0.0f), sideAMax.y, sideAMax.z);
				AddRoomDirect(rMin, rMax, RoomType::Office);
			}

			// Side B: Top / North
			glm::vec3 sideBMin = glm::vec3(floorMin.x, floorMin.y, zCenter + hallWidth * 0.5f + wallThickness * 0.5f);
			glm::vec3 sideBMax = floorMax;

			std::vector<float> splitsB;
			splitsB.push_back(sideBMin.x);
			if (roomsB > 1) {
				std::vector<float> points;
				for (int i = 0; i < roomsB - 1; ++i) {
					float minP = sideBMin.x + (i + 1) * minRoomSize + i * wallThickness;
					float maxP = sideBMax.x - (roomsB - 1 - i) * minRoomSize - (roomsB - 1 - i) * wallThickness;
					if (minP < maxP) {
						std::uniform_real_distribution<float> pDist(minP, maxP);
						points.push_back(pDist(rng));
					} else {
						points.push_back(sideBMin.x + (sideBMax.x - sideBMin.x) * (float)(i + 1) / roomsB);
					}
				}
				std::sort(points.begin(), points.end());
				for (float p : points) splitsB.push_back(p);
			}
			splitsB.push_back(sideBMax.x);

			for (int i = 0; i < roomsB; i++)
			{
				float xStart = splitsB[i];
				float xEnd = splitsB[i + 1];

				if (i < roomsB - 1)
				{
					AddWallDirect(glm::vec3(xEnd - wallThickness * 0.5f, sideBMin.y, sideBMin.z),
								  glm::vec3(xEnd + wallThickness * 0.5f, sideBMax.y, sideBMax.z));
				}

				glm::vec3 rMin = glm::vec3(xStart + (i > 0 ? wallThickness * 0.5f : 0.0f), sideBMin.y, sideBMin.z);
				glm::vec3 rMax = glm::vec3(xEnd - (i < roomsB - 1 ? wallThickness * 0.5f : 0.0f), sideBMax.y, sideBMax.z);
				AddRoomDirect(rMin, rMax, RoomType::Office);
			}
		}
		else
		{
			// Hallway runs along Z (splits X)
			std::uniform_real_distribution<float> centerDist(floorMin.x + minRoomSize + hallWidth * 0.5f, floorMax.x - minRoomSize - hallWidth * 0.5f);
			float xCenter = centerDist(rng);

			glm::vec3 hallMin = glm::vec3(xCenter - hallWidth * 0.5f, floorMin.y, floorMin.z);
			glm::vec3 hallMax = glm::vec3(xCenter + hallWidth * 0.5f, floorMax.y, floorMax.z);
			AddRoomDirect(hallMin, hallMax, RoomType::Corridor);

			// Longitudinal walls bounding the hallway
			AddWallDirect(glm::vec3(xCenter - hallWidth * 0.5f - wallThickness * 0.5f, floorMin.y, floorMin.z),
						  glm::vec3(xCenter - hallWidth * 0.5f + wallThickness * 0.5f, floorMax.y, floorMax.z));
			AddWallDirect(glm::vec3(xCenter + hallWidth * 0.5f - wallThickness * 0.5f, floorMin.y, floorMin.z),
						  glm::vec3(xCenter + hallWidth * 0.5f + wallThickness * 0.5f, floorMax.y, floorMax.z));

			int remaining = targetRooms - 1;
			int roomsA = 1;
			int roomsB = 1;
			if (remaining > 2) {
				std::uniform_int_distribution<int> splitDist(1, remaining - 1);
				roomsA = splitDist(rng);
				roomsB = remaining - roomsA;
			} else if (remaining == 2) {
				roomsA = 1;
				roomsB = 1;
			}

			// Side A: Left / West
			glm::vec3 sideAMin = floorMin;
			glm::vec3 sideAMax = glm::vec3(xCenter - hallWidth * 0.5f - wallThickness * 0.5f, floorMax.y, floorMax.z);

			std::vector<float> splitsA;
			splitsA.push_back(sideAMin.z);
			if (roomsA > 1) {
				std::vector<float> points;
				for (int i = 0; i < roomsA - 1; ++i) {
					float minP = sideAMin.z + (i + 1) * minRoomSize + i * wallThickness;
					float maxP = sideAMax.z - (roomsA - 1 - i) * minRoomSize - (roomsA - 1 - i) * wallThickness;
					if (minP < maxP) {
						std::uniform_real_distribution<float> pDist(minP, maxP);
						points.push_back(pDist(rng));
					} else {
						points.push_back(sideAMin.z + (sideAMax.z - sideAMin.z) * (float)(i + 1) / roomsA);
					}
				}
				std::sort(points.begin(), points.end());
				for (float p : points) splitsA.push_back(p);
			}
			splitsA.push_back(sideAMax.z);

			for (int i = 0; i < roomsA; i++)
			{
				float zStart = splitsA[i];
				float zEnd = splitsA[i + 1];

				if (i < roomsA - 1)
				{
					AddWallDirect(glm::vec3(sideAMin.x, sideAMin.y, zEnd - wallThickness * 0.5f),
								  glm::vec3(sideAMax.x, sideAMax.y, zEnd + wallThickness * 0.5f));
				}

				glm::vec3 rMin = glm::vec3(sideAMin.x, sideAMin.y, zStart + (i > 0 ? wallThickness * 0.5f : 0.0f));
				glm::vec3 rMax = glm::vec3(sideAMax.x, sideAMax.y, zEnd - (i < roomsA - 1 ? wallThickness * 0.5f : 0.0f));
				AddRoomDirect(rMin, rMax, RoomType::Office);
			}

			// Side B: Right / East
			glm::vec3 sideBMin = glm::vec3(xCenter + hallWidth * 0.5f + wallThickness * 0.5f, floorMin.y, floorMin.z);
			glm::vec3 sideBMax = floorMax;

			std::vector<float> splitsB;
			splitsB.push_back(sideBMin.z);
			if (roomsB > 1) {
				std::vector<float> points;
				for (int i = 0; i < roomsB - 1; ++i) {
					float minP = sideBMin.z + (i + 1) * minRoomSize + i * wallThickness;
					float maxP = sideBMax.z - (roomsB - 1 - i) * minRoomSize - (roomsB - 1 - i) * wallThickness;
					if (minP < maxP) {
						std::uniform_real_distribution<float> pDist(minP, maxP);
						points.push_back(pDist(rng));
					} else {
						points.push_back(sideBMin.z + (sideBMax.z - sideBMin.z) * (float)(i + 1) / roomsB);
					}
				}
				std::sort(points.begin(), points.end());
				for (float p : points) splitsB.push_back(p);
			}
			splitsB.push_back(sideBMax.z);

			for (int i = 0; i < roomsB; i++)
			{
				float zStart = splitsB[i];
				float zEnd = splitsB[i + 1];

				if (i < roomsB - 1)
				{
					AddWallDirect(glm::vec3(sideBMin.x, sideBMin.y, zEnd - wallThickness * 0.5f),
								  glm::vec3(sideBMax.x, sideBMax.y, zEnd + wallThickness * 0.5f));
				}

				glm::vec3 rMin = glm::vec3(sideBMin.x, sideBMin.y, zStart + (i > 0 ? wallThickness * 0.5f : 0.0f));
				glm::vec3 rMax = glm::vec3(sideBMax.x, sideBMax.y, zEnd - (i < roomsB - 1 ? wallThickness * 0.5f : 0.0f));
				AddRoomDirect(rMin, rMax, RoomType::Office);
			}
		}
	}
	else if (selectedStyle == LayoutStyle::Whirl)
	{
		// Pinwheel / Central Lobby Layout
		// Randomized central room fractions: between 0.32 and 0.44
		std::uniform_real_distribution<float> sizeDist(0.32f, 0.44f);
		float centerW = width * sizeDist(rng);
		float centerD = depth * sizeDist(rng);

		// Random offsets from perfect center
		std::uniform_real_distribution<float> offsetXDist(-(width - centerW) * 0.08f, (width - centerW) * 0.08f);
		std::uniform_real_distribution<float> offsetZDist(-(depth - centerD) * 0.08f, (depth - centerD) * 0.08f);
		
		float cx = (width - centerW) * 0.5f + offsetXDist(rng);
		float cz = (depth - centerD) * 0.5f + offsetZDist(rng);

		glm::vec3 cMin = glm::vec3(floorMin.x + cx, floorMin.y, floorMin.z + cz);
		glm::vec3 cMax = glm::vec3(cMin.x + centerW, floorMax.y, cMin.z + centerD);

		// Central Lobby Room
		AddRoomDirect(cMin, cMax, RoomType::Lobby);

		// Outer Rooms (0 = SW, 1 = NW, 2 = NE, 3 = SE)
		glm::vec3 swMin = floorMin;
		glm::vec3 swMax = glm::vec3(cMin.x - wallThickness * 0.5f, floorMax.y, cMax.z);
		AddRoomDirect(swMin, swMax, RoomType::Office);

		glm::vec3 nwMin = glm::vec3(floorMin.x, floorMin.y, cMax.z + wallThickness * 0.5f);
		glm::vec3 nwMax = glm::vec3(cMax.x, floorMax.y, floorMax.z);
		AddRoomDirect(nwMin, nwMax, RoomType::Office);

		glm::vec3 neMin = glm::vec3(cMax.x + wallThickness * 0.5f, floorMin.y, cMin.z);
		glm::vec3 neMax = floorMax;
		AddRoomDirect(neMin, neMax, RoomType::Office);

		glm::vec3 seMin = glm::vec3(cMin.x, floorMin.y, floorMin.z);
		glm::vec3 seMax = glm::vec3(floorMax.x, floorMax.y, cMin.z - wallThickness * 0.5f);
		AddRoomDirect(seMin, seMax, RoomType::Office);

		// Walls bounding the central Lobby
		// West wall of center
		AddWallDirect(glm::vec3(cMin.x - wallThickness * 0.5f, floorMin.y, cMin.z),
					  glm::vec3(cMin.x + wallThickness * 0.5f, floorMax.y, cMax.z));
		// North wall of center
		AddWallDirect(glm::vec3(cMin.x, floorMin.y, cMax.z - wallThickness * 0.5f),
					  glm::vec3(cMax.x, floorMax.y, cMax.z + wallThickness * 0.5f));
		// East wall of center
		AddWallDirect(glm::vec3(cMax.x - wallThickness * 0.5f, floorMin.y, cMin.z),
					  glm::vec3(cMax.x + wallThickness * 0.5f, floorMax.y, cMax.z));
		// South wall of center
		AddWallDirect(glm::vec3(cMin.x, floorMin.y, cMin.z - wallThickness * 0.5f),
					  glm::vec3(cMax.x, floorMax.y, cMin.z + wallThickness * 0.5f));

		// Divide-walls separating the outer rooms from each other (in pinwheel)
		// Wall separating SW and NW
		AddWallDirect(glm::vec3(floorMin.x, floorMin.y, cMax.z - wallThickness * 0.5f),
					  glm::vec3(cMin.x - wallThickness * 0.5f, floorMax.y, cMax.z + wallThickness * 0.5f));
		// Wall separating NW and NE
		AddWallDirect(glm::vec3(cMax.x - wallThickness * 0.5f, floorMin.y, cMax.z + wallThickness * 0.5f),
					  glm::vec3(cMax.x + wallThickness * 0.5f, floorMax.y, floorMax.z));
		// Wall separating NE and SE
		AddWallDirect(glm::vec3(cMax.x + wallThickness * 0.5f, floorMin.y, cMin.z - wallThickness * 0.5f),
					  glm::vec3(floorMax.x, floorMax.y, cMin.z + wallThickness * 0.5f));
		// Wall separating SE and SW
		AddWallDirect(glm::vec3(cMin.x - wallThickness * 0.5f, floorMin.y, floorMin.z),
					  glm::vec3(cMin.x + wallThickness * 0.5f, floorMax.y, cMin.z - wallThickness * 0.5f));
	}
	else
	{
		// Fallback: Simple Split (Two rooms or one single room)
		bool canSplitX = (width >= minRoomSize * 2.0f + wallThickness);
		bool canSplitZ = (depth >= minRoomSize * 2.0f + wallThickness);

		if (targetRooms > 1 && (canSplitX || canSplitZ))
		{
			bool splitX = canSplitX;
			if (canSplitX && canSplitZ) {
				std::uniform_int_distribution<int> axisDist(0, 1);
				splitX = (axisDist(rng) == 0);
			}

			// Split fraction randomized between 30% and 70% instead of exact 50%
			std::uniform_real_distribution<float> splitFracDist(0.3f, 0.7f);
			float splitFrac = splitFracDist(rng);

			if (splitX)
			{
				float splitPos = floorMin.x + width * splitFrac;
				AddWallDirect(glm::vec3(splitPos - wallThickness * 0.5f, floorMin.y, floorMin.z),
							  glm::vec3(splitPos + wallThickness * 0.5f, floorMax.y, floorMax.z));

				AddRoomDirect(floorMin, glm::vec3(splitPos - wallThickness * 0.5f, floorMax.y, floorMax.z), RoomType::Office);
				AddRoomDirect(glm::vec3(splitPos + wallThickness * 0.5f, floorMin.y, floorMin.z), floorMax, RoomType::Office);
			}
			else
			{
				float splitPos = floorMin.z + depth * splitFrac;
				AddWallDirect(glm::vec3(floorMin.x, floorMin.y, splitPos - wallThickness * 0.5f),
							  glm::vec3(floorMax.x, floorMax.y, splitPos + wallThickness * 0.5f));

				AddRoomDirect(floorMin, glm::vec3(floorMax.x, floorMax.y, splitPos - wallThickness * 0.5f), RoomType::Office);
				AddRoomDirect(glm::vec3(floorMin.x, floorMin.y, splitPos + wallThickness * 0.5f), floorMax, RoomType::Office);
			}
		}
		else
		{
			// Single Room
			AddRoomDirect(floorMin, floorMax, RoomType::Office);
		}
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

	// Sort assignable rooms by area descending
	std::sort(sortedRooms.begin(), sortedRooms.end(), [](const InteriorRoom* a, const InteriorRoom* b) {
		return a->GetArea() > b->GetArea();
	});

	// If it's commercial, all assignable rooms remain RoomType::Office
	if (isCommercial)
	{
		return;
	}

	// For residential, we distribute functions:
	std::vector<InteriorRoom*> remainingRooms = sortedRooms;

	// 1. Bathroom: smallest remaining rooms
	size_t targetBathrooms = std::max<size_t>(1, (interior.rooms.size() - 1) / 4);
	if (targetBathrooms > remainingRooms.size()) targetBathrooms = remainingRooms.size();

	// Sort remaining by area ascending
	std::sort(remainingRooms.begin(), remainingRooms.end(), [](const InteriorRoom* a, const InteriorRoom* b) {
		return a->GetArea() < b->GetArea();
	});

	size_t assignedBathrooms = 0;
	while (assignedBathrooms < targetBathrooms && !remainingRooms.empty())
	{
		remainingRooms.front()->type = RoomType::Bathroom;
		remainingRooms.erase(remainingRooms.begin());
		assignedBathrooms++;
	}

	// 2. Kitchen: largest remaining room
	// Sort descending
	std::sort(remainingRooms.begin(), remainingRooms.end(), [](const InteriorRoom* a, const InteriorRoom* b) {
		return a->GetArea() > b->GetArea();
	});

	if (!remainingRooms.empty())
	{
		remainingRooms.front()->type = RoomType::Kitchen;
		remainingRooms.erase(remainingRooms.begin());
	}

	// 3. Bedrooms: up to half of remaining, at least 1
	size_t targetBedrooms = std::max<size_t>(1, remainingRooms.size() / 2);
	size_t assignedBedrooms = 0;
	while (assignedBedrooms < targetBedrooms && !remainingRooms.empty())
	{
		remainingRooms.front()->type = RoomType::Bedroom;
		remainingRooms.erase(remainingRooms.begin());
		assignedBedrooms++;
	}

	// 4. Leftovers randomly become Bedroom, Office, or Bathroom
	std::uniform_real_distribution<float> prob(0.0f, 1.0f);
	for (auto* room : remainingRooms)
	{
		float r = prob(rng);
		if (r < 0.45f) room->type = RoomType::Bedroom;
		else if (r < 0.85f) room->type = RoomType::Office;
		else room->type = RoomType::Bathroom;
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

	float wThick = interior.wallThickness;
	float eps = 0.05f; // small tolerance

	std::vector<std::pair<size_t, size_t>> connectedPairs;
	auto IsConnected = [&](size_t a, size_t b) {
		size_t mn = std::min(a, b);
		size_t mx = std::max(a, b);
		for (const auto& p : connectedPairs) {
			if (p.first == mn && p.second == mx) return true;
		}
		return false;
	};
	auto MarkConnected = [&](size_t a, size_t b) {
		size_t mn = std::min(a, b);
		size_t mx = std::max(a, b);
		connectedPairs.push_back({mn, mx});
	};

	for (size_t i = 0; i < interior.rooms.size(); i++)
	{
		auto& room = interior.rooms[i];
		// Corridors and Lobbies don't initiate doors
		if (room.type == RoomType::Corridor || room.type == RoomType::Lobby)
			continue;

		// We want to find the best candidate room to connect to
		struct DoorCandidate {
			size_t targetRoomIdx;
			glm::vec3 doorPos;
			bool runsAlongX;
			int priority; // 0 = Corridor/Lobby, 1 = Other
			float overlapLen;
		};

		std::vector<DoorCandidate> candidates;

		for (size_t j = 0; j < interior.rooms.size(); j++)
		{
			if (i == j) continue;
			auto& other = interior.rooms[j];

			// Shared X boundary (wall runs along Z, door spans Z)
			bool adjacentX = false;
			float overlapZMin = std::max(room.minBounds.z, other.minBounds.z);
			float overlapZMax = std::min(room.maxBounds.z, other.maxBounds.z);
			float overlapZ = overlapZMax - overlapZMin;

			if (overlapZ > doorWidth + eps) {
				if (std::abs(room.maxBounds.x - other.minBounds.x) < wThick + eps ||
					std::abs(room.minBounds.x - other.maxBounds.x) < wThick + eps) {
					adjacentX = true;
				}
			}

			if (adjacentX) {
				DoorCandidate cand;
				cand.targetRoomIdx = j;
				cand.runsAlongX = false;
				float midZ = (overlapZMin + overlapZMax) * 0.5f;
				float wallX = std::abs(room.maxBounds.x - other.minBounds.x) < wThick + eps ? room.maxBounds.x : room.minBounds.x;
				cand.doorPos = glm::vec3(wallX, room.minBounds.y, midZ);
				cand.priority = (other.type == RoomType::Corridor || other.type == RoomType::Lobby) ? 0 : 1;
				cand.overlapLen = overlapZ;
				candidates.push_back(cand);
			}

			// Shared Z boundary (wall runs along X, door spans X)
			bool adjacentZ = false;
			float overlapXMin = std::max(room.minBounds.x, other.minBounds.x);
			float overlapXMax = std::min(room.maxBounds.x, other.maxBounds.x);
			float overlapX = overlapXMax - overlapXMin;

			if (overlapX > doorWidth + eps) {
				if (std::abs(room.maxBounds.z - other.minBounds.z) < wThick + eps ||
					std::abs(room.minBounds.z - other.maxBounds.z) < wThick + eps) {
					adjacentZ = true;
				}
			}

			if (adjacentZ) {
				DoorCandidate cand;
				cand.targetRoomIdx = j;
				cand.runsAlongX = true;
				float midX = (overlapXMin + overlapXMax) * 0.5f;
				float wallZ = std::abs(room.maxBounds.z - other.minBounds.z) < wThick + eps ? room.maxBounds.z : room.minBounds.z;
				cand.doorPos = glm::vec3(midX, room.minBounds.y, wallZ);
				cand.priority = (other.type == RoomType::Corridor || other.type == RoomType::Lobby) ? 0 : 1;
				cand.overlapLen = overlapX;
				candidates.push_back(cand);
			}
		}

		if (candidates.empty()) continue;

		// Sort candidates: Priority 0 first, then overlapLen descending
		std::sort(candidates.begin(), candidates.end(), [](const DoorCandidate& a, const DoorCandidate& b) {
			if (a.priority != b.priority)
				return a.priority < b.priority;
			return a.overlapLen > b.overlapLen;
		});

		// Pick the best candidate that isn't already connected
		for (auto& best : candidates)
		{
			if (IsConnected(i, best.targetRoomIdx)) {
				break; // already connected
			}

			InteriorDoor door;
			door.width = doorWidth;
			door.height = doorHeight;
			door.runsAlongX = best.runsAlongX;
			door.position = best.doorPos;
			door.hingeOnLeft = (rng() % 2 == 0);
			door.isOpen = true;

			interior.doors.push_back(door);
			MarkConnected(i, best.targetRoomIdx);
			break;
		}
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

				// Find if there is an intersecting door
				const InteriorDoor* intersectingDoor = nullptr;
				float eps = 0.1f;

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
								intersectingDoor = &door;
								break;
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
								intersectingDoor = &door;
								break;
							}
						}
					}
				}

				if (intersectingDoor)
				{
					float doorHalfW = intersectingDoor->width * 0.5f;
					if (thinInX)
					{
						// Split wall along Z
						float zSplitLeft = intersectingDoor->position.z - doorHalfW;
						float zSplitRight = intersectingDoor->position.z + doorHalfW;

						// 1. Left/Bottom piece
						if (zSplitLeft - wall.minBounds.z > 0.05f)
						{
							glm::vec3 minB = wall.minBounds;
							glm::vec3 maxB = glm::vec3(wall.maxBounds.x, wall.maxBounds.y, zSplitLeft);
							glm::vec3 center = (minB + maxB) * 0.5f;
							glm::vec3 half = (maxB - minB) * 0.5f;
							meshBuckets[MAT_DRYWALL].Append(MakeWallBox(plotMat, center, half, 2.0f));
						}

						// 2. Right/Top piece
						if (wall.maxBounds.z - zSplitRight > 0.05f)
						{
							glm::vec3 minB = glm::vec3(wall.minBounds.x, wall.minBounds.y, zSplitRight);
							glm::vec3 maxB = wall.maxBounds;
							glm::vec3 center = (minB + maxB) * 0.5f;
							glm::vec3 half = (maxB - minB) * 0.5f;
							meshBuckets[MAT_DRYWALL].Append(MakeWallBox(plotMat, center, half, 2.0f));
						}

						// 3. Header piece above the door
						float headerMinY = intersectingDoor->position.y + intersectingDoor->height;
						if (wall.maxBounds.y - headerMinY > 0.05f)
						{
							glm::vec3 minB = glm::vec3(wall.minBounds.x, headerMinY, zSplitLeft);
							glm::vec3 maxB = glm::vec3(wall.maxBounds.x, wall.maxBounds.y, zSplitRight);
							glm::vec3 center = (minB + maxB) * 0.5f;
							glm::vec3 half = (maxB - minB) * 0.5f;
							meshBuckets[MAT_DRYWALL].Append(MakeWallBox(plotMat, center, half, 2.0f));
						}
					}
					else
					{
						// Split wall along X
						float xSplitLeft = intersectingDoor->position.x - doorHalfW;
						float xSplitRight = intersectingDoor->position.x + doorHalfW;

						// 1. Left piece
						if (xSplitLeft - wall.minBounds.x > 0.05f)
						{
							glm::vec3 minB = wall.minBounds;
							glm::vec3 maxB = glm::vec3(xSplitLeft, wall.maxBounds.y, wall.maxBounds.z);
							glm::vec3 center = (minB + maxB) * 0.5f;
							glm::vec3 half = (maxB - minB) * 0.5f;
							meshBuckets[MAT_DRYWALL].Append(MakeWallBox(plotMat, center, half, 2.0f));
						}

						// 2. Right piece
						if (wall.maxBounds.x - xSplitRight > 0.05f)
						{
							glm::vec3 minB = glm::vec3(xSplitRight, wall.minBounds.y, wall.minBounds.z);
							glm::vec3 maxB = wall.maxBounds;
							glm::vec3 center = (minB + maxB) * 0.5f;
							glm::vec3 half = (maxB - minB) * 0.5f;
							meshBuckets[MAT_DRYWALL].Append(MakeWallBox(plotMat, center, half, 2.0f));
						}

						// 3. Header piece above the door
						float headerMinY = intersectingDoor->position.y + intersectingDoor->height;
						if (wall.maxBounds.y - headerMinY > 0.05f)
						{
							glm::vec3 minB = glm::vec3(xSplitLeft, headerMinY, wall.minBounds.z);
							glm::vec3 maxB = glm::vec3(xSplitRight, wall.maxBounds.y, wall.maxBounds.z);
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
