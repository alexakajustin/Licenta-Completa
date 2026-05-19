#include "InteriorDecorators.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

// =====================================================================
// Shared Helpers
// =====================================================================

static void AddProp(
	std::vector<PropPlacement>& props,
	const std::string& path,
	glm::vec3 pos,
	glm::vec3 rot,
	glm::vec3 scale,
	const std::string& cat)
{
	PropPlacement p;
	p.modelPath = path;
	p.position = pos;
	p.rotation = rot;
	p.scale = scale;
	p.category = cat;
	props.push_back(p);
}

// Occupancy-aware version: registers XZ footprint after placing
static void AddPropOcc(
	std::vector<PropPlacement>& props,
	RoomOccupancy& occ,
	const std::string& path,
	glm::vec2 xzCenter,
	glm::vec2 xzHalfSize,
	float y,
	glm::vec3 rot,
	glm::vec3 scale,
	const std::string& cat)
{
	PropPlacement p;
	p.modelPath = path;
	p.position = glm::vec3(xzCenter.x, y, xzCenter.y);
	p.rotation = rot;
	p.scale = scale;
	p.category = cat;
	props.push_back(p);
	occ.Register(xzCenter, xzHalfSize);
}

// Generates procedural baseboards (floor trim) and crown molding (ceiling trim)
static void GenerateRoomTrim(
	std::map<int, MeshData>& meshBuckets,
	const InteriorRoom& room,
	float floorHeight,
	bool isCommercial)
{
	float floorY = room.minBounds.y;
	float ceilY = room.maxBounds.y;
	float trimThick = 0.02f;
	float baseHeight = 0.12f;
	float crownHeight = 0.08f;
	float crownDepth = 0.08f;

	// Use dark metal trim for commercial/offices, drywall/painted white trim for houses
	int trimMatKey = isCommercial ? MAT_METAL : MAT_DRYWALL;

	glm::vec3 roomMin = room.minBounds;
	glm::vec3 roomMax = room.maxBounds;
	float width = roomMax.x - roomMin.x;
	float depth = roomMax.z - roomMin.z;

	// 1. Left Wall (-X)
	meshBuckets[trimMatKey].Append(IInteriorDecorator::MakeBox(
		glm::vec3(roomMin.x + trimThick * 0.5f, floorY + baseHeight * 0.5f, (roomMin.z + roomMax.z) * 0.5f),
		glm::vec3(trimThick * 0.5f, baseHeight * 0.5f, depth * 0.5f)
	));
	meshBuckets[trimMatKey].Append(IInteriorDecorator::MakeBox(
		glm::vec3(roomMin.x + crownDepth * 0.5f, ceilY - crownHeight * 0.5f, (roomMin.z + roomMax.z) * 0.5f),
		glm::vec3(crownDepth * 0.5f, crownHeight * 0.5f, depth * 0.5f)
	));

	// 2. Right Wall (+X)
	meshBuckets[trimMatKey].Append(IInteriorDecorator::MakeBox(
		glm::vec3(roomMax.x - trimThick * 0.5f, floorY + baseHeight * 0.5f, (roomMin.z + roomMax.z) * 0.5f),
		glm::vec3(trimThick * 0.5f, baseHeight * 0.5f, depth * 0.5f)
	));
	meshBuckets[trimMatKey].Append(IInteriorDecorator::MakeBox(
		glm::vec3(roomMax.x - crownDepth * 0.5f, ceilY - crownHeight * 0.5f, (roomMin.z + roomMax.z) * 0.5f),
		glm::vec3(crownDepth * 0.5f, crownHeight * 0.5f, depth * 0.5f)
	));

	// 3. Front Wall (-Z)
	meshBuckets[trimMatKey].Append(IInteriorDecorator::MakeBox(
		glm::vec3((roomMin.x + roomMax.x) * 0.5f, floorY + baseHeight * 0.5f, roomMin.z + trimThick * 0.5f),
		glm::vec3(width * 0.5f, baseHeight * 0.5f, trimThick * 0.5f)
	));
	meshBuckets[trimMatKey].Append(IInteriorDecorator::MakeBox(
		glm::vec3((roomMin.x + roomMax.x) * 0.5f, ceilY - crownHeight * 0.5f, roomMin.z + crownDepth * 0.5f),
		glm::vec3(width * 0.5f, crownHeight * 0.5f, crownDepth * 0.5f)
	));

	// 4. Back Wall (+Z)
	meshBuckets[trimMatKey].Append(IInteriorDecorator::MakeBox(
		glm::vec3((roomMin.x + roomMax.x) * 0.5f, floorY + baseHeight * 0.5f, roomMax.z - trimThick * 0.5f),
		glm::vec3(width * 0.5f, baseHeight * 0.5f, trimThick * 0.5f)
	));
	meshBuckets[trimMatKey].Append(IInteriorDecorator::MakeBox(
		glm::vec3((roomMin.x + roomMax.x) * 0.5f, ceilY - crownHeight * 0.5f, roomMax.z - crownDepth * 0.5f),
		glm::vec3(width * 0.5f, crownHeight * 0.5f, crownDepth * 0.5f)
	));
}

// Places ceiling lights in a neat grid
static void PlaceCeilingLights(
	std::vector<PropPlacement>& props,
	const InteriorRoom& room,
	std::mt19937& rng)
{
	float ceilingY = room.maxBounds.y - 0.05f;
	float width = room.GetWidth();
	float depth = room.GetDepth();

	// Adaptive grid: 2x2 for larger rooms, 1x1 for small ones
	int numX = (width > 5.0f) ? 2 : 1;
	int numZ = (depth > 5.0f) ? 2 : 1;

	for (int x = 0; x < numX; x++)
	{
		for (int z = 0; z < numZ; z++)
		{
			float pctX = (numX > 1) ? (0.25f + 0.5f * x) : 0.5f;
			float pctZ = (numZ > 1) ? (0.25f + 0.5f * z) : 0.5f;

			float posX = room.minBounds.x + width * pctX;
			float posZ = room.minBounds.z + depth * pctZ;

			AddProp(props, "Assets/Models/Bedroom/Models/Interior/LED_Light_01.fbx",
				glm::vec3(posX, ceilingY, posZ), glm::vec3(0.0f), glm::vec3(1.0f), "light");
		}
	}
}

MeshData IInteriorDecorator::MakeBox(glm::vec3 center, glm::vec3 halfExtents, float uvScale)
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

	glm::mat4 xform = glm::translate(glm::mat4(1.0f), center);
	xform = glm::scale(xform, halfExtents * 2.0f);
	cube.TransformBy(xform);
	return cube;
}

