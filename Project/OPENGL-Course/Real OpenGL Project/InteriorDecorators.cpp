#include "InteriorDecorators.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>

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
	std::mt19937& rng,
	float floorHeight,
	glm::vec3 bedSize,
	glm::vec3 deskSize,
	glm::vec3 tvSize,
	glm::vec3 stoveSize,
	glm::vec3 fridgeSize,
	glm::vec3 sinkSize,
	glm::vec3 toiletSize,
	glm::vec3 bathtubSize)
{
	std::uniform_real_distribution<float> prob(0.0f, 1.0f);
	float floorY = room.minBounds.y;

	RoomOccupancy occ(glm::vec2(room.minBounds.x, room.minBounds.z),
	                  glm::vec2(room.maxBounds.x, room.maxBounds.z));

	float deskW = deskSize.x, deskD = deskSize.z, deskH = deskSize.y;

	// Adaptive scale
	float scaleFactor = 1.0f;
	if (deskW + 0.4f > room.GetWidth()) scaleFactor = std::min(scaleFactor, (room.GetWidth() - 0.4f) / deskW);
	if (deskD + 0.4f > room.GetDepth()) scaleFactor = std::min(scaleFactor, (room.GetDepth() - 0.4f) / deskD);
	float fDW = deskW * scaleFactor, fDD = deskD * scaleFactor;

	// Try each wall for the desk using occupancy
	std::uniform_int_distribution<int> wallDist(0, 3);
	int startWall = wallDist(rng);
	glm::vec2 deskXZ;
	bool deskPlaced = false;

	for (int attempt = 0; attempt < 4 && !deskPlaced; attempt++)
	{
		int wall = (startWall + attempt) % 4;
		// For walls 0,1 (X walls): item depth goes into X, width along Z
		// For walls 2,3 (Z walls): item depth goes into Z, width along X
		glm::vec2 halfSize = (wall < 2) ? glm::vec2(fDD * 0.5f, fDW * 0.5f) : glm::vec2(fDW * 0.5f, fDD * 0.5f);
		float wallOff = ((wall < 2) ? fDD : fDD) * 0.5f + 0.15f;
		if (occ.TryPlaceAlongWall(wall, halfSize, wallOff, deskXZ))
		{
			deskPlaced = true;
			float deskYaw = (wall == 0) ? 90.0f : (wall == 1) ? -90.0f : (wall == 2) ? 0.0f : 180.0f;
			std::uniform_real_distribution<float> rotJ(-4.0f, 4.0f);
			float finalYaw = deskYaw + rotJ(rng);

			AddPropOcc(props, occ, "Assets/Models/Bathroom/Model/Bathroom_props_set/Iron_Wooden_Table.fbx",
				deskXZ, halfSize, floorY, glm::vec3(0.0f, finalYaw, 0.0f), glm::vec3(scaleFactor), "desk");

			// Chair in front of desk
			glm::vec3 fwd(sin(glm::radians(deskYaw)), 0.0f, cos(glm::radians(deskYaw)));
			glm::vec2 chairXZ = deskXZ + glm::vec2(fwd.x, fwd.z) * (fDD * 0.5f + 0.4f * scaleFactor);
			glm::vec2 chairHalf(0.25f * scaleFactor);
			if (occ.CanPlace(chairXZ, chairHalf))
			{
				AddPropOcc(props, occ, "Assets/Models/Kitchen/Models/Chair.fbx",
					chairXZ, chairHalf, floorY, glm::vec3(0.0f, deskYaw + 180.0f + rotJ(rng), 0.0f), glm::vec3(scaleFactor), "chair");
			}

			// Monitor on desk
			if (prob(rng) > 0.4f)
			{
				AddProp(props, "Assets/Models/Bedroom/Models/Interior/Tv_01.fbx",
					glm::vec3(deskXZ.x, floorY + deskH * scaleFactor, deskXZ.y),
					glm::vec3(0.0f, finalYaw, 0.0f), glm::vec3(0.7f * scaleFactor), "monitor");
			}

			// Rug under desk (no occupancy needed — it's flat)
			if (prob(rng) > 0.3f)
			{
				AddProp(props, "Assets/Models/Bedroom/Models/Interior/Rug_01.fbx",
					glm::vec3(deskXZ.x, floorY + 0.005f, deskXZ.y),
					glm::vec3(0.0f, finalYaw, 0.0f), glm::vec3(0.6f * scaleFactor, 1.0f, 0.6f * scaleFactor), "rug");
			}

			// Cabinet next to desk
			glm::vec3 right(cos(glm::radians(deskYaw)), 0.0f, -sin(glm::radians(deskYaw)));
			glm::vec2 cabXZ = deskXZ + glm::vec2(right.x, right.z) * (fDW * 0.5f + 0.4f * scaleFactor);
			glm::vec2 cabHalf(0.3f * scaleFactor);
			if (occ.CanPlace(cabXZ, cabHalf))
			{
				AddPropOcc(props, occ, "Assets/Models/Bathroom/Model/Bathroom_props_set/Drawer.fbx",
					cabXZ, cabHalf, floorY, glm::vec3(0.0f, finalYaw, 0.0f), glm::vec3(scaleFactor), "cabinet");
			}
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
	std::mt19937& rng,
	float floorHeight,
	glm::vec3 bedSize,
	glm::vec3 deskSize,
	glm::vec3 tvSize,
	glm::vec3 stoveSize,
	glm::vec3 fridgeSize,
	glm::vec3 sinkSize,
	glm::vec3 toiletSize,
	glm::vec3 bathtubSize)
{
	float floorY = room.minBounds.y;
	RoomOccupancy occ(glm::vec2(room.minBounds.x, room.minBounds.z),
	                  glm::vec2(room.maxBounds.x, room.maxBounds.z));

	// 1. Toilet — try -Z wall first, then others
	glm::vec2 toiletHalf(0.3f, 0.35f);
	glm::vec2 toiletXZ;
	if (occ.TryPlaceAlongWall(2, toiletHalf, 0.35f, toiletXZ))
	{
		AddPropOcc(props, occ, "Assets/Models/Bathroom/Model/Bathroom_props_set/Bathroom_Props_Set02.fbx",
			toiletXZ, toiletHalf, floorY, glm::vec3(0.0f), glm::vec3(1.0f), "toilet");
	}

	// 2. Sink — try +X wall
	glm::vec2 sinkHalf(0.3f, 0.3f);
	glm::vec2 sinkXZ;
	if (occ.TryPlaceAlongWall(1, sinkHalf, 0.35f, sinkXZ))
	{
		AddPropOcc(props, occ, "Assets/Models/Bathroom/Model/Bathroom_props_set/Bathroom_props_Set01.fbx",
			sinkXZ, sinkHalf, floorY, glm::vec3(0.0f, 90.0f, 0.0f), glm::vec3(1.0f), "sink");

		// 3. Washing Machine — next to sink on same wall
		glm::vec2 wmHalf(0.35f, 0.35f);
		glm::vec2 wmXZ;
		if (occ.TryPlaceAlongWall(1, wmHalf, 0.35f, wmXZ))
		{
			AddPropOcc(props, occ, "Assets/Models/Bathroom/Model/Bathroom_props_set/Washing_Machine.fbx",
				wmXZ, wmHalf, floorY, glm::vec3(0.0f, 90.0f, 0.0f), glm::vec3(1.0f), "washing_machine");
		}
	}

	// 4. Wooden Rack — try a corner
	glm::vec2 rackHalf(0.25f, 0.2f);
	glm::vec2 rackXZ;
	if (occ.TryPlaceInCorner(rackHalf, rackXZ, 0.35f))
	{
		AddPropOcc(props, occ, "Assets/Models/Bathroom/Model/Bathroom_props_set/Wooden_Rack.fbx",
			rackXZ, rackHalf, floorY, glm::vec3(0.0f), glm::vec3(1.0f), "cabinet");
	}
}

// =====================================================================
// CorridorDecorator — Ceiling lamps at regular intervals (static batched)
// =====================================================================

void CorridorDecorator::Decorate(
	std::map<int, MeshData>& meshBuckets,
	std::vector<PropPlacement>& props,
	const InteriorRoom& room,
	std::mt19937& rng,
	float floorHeight,
	glm::vec3 bedSize,
	glm::vec3 deskSize,
	glm::vec3 tvSize,
	glm::vec3 stoveSize,
	glm::vec3 fridgeSize,
	glm::vec3 sinkSize,
	glm::vec3 toiletSize,
	glm::vec3 bathtubSize)
{
	float ceilingY = room.maxBounds.y - 0.02f;
	float centerX = (room.minBounds.x + room.maxBounds.x) * 0.5f;
	float centerZ = (room.minBounds.z + room.maxBounds.z) * 0.5f;

	// Determine corridor direction
	bool longX = room.GetWidth() > room.GetDepth();
	float corridorLen = longX ? room.GetWidth() : room.GetDepth();
	float lampSpacing = 3.0f; // one lamp every 3m
	int numLamps = std::max(1, (int)(corridorLen / lampSpacing));

	// Corridors will remain blank unless actual FBX props are added.
}

// =====================================================================
// BedroomDecorator — High-fidelity Bed, TV Stand, TV, Lamp, Closet
// =====================================================================

void BedroomDecorator::Decorate(
	std::map<int, MeshData>& meshBuckets,
	std::vector<PropPlacement>& props,
	const InteriorRoom& room,
	std::mt19937& rng,
	float floorHeight,
	glm::vec3 bedSize,
	glm::vec3 deskSize,
	glm::vec3 tvSize,
	glm::vec3 stoveSize,
	glm::vec3 fridgeSize,
	glm::vec3 sinkSize,
	glm::vec3 toiletSize,
	glm::vec3 bathtubSize)
{
	std::uniform_real_distribution<float> prob(0.0f, 1.0f);
	float floorY = room.minBounds.y;

	RoomOccupancy occ(glm::vec2(room.minBounds.x, room.minBounds.z),
	                  glm::vec2(room.maxBounds.x, room.maxBounds.z));

	float bedLen = std::max(bedSize.x, bedSize.z);
	float bedW = std::min(bedSize.x, bedSize.z);
	float sf = 1.0f;
	if (bedLen + 0.4f > room.GetDepth()) sf = std::min(sf, (room.GetDepth() - 0.4f) / bedLen);
	if (bedW + 0.4f > room.GetWidth()) sf = std::min(sf, (room.GetWidth() - 0.4f) / bedW);
	float fBL = bedLen * sf, fBW = bedW * sf;

	// 1. Bed — try walls using occupancy
	std::uniform_int_distribution<int> wallDist(0, 3);
	int startWall = wallDist(rng);
	glm::vec2 bedXZ;
	bool bedPlaced = false;

	for (int attempt = 0; attempt < 4 && !bedPlaced; attempt++)
	{
		int wall = (startWall + attempt) % 4;
		glm::vec2 halfSize = (wall < 2) ? glm::vec2(fBL * 0.5f, fBW * 0.5f) : glm::vec2(fBW * 0.5f, fBL * 0.5f);
		float wallOff = ((wall < 2) ? fBL : fBL) * 0.5f + 0.15f;
		if (occ.TryPlaceAlongWall(wall, halfSize, wallOff, bedXZ))
		{
			bedPlaced = true;
			float bedYaw = (wall == 0) ? 90.0f : (wall == 1) ? -90.0f : (wall == 2) ? 0.0f : 180.0f;

			AddPropOcc(props, occ, "Assets/Models/Bedroom/Models/Interior/Bed_01.fbx",
				bedXZ, halfSize, floorY, glm::vec3(0.0f, bedYaw, 0.0f), glm::vec3(sf), "bed");

			// Rug under bed
			if (prob(rng) > 0.2f)
				AddProp(props, "Assets/Models/Bedroom/Models/Interior/Rug_01.fbx",
					glm::vec3(bedXZ.x, floorY + 0.005f, bedXZ.y), glm::vec3(0.0f, bedYaw + 90.0f, 0.0f), glm::vec3(sf), "rug");

			// 2. Nightstand next to bed head
			glm::vec3 fwd(sin(glm::radians(bedYaw)), 0.0f, cos(glm::radians(bedYaw)));
			glm::vec3 right(cos(glm::radians(bedYaw)), 0.0f, -sin(glm::radians(bedYaw)));
			int side = (prob(rng) > 0.5f) ? 1 : -1;
			glm::vec2 headXZ = bedXZ - glm::vec2(fwd.x, fwd.z) * (fBL * 0.5f);
			glm::vec2 nightXZ = headXZ + glm::vec2(right.x, right.z) * ((float)side * (fBW * 0.5f + 0.35f * sf));
			glm::vec2 nightHalf(0.25f * sf);

			if (occ.CanPlace(nightXZ, nightHalf))
			{
				AddPropOcc(props, occ, "Assets/Models/Bathroom/Model/Bathroom_props_set/Drawer.fbx",
					nightXZ, nightHalf, floorY, glm::vec3(0.0f, bedYaw, 0.0f), glm::vec3(0.8f * sf), "cabinet");
				if (prob(rng) > 0.3f)
					AddProp(props, "Assets/Models/Bedroom/Models/Interior/NightLight_01.fbx",
						glm::vec3(nightXZ.x, floorY + 0.65f * sf, nightXZ.y), glm::vec3(0.0f, bedYaw, 0.0f), glm::vec3(sf), "lamp");
			}

			// 3. TV Stand on opposite wall
			int oppWall = (wall < 2) ? (1 - wall) : (wall == 2 ? 3 : 2);
			glm::vec2 tvHalf(deskSize.x * 0.5f, deskSize.z * 0.5f);
			glm::vec2 tvXZ;
			if (occ.TryPlaceAlongWall(oppWall, tvHalf, deskSize.z * 0.5f + 0.1f, tvXZ))
			{
				float deskYaw = bedYaw + 180.0f;
				AddPropOcc(props, occ, "Assets/Models/Bedroom/Models/Interior/TvStand_01.fbx",
					tvXZ, tvHalf, floorY, glm::vec3(0.0f, deskYaw, 0.0f), glm::vec3(1.0f), "desk");
				AddProp(props, "Assets/Models/Bedroom/Models/Interior/Tv_01.fbx",
					glm::vec3(tvXZ.x, floorY + deskSize.y, tvXZ.y), glm::vec3(0.0f, deskYaw, 0.0f), glm::vec3(1.0f), "tv");
			}
		}
	}

	// 4. Closet — try corners
	glm::vec2 closetHalf(0.4f, 0.3f);
	glm::vec2 closetXZ;
	if (occ.TryPlaceInCorner(closetHalf, closetXZ, 0.5f))
		AddPropOcc(props, occ, "Assets/Models/Bedroom/Models/Interior/Cupboard_a_01.fbx",
			closetXZ, closetHalf, floorY, glm::vec3(0.0f), glm::vec3(1.0f), "closet");

	// 5. Standing Lamp — try corners
	glm::vec2 lampHalf(0.15f);
	glm::vec2 lampXZ;
	if (occ.TryPlaceInCorner(lampHalf, lampXZ, 0.3f))
		AddPropOcc(props, occ, "Assets/Models/Bedroom/Models/Interior/StandingLamp_01.fbx",
			lampXZ, lampHalf, floorY, glm::vec3(0.0f, 45.0f, 0.0f), glm::vec3(1.0f), "lamp");
}

// =====================================================================
// KitchenDecorator — Fridge, Stove, Sink Table & Chairs, Countertop
// =====================================================================

void KitchenDecorator::Decorate(
	std::map<int, MeshData>& meshBuckets,
	std::vector<PropPlacement>& props,
	const InteriorRoom& room,
	std::mt19937& rng,
	float floorHeight,
	glm::vec3 bedSize,
	glm::vec3 deskSize,
	glm::vec3 tvSize,
	glm::vec3 stoveSize,
	glm::vec3 fridgeSize,
	glm::vec3 sinkSize,
	glm::vec3 toiletSize,
	glm::vec3 bathtubSize)
{
	float floorY = room.minBounds.y;
	glm::vec3 roomMin = room.minBounds;
	glm::vec3 roomMax = room.maxBounds;
	float roomW = room.GetWidth();

	RoomOccupancy occ(glm::vec2(roomMin.x, roomMin.z), glm::vec2(roomMax.x, roomMax.z));

	// Smart Side-by-Side Layout with Adaptive Scaling
	float gap = 0.15f, margin = 0.2f;
	float totalW = stoveSize.x + sinkSize.x + fridgeSize.x + gap * 2.0f + margin * 2.0f;
	float scale = (totalW > roomW) ? roomW / totalW : 1.0f;

	float sStoveW = stoveSize.x * scale, sSinkW = sinkSize.x * scale, sFridgeW = fridgeSize.x * scale;
	float sGap = gap * scale, sMargin = margin * scale;
	float usedW = sMargin + sStoveW + sGap + sSinkW + sGap + sFridgeW + sMargin;

	float centerX = (roomMin.x + roomMax.x) * 0.5f;
	float rowStartX = centerX - usedW * 0.5f;

	float stoveX = rowStartX + sMargin + sStoveW * 0.5f;
	float sinkX  = stoveX + sStoveW * 0.5f + sGap + sSinkW * 0.5f;
	float fridgeX = sinkX + sSinkW * 0.5f + sGap + sFridgeW * 0.5f;

	float maxDepth = std::max({ stoveSize.z, sinkSize.z, fridgeSize.z }) * scale;
	float counterZ = roomMin.z + maxDepth * 0.5f + 0.05f;

	// Register and place appliances
	AddPropOcc(props, occ, "Assets/Models/Kitchen/Models/Stove.fbx",
		glm::vec2(stoveX, counterZ), glm::vec2(sStoveW * 0.5f, stoveSize.z * scale * 0.5f),
		floorY, glm::vec3(0.0f), glm::vec3(scale), "stove");

	AddPropOcc(props, occ, "Assets/Models/Kitchen/Models/sink.fbx",
		glm::vec2(sinkX, counterZ), glm::vec2(sSinkW * 0.5f, sinkSize.z * scale * 0.5f),
		floorY, glm::vec3(0.0f), glm::vec3(scale), "sink");

	AddPropOcc(props, occ, "Assets/Models/Kitchen/Models/Fridge.fbx",
		glm::vec2(fridgeX, counterZ), glm::vec2(sFridgeW * 0.5f, fridgeSize.z * scale * 0.5f),
		floorY, glm::vec3(0.0f), glm::vec3(scale), "fridge");

	// Microwave/Toaster — use actual stove height instead of hardcoded 0.9
	float counterH = stoveSize.y * scale;
	AddProp(props, "Assets/Models/Kitchen/Models/Microwave.fbx",
		glm::vec3((stoveX + sinkX) * 0.5f, floorY + counterH + 0.02f, counterZ),
		glm::vec3(0.0f), glm::vec3(0.9f * scale), "appliances");
	AddProp(props, "Assets/Models/Kitchen/Models/Toaster.fbx",
		glm::vec3((sinkX + fridgeX) * 0.5f, floorY + counterH + 0.02f, counterZ),
		glm::vec3(0.0f), glm::vec3(0.9f * scale), "appliances");

	// Kitchen Table and Chairs — only if occupancy allows
	float counterFrontZ = counterZ + maxDepth * 0.5f;
	float tableDepthNeeded = 1.5f;
	float remainingD = roomMax.z - counterFrontZ;

	if (remainingD > tableDepthNeeded + 0.5f)
	{
		float tableZ = counterFrontZ + 0.8f + tableDepthNeeded * 0.5f;
		glm::vec2 tableXZ(centerX, tableZ);
		glm::vec2 tableHalf(0.5f * scale, 0.4f * scale);

		if (occ.CanPlace(tableXZ, tableHalf))
		{
			AddPropOcc(props, occ, "Assets/Models/Kitchen/Models/Table.fbx",
				tableXZ, tableHalf, floorY, glm::vec3(0.0f, 90.0f, 0.0f), glm::vec3(scale), "desk");

			glm::vec2 chairHalf(0.25f * scale);
			glm::vec2 chairL(centerX - 0.6f * scale, tableZ);
			if (occ.CanPlace(chairL, chairHalf))
				AddPropOcc(props, occ, "Assets/Models/Kitchen/Models/Chair.fbx",
					chairL, chairHalf, floorY, glm::vec3(0.0f, 90.0f, 0.0f), glm::vec3(scale), "chair");

			glm::vec2 chairR(centerX + 0.6f * scale, tableZ);
			if (occ.CanPlace(chairR, chairHalf))
				AddPropOcc(props, occ, "Assets/Models/Kitchen/Models/Chair.fbx",
					chairR, chairHalf, floorY, glm::vec3(0.0f, -90.0f, 0.0f), glm::vec3(scale), "chair");
		}
	}
}

// =====================================================================
// LobbyDecorator — Reception stand, Glass Table, Sofa Bench, TV Cabinet, Speakers, Printer
// =====================================================================

void LobbyDecorator::Decorate(
	std::map<int, MeshData>& meshBuckets,
	std::vector<PropPlacement>& props,
	const InteriorRoom& room,
	std::mt19937& rng,
	float floorHeight,
	glm::vec3 bedSize,
	glm::vec3 deskSize,
	glm::vec3 tvSize,
	glm::vec3 stoveSize,
	glm::vec3 fridgeSize,
	glm::vec3 sinkSize,
	glm::vec3 toiletSize,
	glm::vec3 bathtubSize)
{
	float floorY = room.minBounds.y;
	glm::vec3 roomMin = room.minBounds;
	glm::vec3 roomMax = room.maxBounds;
	float centerX = (roomMin.x + roomMax.x) * 0.5f;
	float centerZ = (roomMin.z + roomMax.z) * 0.5f;

	RoomOccupancy occ(glm::vec2(roomMin.x, roomMin.z), glm::vec2(roomMax.x, roomMax.z));

	// 1. Coffee table — center
	glm::vec2 tableHalf(0.5f, 0.35f);
	glm::vec2 tableXZ(centerX, centerZ + 0.2f);
	if (occ.CanPlace(tableXZ, tableHalf))
		AddPropOcc(props, occ, "Assets/Models/Livingroom/glass_table/glass_table.FBX",
			tableXZ, tableHalf, floorY, glm::vec3(0.0f), glm::vec3(1.1f), "coffee_table");

	// 2. Sofa behind table
	glm::vec2 sofaHalf(0.8f, 0.4f);
	glm::vec2 sofaXZ(centerX, centerZ + 1.0f);
	if (occ.CanPlace(sofaXZ, sofaHalf))
		AddPropOcc(props, occ, "Assets/Models/Livingroom/interior/bank.FBX",
			sofaXZ, sofaHalf, floorY, glm::vec3(0.0f, 180.0f, 0.0f), glm::vec3(1.0f), "couch");

	// 3. TV Cabinet — opposite side
	glm::vec2 tvHalf(0.6f, 0.3f);
	glm::vec2 tvXZ(centerX, centerZ - 0.7f);
	if (occ.CanPlace(tvXZ, tvHalf))
	{
		AddPropOcc(props, occ, "Assets/Models/Livingroom/tumba_fur/tumba_fur.FBX",
			tvXZ, tvHalf, floorY, glm::vec3(0.0f), glm::vec3(1.0f), "tv_stand");
		AddProp(props, "Assets/Models/Bedroom/Models/Interior/Tv_01.fbx",
			glm::vec3(tvXZ.x, floorY + 0.45f, tvXZ.y), glm::vec3(0.0f), glm::vec3(0.9f), "tv");

		// 4. Speakers — clamp inward if room is narrow
		float spkOff = std::min(1.0f, (room.GetWidth() - 1.0f) * 0.5f);
		glm::vec2 spkHalf(0.2f, 0.2f);
		glm::vec2 spkL(centerX - spkOff, tvXZ.y);
		if (occ.CanPlace(spkL, spkHalf))
			AddPropOcc(props, occ, "Assets/Models/Livingroom/speaker/speaker.FBX",
				spkL, spkHalf, floorY, glm::vec3(0.0f), glm::vec3(1.0f), "speaker");
		glm::vec2 spkR(centerX + spkOff, tvXZ.y);
		if (occ.CanPlace(spkR, spkHalf))
			AddPropOcc(props, occ, "Assets/Models/Livingroom/speaker/speaker.FBX",
				spkR, spkHalf, floorY, glm::vec3(0.0f), glm::vec3(1.0f), "speaker");
	}

	// 5. Printer — try corners
	glm::vec2 printerHalf(0.25f, 0.2f);
	glm::vec2 printerXZ;
	if (occ.TryPlaceInCorner(printerHalf, printerXZ, 0.4f))
		AddPropOcc(props, occ, "Assets/Models/Livingroom/printer/printer.FBX",
			printerXZ, printerHalf, floorY, glm::vec3(0.0f, 45.0f, 0.0f), glm::vec3(1.0f), "office");
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
