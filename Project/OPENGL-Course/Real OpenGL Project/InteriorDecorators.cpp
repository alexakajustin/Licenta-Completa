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
	float floorHeight)
{
	std::uniform_real_distribution<float> prob(0.0f, 1.0f);
	glm::vec3 roomMin = room.minBounds;
	glm::vec3 roomMax = room.maxBounds;
	float floorY = roomMin.y;

	float roomW = room.GetWidth();
	float roomD = room.GetDepth();

	// Desk dimensions (AABB approximation)
	float deskW = 1.2f, deskD = 0.6f, deskH = 0.75f;

	float deskX, deskZ;
	bool deskAlongX = (roomW > roomD); // desk faces the longer dimension
	float deskYaw = 0.0f;

	if (deskAlongX)
	{
		deskX = roomMin.x + 0.3f + deskD * 0.5f; // against -X wall
		deskZ = (roomMin.z + roomMax.z) * 0.5f;
		deskYaw = 90.0f;
	}
	else
	{
		deskX = (roomMin.x + roomMax.x) * 0.5f;
		deskZ = roomMin.z + 0.3f + deskD * 0.5f; // against -Z wall
		deskYaw = 0.0f;
	}

	// 1. High-fidelity Desk (using Iron Wooden Table fbx!)
	AddProp(props, "Assets/Models/Bathroom/Model/Bathroom_props_set/Iron_Wooden_Table.fbx", glm::vec3(deskX, floorY, deskZ), glm::vec3(0.0f, deskYaw, 0.0f), glm::vec3(1.2f), "desk");

	// 2. High-fidelity Office Chair (using beautiful Chair fbx!)
	float chairOffset = deskAlongX ? deskD + 0.45f : 0.0f;
	float chairOffsetZ = deskAlongX ? 0.0f : deskD + 0.45f;
	float chairX = deskX + (deskAlongX ? chairOffset : 0.0f);
	float chairZ = deskZ + (deskAlongX ? 0.0f : chairOffsetZ);
	float chairYaw = deskAlongX ? -90.0f : 180.0f; // face towards desk

	AddProp(props, "Assets/Models/Kitchen/Models/Chair.fbx", glm::vec3(chairX, floorY, chairZ), glm::vec3(0.0f, chairYaw, 0.0f), glm::vec3(1.0f), "chair");

	// 3. High-fidelity Screen/TV Monitor (sits on desktop surface)
	if (prob(rng) > 0.4f)
	{
		float monY = floorY + deskH;
		AddProp(props, "Assets/Models/Bedroom/Models/Interior/Tv_01.fbx", glm::vec3(deskX, monY, deskZ), glm::vec3(0.0f, deskYaw, 0.0f), glm::vec3(0.7f), "monitor");
	}

	// 4. High-fidelity Rug (sits under the desk)
	if (prob(rng) > 0.3f)
	{
		AddProp(props, "Assets/Models/Bedroom/Models/Interior/Rug_01.fbx", glm::vec3(deskX, floorY + 0.005f, deskZ), glm::vec3(0.0f, deskYaw, 0.0f), glm::vec3(0.6f, 1.0f, 0.6f), "rug");
	}

	// 5. Drawer/Cabinet next to the desk
	float drawerX = deskX + (deskAlongX ? 0.0f : 1.0f);
	float drawerZ = deskZ + (deskAlongX ? 1.0f : 0.0f);
	if (drawerX < roomMax.x - 0.4f && drawerZ < roomMax.z - 0.4f)
	{
		AddProp(props, "Assets/Models/Bathroom/Model/Bathroom_props_set/Drawer.fbx", glm::vec3(drawerX, floorY, drawerZ), glm::vec3(0.0f, deskYaw, 0.0f), glm::vec3(1.0f), "cabinet");
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
	float floorHeight)
{
	glm::vec3 roomMin = room.minBounds;
	float floorY = roomMin.y;

	// Toilet (against -Z wall, left side)
	float toiletX = roomMin.x + 0.5f;
	float toiletZ = roomMin.z + 0.35f;

	// 1. High-fidelity Toilet Set (Set02.fbx)
	AddProp(props, "Assets/Models/Bathroom/Model/Bathroom_props_set/Bathroom_Props_Set02.fbx", glm::vec3(toiletX, floorY, toiletZ), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f), "toilet");

	// Sink (against +X wall)
	float sinkX = room.maxBounds.x - 0.35f;
	float sinkZ = (roomMin.z + room.maxBounds.z) * 0.5f;

	// 2. High-fidelity Bathroom Sink (Set01.fbx)
	AddProp(props, "Assets/Models/Bathroom/Model/Bathroom_props_set/Bathroom_props_Set01.fbx", glm::vec3(sinkX, floorY, sinkZ), glm::vec3(0.0f, 90.0f, 0.0f), glm::vec3(1.0f), "sink");

	// 3. Washing Machine next to sink
	float wmZ = sinkZ - 0.8f;
	if (wmZ > roomMin.z + 0.4f)
	{
		AddProp(props, "Assets/Models/Bathroom/Model/Bathroom_props_set/Washing_Machine.fbx", glm::vec3(sinkX, floorY, wmZ), glm::vec3(0.0f, 90.0f, 0.0f), glm::vec3(1.0f), "washing_machine");
	}

	// 4. Wooden Rack next to toilet
	float rackZ = toiletZ + 0.8f;
	if (rackZ < room.maxBounds.z - 0.4f)
	{
		AddProp(props, "Assets/Models/Bathroom/Model/Bathroom_props_set/Wooden_Rack.fbx", glm::vec3(toiletX, floorY, rackZ), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f), "cabinet");
	}

	// 5. Batched Mirror above sink (simple thin box on the wall - perfect for static batching)
	meshBuckets[MAT_GLASS].Append(MakeBox(
		glm::vec3(sinkX + 0.24f, floorY + 1.4f, sinkZ),
		glm::vec3(0.01f, 0.3f, 0.25f), 1.0f));
}

