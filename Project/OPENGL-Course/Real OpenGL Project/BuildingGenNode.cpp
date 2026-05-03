#include "BuildingGenNode.h"
#include "GameObject.h"
#include "Mesh.h"
#include "Material.h"
#include "Texture.h"
#include "TextureLayer.h"
#include <cmath>
#include <random>
#include <map>

// Wall texture pool (from 3DWorld buildings/)
static const char* WALL_TEXTURES[] = {
	"Assets/Textures/buildings/office_windows.jpg",
	"Assets/Textures/buildings/apartment_windows.jpg",
	"Assets/Textures/buildings/skyscraper1.jpg",
	"Assets/Textures/buildings/skyscraper2.jpg",
	"Assets/Textures/buildings/skyscraper3.jpg",
	"Assets/Textures/buildings/brick1.jpg",
	"Assets/Textures/buildings/window_blocks.jpg",
	"Assets/Textures/buildings/metal_building.jpg",
};
static const int NUM_WALL_TEXTURES = sizeof(WALL_TEXTURES) / sizeof(WALL_TEXTURES[0]);

static const char* ROOF_TEXTURE = "Assets/Textures/buildings/concrete.jpg";

// =====================================================================
// Construction
// =====================================================================

BuildingGenNode::BuildingGenNode(NodeGraph& graph)
{
	id = graph.NextNodeId();
	title = "Building Gen";

	// Input: plot transforms from CityGridNode
	Pin plotsIn(graph.NextPinId(), PinDataType::TransformList, "Plots");
	inputs.push_back(plotsIn);
}

// =====================================================================
// UI
// =====================================================================

void BuildingGenNode::RenderContent(SceneManager* scene)
{
	ImGui::Text("Building Generation");
	ImGui::Separator();

	ImGui::DragFloat("Min Height", &minHeight, 0.5f, 1.0f, 50.0f, "%.1f");
	ImGui::DragFloat("Max Height", &maxHeight, 0.5f, 5.0f, 200.0f, "%.1f");
	ImGui::DragFloat("Floor Height", &floorHeight, 0.1f, 1.0f, 10.0f, "%.1f");
	ImGui::DragFloat("Wall Inset", &wallInset, 0.1f, 0.0f, 5.0f, "%.1f");
	ImGui::DragFloat("Roof Overhang", &roofOverhang, 0.05f, 0.0f, 2.0f, "%.2f");
	ImGui::DragFloat("Roof Thickness", &roofThickness, 0.05f, 0.1f, 2.0f, "%.2f");
	ImGui::DragInt("Seed", &seed, 1, 0, 9999);
}

// =====================================================================
// Serialization
// =====================================================================

json BuildingGenNode::Serialize() const
{
	json j = GraphNode::Serialize();
	j["minHeight"] = minHeight;
	j["maxHeight"] = maxHeight;
	j["floorHeight"] = floorHeight;
	j["wallInset"] = wallInset;
	j["roofOverhang"] = roofOverhang;
	j["roofThickness"] = roofThickness;
	j["seed"] = seed;
	return j;
}

void BuildingGenNode::Deserialize(const json& j)
{
	GraphNode::Deserialize(j);
	minHeight = j.value("minHeight", 5.0f);
	maxHeight = j.value("maxHeight", 30.0f);
	floorHeight = j.value("floorHeight", 3.0f);
	wallInset = j.value("wallInset", 0.5f);
	roofOverhang = j.value("roofOverhang", 0.2f);
	roofThickness = j.value("roofThickness", 0.3f);
	seed = j.value("seed", 42);
}

// =====================================================================
// Mesh Building — Box with proper UVs on each face
// =====================================================================