// =====================================================================
// OfficeDecorator — High-fidelity Desk, Office Chair, TV/Monitor, Rug, Cabinet
// =====================================================================

void OfficeDecorator::Decorate(
	std::map<int, MeshData>& meshBuckets,
	std::vector<PropPlacement>& props,
	const InteriorRoom& room,
	const std::vector<InteriorDoor>& doors,
	std::mt19937& rng,
	float floorHeight,
	const FurnitureSpecs& f,
	bool isCommercial)
{
	glm::vec3 bedSize = f.bed.size;
	glm::vec3 deskSize = f.desk.size;
	glm::vec3 tvSize = f.tv.size;
	glm::vec3 stoveSize = f.stove.size;
	glm::vec3 fridgeSize = f.fridge.size;
	glm::vec3 sinkSize = f.sink.size;
	glm::vec3 toiletSize = f.toilet.size;
	glm::vec3 bathtubSize = f.bathtub.size;
	glm::vec3 sofaSize = f.sofa.size;
	glm::vec3 coffeeTableSize = f.coffeeTable.size;
	glm::vec3 tvStandSize = f.tvStand.size;

	GenerateRoomTrim(meshBuckets, room, floorHeight, isCommercial);
	PlaceCeilingLights(props, room, rng);

	float floorY = room.minBounds.y;
	RoomOccupancy occ(glm::vec2(room.minBounds.x, room.minBounds.z),
	                  glm::vec2(room.maxBounds.x, room.maxBounds.z));
	for (const auto& d : doors) occ.BlockDoor(d.position, d.width, d.runsAlongX, 0.15f);

	std::uniform_real_distribution<float> prob(0.0f, 1.0f);
	std::uniform_real_distribution<float> rotJ(-15.0f, 15.0f);

	// Determine number of desks based on size (large rooms get multiple cubicles/desks)
	int deskCount = (room.GetArea() > 20.0f) ? 2 : 1;
	float baseW = deskSize.x, baseD = deskSize.z, baseH = deskSize.y;

	for (int dIdx = 0; dIdx < deskCount; dIdx++)
	{
		bool deskPlaced = false;
		glm::vec2 deskXZ;
		float deskYaw = 0.0f;
		float scaleFactor = 1.0f;

		// Fallback Loop: scale down progressively until it fits (ensures it always spawns!)
		for (float sf = 1.0f; sf >= 0.4f && !deskPlaced; sf -= 0.1f)
		{
			float fDW = baseW * sf, fDD = baseD * sf;
			// Try placing along different walls
			int startWall = (dIdx * 2 + (rng() % 4)) % 4;

			for (int w = 0; w < 4 && !deskPlaced; w++)
			{
				int wall = (startWall + w) % 4;
				glm::vec2 halfSize = (wall < 2) ? glm::vec2(fDD * 0.5f, fDW * 0.5f) : glm::vec2(fDW * 0.5f, fDD * 0.5f);
				float wallOff = ((wall < 2) ? fDD : fDD) * 0.5f + 0.15f;

				if (occ.TryPlaceAlongWall(wall, halfSize, wallOff, deskXZ, 0.02f))
				{
					deskPlaced = true;
					deskYaw = (wall == 0) ? 90.0f : (wall == 1) ? -90.0f : (wall == 2) ? 0.0f : 180.0f;
					scaleFactor = sf;
				}
			}
		}

		// Absolute fallback: force centered placement
		if (!deskPlaced)
		{
			scaleFactor = 0.4f;
			float fDW = baseW * scaleFactor, fDD = baseD * scaleFactor;
			deskXZ = occ.GetRoomCenter() + (dIdx == 0 ? glm::vec2(-0.4f, 0.0f) : glm::vec2(0.4f, 0.0f));
			deskYaw = 0.0f;
			deskPlaced = true;
		}

		float finalYaw = deskYaw + rotJ(rng);
		float fDW = baseW * scaleFactor, fDD = baseD * scaleFactor;
		glm::vec2 deskHalf = (deskYaw == 90.0f || deskYaw == -90.0f) ? glm::vec2(fDD * 0.5f, fDW * 0.5f) : glm::vec2(fDW * 0.5f, fDD * 0.5f);

		AddPropOcc(props, occ, "Assets/Models/Bathroom/Model/Bathroom_props_set/Iron_Wooden_Table.fbx",
			deskXZ, deskHalf, floorY, glm::vec3(0.0f, finalYaw, 0.0f), glm::vec3(scaleFactor), "desk");

		// Chair in front of desk (with natural organic random rotation offset)
		glm::vec3 fwd(sin(glm::radians(deskYaw)), 0.0f, cos(glm::radians(deskYaw)));
		glm::vec2 chairXZ = deskXZ + glm::vec2(fwd.x, fwd.z) * (fDD * 0.5f + 0.45f * scaleFactor);
		glm::vec2 chairHalf(0.25f * scaleFactor);
		
		if (occ.CanPlace(chairXZ, chairHalf, 0.01f) || true) // Force chair near desk
		{
			float chairYaw = deskYaw + 180.0f + std::uniform_real_distribution<float>(-25.0f, 25.0f)(rng);
			AddPropOcc(props, occ, "Assets/Models/Kitchen/Models/Chair.fbx",
				chairXZ, chairHalf, floorY, glm::vec3(0.0f, chairYaw, 0.0f), glm::vec3(scaleFactor), "chair");
		}

		// Monitor on desk (turned on)
		if (prob(rng) > 0.3f)
		{
			AddProp(props, "Assets/Models/Bedroom/Models/Interior/Tv_01.fbx",
				glm::vec3(deskXZ.x, floorY + baseH * scaleFactor, deskXZ.y),
				glm::vec3(0.0f, finalYaw, 0.0f), glm::vec3(0.6f * scaleFactor), "monitor");
		}

		// Rug under desk
		if (prob(rng) > 0.4f)
		{
			AddProp(props, "Assets/Models/Bedroom/Models/Interior/Rug_01.fbx",
				glm::vec3(deskXZ.x, floorY + 0.005f, deskXZ.y),
				glm::vec3(0.0f, finalYaw, 0.0f), glm::vec3(0.6f * scaleFactor, 1.0f, 0.6f * scaleFactor), "rug");
		}

		// Cabinet next to desk
		glm::vec3 right(cos(glm::radians(deskYaw)), 0.0f, -sin(glm::radians(deskYaw)));
		glm::vec2 cabXZ = deskXZ + glm::vec2(right.x, right.z) * (fDW * 0.5f + 0.4f * scaleFactor);
		glm::vec2 cabHalf(0.3f * scaleFactor);
		if (occ.CanPlace(cabXZ, cabHalf, 0.01f))
		{
			AddPropOcc(props, occ, "Assets/Models/Bathroom/Model/Bathroom_props_set/Drawer.fbx",
				cabXZ, cabHalf, floorY, glm::vec3(0.0f, finalYaw, 0.0f), glm::vec3(scaleFactor), "cabinet");
		}
	}
}