// =====================================================================
// CorridorDecorator — Ceiling lamps at regular intervals (static batched)
// =====================================================================

void CorridorDecorator::Decorate(
	std::map<int, MeshData>& meshBuckets,
	std::vector<PropPlacement>& props,
	const InteriorRoom& room,
	std::mt19937& rng,
	float floorHeight)
{
	float ceilingY = room.maxBounds.y - 0.02f;
	float centerX = (room.minBounds.x + room.maxBounds.x) * 0.5f;
	float centerZ = (room.minBounds.z + room.maxBounds.z) * 0.5f;

	// Determine corridor direction
	bool longX = room.GetWidth() > room.GetDepth();
	float corridorLen = longX ? room.GetWidth() : room.GetDepth();
	float lampSpacing = 3.0f; // one lamp every 3m
	int numLamps = std::max(1, (int)(corridorLen / lampSpacing));

	// Lamps are perfect candidates for lightweight static batching!
	for (int i = 0; i < numLamps; i++)
	{
		float t = (i + 0.5f) / (float)numLamps;
		float lx = longX ? glm::mix(room.minBounds.x, room.maxBounds.x, t) : centerX;
		float lz = longX ? centerZ : glm::mix(room.minBounds.z, room.maxBounds.z, t);

		// Lamp fixture (thin flat box on ceiling)
		meshBuckets[MAT_METAL].Append(MakeBox(
			glm::vec3(lx, ceilingY, lz),
			glm::vec3(0.3f, 0.02f, 0.15f), 1.0f));
	}
}

// =====================================================================
// BedroomDecorator — High-fidelity Bed, TV Stand, TV, Lamp, Closet
// =====================================================================