void BuildingGenNode::BuildBoxMesh(MeshData& mesh, glm::vec3 center, glm::vec3 half, float texTilingU, float texTilingV)
{
	// 4 wall faces of a box (no top/bottom — roof is separate)
	// Each face: 4 verts, 2 triangles
	// Vertex layout: pos(3) + uv(2) + normal(3) + tangent(3) + bitangent(3) = 14

	struct FaceData {
		glm::vec3 normal, tangent, bitangent;
		glm::vec3 corners[4]; // BL, BR, TR, TL
		float uScale, vScale;
	};

	float w = half.x, h = half.y, d = half.z;

	// UV: U tiles along wall width, V tiles along height (floor-based)
	float uFront = (2.0f * w) / floorHeight * texTilingU;
	float vFront = (2.0f * h) / floorHeight * texTilingV;
	float uSide  = (2.0f * d) / floorHeight * texTilingU;
	float vSide  = (2.0f * h) / floorHeight * texTilingV;

	FaceData faces[4] = {
		// Front face (-Z)
		{ {0,0,-1}, {1,0,0}, {0,1,0},
		  { {center.x - w, center.y - h, center.z - d},
		    {center.x + w, center.y - h, center.z - d},
		    {center.x + w, center.y + h, center.z - d},
		    {center.x - w, center.y + h, center.z - d} },
		  uFront, vFront },

		// Back face (+Z)
		{ {0,0,1}, {-1,0,0}, {0,1,0},
		  { {center.x + w, center.y - h, center.z + d},
		    {center.x - w, center.y - h, center.z + d},
		    {center.x - w, center.y + h, center.z + d},
		    {center.x + w, center.y + h, center.z + d} },
		  uFront, vFront },

		// Left face (-X)
		{ {-1,0,0}, {0,0,1}, {0,1,0},
		  { {center.x - w, center.y - h, center.z + d},
		    {center.x - w, center.y - h, center.z - d},
		    {center.x - w, center.y + h, center.z - d},
		    {center.x - w, center.y + h, center.z + d} },
		  uSide, vSide },

		// Right face (+X)
		{ {1,0,0}, {0,0,-1}, {0,1,0},
		  { {center.x + w, center.y - h, center.z - d},
		    {center.x + w, center.y - h, center.z + d},
		    {center.x + w, center.y + h, center.z + d},
		    {center.x + w, center.y + h, center.z - d} },
		  uSide, vSide },
	};

	for (int f = 0; f < 4; f++)
	{
		unsigned int base = mesh.GetVertexCount();
		const FaceData& fd = faces[f];

		float uvs[4][2] = { {0, 0}, {fd.uScale, 0}, {fd.uScale, fd.vScale}, {0, fd.vScale} };

		for (int v = 0; v < 4; v++)
		{
			mesh.AddVertex(
				fd.corners[v].x, fd.corners[v].y, fd.corners[v].z,
				uvs[v][0], uvs[v][1],
				fd.normal.x, fd.normal.y, fd.normal.z,
				fd.tangent.x, fd.tangent.y, fd.tangent.z,
				fd.bitangent.x, fd.bitangent.y, fd.bitangent.z
			);
		}

		mesh.AddTriangle(base, base + 2, base + 1);
		mesh.AddTriangle(base, base + 3, base + 2);
	}
}

void BuildingGenNode::BuildRoofMesh(MeshData& mesh, glm::vec3 center, glm::vec3 half)
{
	// Flat roof slab on top of the building
	unsigned int base = mesh.GetVertexCount();
	glm::vec3 n(0, 1, 0), t(1, 0, 0), b(0, 0, 1);

	float w = half.x + roofOverhang;
	float d = half.z + roofOverhang;
	float y = center.y + half.y;

	// Top face (CCW from above for upward normal)
	glm::vec3 p0(center.x - w, y, center.z - d);
	glm::vec3 p1(center.x - w, y, center.z + d);
	glm::vec3 p2(center.x + w, y, center.z + d);
	glm::vec3 p3(center.x + w, y, center.z - d);

	mesh.AddVertex(p0.x, p0.y, p0.z, 0, 0, n.x, n.y, n.z, t.x, t.y, t.z, b.x, b.y, b.z);
	mesh.AddVertex(p1.x, p1.y, p1.z, 0, 1, n.x, n.y, n.z, t.x, t.y, t.z, b.x, b.y, b.z);
	mesh.AddVertex(p2.x, p2.y, p2.z, 1, 1, n.x, n.y, n.z, t.x, t.y, t.z, b.x, b.y, b.z);
	mesh.AddVertex(p3.x, p3.y, p3.z, 1, 0, n.x, n.y, n.z, t.x, t.y, t.z, b.x, b.y, b.z);

	mesh.AddTriangle(base, base + 1, base + 2);
	mesh.AddTriangle(base, base + 2, base + 3);
}

