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

// Different textures contain different numbers of windows per image.
// We apply a multiplier to the tiling so that densely-packed window textures
// aren't shrunk to the size of a matchbox.
static const float WALL_TEXTURE_SCALES[] = {
	0.25f, // office_windows: extremely dense
	0.35f, // apartment_windows: dense
	0.60f, // skyscraper1
	0.80f, // skyscraper2: already looks quite large
	0.80f, // skyscraper3
	0.25f, // window_blocks: extremely dense
	1.00f, // metal_building: horizontal siding, scale 1.0 is fine
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

	float sizeX = halfExtents.x * 2.0f;
	float sizeY = halfExtents.y * 2.0f;
	float sizeZ = halfExtents.z * 2.0f;

	// Scale UVs so that texture doesn't stretch on large buildings.
	// We map 1 unit of UV to 'floorHeight' world units, but scale it down slightly
	// so the base texture covers a couple of floors, making windows generally larger.
	float uvScale = 0.5f / floorHeight;

	for (int i = 0; i < cube.GetVertexCount(); i++) {
		// vertex layout: x, y, z, u, v, nx, ny, nz
		float nx = cube.vertices[i * 14 + 5];
		float ny = cube.vertices[i * 14 + 6];
		float nz = cube.vertices[i * 14 + 7];

		if (std::abs(nx) > 0.5f) {
			// Left/Right faces (+X / -X). Horizontal is Z, Vertical is Y
			cube.vertices[i * 14 + 3] *= sizeZ * uvScale;
			cube.vertices[i * 14 + 4] *= sizeY * uvScale;
		}
		else if (std::abs(nz) > 0.5f) {
			// Front/Back faces (+Z / -Z). Horizontal is X, Vertical is Y
			cube.vertices[i * 14 + 3] *= sizeX * uvScale;
			cube.vertices[i * 14 + 4] *= sizeY * uvScale;
		}
		else if (std::abs(ny) > 0.5f) {
			// Top/Bottom faces (+Y / -Y). Horizontal is X, Vertical is Z
			cube.vertices[i * 14 + 3] *= sizeX * uvScale;
			cube.vertices[i * 14 + 4] *= sizeZ * uvScale;
		}
	}

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
	auto addQuad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
		glm::vec3 edge1 = b - a, edge2 = c - a;
		glm::vec3 n = glm::normalize(glm::cross(edge1, edge2));
		glm::vec3 t = glm::normalize(edge1);
		glm::vec3 bt = glm::normalize(glm::cross(n, t));

		float w = glm::length(b - a) * 0.25f; // Scale UVs by physical distance
		float h = glm::length(d - a) * 0.25f;

		unsigned int base = mesh.GetVertexCount();
		mesh.AddVertex(a.x, a.y, a.z, 0, 0, n.x, n.y, n.z, t.x, t.y, t.z, bt.x, bt.y, bt.z);
		mesh.AddVertex(b.x, b.y, b.z, w, 0, n.x, n.y, n.z, t.x, t.y, t.z, bt.x, bt.y, bt.z);
		mesh.AddVertex(c.x, c.y, c.z, w, h, n.x, n.y, n.z, t.x, t.y, t.z, bt.x, bt.y, bt.z);
		mesh.AddVertex(d.x, d.y, d.z, 0, h, n.x, n.y, n.z, t.x, t.y, t.z, bt.x, bt.y, bt.z);
		mesh.AddTriangle(base, base + 1, base + 2);
		mesh.AddTriangle(base, base + 2, base + 3);
	};

	// Helper to add a triangle (3 verts, 1 triangle) - for gable ends
	auto addTri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
		glm::vec3 edge1 = b - a, edge2 = c - a;
		glm::vec3 n = glm::normalize(glm::cross(edge1, edge2));
		glm::vec3 t = glm::normalize(edge1);
		glm::vec3 bt = glm::normalize(glm::cross(n, t));

		float w = glm::length(b - a) * 0.25f;
		float h = glm::length(c - (a + b) * 0.5f) * 0.25f;

		unsigned int base = mesh.GetVertexCount();
		mesh.AddVertex(a.x, a.y, a.z, 0, 0, n.x, n.y, n.z, t.x, t.y, t.z, bt.x, bt.y, bt.z);
		mesh.AddVertex(b.x, b.y, b.z, w, 0, n.x, n.y, n.z, t.x, t.y, t.z, bt.x, bt.y, bt.z);
		mesh.AddVertex(c.x, c.y, c.z, w * 0.5f, h, n.x, n.y, n.z, t.x, t.y, t.z, bt.x, bt.y, bt.z);
		mesh.AddTriangle(base, base + 1, base + 2);
	};

	if (dimX) {
		// Front slope (+Z side)
		addQuad(eave[3], eave[2], ridge1, ridge0);
		// Back slope (-Z side)
		addQuad(eave[1], eave[0], ridge0, ridge1);
		// Right gable (+X side)
		addTri(eave[2], eave[1], ridge1);
		// Left gable (-X side)
		addTri(eave[0], eave[3], ridge0);
	} else {
		// Right slope (+X side)
		addQuad(eave[2], eave[3], ridge0, ridge1);
		// Left slope (-X side)
		addQuad(eave[0], eave[1], ridge1, ridge0);
		// Front gable (+Z side)
		addTri(eave[1], eave[2], ridge1);
		// Back gable (-Z side)
		addTri(eave[3], eave[0], ridge0);
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
		// Residential plots get a larger inset to create a "yard", while commercial uses the standard small wallInset
		float currentInset = isCommercial ? wallInset : std::max(wallInset, 4.0f);
		float bW = (plotW - currentInset * 2.0f) * 0.5f;
		float bD = (plotD - currentInset * 2.0f) * 0.5f;
		if (bW < 0.5f || bD < 0.5f) continue;

		// ---- Section 1: Base ----
		float baseH = heightDist(rng);
		if (isCommercial) {
			baseH *= 1.5f;
		} else {
			// Residential houses should typically be 1-2 stories, maybe 3 max
			baseH = std::min(baseH, floorHeight * 2.5f);
		}

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
		MeshData roofMesh;
		glm::vec3 roofBase(roofCenterX, topY, roofCenterZ);

		bool isPeaked = (!isCommercial && floors <= 4);
		if (isPeaked) {
			// Residential: peaked gable roof
			bool ridgeDimX = (roofW > roofD); // Ridge runs along longer axis
			float peakH = std::min(roofW, roofD) * 0.6f; // Roof pitch ~60% of narrow side
			AddPeakedRoof(roofMesh, roofBase, roofW, roofD, peakH, ridgeDimX);
		} else {
			// Commercial: flat roof slab
			AddFlatRoof(roofMesh, roofBase, roofW, roofD, 0.3f);
		}

		// ---- Spawn Base/Upper GameObject ----
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

		// ---- Spawn Roof GameObject ----
		std::string roofName = objName + "_Roof";
		GameObject* roofObj = scene.FindObject(roofName);
		if (!roofObj) {
			roofObj = new GameObject(roofName);
			scene.AddObject(roofObj);
		}
		
		roofObj->GetTransform().SetPosition(glm::vec3(0.0f));
		roofObj->GetTransform().SetRotation(glm::vec3(0.0f));
		roofObj->GetTransform().SetScale(glm::vec3(1.0f));
		roofObj->SetMesh(roofMesh.ToMesh());
		roofObj->SetCPUMeshData(roofMesh);

		// Texture layers (Clear old)
		while (obj->GetTextureLayers().size() > 0) obj->RemoveTextureLayer(0);
		while (roofObj->GetTextureLayers().size() > 0) roofObj->RemoveTextureLayer(0);

		// Pick wall texture
		int texIdx = texDist(rng);
		if (isCommercial) texIdx = 2 + (rng() % 3); // Prefer skyscraper textures (idx 2-4)
		texIdx = texIdx % NUM_WALL_TEXTURES;

		if (texCache.find(texIdx) == texCache.end()) {
			texCache[texIdx] = new Texture(WALL_TEXTURES[texIdx]);
			texCache[texIdx]->LoadTexture();
		}

		TextureLayer wallLayer;
		wallLayer.texturePath = WALL_TEXTURES[texIdx];
		wallLayer.texture = texCache[texIdx];
		wallLayer.blendMode = LayerBlendMode::Normal;
		wallLayer.opacity = 1.0f;
		wallLayer.tiling = WALL_TEXTURE_SCALES[texIdx]; // Apply texture-specific scale to the baked UVs
		obj->AddTextureLayer(wallLayer);

		// Pick roof texture
		int roofTexIdx = isPeaked ? 100 : 101;
		const char* roofTexPath = isPeaked ? "Assets/Textures/buildings/roof_shingles.jpg" : "Assets/Textures/buildings/concrete.jpg";
		if (texCache.find(roofTexIdx) == texCache.end()) {
			texCache[roofTexIdx] = new Texture(roofTexPath);
			texCache[roofTexIdx]->LoadTexture();
		}

		TextureLayer roofLayer;
		roofLayer.texturePath = roofTexPath;
		roofLayer.texture = texCache[roofTexIdx];
		roofLayer.blendMode = LayerBlendMode::Normal;
		roofLayer.opacity = 1.0f;
		roofLayer.tiling = 1.0f; // Tiling baked directly into mesh UVs for both peaked and flat roofs 
		roofObj->AddTextureLayer(roofLayer);

		// ---- Fences and Parking Spots (Residential only) ----
		MeshData fenceMesh;
		MeshData parkingMesh;
		bool hasFenceAndParking = (!isCommercial);

		if (hasFenceAndParking) {
			float fenceThickness = 0.15f;
			float fenceHeight = 1.5f;
			float plotHalfW = plotW * 0.5f;
			float plotHalfD = plotD * 0.5f;
			float yBase = plot.position.y;

			// Left Fence (-X)
			fenceMesh.Append(MakeCubePart(
				glm::vec3(plot.position.x - plotHalfW + fenceThickness * 0.5f, yBase + fenceHeight * 0.5f, plot.position.z),
				glm::vec3(fenceThickness * 0.5f, fenceHeight * 0.5f, plotHalfD)
			));
			// Right Fence (+X)
			fenceMesh.Append(MakeCubePart(
				glm::vec3(plot.position.x + plotHalfW - fenceThickness * 0.5f, yBase + fenceHeight * 0.5f, plot.position.z),
				glm::vec3(fenceThickness * 0.5f, fenceHeight * 0.5f, plotHalfD)
			));
			// Back Fence (-Z)
			fenceMesh.Append(MakeCubePart(
				glm::vec3(plot.position.x, yBase + fenceHeight * 0.5f, plot.position.z - plotHalfD + fenceThickness * 0.5f),
				glm::vec3(plotHalfW, fenceHeight * 0.5f, fenceThickness * 0.5f)
			));

			// Front Fence (+Z) with driveway gap
			float drivewayW = 4.0f;
			float drivewayHalf = drivewayW * 0.5f;
			
			// Front Left
			float frontLeftW = plotHalfW - drivewayHalf;
			if (frontLeftW > 0.1f) {
				fenceMesh.Append(MakeCubePart(
					glm::vec3(plot.position.x - plotHalfW + frontLeftW * 0.5f, yBase + fenceHeight * 0.5f, plot.position.z + plotHalfD - fenceThickness * 0.5f),
					glm::vec3(frontLeftW * 0.5f, fenceHeight * 0.5f, fenceThickness * 0.5f)
				));
			}
			// Front Right
			float frontRightW = plotHalfW - drivewayHalf;
			if (frontRightW > 0.1f) {
				fenceMesh.Append(MakeCubePart(
					glm::vec3(plot.position.x + drivewayHalf + frontRightW * 0.5f, yBase + fenceHeight * 0.5f, plot.position.z + plotHalfD - fenceThickness * 0.5f),
					glm::vec3(frontRightW * 0.5f, fenceHeight * 0.5f, fenceThickness * 0.5f)
				));
			}

			// Driveway Slab (from building to front of plot)
			float drivewayDepth = plotHalfD - bD;
			if (drivewayDepth > 0.1f) {
				parkingMesh.Append(MakeCubePart(
					glm::vec3(plot.position.x, yBase + 0.05f, plot.position.z + bD + drivewayDepth * 0.5f),
					glm::vec3(drivewayHalf, 0.05f, drivewayDepth * 0.5f)
				));
			}
		}

		// ---- Spawn Fence GameObject ----
		std::string fenceName = objName + "_Fence";
		GameObject* fenceObj = scene.FindObject(fenceName);
		if (hasFenceAndParking) {
			if (!fenceObj) {
				fenceObj = new GameObject(fenceName);
				scene.AddObject(fenceObj);
			}
			fenceObj->GetTransform().SetPosition(glm::vec3(0.0f));
			fenceObj->GetTransform().SetScale(glm::vec3(1.0f));
			fenceObj->SetMesh(fenceMesh.ToMesh());
			fenceObj->SetCPUMeshData(fenceMesh);

			while (fenceObj->GetTextureLayers().size() > 0) fenceObj->RemoveTextureLayer(0);
			
			if (texCache.find(102) == texCache.end()) {
				texCache[102] = new Texture("Assets/Textures/buildings/fence.jpg");
				texCache[102]->LoadTexture();
			}
			TextureLayer fenceLayer;
			fenceLayer.texturePath = "Assets/Textures/buildings/fence.jpg";
			fenceLayer.texture = texCache[102];
			fenceLayer.blendMode = LayerBlendMode::Normal;
			fenceLayer.opacity = 1.0f;
			fenceLayer.tiling = 1.0f; // UVs baked in MakeCubePart
			fenceObj->AddTextureLayer(fenceLayer);
		} else if (fenceObj) {
			fenceObj->SetMesh(nullptr);
		}

		// ---- Spawn Parking GameObject ----
		std::string parkingName = objName + "_Parking";
		GameObject* parkingObj = scene.FindObject(parkingName);
		if (hasFenceAndParking && parkingMesh.GetVertexCount() > 0) {
			if (!parkingObj) {
				parkingObj = new GameObject(parkingName);
				scene.AddObject(parkingObj);
			}
			parkingObj->GetTransform().SetPosition(glm::vec3(0.0f));
			parkingObj->GetTransform().SetScale(glm::vec3(1.0f));
			parkingObj->SetMesh(parkingMesh.ToMesh());
			parkingObj->SetCPUMeshData(parkingMesh);

			while (parkingObj->GetTextureLayers().size() > 0) parkingObj->RemoveTextureLayer(0);
			
			if (texCache.find(101) == texCache.end()) {
				texCache[101] = new Texture("Assets/Textures/buildings/concrete.jpg");
				texCache[101]->LoadTexture();
			}
			TextureLayer parkingLayer;
			parkingLayer.texturePath = "Assets/Textures/buildings/concrete.jpg";
			parkingLayer.texture = texCache[101];
			parkingLayer.blendMode = LayerBlendMode::Normal;
			parkingLayer.opacity = 1.0f;
			parkingLayer.tiling = 1.0f; // UVs baked in MakeCubePart
			parkingObj->AddTextureLayer(parkingLayer);
		} else if (parkingObj) {
			parkingObj->SetMesh(nullptr);
		}

		builtCount++;

		if (progress && i % 10 == 0)
			progress(10.0f + 85.0f * (float)i / (float)plots.size(), "Placed " + std::to_string(builtCount) + " buildings");
	}

	if (progress) progress(100.0f, "Buildings complete!");
	printf("[BuildingGenNode] Generated %d multi-part buildings.\n", builtCount);
}