void BedroomDecorator::Decorate(
	std::map<int, MeshData>& meshBuckets,
	std::vector<PropPlacement>& props,
	const InteriorRoom& room,
	std::mt19937& rng,
	float floorHeight)
{
	std::uniform_real_distribution<float> prob(0.0f, 1.0f);
	float floorY = room.minBounds.y;
	glm::vec3 roomMin = room.minBounds;
	glm::vec3 roomMax = room.maxBounds;
	float centerX = (roomMin.x + roomMax.x) * 0.5f;

	// Bed (centered, against -Z wall)
	float bedD = 2.0f;
	float bedZ = roomMin.z + 0.3f + bedD * 0.5f;

	// 1. High-fidelity Bed
	AddProp(props, "Assets/Models/Bedroom/Models/Interior/Bed_01.fbx", glm::vec3(centerX, floorY, bedZ), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f), "bed");

	// 2. High-fidelity Rug under the bed
	AddProp(props, "Assets/Models/Bedroom/Models/Interior/Rug_01.fbx", glm::vec3(centerX, floorY + 0.005f, bedZ + 0.2f), glm::vec3(0.0f, 90.0f, 0.0f), glm::vec3(1.0f), "rug");

	// 3. Nightstand (Drawer) to the right of bed
	float nsX = centerX + 1.1f;
	if (nsX + 0.3f < roomMax.x) // check it fits
	{
		// Place a beautiful nightstand/wood cabinet
		AddProp(props, "Assets/Models/Bathroom/Model/Bathroom_props_set/Drawer.fbx", glm::vec3(nsX, floorY, bedZ - 0.7f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.8f), "cabinet");
		// Place a Night Light on top of it!
		AddProp(props, "Assets/Models/Bedroom/Models/Interior/NightLight_01.fbx", glm::vec3(nsX, floorY + 0.65f, bedZ - 0.7f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f), "lamp");
	}

	// 4. TV Stand opposite the bed
	float tvStandZ = roomMax.z - 0.45f;
	if (tvStandZ > bedZ + 1.2f)
	{
		AddProp(props, "Assets/Models/Bedroom/Models/Interior/TvStand_01.fbx", glm::vec3(centerX, floorY, tvStandZ), glm::vec3(0.0f, 180.0f, 0.0f), glm::vec3(1.0f), "desk");
		// Place the TV on top of the stand!
		AddProp(props, "Assets/Models/Bedroom/Models/Interior/Tv_01.fbx", glm::vec3(centerX, floorY + 0.4f, tvStandZ), glm::vec3(0.0f, 180.0f, 0.0f), glm::vec3(1.0f), "tv");
	}

	// 5. Cupboard/Closet against the left wall
	float closetX = roomMin.x + 0.45f;
	float closetZ = bedZ + 0.4f;
	if (closetX + 0.4f < centerX && closetZ < roomMax.z - 0.5f)
	{
		AddProp(props, "Assets/Models/Bedroom/Models/Interior/Cupboard_a_01.fbx", glm::vec3(closetX, floorY, closetZ), glm::vec3(0.0f, 90.0f, 0.0f), glm::vec3(1.0f), "closet");
	}

	// 6. Standing Lamp in a cozy corner
	AddProp(props, "Assets/Models/Bedroom/Models/Interior/StandingLamp_01.fbx", glm::vec3(roomMin.x + 0.4f, floorY, roomMin.z + 0.4f), glm::vec3(0.0f, 45.0f, 0.0f), glm::vec3(1.0f), "lamp");
}

// =====================================================================
// KitchenDecorator — Fridge, Stove, Sink Table & Chairs, Countertop
// =====================================================================

