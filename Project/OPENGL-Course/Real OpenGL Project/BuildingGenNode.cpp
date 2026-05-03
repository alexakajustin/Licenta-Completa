#include "BuildingGenNode.h"
#include "GameObject.h"
#include "Mesh.h"
#include "Material.h"
#include "Texture.h"
#include "TextureLayer.h"
#include "PrimitiveGenerator.h"
#include <cmath>
#include <random>
#include <map>
#include <glm/gtc/matrix_transform.hpp>

// Wall texture pool — no brick, only proper building facades
static const char* WALL_TEXTURES[] = {
	"Assets/Textures/buildings/office_windows.jpg",
	"Assets/Textures/buildings/apartment_windows.jpg",
	"Assets/Textures/buildings/skyscraper1.jpg",
	"Assets/Textures/buildings/skyscraper2.jpg",
	"Assets/Textures/buildings/skyscraper3.jpg",
	"Assets/Textures/buildings/window_blocks.jpg",
	"Assets/Textures/buildings/metal_building.jpg",
};
static const int NUM_WALL_TEXTURES = sizeof(WALL_TEXTURES) / sizeof(WALL_TEXTURES[0]);

static const char* ROOF_TEXTURE = "Assets/Textures/buildings/roof_shingles.jpg";
static const char* FLAT_ROOF_TEXTURE = "Assets/Textures/buildings/concrete.jpg";

// =====================================================================
// Construction
// =====================================================================

BuildingGenNode::BuildingGenNode(NodeGraph& graph)
{
	id = graph.NextNodeId();
	title = "Building Gen";

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
	ImGui::DragFloat("Upper Section %", &upperSectionProb, 0.01f, 0.0f, 1.0f, "%.2f");
	ImGui::DragFloat("Upper Scale", &upperSectionScale, 0.01f, 0.3f, 0.95f, "%.2f");
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
	j["upperSectionProb"] = upperSectionProb;
	j["upperSectionScale"] = upperSectionScale;
	j["seed"] = seed;
	return j;
}

void BuildingGenNode::Deserialize(const json& j)
{
	GraphNode::Deserialize(j);
	minHeight = j.value("minHeight", 5.0f);
	maxHeight = j.value("maxHeight", 25.0f);
	floorHeight = j.value("floorHeight", 3.0f);
	wallInset = j.value("wallInset", 0.5f);
	roofOverhang = j.value("roofOverhang", 0.3f);
	upperSectionProb = j.value("upperSectionProb", 0.5f);
	upperSectionScale = j.value("upperSectionScale", 0.65f);
	seed = j.value("seed", 42);
}

// =====================================================================
// MakeCubePart — engine primitive scaled to building dimensions
// =====================================================================

MeshData BuildingGenNode::MakeCubePart(glm::vec3 center, glm::vec3 halfExtents)
{
	// Get the engine's unit cube (correct UVs on all 6 faces)
	MeshData cube = PrimitiveGenerator::GetCubeData();

	// The engine cube goes from [-0.5, 0.5] on each axis (half-extent = 0.5).
	// Scale by 2*halfExtents to get the correct building dimensions,
	// then translate to center position.
	glm::mat4 xform = glm::mat4(1.0f);
	xform = glm::translate(xform, center);
	xform = glm::scale(xform, halfExtents * 2.0f);

	cube.TransformBy(xform);
	return cube;
}

// =====================================================================
// AddPeakedRoof — triangular gable roof (residential)
// =====================================================================