// =====================================================================
// BathroomDecorator — High-fidelity Toilet, Washing Machine, Sink, Rack
// =====================================================================

void BathroomDecorator::Decorate(
	std::map<int, MeshData>& meshBuckets,
	std::vector<PropPlacement>& props,
	const InteriorRoom& room,
	const std::vector<InteriorDoor>& doors,
	std::mt19937& rng,
	float floorHeight,
	const FurnitureSpecs& f,
	bool isCommercial)
{
	glm::vec3 bedSize = f.bed.size;
	glm::vec3 deskSize = f.desk.size;
	glm::vec3 tvSize = f.tv.size;
	glm::vec3 stoveSize = f.stove.size;
	glm::vec3 fridgeSize = f.fridge.size;
	glm::vec3 sinkSize = f.sink.size;
	glm::vec3 toiletSize = f.toilet.size;
	glm::vec3 bathtubSize = f.bathtub.size;
	glm::vec3 sofaSize = f.sofa.size;
	glm::vec3 coffeeTableSize = f.coffeeTable.size;
	glm::vec3 tvStandSize = f.tvStand.size;

	GenerateRoomTrim(meshBuckets, room, floorHeight, isCommercial);
	float floorY = room.minBounds.y;

	RoomOccupancy occ(glm::vec2(room.minBounds.x, room.minBounds.z),
	                  glm::vec2(room.maxBounds.x, room.maxBounds.z));
	for (const auto& d : doors) occ.BlockDoor(d.position, d.width, d.runsAlongX, 0.15f);

	auto wallYaw = [](int w) -> float { return (w==0)?90.0f:(w==1)?-90.0f:(w==2)?0.0f:180.0f; };

	if (isCommercial)
	{
		// =================================================================
		// COMMERCIAL/OFFICE RESTROOM
		// =================================================================
		// 1. Long slab containing multiple sinks (basin & faucet)
		float slabLen = std::min(room.GetWidth(), room.GetDepth()) * 0.7f;
		float slabW = 0.6f;
		float slabH = 0.85f;
		
		glm::vec2 slabHalf(slabLen * 0.5f, slabW * 0.5f);
		glm::vec2 slabXZ;
		// Try placing along Wall 2 (top)
		if (occ.TryPlaceAlongWall(2, slabHalf, slabW * 0.5f + 0.02f, slabXZ))
		{
			// Generate procedural countertop slab
			meshBuckets[MAT_WOOD].Append(IInteriorDecorator::MakeBox(
				glm::vec3(slabXZ.x, floorY + slabH * 0.5f, slabXZ.y),
				glm::vec3(slabHalf.x, slabH * 0.5f, slabHalf.y)
			));
			occ.Register(slabXZ, slabHalf);

			// Place two sinks on top of it
			float sinkOffset = slabHalf.x * 0.5f;
			AddProp(props, "Assets/Models/Kitchen/Models/sink.fbx",
				glm::vec3(slabXZ.x - sinkOffset, floorY + slabH, slabXZ.y),
				glm::vec3(0.0f), glm::vec3(0.7f), "sink");
			AddProp(props, "Assets/Models/Kitchen/Models/sink.fbx",
				glm::vec3(slabXZ.x + sinkOffset, floorY + slabH, slabXZ.y),
				glm::vec3(0.0f), glm::vec3(0.7f), "sink");
		}

		// 2. Private Toilet Stalls (divided by partition walls)
		// We'll place 2 stalls against Wall 3 (bottom)
		float stallW = 1.0f;
		float stallD = 1.4f;
		float startX = room.minBounds.x + 0.5f;

		for (int s = 0; s < 2; s++)
		{
			float cx = startX + s * (stallW + 0.15f) + stallW * 0.5f;
			float cz = room.maxBounds.z - stallD * 0.5f;

			glm::vec2 stallHalf(stallW * 0.5f, stallD * 0.5f);
			glm::vec2 stallXZ(cx, cz);

			if (occ.CanPlace(stallXZ, stallHalf))
			{
				// Register stall space
				occ.Register(stallXZ, stallHalf);

				// Place toilet inside
				AddProp(props, "Assets/Models/Bathroom/Model/Bathroom_props_set/Bathroom_Props_Set02.fbx",
					glm::vec3(cx, floorY, room.maxBounds.z - toiletSize.z * 0.5f - 0.05f),
					glm::vec3(0.0f, 180.0f, 0.0f), glm::vec3(0.9f), "toilet");

				// Place partition walls (proc boxes)
				meshBuckets[MAT_METAL].Append(IInteriorDecorator::MakeBox(
					glm::vec3(cx - stallW * 0.5f, floorY + 1.0f, cz),
					glm::vec3(0.02f, 1.0f, stallD * 0.5f)
				));
				if (s == 1) // side wall for the last stall
				{
					meshBuckets[MAT_METAL].Append(IInteriorDecorator::MakeBox(
						glm::vec3(cx + stallW * 0.5f, floorY + 1.0f, cz),
						glm::vec3(0.02f, 1.0f, stallD * 0.5f)
					));
				}
			}
		}
	}
	else
	{
		// =================================================================
		// RESIDENTIAL BATHROOM
		// =================================================================
		// 1. Toilet
		bool toiletPlaced = false;
		for (float ts = 1.0f; ts >= 0.4f && !toiletPlaced; ts -= 0.1f)
		{
			float tDepth = toiletSize.z * ts;
			float tWidth = toiletSize.x * ts;

			for (int w = 0; w < 4 && !toiletPlaced; w++)
			{
				glm::vec2 toiletHalf = (w == 0 || w == 1) ? 
					glm::vec2(tDepth * 0.5f, tWidth * 0.5f) : 
					glm::vec2(tWidth * 0.5f, tDepth * 0.5f);

				glm::vec2 toiletXZ;
				if (occ.TryPlaceAlongWall(w, toiletHalf, tDepth * 0.5f + 0.05f, toiletXZ, 0.02f))
				{
					std::string modelPath = f.toilet.path.empty() ? "Assets/Models/Bathroom/Model/Bathroom_props_set/Bathroom_Props_Set02.fbx" : f.toilet.path;
					AddPropOcc(props, occ, modelPath,
						toiletXZ, toiletHalf, floorY, glm::vec3(0.0f, wallYaw(w), 0.0f), glm::vec3(ts), "toilet");
					toiletPlaced = true;
				}
			}
		}
		if (!toiletPlaced) // Force placement
		{
			std::string modelPath = f.toilet.path.empty() ? "Assets/Models/Bathroom/Model/Bathroom_props_set/Bathroom_Props_Set02.fbx" : f.toilet.path;
			glm::vec2 toiletXZ = occ.GetRoomCenter() - glm::vec2(0.5f);
			AddPropOcc(props, occ, modelPath,
				toiletXZ, glm::vec2(toiletSize.x * 0.4f), floorY, glm::vec3(0.0f), glm::vec3(0.8f), "toilet");
		}

		// 2. Bathtub
		bool bathPlaced = false;
		float actualDepth = std::min(bathtubSize.x, bathtubSize.z);
		float actualLength = std::max(bathtubSize.x, bathtubSize.z);

		for (float bathScale = 1.0f; bathScale >= 0.3f && !bathPlaced; bathScale -= 0.05f)
		{
			glm::vec3 scaledSize = bathtubSize * bathScale;

			auto getBathWorldAABB = [&](glm::vec2 center, float yaw) -> std::pair<glm::vec2, glm::vec2> {
				float hX = scaledSize.x * 0.5f;
				float hZ = scaledSize.z * 0.5f;
				glm::vec2 corners[4] = {{-hX, -hZ}, {hX, -hZ}, {-hX, hZ}, {hX, hZ}};
				float rad = glm::radians(yaw);
				float cosA = cos(rad), sinA = sin(rad);
				glm::vec2 minW(1e10f), maxW(-1e10f);
				for (int i = 0; i < 4; i++) {
					glm::vec2 rotated(corners[i].x * cosA - corners[i].y * sinA, corners[i].x * sinA + corners[i].y * cosA);
					glm::vec2 worldPoint = center + rotated;
					minW = glm::min(minW, worldPoint);
					maxW = glm::max(maxW, worldPoint);
				}
				return {minW, maxW};
			};

			auto isValidPlacement = [&](glm::vec2 center, float yaw) -> bool {
				auto [minW, maxW] = getBathWorldAABB(center, yaw);
				if (minW.x < room.minBounds.x + 0.02f || maxW.x > room.maxBounds.x - 0.02f) return false;
				if (minW.y < room.minBounds.z + 0.02f || maxW.y > room.maxBounds.z - 0.02f) return false;
				glm::vec2 size = maxW - minW;
				return occ.CanPlace(center, size * 0.5f, 0.01f);
			};

			int wallPriority[4] = { 1, 0, 2, 3 };
			for (int wIdx = 0; wIdx < 4 && !bathPlaced; wIdx++)
			{
				int w = wallPriority[wIdx];
				std::vector<float> yaws = (w == 0 || w == 1) ? std::vector<float>{90.0f, -90.0f} : std::vector<float>{0.0f, 180.0f};

				for (float yaw : yaws)
				{
					auto [minW, maxW] = getBathWorldAABB(glm::vec2(0.0f), yaw);
					glm::vec2 bathHalf = (maxW - minW) * 0.5f;
					float wallOffset = (w == 0 || w == 1) ? (bathHalf.x + 0.01f) : (bathHalf.y + 0.01f);
					glm::vec2 bathXZ;

					if (occ.TryPlaceAlongWall(w, bathHalf, wallOffset, bathXZ, 0.01f, 0.1f, true))
					{
						if (isValidPlacement(bathXZ, yaw))
						{
							std::string modelPath = f.bathtub.path.empty() ? "Assets/Models/Bathroom/Model/Bathroom_props_set/Bathtub.fbx" : f.bathtub.path;
							AddPropOcc(props, occ, modelPath,
								bathXZ, bathHalf, floorY, glm::vec3(0.0f, yaw, 0.0f), glm::vec3(bathScale), "bathtub");
							bathPlaced = true;
							break;
						}
					}
				}
			}
		}

		if (!bathPlaced) // Force snap against wall 1 with scaled size
		{
			float bathScale = 0.5f;
			glm::vec3 scaledSize = bathtubSize * bathScale;
			float scaledDepth = std::min(scaledSize.x, scaledSize.z);
			float scaledLength = std::max(scaledSize.x, scaledSize.z);
			float wallOffset = scaledDepth * 0.5f + 0.01f;
			float cornerOffset = scaledLength * 0.5f + 0.01f;
			glm::vec2 forceXZ(room.maxBounds.x - wallOffset, room.maxBounds.z - cornerOffset);
			
			AddPropOcc(props, occ, "Assets/Models/Bathroom/Model/Bathroom_props_set/Bathtub.fbx",
				forceXZ, glm::vec2(scaledDepth * 0.5f, scaledLength * 0.5f), floorY, glm::vec3(0.0f, -90.0f, 0.0f), glm::vec3(bathScale), "bathtub");
		}

		// 3. Sink Vanity & Mirror
		bool sinkPlaced = false;
		for (float ss = 1.0f; ss >= 0.4f && !sinkPlaced; ss -= 0.1f)
		{
			float sD = sinkSize.z * ss;
			float sW = sinkSize.x * ss;

			for (int w = 0; w < 4 && !sinkPlaced; w++)
			{
				glm::vec2 sinkHalf = (w == 0 || w == 1) ? glm::vec2(sD * 0.5f, sW * 0.5f) : glm::vec2(sW * 0.5f, sD * 0.5f);
				glm::vec2 sinkXZ;

				if (occ.TryPlaceAlongWall(w, sinkHalf, sD * 0.5f + 0.05f, sinkXZ, 0.02f))
				{
					AddPropOcc(props, occ, "Assets/Models/Bathroom/Model/Bathroom_props_set/Bathroom_props_Set01.fbx",
						sinkXZ, sinkHalf, floorY, glm::vec3(0.0f, wallYaw(w), 0.0f), glm::vec3(ss), "sink");

					// Mirror on the wall above sink
					float yaw = wallYaw(w);
					glm::vec3 fwd(sin(glm::radians(yaw)), 0.0f, cos(glm::radians(yaw)));
					glm::vec3 mirrorPos = glm::vec3(sinkXZ.x, floorY + 1.4f, sinkXZ.y) - fwd * (sD * 0.5f - 0.01f);
					AddProp(props, "Assets/Models/Bedroom/Models/Interior/WallMirror_01.fbx",
						mirrorPos, glm::vec3(0.0f, yaw, 0.0f), glm::vec3(ss), "mirror");

					sinkPlaced = true;
				}
			}
		}
	}
}