// =====================================================================
// Execute — generate buildings and spawn as GameObjects
// =====================================================================

void BuildingGenNode::Execute(SceneManager& scene, NodeProgressCallback progress)
{
	if (progress) progress(0.0f, "Reading plot data...");

	// Get plot transforms from input
	if (inputs[0].data.type != PinDataType::TransformList) {
		printf("[BuildingGenNode] No plot data connected!\n");
		return;
	}

	const TransformList& plots = inputs[0].data.transforms;
	if (plots.empty()) {
		printf("[BuildingGenNode] No plots to build on!\n");
		return;
	}

	if (progress) progress(10.0f, "Generating buildings...");

	std::mt19937 rng(seed);
	std::uniform_real_distribution<float> heightDist(minHeight, maxHeight);
	std::uniform_int_distribution<int> texDist(0, NUM_WALL_TEXTURES - 1);

	// Clear any old generated buildings
	std::string prefix = "Building_" + std::to_string(id) + "_";

	// Cache textures to avoid reloading
	std::map<int, Texture*> texCache;

	Texture* roofTex = new Texture(ROOF_TEXTURE);
	roofTex->LoadTexture();

	for (size_t i = 0; i < plots.size(); i++)
	{
		const TransformData& plot = plots[i];

		float plotW = plot.scale.x;
		float plotD = plot.scale.z;
		bool isCommercial = (plot.scale.y > 1.5f);

		float bHeight = heightDist(rng);
		if (isCommercial) bHeight *= 1.5f;

		float bW = (plotW - wallInset * 2.0f) * 0.5f;
		float bD = (plotD - wallInset * 2.0f) * 0.5f;
		if (bW < 0.5f || bD < 0.5f) continue;

		float bH = bHeight * 0.5f;
		glm::vec3 center(plot.position.x, plot.position.y + bH, plot.position.z);
		glm::vec3 halfExtents(bW, bH, bD);

		// Pick texture
		int texIdx = texDist(rng);
		if (isCommercial) texIdx = 2 + (rng() % 5);
		texIdx = texIdx % NUM_WALL_TEXTURES;

		// Build mesh
		MeshData buildingMesh;
		BuildBoxMesh(buildingMesh, center, halfExtents, 1.0f, 1.0f);
		BuildRoofMesh(buildingMesh, center, halfExtents);

		// Create or find the building GameObject
		std::string objName = prefix + std::to_string(i);
		GameObject* obj = scene.FindObject(objName);
		if (!obj) {
			obj = new GameObject(objName);
			scene.AddObject(obj);
		}

		obj->GetTransform().SetPosition(glm::vec3(0.0f));
		obj->GetTransform().SetRotation(glm::vec3(0.0f));
		obj->GetTransform().SetScale(glm::vec3(1.0f));

		obj->SetMesh(buildingMesh.ToMesh());
		obj->SetCPUMeshData(buildingMesh);

		// Set wall texture via texture layer
		while (obj->GetTextureLayers().size() > 0)
			obj->RemoveTextureLayer(0);

		// Load and cache wall texture
		if (texCache.find(texIdx) == texCache.end()) {
			texCache[texIdx] = new Texture(WALL_TEXTURES[texIdx]);
			texCache[texIdx]->LoadTexture();
		}

		TextureLayer wallLayer;
		wallLayer.texturePath = WALL_TEXTURES[texIdx];
		wallLayer.texture = texCache[texIdx];
		wallLayer.blendMode = LayerBlendMode::Normal;
		wallLayer.opacity = 1.0f;
		wallLayer.tiling = 1.0f;
		obj->AddTextureLayer(wallLayer);

		if (progress && i % 10 == 0)
			progress(10.0f + 85.0f * (float)i / (float)plots.size(), "Placed " + std::to_string(i + 1) + " buildings");
	}

	if (progress) progress(100.0f, "Buildings complete!");
	printf("[BuildingGenNode] Generated %d buildings.\n", (int)plots.size());
}