void BuildingGenNode::AddPeakedRoof(MeshData& mesh, glm::vec3 base, float halfW, float halfD, float peakH, bool dimX)
{
	// Peaked roof: two sloped triangular faces + two triangular gable ends
	// 'dimX' controls the ridge direction — if true, ridge runs along X; peak in Z
	
	float y = base.y;
	float peakY = y + peakH;
	float oh = roofOverhang; // Overhang

	glm::vec3 ridge0, ridge1; // Ridge line endpoints
	glm::vec3 eave[4];        // Eave corners (bottom of roof)

	if (dimX) {
		// Ridge runs along X, peak goes up in Z direction
		ridge0 = glm::vec3(base.x - halfW - oh, peakY, base.z);
		ridge1 = glm::vec3(base.x + halfW + oh, peakY, base.z);
		eave[0] = glm::vec3(base.x - halfW - oh, y, base.z - halfD - oh);
		eave[1] = glm::vec3(base.x + halfW + oh, y, base.z - halfD - oh);
		eave[2] = glm::vec3(base.x + halfW + oh, y, base.z + halfD + oh);
		eave[3] = glm::vec3(base.x - halfW - oh, y, base.z + halfD + oh);
	} else {
		// Ridge runs along Z, peak goes up in X direction
		ridge0 = glm::vec3(base.x, peakY, base.z - halfD - oh);
		ridge1 = glm::vec3(base.x, peakY, base.z + halfD + oh);
		eave[0] = glm::vec3(base.x - halfW - oh, y, base.z - halfD - oh);
		eave[1] = glm::vec3(base.x - halfW - oh, y, base.z + halfD + oh);
		eave[2] = glm::vec3(base.x + halfW + oh, y, base.z + halfD + oh);
		eave[3] = glm::vec3(base.x + halfW + oh, y, base.z - halfD - oh);
	}

	// Helper to add a quad (4 verts, 2 triangles) with outward normal
	auto addQuad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
		float u0, float v0, float u1, float v1, float u2, float v2, float u3, float v3) {
		glm::vec3 edge1 = b - a, edge2 = c - a;
		glm::vec3 n = glm::normalize(glm::cross(edge1, edge2));
		glm::vec3 t = glm::normalize(edge1);
		glm::vec3 bt = glm::normalize(glm::cross(n, t));

		unsigned int base = mesh.GetVertexCount();
		mesh.AddVertex(a.x, a.y, a.z, u0, v0, n.x, n.y, n.z, t.x, t.y, t.z, bt.x, bt.y, bt.z);
		mesh.AddVertex(b.x, b.y, b.z, u1, v1, n.x, n.y, n.z, t.x, t.y, t.z, bt.x, bt.y, bt.z);
		mesh.AddVertex(c.x, c.y, c.z, u2, v2, n.x, n.y, n.z, t.x, t.y, t.z, bt.x, bt.y, bt.z);
		mesh.AddVertex(d.x, d.y, d.z, u3, v3, n.x, n.y, n.z, t.x, t.y, t.z, bt.x, bt.y, bt.z);
		mesh.AddTriangle(base, base + 1, base + 2);
		mesh.AddTriangle(base, base + 2, base + 3);
	};

	// Helper to add a triangle (3 verts, 1 triangle) - for gable ends
	auto addTri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
		glm::vec3 edge1 = b - a, edge2 = c - a;
		glm::vec3 n = glm::normalize(glm::cross(edge1, edge2));
		glm::vec3 t = glm::normalize(edge1);
		glm::vec3 bt = glm::normalize(glm::cross(n, t));

		unsigned int base = mesh.GetVertexCount();
		mesh.AddVertex(a.x, a.y, a.z, 0, 0, n.x, n.y, n.z, t.x, t.y, t.z, bt.x, bt.y, bt.z);
		mesh.AddVertex(b.x, b.y, b.z, 1, 0, n.x, n.y, n.z, t.x, t.y, t.z, bt.x, bt.y, bt.z);
		mesh.AddVertex(c.x, c.y, c.z, 0.5f, 1, n.x, n.y, n.z, t.x, t.y, t.z, bt.x, bt.y, bt.z);
		mesh.AddTriangle(base, base + 1, base + 2);
	};

	if (dimX) {
		// Two sloped panels
		// Front slope: eave[0], eave[1], ridge1, ridge0
		addQuad(eave[0], eave[1], ridge1, ridge0,  0,0, 1,0, 1,1, 0,1);
		// Back slope: eave[3], eave[2], ridge1, ridge0 (reversed)
		addQuad(eave[2], eave[3], ridge0, ridge1,  0,0, 1,0, 1,1, 0,1);
		// Gable ends (triangles)
		// Left gable
		addTri(eave[0], eave[3], ridge0);
		// Right gable
		addTri(eave[1], eave[2], ridge1);
	} else {
		// Two sloped panels
		// Left slope: eave[0], eave[1], ridge1, ridge0
		addQuad(eave[0], eave[1], ridge1, ridge0,  0,0, 1,0, 1,1, 0,1);
		// Right slope: eave[3], eave[2], ridge1, ridge0 (reversed)
		addQuad(eave[3], eave[2], ridge1, ridge0,  0,0, 1,0, 1,1, 0,1);
		// Gable ends (triangles)
		addTri(eave[0], eave[3], ridge0);
		addTri(eave[2], eave[1], ridge1);
	}
}

// =====================================================================
// AddFlatRoof — thin slab on top (commercial buildings)
// =====================================================================

void BuildingGenNode::AddFlatRoof(MeshData& mesh, glm::vec3 base, float halfW, float halfD, float thickness)
{
	float oh = roofOverhang;
	MeshData slab = MakeCubePart(
		glm::vec3(base.x, base.y + thickness * 0.5f, base.z),
		glm::vec3(halfW + oh, thickness * 0.5f, halfD + oh)
	);
	mesh.Append(slab);
}

// =====================================================================
// Execute — generate multi-part buildings and spawn as GameObjects
// =====================================================================