// =====================================================================
// CorridorDecorator — Ceiling lamps at regular intervals
// =====================================================================

void CorridorDecorator::Decorate(
	std::map<int, MeshData>& meshBuckets,
	std::vector<PropPlacement>& props,
	const InteriorRoom& room,
	const std::vector<InteriorDoor>& doors,
	std::mt19937& rng,
	float floorHeight,
	const FurnitureSpecs& f,
	bool isCommercial)
{
	glm::vec3 bedSize = f.bed.size;
	glm::vec3 deskSize = f.desk.size;
	glm::vec3 tvSize = f.tv.size;
	glm::vec3 stoveSize = f.stove.size;
	glm::vec3 fridgeSize = f.fridge.size;
	glm::vec3 sinkSize = f.sink.size;
	glm::vec3 toiletSize = f.toilet.size;
	glm::vec3 bathtubSize = f.bathtub.size;
	glm::vec3 sofaSize = f.sofa.size;
	glm::vec3 coffeeTableSize = f.coffeeTable.size;
	glm::vec3 tvStandSize = f.tvStand.size;

	// Corridors get lights and nice wall trims too!
	GenerateRoomTrim(meshBuckets, room, floorHeight, isCommercial);
	PlaceCeilingLights(props, room, rng);

	float floorY = room.minBounds.y;
	RoomOccupancy occ(glm::vec2(room.minBounds.x, room.minBounds.z),
	                  glm::vec2(room.maxBounds.x, room.maxBounds.z));
	for (const auto& d : doors) occ.BlockDoor(d.position, d.width, d.runsAlongX, 0.15f);

	// Place a plant pot in one of the corners if space permits
	glm::vec2 plantHalf(0.2f);
	glm::vec2 plantXZ;
	if (occ.TryPlaceInCorner(plantHalf, plantXZ, 0.35f))
	{
		AddPropOcc(props, occ, "Assets/Models/Bedroom/Models/Interior/PlantPot_c_01.fbx",
			plantXZ, plantHalf, floorY, glm::vec3(0.0f), glm::vec3(1.0f), "decoration");
	}
}

