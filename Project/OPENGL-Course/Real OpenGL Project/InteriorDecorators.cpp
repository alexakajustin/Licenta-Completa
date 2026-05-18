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
	glm::vec3 sinkSize)
{
	std::uniform_real_distribution<float> prob(0.0f, 1.0f);
	glm::vec3 roomMin = room.minBounds;
	glm::vec3 roomMax = room.maxBounds;
	float floorY = roomMin.y;
	float centerX = (roomMin.x + roomMax.x) * 0.5f;
	float centerZ = (roomMin.z + roomMax.z) * 0.5f;

	float roomW = room.GetWidth();
	float roomD = room.GetDepth();

	// Custom desk size
	float deskW = deskSize.x;
	float deskD = deskSize.z;
	float deskH = deskSize.y;

	// Randomly choose wall for the desk (0 = -X, 1 = +X, 2 = -Z, 3 = +Z)
	std::uniform_int_distribution<int> wallDist(0, 3);
	int deskWall = wallDist(rng);

	// Adaptive scale to prevent clipping through opposite walls
	float scaleFactor = 1.0f;
	float reqLength = (deskWall < 2) ? deskD : deskW;
	float reqWidth = (deskWall < 2) ? deskW : deskD;
	
	if (reqLength + 0.4f > roomD) {
		scaleFactor = std::min(scaleFactor, (roomD - 0.4f) / reqLength);
	}
	if (reqWidth + 0.4f > roomW) {
		scaleFactor = std::min(scaleFactor, (roomW - 0.4f) / reqWidth);
	}

	glm::vec3 finalScale = glm::vec3(scaleFactor);
	float finalDeskW = deskW * scaleFactor;
	float finalDeskD = deskD * scaleFactor;

	glm::vec3 deskPos(0.0f);
	float deskYaw = 0.0f;
	std::uniform_real_distribution<float> jitter(-0.1f, 0.1f);
	float jVal = jitter(rng);

	if (deskWall == 0) // -X wall (left)
	{
		deskPos.x = roomMin.x + 0.2f + finalDeskD * 0.5f;
		deskPos.z = centerZ + jVal;
		deskYaw = 90.0f;
	}
	else if (deskWall == 1) // +X wall (right)
	{
		deskPos.x = roomMax.x - 0.2f - finalDeskD * 0.5f;
		deskPos.z = centerZ + jVal;
		deskYaw = -90.0f;
	}
	else if (deskWall == 2) // -Z wall (front)
	{
		deskPos.x = centerX + jVal;
		deskPos.z = roomMin.z + 0.2f + finalDeskD * 0.5f;
		deskYaw = 0.0f;
	}
	else // +Z wall (back)
	{
		deskPos.x = centerX + jVal;
		deskPos.z = roomMax.z - 0.2f - finalDeskD * 0.5f;
		deskYaw = 180.0f;
	}
	deskPos.y = floorY;

	std::uniform_real_distribution<float> rotJitter(-4.0f, 4.0f);
	float finalDeskYaw = deskYaw + rotJitter(rng);

	// 1. Custom Desk placement
	AddProp(props, "Assets/Models/Bathroom/Model/Bathroom_props_set/Iron_Wooden_Table.fbx", deskPos, glm::vec3(0.0f, finalDeskYaw, 0.0f), finalScale, "desk");

	// 2. Chair positioned facing the desk
	glm::vec3 deskForward = glm::vec3(sin(glm::radians(deskYaw)), 0.0f, cos(glm::radians(deskYaw)));
	glm::vec3 chairPos = deskPos + deskForward * (finalDeskD * 0.5f + 0.45f * scaleFactor);
	float chairYaw = deskYaw + 180.0f + rotJitter(rng);
	AddProp(props, "Assets/Models/Kitchen/Models/Chair.fbx", chairPos, glm::vec3(0.0f, chairYaw, 0.0f), finalScale, "chair");

	// 3. High-fidelity Screen/TV Monitor on top of the desk
	if (prob(rng) > 0.4f)
	{
		AddProp(props, "Assets/Models/Bedroom/Models/Interior/Tv_01.fbx", deskPos + glm::vec3(0.0f, deskH * scaleFactor, 0.0f), glm::vec3(0.0f, finalDeskYaw, 0.0f), glm::vec3(0.7f * scaleFactor), "monitor");
	}

	// 4. Rug under the desk
	if (prob(rng) > 0.3f)
	{
		AddProp(props, "Assets/Models/Bedroom/Models/Interior/Rug_01.fbx", deskPos + glm::vec3(0.0f, 0.005f, 0.0f), glm::vec3(0.0f, finalDeskYaw, 0.0f), glm::vec3(0.6f * scaleFactor, 1.0f, 0.6f * scaleFactor), "rug");
	}

	// 5. Adjacent cabinet next to the desk if space allows
	glm::vec3 deskRight = glm::vec3(cos(glm::radians(deskYaw)), 0.0f, -sin(glm::radians(deskYaw)));
	glm::vec3 cabinetPos = deskPos + deskRight * (finalDeskW * 0.5f + 0.4f * scaleFactor);
	float cabSize = 0.5f * scaleFactor;
	if (cabinetPos.x - cabSize > roomMin.x && cabinetPos.x + cabSize < roomMax.x &&
		cabinetPos.z - cabSize > roomMin.z && cabinetPos.z + cabSize < roomMax.z)
	{
		AddProp(props, "Assets/Models/Bathroom/Model/Bathroom_props_set/Drawer.fbx", cabinetPos, glm::vec3(0.0f, finalDeskYaw, 0.0f), finalScale, "cabinet");
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
	glm::vec3 sinkSize)
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
	glm::vec3 sinkSize)
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
	glm::vec3 sinkSize)
{
	std::uniform_real_distribution<float> prob(0.0f, 1.0f);
	float floorY = room.minBounds.y;
	glm::vec3 roomMin = room.minBounds;
	glm::vec3 roomMax = room.maxBounds;
	float centerX = (roomMin.x + roomMax.x) * 0.5f;
	float centerZ = (roomMin.z + roomMax.z) * 0.5f;

	float roomW = room.GetWidth();
	float roomD = room.GetDepth();

	// Custom bed size
	float bedLen = std::max(bedSize.x, bedSize.z);
	float bedW = std::min(bedSize.x, bedSize.z);

	// Randomly choose wall for the bed (0 = -Z, 1 = +Z, 2 = -X, 3 = +X)
	std::uniform_int_distribution<int> wallDist(0, 3);
	int bedWall = wallDist(rng);

	// Adaptive scale to prevent clipping through opposite walls
	float scaleFactor = 1.0f;
	float reqLength = (bedWall < 2) ? bedLen : bedW;
	float reqWidth = (bedWall < 2) ? bedW : bedLen;

	if (reqLength + 0.4f > roomD) {
		scaleFactor = std::min(scaleFactor, (roomD - 0.4f) / reqLength);
	}
	if (reqWidth + 0.4f > roomW) {
		scaleFactor = std::min(scaleFactor, (roomW - 0.4f) / reqWidth);
	}

	// Write decorator choices to the debug file
	{
		std::ofstream f("C:\\Users\\Justin\\Desktop\\Licenta-Completa\\debug_interior.txt", std::ios::app);
		if (f.is_open()) {
			f << "\n=== BedroomDecorator::Decorate ===\n";
			f << "  Room Bounds: (" << roomMin.x << ", " << roomMin.z << ") to (" << roomMax.x << ", " << roomMax.z << ")\n";
			f << "  Room Size: Width=" << roomW << ", Depth=" << roomD << "\n";
			f << "  Incoming Bed Size: " << bedSize.x << " x " << bedSize.z << "\n";
			f << "  Parsed Bed Dimensions: Len=" << bedLen << ", W=" << bedW << "\n";
			f << "  Bed Wall: " << bedWall << " (reqLength=" << reqLength << ", reqWidth=" << reqWidth << ")\n";
			f << "  Calculated Scale Factor: " << scaleFactor << "\n";
			f.close();
		}
	}

	glm::vec3 finalBedScale = glm::vec3(scaleFactor);
	float finalBedLen = bedLen * scaleFactor;
	float finalBedW = bedW * scaleFactor;

	glm::vec3 bedPos(0.0f);
	float bedYaw = 0.0f;

	// Slight layout jitter for lived-in realism! (Safe range to prevent wall clipping)
	std::uniform_real_distribution<float> jitter(-0.02f, 0.02f);
	float jX = jitter(rng);
	float jZ = jitter(rng);

	if (bedWall == 0) // -Z wall (front)
	{
		bedPos.x = centerX + jX;
		bedPos.z = roomMin.z + 0.15f + finalBedLen * 0.5f;
		bedYaw = 0.0f;
	}
	else if (bedWall == 1) // +Z wall (back)
	{
		bedPos.x = centerX + jX;
		bedPos.z = roomMax.z - 0.15f - finalBedLen * 0.5f;
		bedYaw = 180.0f;
	}
	else if (bedWall == 2) // -X wall (left)
	{
		bedPos.x = roomMin.x + 0.15f + finalBedLen * 0.5f;
		bedPos.z = centerZ + jZ;
		bedYaw = 90.0f;
	}
	else // +X wall (right)
	{
		bedPos.x = roomMax.x - 0.15f - finalBedLen * 0.5f;
		bedPos.z = centerZ + jZ;
		bedYaw = -90.0f;
	}
	bedPos.y = floorY;

	std::uniform_real_distribution<float> rotJitter(-3.0f, 3.0f);
	float finalBedYaw = bedYaw + rotJitter(rng);

	// 1. High-fidelity Bed placement
	AddProp(props, "Assets/Models/Bedroom/Models/Interior/Bed_01.fbx", bedPos, glm::vec3(0.0f, finalBedYaw, 0.0f), finalBedScale, "bed");

	// 2. High-fidelity Rug under the bed
	if (prob(rng) > 0.2f) {
		AddProp(props, "Assets/Models/Bedroom/Models/Interior/Rug_01.fbx", bedPos + glm::vec3(0.0f, 0.005f, 0.0f), glm::vec3(0.0f, finalBedYaw + 90.0f, 0.0f), finalBedScale, "rug");
	}

	// 3. Nightstand (Drawer) placed correctly next to the bed head
	float drawerSize = 0.5f * scaleFactor;
	glm::vec3 drawerPos(0.0f);
	int side = (prob(rng) > 0.5f) ? 1 : -1;

	glm::vec3 bedForward = glm::vec3(sin(glm::radians(bedYaw)), 0.0f, cos(glm::radians(bedYaw)));
	glm::vec3 bedRight = glm::vec3(cos(glm::radians(bedYaw)), 0.0f, -sin(glm::radians(bedYaw)));

	glm::vec3 bedHeadPos = bedPos - bedForward * (finalBedLen * 0.5f);
	drawerPos = bedHeadPos + bedRight * ((float)side * (finalBedW * 0.5f + drawerSize * 0.5f + 0.1f));

	if (drawerPos.x - drawerSize > roomMin.x && drawerPos.x + drawerSize < roomMax.x &&
		drawerPos.z - drawerSize > roomMin.z && drawerPos.z + drawerSize < roomMax.z)
	{
		AddProp(props, "Assets/Models/Bathroom/Model/Bathroom_props_set/Drawer.fbx", drawerPos, glm::vec3(0.0f, finalBedYaw, 0.0f), glm::vec3(0.8f * scaleFactor), "cabinet");

		// Place a Night Light on top!
		if (prob(rng) > 0.3f) {
			AddProp(props, "Assets/Models/Bedroom/Models/Interior/NightLight_01.fbx", drawerPos + glm::vec3(0.0f, 0.65f * scaleFactor, 0.0f), glm::vec3(0.0f, finalBedYaw, 0.0f), glm::vec3(scaleFactor), "lamp");
		}
	}

	// 4. Desk opposite the bed
	float deskW = deskSize.x;
	float deskD = deskSize.z;
	float deskScale = 1.0f;

	glm::vec3 deskPos = bedPos + bedForward * (roomD * 0.5f + finalBedLen * 0.5f);
	if (bedWall == 0) deskPos.z = roomMax.z - (deskD * 0.5f + 0.05f);
	else if (bedWall == 1) deskPos.z = roomMin.z + (deskD * 0.5f + 0.05f);
	else if (bedWall == 2) deskPos.x = roomMax.x - (deskD * 0.5f + 0.05f);
	else deskPos.x = roomMin.x + (deskD * 0.5f + 0.05f);

	float deskOppSpace = (bedWall < 2) ? roomW : roomD;
	if (deskW + 0.4f > deskOppSpace) {
		deskScale = std::min(deskScale, (deskOppSpace - 0.4f) / deskW);
	}

	// Desk effective width and depth in WORLD space depends on orientation!
	float deskWorldX = (bedWall < 2) ? (deskW * deskScale) : (deskD * deskScale);
	float deskWorldZ = (bedWall < 2) ? (deskD * deskScale) : (deskW * deskScale);

	// Ensure the desk fits perfectly within the room's X and Z bounds without clipping adjacent walls
	if (deskPos.x - deskWorldX * 0.5f >= roomMin.x && deskPos.x + deskWorldX * 0.5f <= roomMax.x &&
		deskPos.z - deskWorldZ * 0.5f >= roomMin.z && deskPos.z + deskWorldZ * 0.5f <= roomMax.z)
	{
		float deskYaw = bedYaw + 180.0f;
		AddProp(props, "Assets/Models/Bedroom/Models/Interior/TvStand_01.fbx", deskPos, glm::vec3(0.0f, deskYaw, 0.0f), glm::vec3(deskScale), "desk");
		
		// Place the TV on top of the desk/stand!
		AddProp(props, "Assets/Models/Bedroom/Models/Interior/Tv_01.fbx", deskPos + glm::vec3(0.0f, deskSize.y * deskScale, 0.0f), glm::vec3(0.0f, deskYaw, 0.0f), glm::vec3(deskScale), "tv");
	}

	// 5. Cupboard/Closet in a corner
	glm::vec3 closetPos(0.0f);
	float closetYaw = 0.0f;
	bool hasCloset = false;
	std::vector<int> corners = { 0, 1, 2, 3 };
	std::shuffle(corners.begin(), corners.end(), rng);

	for (int c : corners)
	{
		glm::vec3 corner(0.0f);
		if (c == 0) { corner = glm::vec3(roomMin.x + 0.5f, floorY, roomMin.z + 0.5f); closetYaw = 45.0f; }
		else if (c == 1) { corner = glm::vec3(roomMax.x - 0.5f, floorY, roomMin.z + 0.5f); closetYaw = -45.0f; }
		else if (c == 2) { corner = glm::vec3(roomMin.x + 0.5f, floorY, roomMax.z - 0.5f); closetYaw = 135.0f; }
		else { corner = glm::vec3(roomMax.x - 0.5f, floorY, roomMax.z - 0.5f); closetYaw = -135.0f; }

		// Don't spawn on top of the bed
		if (glm::distance(corner, bedPos) > finalBedLen * 0.5f + 1.0f)
		{
			closetPos = corner;
			hasCloset = true;
			break;
		}
	}

	if (hasCloset)
	{
		AddProp(props, "Assets/Models/Bedroom/Models/Interior/Cupboard_a_01.fbx", closetPos, glm::vec3(0.0f, closetYaw, 0.0f), glm::vec3(1.0f), "closet");
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
	float floorHeight,
	glm::vec3 bedSize,
	glm::vec3 deskSize,
	glm::vec3 tvSize,
	glm::vec3 stoveSize,
	glm::vec3 fridgeSize,
	glm::vec3 sinkSize)
{
	float floorY = room.minBounds.y;
	glm::vec3 roomMin = room.minBounds;
	glm::vec3 roomMax = room.maxBounds;
	float roomW = room.GetWidth();
	float roomD = room.GetDepth();

	// =====================================================================
	// Smart Side-by-Side Layout with Adaptive Scaling
	// =====================================================================
	// Layout order along -Z wall: [Stove] [gap] [Sink] [gap] [Fridge]
	// Each item's width is the X dimension of its AABB.

	float gap = 0.15f; // gap between adjacent appliances
	float margin = 0.2f; // gap from side walls

	float totalW = stoveSize.x + sinkSize.x + fridgeSize.x + gap * 2.0f + margin * 2.0f;
	float availW = roomW;

	// Adaptive scale: shrink everything proportionally if it doesn't fit
	float scale = 1.0f;
	if (totalW > availW) {
		scale = availW / totalW;
	}

	float sStoveW = stoveSize.x * scale;
	float sSinkW = sinkSize.x * scale;
	float sFridgeW = fridgeSize.x * scale;
	float sGap = gap * scale;
	float sMargin = margin * scale;

	// Total used width after scaling
	float usedW = sMargin + sStoveW + sGap + sSinkW + sGap + sFridgeW + sMargin;

	// Center the appliance row in the room
	float centerX = (roomMin.x + roomMax.x) * 0.5f;
	float rowStartX = centerX - usedW * 0.5f;

	// X positions (center of each appliance)
	float stoveX = rowStartX + sMargin + sStoveW * 0.5f;
	float sinkX  = stoveX + sStoveW * 0.5f + sGap + sSinkW * 0.5f;
	float fridgeX = sinkX + sSinkW * 0.5f + sGap + sFridgeW * 0.5f;

	// Z position: flush against -Z wall, offset by half the deepest item's depth
	float maxDepth = std::max({ stoveSize.z, sinkSize.z, fridgeSize.z }) * scale;
	float counterZ = roomMin.z + maxDepth * 0.5f + 0.05f;

	// 1. Stove
	AddProp(props, "Assets/Models/Kitchen/Models/Stove.fbx",
		glm::vec3(stoveX, floorY, counterZ),
		glm::vec3(0.0f), glm::vec3(scale), "stove");

	// 2. Sink
	AddProp(props, "Assets/Models/Kitchen/Models/sink.fbx",
		glm::vec3(sinkX, floorY, counterZ),
		glm::vec3(0.0f), glm::vec3(scale), "sink");

	// 3. Fridge
	AddProp(props, "Assets/Models/Kitchen/Models/Fridge.fbx",
		glm::vec3(fridgeX, floorY, counterZ),
		glm::vec3(0.0f), glm::vec3(scale), "fridge");

	// 4. Microwave and Toaster on countertop (placed between stove and sink, sink and fridge)
	float counterH = 0.9f * scale;
	AddProp(props, "Assets/Models/Kitchen/Models/Microwave.fbx",
		glm::vec3((stoveX + sinkX) * 0.5f, floorY + counterH + 0.02f, counterZ),
		glm::vec3(0.0f), glm::vec3(0.9f * scale), "appliances");
	AddProp(props, "Assets/Models/Kitchen/Models/Toaster.fbx",
		glm::vec3((sinkX + fridgeX) * 0.5f, floorY + counterH + 0.02f, counterZ),
		glm::vec3(0.0f), glm::vec3(0.9f * scale), "appliances");

	// 5. Kitchen Table and Chairs (only if there is enough depth in front of the counter)
	float counterFrontZ = counterZ + maxDepth * 0.5f; // front edge of appliances
	float tableDepthNeeded = 1.5f; // table + chair clearance
	float remainingD = roomMax.z - counterFrontZ;

	if (remainingD > tableDepthNeeded + 0.5f)
	{
		float tableZ = counterFrontZ + 0.8f + tableDepthNeeded * 0.5f;
		// Table
		AddProp(props, "Assets/Models/Kitchen/Models/Table.fbx",
			glm::vec3(centerX, floorY, tableZ),
			glm::vec3(0.0f, 90.0f, 0.0f), glm::vec3(scale), "desk");
		// Chair Left
		AddProp(props, "Assets/Models/Kitchen/Models/Chair.fbx",
			glm::vec3(centerX - 0.6f * scale, floorY, tableZ),
			glm::vec3(0.0f, 90.0f, 0.0f), glm::vec3(scale), "chair");
		// Chair Right
		AddProp(props, "Assets/Models/Kitchen/Models/Chair.fbx",
			glm::vec3(centerX + 0.6f * scale, floorY, tableZ),
			glm::vec3(0.0f, -90.0f, 0.0f), glm::vec3(scale), "chair");
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
	glm::vec3 sinkSize)
{
	float floorY = room.minBounds.y;
	glm::vec3 roomMin = room.minBounds;
	glm::vec3 roomMax = room.maxBounds;
	float centerX = (roomMin.x + roomMax.x) * 0.5f;
	float centerZ = (roomMin.z + roomMax.z) * 0.5f;

	// Mock reception desk removed to keep the room blank unless populated by proper node inputs.

	// 1. High-fidelity glass coffee table
	AddProp(props, "Assets/Models/Livingroom/glass_table/glass_table.FBX", glm::vec3(centerX, floorY, centerZ + 0.2f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.1f), "coffee_table");

	// 2. High-fidelity visitor sofa bench (using bank.FBX)
	AddProp(props, "Assets/Models/Livingroom/interior/bank.FBX", glm::vec3(centerX, floorY, centerZ + 1.0f), glm::vec3(0.0f, 180.0f, 0.0f), glm::vec3(1.0f), "couch");

	// 3. High-fidelity media cabinet stand (tumba_fur.FBX)
	float tvCabinetZ = centerZ - 0.7f;
	AddProp(props, "Assets/Models/Livingroom/tumba_fur/tumba_fur.FBX", glm::vec3(centerX, floorY, tvCabinetZ), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f), "tv_stand");

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