void KitchenDecorator::Decorate(
	std::map<int, MeshData>& meshBuckets,
	std::vector<PropPlacement>& props,
	const InteriorRoom& room,
	std::mt19937& rng,
	float floorHeight)
{
	float floorY = room.minBounds.y;
	float counterH = 0.9f, counterD = 0.6f;

	// Counter along -Z wall
	float counterLen = room.GetWidth() - 0.4f; // small gap from side walls
	float counterX = (room.minBounds.x + room.maxBounds.x) * 0.5f;
	float counterZ = room.minBounds.z + 0.2f + counterD * 0.5f;

	// Counter base (cabinet - customized to room size, so batched is perfect)
	meshBuckets[MAT_WOOD].Append(MakeBox(
		glm::vec3(counterX, floorY + counterH * 0.5f, counterZ),
		glm::vec3(counterLen * 0.5f, counterH * 0.5f, counterD * 0.5f), 2.0f));

	// Countertop surface
	meshBuckets[MAT_FLOOR_TILE].Append(MakeBox(
		glm::vec3(counterX, floorY + counterH + 0.02f, counterZ),
		glm::vec3(counterLen * 0.5f + 0.02f, 0.02f, counterD * 0.5f + 0.02f), 2.0f));

	// Wall cabinets (upper)
	float upperY = floorY + 1.5f;
	meshBuckets[MAT_WOOD].Append(MakeBox(
		glm::vec3(counterX, upperY + 0.3f, room.minBounds.z + 0.2f),
		glm::vec3(counterLen * 0.5f, 0.3f, 0.3f), 2.0f));

	// 1. High-fidelity Stove (placed on left counter end)
	float stoveX = counterX - counterLen * 0.28f;
	AddProp(props, "Assets/Models/Kitchen/Models/Stove.fbx", glm::vec3(stoveX, floorY, counterZ + 0.05f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f), "stove");

	// 2. High-fidelity Kitchen Sink (sits centrally in the counter)
	AddProp(props, "Assets/Models/Kitchen/Models/sink.fbx", glm::vec3(counterX, floorY, counterZ + 0.05f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f), "sink");

	// 3. High-fidelity Fridge (placed on right counter end)
	float fridgeX = counterX + counterLen * 0.32f;
	AddProp(props, "Assets/Models/Kitchen/Models/Fridge.fbx", glm::vec3(fridgeX, floorY, counterZ + 0.05f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f), "fridge");

	// 4. Microwave and Toaster placed on the countertop
	AddProp(props, "Assets/Models/Kitchen/Models/Microwave.fbx", glm::vec3(counterX + counterLen * 0.14f, floorY + counterH + 0.02f, counterZ), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.9f), "appliances");
	AddProp(props, "Assets/Models/Kitchen/Models/Toaster.fbx", glm::vec3(counterX - counterLen * 0.12f, floorY + counterH + 0.02f, counterZ), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.9f), "appliances");

	// 5. Kitchen Table and Chairs (if space is wide enough)
	if (room.GetDepth() > 3.0f)
	{
		float tableZ = counterZ + 1.7f;
		// Table
		AddProp(props, "Assets/Models/Kitchen/Models/Table.fbx", glm::vec3(counterX, floorY, tableZ), glm::vec3(0.0f, 90.0f, 0.0f), glm::vec3(1.0f), "desk");
		// Chair Left
		AddProp(props, "Assets/Models/Kitchen/Models/Chair.fbx", glm::vec3(counterX - 0.6f, floorY, tableZ), glm::vec3(0.0f, 90.0f, 0.0f), glm::vec3(1.0f), "chair");
		// Chair Right
		AddProp(props, "Assets/Models/Kitchen/Models/Chair.fbx", glm::vec3(counterX + 0.6f, floorY, tableZ), glm::vec3(0.0f, -90.0f, 0.0f), glm::vec3(1.0f), "chair");
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
	float floorHeight)
{
	float floorY = room.minBounds.y;
	glm::vec3 roomMin = room.minBounds;
	glm::vec3 roomMax = room.maxBounds;
	float centerX = (roomMin.x + roomMax.x) * 0.5f;
	float centerZ = (roomMin.z + roomMax.z) * 0.5f;

	// Reception desk (centered reception stand - static batched wood)
	float deskW = std::min(2.5f, room.GetWidth() * 0.5f);
	float deskD = 0.8f, deskH = 1.1f;
	meshBuckets[MAT_WOOD].Append(MakeBox(
		glm::vec3(centerX, floorY + deskH * 0.5f, centerZ - room.GetDepth() * 0.2f),
		glm::vec3(deskW * 0.5f, deskH * 0.5f, deskD * 0.5f), 2.0f));

	// 1. High-fidelity glass coffee table
	AddProp(props, "Assets/Models/Livingroom/glass_table/glass_table.FBX", glm::vec3(centerX, floorY, centerZ + 0.2f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.1f), "desk");

	// 2. High-fidelity visitor sofa bench (using bank.FBX)
	AddProp(props, "Assets/Models/Livingroom/interior/bank.FBX", glm::vec3(centerX, floorY, centerZ + 1.0f), glm::vec3(0.0f, 180.0f, 0.0f), glm::vec3(1.0f), "couch");

	// 3. High-fidelity media cabinet stand (tumba_fur.FBX)
	float tvCabinetZ = centerZ - 0.7f;
	AddProp(props, "Assets/Models/Livingroom/tumba_fur/tumba_fur.FBX", glm::vec3(centerX, floorY, tvCabinetZ), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f), "desk");

	// 4. TV placed on top of media cabinet
	AddProp(props, "Assets/Models/Bedroom/Models/Interior/Tv_01.fbx", glm::vec3(centerX, floorY + 0.45f, tvCabinetZ), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.9f), "tv");

	// 5. Left and Right Stereo Speakers
	AddProp(props, "Assets/Models/Livingroom/speaker/speaker.FBX", glm::vec3(centerX - 1.0f, floorY, tvCabinetZ), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f), "speaker");
	AddProp(props, "Assets/Models/Livingroom/speaker/speaker.FBX", glm::vec3(centerX + 1.0f, floorY, tvCabinetZ), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f), "speaker");

	// 6. High-fidelity printer in the corner
	AddProp(props, "Assets/Models/Livingroom/printer/printer.FBX", glm::vec3(roomMin.x + 0.5f, floorY, roomMin.z + 0.5f), glm::vec3(0.0f, 45.0f, 0.0f), glm::vec3(1.0f), "office");
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