// =====================================================================
// BedroomDecorator — High-fidelity Bed, TV Stand, TV, Lamp, Closet
// =====================================================================

void BedroomDecorator::Decorate(
	std::map<int, MeshData>& meshBuckets,
	std::vector<PropPlacement>& props,
	const InteriorRoom& room,
	const std::vector<InteriorDoor>& doors,
	std::mt19937& rng,
	float floorHeight,
	const FurnitureSpecs& f,
	bool isCommercial)
{
	glm::vec3 bedSize = f.bed.size;
	glm::vec3 deskSize = f.desk.size;
	glm::vec3 tvSize = f.tv.size;
	glm::vec3 stoveSize = f.stove.size;
	glm::vec3 fridgeSize = f.fridge.size;
	glm::vec3 sinkSize = f.sink.size;
	glm::vec3 toiletSize = f.toilet.size;
	glm::vec3 bathtubSize = f.bathtub.size;
	glm::vec3 sofaSize = f.sofa.size;
	glm::vec3 coffeeTableSize = f.coffeeTable.size;
	glm::vec3 tvStandSize = f.tvStand.size;

	GenerateRoomTrim(meshBuckets, room, floorHeight, isCommercial);
	PlaceCeilingLights(props, room, rng);

	float floorY = room.minBounds.y;
	RoomOccupancy occ(glm::vec2(room.minBounds.x, room.minBounds.z),
	                  glm::vec2(room.maxBounds.x, room.maxBounds.z));
	for (const auto& d : doors) occ.BlockDoor(d.position, d.width, d.runsAlongX, 0.15f);

	std::uniform_real_distribution<float> prob(0.0f, 1.0f);

	float bedLen = std::max(bedSize.x, bedSize.z);
	float bedW = std::min(bedSize.x, bedSize.z);

	bool bedPlaced = false;
	glm::vec2 bedXZ;
	float bedYaw = 0.0f;
	float sf = 1.0f;
	int bedWall = 0;

	// Progressive Scaling Fallback (ensures the bed ALWAYS spawns)
	for (float scale = 1.0f; scale >= 0.4f && !bedPlaced; scale -= 0.05f)
	{
		float fBL = bedLen * scale;
		float fBW = bedW * scale;
		int startWall = rng() % 4;

		for (int w = 0; w < 4 && !bedPlaced; w++)
		{
			int wall = (startWall + w) % 4;
			glm::vec2 halfSize = (wall < 2) ? glm::vec2(fBL * 0.5f, fBW * 0.5f) : glm::vec2(fBW * 0.5f, fBL * 0.5f);
			float wallOff = ((wall < 2) ? fBL : fBL) * 0.5f + 0.15f;

			if (occ.TryPlaceAlongWall(wall, halfSize, wallOff, bedXZ, 0.02f))
			{
				bedPlaced = true;
				bedYaw = (wall == 0) ? 90.0f : (wall == 1) ? -90.0f : (wall == 2) ? 0.0f : 180.0f;
				sf = scale;
				bedWall = wall;
			}
		}
	}

	// Absolute center fallback
	if (!bedPlaced)
	{
		sf = 0.5f;
		bedXZ = occ.GetRoomCenter();
		bedYaw = 0.0f;
		bedWall = 2;
		bedPlaced = true;
	}

	float fBL = bedLen * sf, fBW = bedW * sf;
	glm::vec2 bedHalf = (bedYaw == 90.0f || bedYaw == -90.0f) ? glm::vec2(fBL * 0.5f, fBW * 0.5f) : glm::vec2(fBW * 0.5f, fBL * 0.5f);

	// Register and place bed
	std::string bedPath = f.bed.path.empty() ? "Assets/Models/Bedroom/Models/Interior/Bed_01.fbx" : f.bed.path;
	AddPropOcc(props, occ, bedPath,
		bedXZ, bedHalf, floorY, glm::vec3(0.0f, bedYaw, 0.0f), glm::vec3(sf), "bed");

	// Rug under bed
	if (prob(rng) > 0.15f)
	{
		AddProp(props, "Assets/Models/Bedroom/Models/Interior/Rug_01.fbx",
			glm::vec3(bedXZ.x, floorY + 0.005f, bedXZ.y), glm::vec3(0.0f, bedYaw + 90.0f, 0.0f), glm::vec3(sf), "rug");
	}

	// 2. Nightstand next to bed head
	glm::vec3 fwd(sin(glm::radians(bedYaw)), 0.0f, cos(glm::radians(bedYaw)));
	glm::vec3 right(cos(glm::radians(bedYaw)), 0.0f, -sin(glm::radians(bedYaw)));
	int side = (prob(rng) > 0.5f) ? 1 : -1;
	glm::vec2 headXZ = bedXZ - glm::vec2(fwd.x, fwd.z) * (fBL * 0.5f);
	glm::vec2 nightXZ = headXZ + glm::vec2(right.x, right.z) * ((float)side * (fBW * 0.5f + 0.35f * sf));
	glm::vec2 nightHalf(0.25f * sf);

	if (occ.CanPlace(nightXZ, nightHalf, 0.01f) || true) // Force nightstand placement
	{
		AddPropOcc(props, occ, "Assets/Models/Bathroom/Model/Bathroom_props_set/Drawer.fbx",
			nightXZ, nightHalf, floorY, glm::vec3(0.0f, bedYaw, 0.0f), glm::vec3(0.8f * sf), "cabinet");
		if (prob(rng) > 0.25f)
		{
			AddProp(props, "Assets/Models/Bedroom/Models/Interior/NightLight_01.fbx",
				glm::vec3(nightXZ.x, floorY + 0.65f * sf, nightXZ.y), glm::vec3(0.0f, bedYaw, 0.0f), glm::vec3(sf), "lamp");
		}
	}

	// 3. TV Stand and TV directly on the opposite wall (perfect line of sight!)
	int oppWall = (bedWall < 2) ? (1 - bedWall) : (bedWall == 2 ? 3 : 2);
	glm::vec2 tvHalf;
	glm::vec2 tvXZ;
	bool tvPlaced = false;
	float tvScaleFactor = sf;

	// Scale fallback loop to ensure the TV and stand ALWAYS spawn
	for (float tvSf = sf; tvSf >= 0.3f && !tvPlaced; tvSf -= 0.05f)
	{
		// Swap X/Z half sizes for side walls because the TV stand is parallel to the wall
		tvHalf = (oppWall < 2) ? 
			glm::vec2(tvStandSize.z * 0.5f * tvSf, tvStandSize.x * 0.5f * tvSf) : 
			glm::vec2(tvStandSize.x * 0.5f * tvSf, tvStandSize.z * 0.5f * tvSf);

		float wallOff = tvStandSize.z * 0.5f * tvSf + 0.05f;
		if (occ.TryPlaceAlongWall(oppWall, tvHalf, wallOff, tvXZ, 0.01f))
		{
			tvPlaced = true;
			tvScaleFactor = tvSf;
		}
	}

	// Absolute fallback: if still not placed, force place on opposite wall with small scale
	if (!tvPlaced)
	{
		tvScaleFactor = 0.35f;
		tvHalf = (oppWall < 2) ? 
			glm::vec2(tvStandSize.z * 0.5f * tvScaleFactor, tvStandSize.x * 0.5f * tvScaleFactor) : 
			glm::vec2(tvStandSize.x * 0.5f * tvScaleFactor, tvStandSize.z * 0.5f * tvScaleFactor);
		
		float wallOff = tvStandSize.z * 0.5f * tvScaleFactor + 0.02f;
		tvXZ = occ.GetRoomCenter();
		// Snap towards opposite wall
		if (oppWall == 0) tvXZ.x = room.minBounds.x + wallOff;
		else if (oppWall == 1) tvXZ.x = room.maxBounds.x - wallOff;
		else if (oppWall == 2) tvXZ.y = room.minBounds.z + wallOff;
		else if (oppWall == 3) tvXZ.y = room.maxBounds.z - wallOff;

		tvPlaced = true;
	}

	if (tvPlaced)
	{
		float deskYaw = bedYaw + 180.0f;
		AddPropOcc(props, occ, "Assets/Models/Bedroom/Models/Interior/TvStand_01.fbx",
			tvXZ, tvHalf, floorY, glm::vec3(0.0f, deskYaw, 0.0f), glm::vec3(tvScaleFactor), "tv_stand");
		AddProp(props, "Assets/Models/Bedroom/Models/Interior/Tv_01.fbx",
			glm::vec3(tvXZ.x, floorY + tvStandSize.y * tvScaleFactor, tvXZ.y), glm::vec3(0.0f, deskYaw, 0.0f), glm::vec3(tvScaleFactor), "tv");
	}

	// 4. Closet (always try to add closet/wardrobe in corner)
	glm::vec2 closetHalf(0.45f * sf, 0.35f * sf);
	glm::vec2 closetXZ;
	if (occ.TryPlaceInCorner(closetHalf, closetXZ, 0.5f * sf, 0.01f))
	{
		AddPropOcc(props, occ, "Assets/Models/Bedroom/Models/Interior/Cupboard_a_01.fbx",
			closetXZ, closetHalf, floorY, glm::vec3(0.0f), glm::vec3(sf), "closet");
	}

	// 5. Standing Lamp in corner
	glm::vec2 lampHalf(0.15f * sf);
	glm::vec2 lampXZ;
	if (occ.TryPlaceInCorner(lampHalf, lampXZ, 0.3f * sf, 0.01f))
	{
		AddPropOcc(props, occ, "Assets/Models/Bedroom/Models/Interior/StandingLamp_01.fbx",
			lampXZ, lampHalf, floorY, glm::vec3(0.0f, 45.0f, 0.0f), glm::vec3(sf), "lamp");
	}
}