void BuildingGenNode::Execute(SceneManager& scene, NodeProgressCallback progress)
{
	if (progress) progress(0.0f, "Reading plot data...");

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
	std::uniform_real_distribution<float> prob01(0.0f, 1.0f);
	std::uniform_int_distribution<int> texDist(0, NUM_WALL_TEXTURES - 1);

	std::string prefix = "Building_" + std::to_string(id) + "_";
	std::map<int, Texture*> texCache;

	Texture* roofTex = new Texture(ROOF_TEXTURE);
	roofTex->LoadTexture();
	Texture* flatRoofTex = new Texture(FLAT_ROOF_TEXTURE);
	flatRoofTex->LoadTexture();

	int builtCount = 0;

	for (size_t i = 0; i < plots.size(); i++)
	{
		const TransformData& plot = plots[i];

		float plotW = plot.scale.x;
		float plotD = plot.scale.z;
		bool isCommercial = (plot.scale.y > 1.5f);

		// Building footprint (shrink from plot edge)
		float bW = (plotW - wallInset * 2.0f) * 0.5f;
		float bD = (plotD - wallInset * 2.0f) * 0.5f;
		if (bW < 0.5f || bD < 0.5f) continue;

		// ---- Section 1: Base ----
		float baseH = heightDist(rng);
		if (isCommercial) baseH *= 1.5f;

		// Snap to floor count
		int floors = std::max(1, (int)(baseH / floorHeight));
		baseH = floors * floorHeight;

		glm::vec3 baseCenter(plot.position.x, plot.position.y + baseH * 0.5f, plot.position.z);
		glm::vec3 baseHalf(bW, baseH * 0.5f, bD);

		MeshData buildingMesh;
		MeshData basePart = MakeCubePart(baseCenter, baseHalf);
		buildingMesh.Append(basePart);

		float topY = plot.position.y + baseH;
		float roofW = bW;
		float roofD = bD;

		// ---- Section 2: Upper section (optional) ----
		bool hasUpper = (prob01(rng) < upperSectionProb) && (floors >= 2);
		float roofCenterX = plot.position.x;
		float roofCenterZ = plot.position.z;

		if (hasUpper)
		{
			float upperW = bW * (upperSectionScale + prob01(rng) * (1.0f - upperSectionScale));
			float upperD = bD * (upperSectionScale + prob01(rng) * (1.0f - upperSectionScale));
			float upperH = heightDist(rng) * 0.5f;
			int upperFloors = std::max(1, (int)(upperH / floorHeight));
			upperH = upperFloors * floorHeight;

			// Offset the upper section slightly for L/T shapes
			float offsetX = (bW - upperW) * (prob01(rng) * 2.0f - 1.0f) * 0.5f;
			float offsetZ = (bD - upperD) * (prob01(rng) * 2.0f - 1.0f) * 0.5f;

			roofCenterX = plot.position.x + offsetX;
			roofCenterZ = plot.position.z + offsetZ;

			glm::vec3 upperCenter(
				roofCenterX,
				topY + upperH * 0.5f,
				roofCenterZ
			);
			glm::vec3 upperHalf(upperW, upperH * 0.5f, upperD);

			MeshData upperPart = MakeCubePart(upperCenter, upperHalf);
			buildingMesh.Append(upperPart);

			topY += upperH;
			roofW = upperW;
			roofD = upperD;
		}

		// ---- Roof ----
		glm::vec3 roofBase(roofCenterX, topY, roofCenterZ);

		if (!isCommercial && floors <= 4) {
			// Residential: peaked gable roof
			bool ridgeDimX = (roofW > roofD); // Ridge runs along longer axis
			float peakH = std::min(roofW, roofD) * 0.6f; // Roof pitch ~60% of narrow side
			AddPeakedRoof(buildingMesh, roofBase, roofW, roofD, peakH, ridgeDimX);
		} else {
			// Commercial: flat roof slab
			AddFlatRoof(buildingMesh, roofBase, roofW, roofD, 0.3f);
		}

		// ---- Spawn as GameObject ----
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

		// Texture layers
		while (obj->GetTextureLayers().size() > 0)
			obj->RemoveTextureLayer(0);

		// Pick wall texture
		int texIdx = texDist(rng);
		if (isCommercial) texIdx = 2 + (rng() % 3); // Prefer skyscraper textures (idx 2-4)
		texIdx = texIdx % NUM_WALL_TEXTURES;

		if (texCache.find(texIdx) == texCache.end()) {
			texCache[texIdx] = new Texture(WALL_TEXTURES[texIdx]);
			texCache[texIdx]->LoadTexture();
		}

		// Calculate wall tiling based on floor count
		float wallTiling = std::max(1.0f, (float)floors * 0.5f);

		TextureLayer wallLayer;
		wallLayer.texturePath = WALL_TEXTURES[texIdx];
		wallLayer.texture = texCache[texIdx];
		wallLayer.blendMode = LayerBlendMode::Normal;
		wallLayer.opacity = 1.0f;
		wallLayer.tiling = wallTiling;
		obj->AddTextureLayer(wallLayer);

		builtCount++;

		if (progress && i % 10 == 0)
			progress(10.0f + 85.0f * (float)i / (float)plots.size(), "Placed " + std::to_string(builtCount) + " buildings");
	}

	if (progress) progress(100.0f, "Buildings complete!");
	printf("[BuildingGenNode] Generated %d multi-part buildings.\n", builtCount);
}