// =====================================================================
// KitchenDecorator — Fridge, Stove, Sink Table & Chairs, Countertop
// =====================================================================

void KitchenDecorator::Decorate(
	std::map<int, MeshData>& meshBuckets,
	std::vector<PropPlacement>& props,
	const InteriorRoom& room,
	const std::vector<InteriorDoor>& doors,
	std::mt19937& rng,
	float floorHeight,
	const FurnitureSpecs& f,
	bool isCommercial)
{
	glm::vec3 bedSize = f.bed.size;
	glm::vec3 deskSize = f.desk.size;
	glm::vec3 tvSize = f.tv.size;
	glm::vec3 stoveSize = f.stove.size;
	glm::vec3 fridgeSize = f.fridge.size;
	glm::vec3 sinkSize = f.sink.size;
	glm::vec3 toiletSize = f.toilet.size;
	glm::vec3 bathtubSize = f.bathtub.size;
	glm::vec3 sofaSize = f.sofa.size;
	glm::vec3 coffeeTableSize = f.coffeeTable.size;
	glm::vec3 tvStandSize = f.tvStand.size;

	GenerateRoomTrim(meshBuckets, room, floorHeight, isCommercial);
	float floorY = room.minBounds.y;

	RoomOccupancy occ(glm::vec2(room.minBounds.x, room.minBounds.z),
	                  glm::vec2(room.maxBounds.x, room.maxBounds.z));
	for (const auto& d : doors) occ.BlockDoor(d.position, d.width, d.runsAlongX, 0.15f);

	// Layout is aligned along wall 2 (-Z)
	float roomW = room.GetWidth();
	float gap = 0.15f, margin = 0.2f;
	float totalW = stoveSize.x + sinkSize.x + fridgeSize.x + gap * 2.0f + margin * 2.0f;
	float scale = (totalW > roomW) ? roomW / totalW : 1.0f;

	float sStoveW = stoveSize.x * scale, sSinkW = sinkSize.x * scale, sFridgeW = fridgeSize.x * scale;
	float sGap = gap * scale, sMargin = margin * scale;
	float usedW = sMargin + sStoveW + sGap + sSinkW + sGap + sFridgeW + sMargin;

	float centerX = (room.minBounds.x + room.maxBounds.x) * 0.5f;
	float rowStartX = centerX - usedW * 0.5f;

	float stoveX = rowStartX + sMargin + sStoveW * 0.5f;
	float sinkX  = stoveX + sStoveW * 0.5f + sGap + sSinkW * 0.5f;
	float fridgeX = sinkX + sSinkW * 0.5f + sGap + sFridgeW * 0.5f;

	float maxDepth = std::max({ stoveSize.z, sinkSize.z, fridgeSize.z }) * scale;
	float counterZ = room.minBounds.z + maxDepth * 0.5f + 0.05f;

	// Place main appliances
	AddPropOcc(props, occ, "Assets/Models/Kitchen/Models/Stove.fbx",
		glm::vec2(stoveX, counterZ), glm::vec2(sStoveW * 0.5f, stoveSize.z * scale * 0.5f),
		floorY, glm::vec3(0.0f), glm::vec3(scale), "stove");

	AddPropOcc(props, occ, "Assets/Models/Kitchen/Models/sink.fbx",
		glm::vec2(sinkX, counterZ), glm::vec2(sSinkW * 0.5f, sinkSize.z * scale * 0.5f),
		floorY, glm::vec3(0.0f), glm::vec3(scale), "sink");

	AddPropOcc(props, occ, "Assets/Models/Kitchen/Models/Fridge.fbx",
		glm::vec2(fridgeX, counterZ), glm::vec2(sFridgeW * 0.5f, fridgeSize.z * scale * 0.5f),
		floorY, glm::vec3(0.0f), glm::vec3(scale), "fridge");

	// 1. Procedural built-in countertop filling the gaps between stove, sink, and fridge
	float counterH = stoveSize.y * scale;
	float counterD = maxDepth;
	
	// Left countertop (between stove and sink)
	float gapL_W = (sinkX - sSinkW * 0.5f) - (stoveX + sStoveW * 0.5f);
	if (gapL_W > 0.1f)
	{
		float gapL_X = (stoveX + sStoveW * 0.5f) + gapL_W * 0.5f;
		meshBuckets[MAT_WOOD].Append(IInteriorDecorator::MakeBox(
			glm::vec3(gapL_X, floorY + counterH * 0.5f, counterZ),
			glm::vec3(gapL_W * 0.5f, counterH * 0.5f, counterD * 0.5f)
		));
	}

	// Right countertop (between sink and fridge)
	float gapR_W = (fridgeX - sFridgeW * 0.5f) - (sinkX + sSinkW * 0.5f);
	if (gapR_W > 0.1f)
	{
		float gapR_X = (sinkX + sSinkW * 0.5f) + gapR_W * 0.5f;
		meshBuckets[MAT_WOOD].Append(IInteriorDecorator::MakeBox(
			glm::vec3(gapR_X, floorY + counterH * 0.5f, counterZ),
			glm::vec3(gapR_W * 0.5f, counterH * 0.5f, counterD * 0.5f)
		));

		// 2. Procedural built-in Dishwasher: place metallic front dishwasher in this countertop gap!
		if (gapR_W >= 0.6f)
		{
			// Dishwasher fits! Make a metallic box door under the counter
			meshBuckets[MAT_METAL].Append(IInteriorDecorator::MakeBox(
				glm::vec3(gapR_X, floorY + counterH * 0.45f, counterZ + counterD * 0.5f - 0.01f),
				glm::vec3(0.28f * scale, counterH * 0.45f, 0.02f)
			));
		}
	}

	// Small microwave & toaster on countertop
	AddProp(props, "Assets/Models/Kitchen/Models/Microwave.fbx",
		glm::vec3((stoveX + sinkX) * 0.5f, floorY + counterH + 0.02f, counterZ),
		glm::vec3(0.0f), glm::vec3(0.9f * scale), "appliances");
	AddProp(props, "Assets/Models/Kitchen/Models/Toaster.fbx",
		glm::vec3((sinkX + fridgeX) * 0.5f, floorY + counterH + 0.02f, counterZ),
		glm::vec3(0.0f), glm::vec3(0.9f * scale), "appliances");

	// 3. Dining table and chairs (only if occupancy allows)
	float counterFrontZ = counterZ + maxDepth * 0.5f;
	float tableDepthNeeded = 1.5f;
	float remainingD = room.maxBounds.z - counterFrontZ;

	if (remainingD > tableDepthNeeded + 0.5f)
	{
		float tableZ = counterFrontZ + 0.8f + tableDepthNeeded * 0.5f;
		glm::vec2 tableXZ(centerX, tableZ);
		glm::vec2 tableHalf(0.5f * scale, 0.4f * scale);

		if (occ.CanPlace(tableXZ, tableHalf, 0.01f))
		{
			AddPropOcc(props, occ, "Assets/Models/Kitchen/Models/Table.fbx",
				tableXZ, tableHalf, floorY, glm::vec3(0.0f, 90.0f, 0.0f), glm::vec3(scale), "desk");

			glm::vec2 chairHalf(0.25f * scale);
			glm::vec2 chairL(centerX - 0.6f * scale, tableZ);
			if (occ.CanPlace(chairL, chairHalf, 0.01f))
			{
				AddPropOcc(props, occ, "Assets/Models/Kitchen/Models/Chair.fbx",
					chairL, chairHalf, floorY, glm::vec3(0.0f, 90.0f, 0.0f), glm::vec3(scale), "chair");
			}

			glm::vec2 chairR(centerX + 0.6f * scale, tableZ);
			if (occ.CanPlace(chairR, chairHalf, 0.01f))
			{
				AddPropOcc(props, occ, "Assets/Models/Kitchen/Models/Chair.fbx",
					chairR, chairHalf, floorY, glm::vec3(0.0f, -90.0f, 0.0f), glm::vec3(scale), "chair");
			}
		}
	}
}

// =====================================================================
// LobbyDecorator — Reception stand, Glass Table, Sofa Bench, TV Cabinet
// =====================================================================

void LobbyDecorator::Decorate(
	std::map<int, MeshData>& meshBuckets,
	std::vector<PropPlacement>& props,
	const InteriorRoom& room,
	const std::vector<InteriorDoor>& doors,
	std::mt19937& rng,
	float floorHeight,
	const FurnitureSpecs& f,
	bool isCommercial)
{
	glm::vec3 bedSize = f.bed.size;
	glm::vec3 deskSize = f.desk.size;
	glm::vec3 tvSize = f.tv.size;
	glm::vec3 stoveSize = f.stove.size;
	glm::vec3 fridgeSize = f.fridge.size;
	glm::vec3 sinkSize = f.sink.size;
	glm::vec3 toiletSize = f.toilet.size;
	glm::vec3 bathtubSize = f.bathtub.size;
	glm::vec3 sofaSize = f.sofa.size;
	glm::vec3 coffeeTableSize = f.coffeeTable.size;
	glm::vec3 tvStandSize = f.tvStand.size;

	GenerateRoomTrim(meshBuckets, room, floorHeight, isCommercial);
	PlaceCeilingLights(props, room, rng);

	float floorY = room.minBounds.y;
	glm::vec3 roomMin = room.minBounds;
	glm::vec3 roomMax = room.maxBounds;
	float centerX = (roomMin.x + roomMax.x) * 0.5f;
	float centerZ = (roomMin.z + roomMax.z) * 0.5f;

	RoomOccupancy occ(glm::vec2(roomMin.x, roomMin.z), glm::vec2(roomMax.x, roomMax.z));
	for (const auto& d : doors) occ.BlockDoor(d.position, d.width, d.runsAlongX, 0.15f);

	// 1. Coffee table in the center
	glm::vec2 tableHalf(coffeeTableSize.x * 0.5f, coffeeTableSize.z * 0.5f);
	glm::vec2 tableXZ(centerX, centerZ + 0.2f);
	if (occ.CanPlace(tableXZ, tableHalf, 0.01f))
	{
		AddPropOcc(props, occ, "Assets/Models/Livingroom/glass_table/glass_table.FBX",
			tableXZ, tableHalf, floorY, glm::vec3(0.0f), glm::vec3(1.0f), "coffee_table");
	}

	// 2. Sofa behind the coffee table
	glm::vec2 sofaHalf(sofaSize.x * 0.5f, sofaSize.z * 0.5f);
	glm::vec2 sofaXZ(centerX, centerZ + 1.0f);
	if (occ.CanPlace(sofaXZ, sofaHalf, 0.01f))
	{
		AddPropOcc(props, occ, "Assets/Models/Livingroom/interior/bank.FBX",
			sofaXZ, sofaHalf, floorY, glm::vec3(0.0f, 180.0f, 0.0f), glm::vec3(1.0f), "couch");
	}

	// 3. TV Cabinet on the opposite side of the table
	glm::vec2 tvHalf(tvStandSize.x * 0.5f, tvStandSize.z * 0.5f);
	glm::vec2 tvXZ(centerX, centerZ - 0.7f);
	if (occ.CanPlace(tvXZ, tvHalf, 0.01f))
	{
		AddPropOcc(props, occ, "Assets/Models/Livingroom/tumba_fur/tumba_fur.FBX",
			tvXZ, tvHalf, floorY, glm::vec3(0.0f), glm::vec3(1.0f), "tv_stand");
		AddProp(props, "Assets/Models/Bedroom/Models/Interior/Tv_01.fbx",
			glm::vec3(tvXZ.x, floorY + tvStandSize.y, tvXZ.y), glm::vec3(0.0f), glm::vec3(1.0f), "tv");

		// Speakers on both sides of TV Cabinet
		float spkOff = std::min(1.0f, (room.GetWidth() - 1.0f) * 0.5f);
		glm::vec2 spkHalf(0.2f, 0.2f);
		glm::vec2 spkL(centerX - spkOff, tvXZ.y);
		if (occ.CanPlace(spkL, spkHalf, 0.01f))
		{
			AddPropOcc(props, occ, "Assets/Models/Livingroom/speaker/speaker.FBX",
				spkL, spkHalf, floorY, glm::vec3(0.0f), glm::vec3(1.0f), "speaker");
		}
		glm::vec2 spkR(centerX + spkOff, tvXZ.y);
		if (occ.CanPlace(spkR, spkHalf, 0.01f))
		{
			AddPropOcc(props, occ, "Assets/Models/Livingroom/speaker/speaker.FBX",
				spkR, spkHalf, floorY, glm::vec3(0.0f), glm::vec3(1.0f), "speaker");
		}
	}

	// 4. Printer in one of the corners
	glm::vec2 printerHalf(0.25f, 0.2f);
	glm::vec2 printerXZ;
	if (occ.TryPlaceInCorner(printerHalf, printerXZ, 0.4f, 0.01f))
	{
		AddPropOcc(props, occ, "Assets/Models/Livingroom/printer/printer.FBX",
			printerXZ, printerHalf, floorY, glm::vec3(0.0f, 45.0f, 0.0f), glm::vec3(1.0f), "office");
	}
}

// =====================================================================
// Factory
// =====================================================================

std::unique_ptr<IInteriorDecorator> CreateDecoratorForRoom(RoomType type)
{
	switch (type)
	{
		case RoomType::Office:    return std::make_unique<OfficeDecorator>();
		case RoomType::Bathroom:  return std::make_unique<BathroomDecorator>();
		case RoomType::Corridor:  return std::make_unique<CorridorDecorator>();
		case RoomType::Bedroom:   return std::make_unique<BedroomDecorator>();
		case RoomType::Kitchen:   return std::make_unique<KitchenDecorator>();
		case RoomType::Lobby:     return std::make_unique<LobbyDecorator>();
		default:                  return std::make_unique<CorridorDecorator>();
	}
}
