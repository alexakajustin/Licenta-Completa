#include "CityGridNode.h"
#include "PrimitiveGenerator.h"
#include "GameObject.h"
#include "Mesh.h"
#include "Material.h"
#include "Texture.h"
#include <cmath>
#include <random>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =====================================================================
// Construction
// =====================================================================

CityGridNode::CityGridNode(NodeGraph& graph)
{
	id = graph.NextNodeId();
	title = "City Grid";

	// Inputs
	Pin surfaceIn(graph.NextPinId(), PinDataType::Mesh, "Surface");
	inputs.push_back(surfaceIn);

	// Outputs
	Pin roadsOut(graph.NextPinId(), PinDataType::Mesh, "Roads");
	Pin plotsOut(graph.NextPinId(), PinDataType::TransformList, "Plots");
	outputs.push_back(roadsOut);
	outputs.push_back(plotsOut);
}

// =====================================================================
// UI
// =====================================================================

void CityGridNode::RenderContent(SceneManager* scene)
{
	ImGui::Text("City Layout");
	ImGui::Separator();

	ImGui::DragFloat("City Size", &citySize, 1.0f, 20.0f, 2000.0f, "%.0f");
	ImGui::DragFloat("Road Width", &roadWidth, 0.1f, 0.5f, 10.0f, "%.1f");
	ImGui::DragFloat("Spacing X", &roadSpacingX, 0.5f, 5.0f, 100.0f, "%.1f");
	ImGui::DragFloat("Spacing Z", &roadSpacingZ, 0.5f, 5.0f, 100.0f, "%.1f");
	ImGui::DragFloat("Sidewalk W", &sidewalkWidth, 0.05f, 0.0f, 2.0f, "%.2f");
	ImGui::DragFloat("Road Height", &roadHeight, 0.005f, 0.0f, 0.5f, "%.3f");
	ImGui::DragFloat("Setback", &buildingSetback, 0.1f, 0.0f, 10.0f, "%.1f");
	ImGui::SliderFloat("Residential", &residentialProbability, 0.0f, 1.0f, "%.2f");
	ImGui::DragInt("Park Rate", &parkRate, 1, 0, 20);
	ImGui::DragInt("Seed", &seed, 1, 0, 9999);
}

// =====================================================================
// Serialization
// =====================================================================

json CityGridNode::Serialize() const
{
	json j = GraphNode::Serialize();
	j["citySize"] = citySize;
	j["roadWidth"] = roadWidth;
	j["roadSpacingX"] = roadSpacingX;
	j["roadSpacingZ"] = roadSpacingZ;
	j["sidewalkWidth"] = sidewalkWidth;
	j["roadHeight"] = roadHeight;
	j["buildingSetback"] = buildingSetback;
	j["residentialProbability"] = residentialProbability;
	j["parkRate"] = parkRate;
	j["seed"] = seed;
	return j;
}

void CityGridNode::Deserialize(const json& j)
{
	GraphNode::Deserialize(j);
	citySize = j.value("citySize", 100.0f);
	roadWidth = j.value("roadWidth", 6.0f);
	roadSpacingX = j.value("roadSpacingX", 30.0f);
	roadSpacingZ = j.value("roadSpacingZ", 30.0f);
	sidewalkWidth = j.value("sidewalkWidth", 1.5f);
	roadHeight = j.value("roadHeight", 0.02f);
	buildingSetback = j.value("buildingSetback", 1.0f);
	residentialProbability = j.value("residentialProbability", 0.5f);
	parkRate = j.value("parkRate", 8);
	seed = j.value("seed", 42);
}

// =====================================================================
// Grid Generation (Algorithm inspired by 3DWorld city_gen.cpp)
// =====================================================================

void CityGridNode::GenerateGrid()
{
	plots.clear();
	roadSegs.clear();
	intersections.clear();

	float halfCity = citySize * 0.5f;
	float halfRoad = roadWidth * 0.5f;

	// Calculate road positions
	// Roads are placed at regular intervals spanning the city
	float roadPitchX = roadWidth + roadSpacingX;
	float roadPitchZ = roadWidth + roadSpacingZ;

	// Number of roads in each direction
	int numRoadsX = std::max(2, (int)(citySize / roadPitchX));
	int numRoadsZ = std::max(2, (int)(citySize / roadPitchZ));

	// Recalculate pitch to fit evenly
	roadPitchX = citySize / (float)numRoadsX;
	roadPitchZ = citySize / (float)numRoadsZ;

	// Generate road center positions
	std::vector<float> roadPosX, roadPosZ;
	for (int i = 0; i <= numRoadsX; i++)
	{
		float x = -halfCity + i * roadPitchX;
		roadPosX.push_back(x);
	}
	for (int i = 0; i <= numRoadsZ; i++)
	{
		float z = -halfCity + i * roadPitchZ;
		roadPosZ.push_back(z);
	}

	// Create road segments between intersections — full span, no gaps
	// X-direction roads (run along X, at each Z position)
	for (size_t iz = 0; iz < roadPosZ.size(); iz++)
	{
		for (size_t ix = 0; ix + 1 < roadPosX.size(); ix++)
		{
			RoadSegment seg;
			seg.start = glm::vec3(roadPosX[ix], roadHeight, roadPosZ[iz]);
			seg.end = glm::vec3(roadPosX[ix + 1], roadHeight, roadPosZ[iz]);
			seg.width = roadWidth;
			seg.dimX = true;
			if (seg.end.x > seg.start.x)
				roadSegs.push_back(seg);
		}
	}

	// Z-direction roads (run along Z, at each X position)
	for (size_t ix = 0; ix < roadPosX.size(); ix++)
	{
		for (size_t iz = 0; iz + 1 < roadPosZ.size(); iz++)
		{
			RoadSegment seg;
			seg.start = glm::vec3(roadPosX[ix], roadHeight, roadPosZ[iz]);
			seg.end = glm::vec3(roadPosX[ix], roadHeight, roadPosZ[iz + 1]);
			seg.width = roadWidth;
			seg.dimX = false;
			if (seg.end.z > seg.start.z)
				roadSegs.push_back(seg);
		}
	}

	// Create intersections at every road crossing
	for (size_t ix = 0; ix < roadPosX.size(); ix++)
	{
		for (size_t iz = 0; iz < roadPosZ.size(); iz++)
		{
			bool hasLeft = (ix > 0);
			bool hasRight = (ix + 1 < roadPosX.size());
			bool hasFront = (iz > 0);
			bool hasBack = (iz + 1 < roadPosZ.size());

			uint8_t conn = 0;
			if (hasLeft) conn |= 1;   // -X
			if (hasRight) conn |= 2;  // +X
			if (hasFront) conn |= 4;  // -Z
			if (hasBack) conn |= 8;   // +Z

			int numConn = ((conn & 1) ? 1 : 0) + ((conn & 2) ? 1 : 0) +
				((conn & 4) ? 1 : 0) + ((conn & 8) ? 1 : 0);

			if (numConn >= 2) // At least a 2-way
			{
				RoadIntersection isec;
				isec.center = glm::vec3(roadPosX[ix], roadHeight, roadPosZ[iz]);
				isec.size = roadWidth;
				isec.numConnections = numConn;
				isec.connectionMask = conn;
				intersections.push_back(isec);
			}
		}
	}

	// Create plots (land parcels between roads)
	std::mt19937 rng(seed);
	std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

	for (size_t ix = 0; ix + 1 < roadPosX.size(); ix++)
	{
		for (size_t iz = 0; iz + 1 < roadPosZ.size(); iz++)
		{
			float x1 = roadPosX[ix] + halfRoad;
			float x2 = roadPosX[ix + 1] - halfRoad;
			float z1 = roadPosZ[iz] + halfRoad;
			float z2 = roadPosZ[iz + 1] - halfRoad;

			if (x2 <= x1 || z2 <= z1) continue; // Degenerate plot

			CityPlot plot;
			plot.center = glm::vec3((x1 + x2) * 0.5f, roadHeight, (z1 + z2) * 0.5f);
			plot.size = glm::vec2(x2 - x1, z2 - z1);
			plot.gridX = (int)ix;
			plot.gridY = (int)iz;
			plot.isResidential = (dist01(rng) < residentialProbability);
			plot.isPark = (parkRate > 0 && (rng() % parkRate) == 0);

			plots.push_back(plot);
		}
	}

	printf("[CityGridNode] Generated %d road segments, %d intersections, %d plots\n",
		(int)roadSegs.size(), (int)intersections.size(), (int)plots.size());
}

// =====================================================================
// Mesh Building — Road Quads
// =====================================================================

void CityGridNode::AddRoadQuad(MeshData& mesh, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
	float uMin, float uMax, float vMin, float vMax)
{
	// Normal: up (roads are flat)
	glm::vec3 normal(0.0f, 1.0f, 0.0f);

	// Tangent: along the road direction (p1 - p0 roughly)
	glm::vec3 edge1 = glm::normalize(p1 - p0);
	glm::vec3 tangent = edge1;
	glm::vec3 bitangent = glm::normalize(glm::cross(normal, tangent));

	unsigned int base = mesh.GetVertexCount();

	// Vertex layout: pos(3) + uv(2) + normal(3) + tangent(3) + bitangent(3) = 14 floats
	mesh.AddVertex(p0.x, p0.y, p0.z, uMin, vMin, normal.x, normal.y, normal.z, tangent.x, tangent.y, tangent.z, bitangent.x, bitangent.y, bitangent.z);
	mesh.AddVertex(p1.x, p1.y, p1.z, uMax, vMin, normal.x, normal.y, normal.z, tangent.x, tangent.y, tangent.z, bitangent.x, bitangent.y, bitangent.z);
	mesh.AddVertex(p2.x, p2.y, p2.z, uMax, vMax, normal.x, normal.y, normal.z, tangent.x, tangent.y, tangent.z, bitangent.x, bitangent.y, bitangent.z);
	mesh.AddVertex(p3.x, p3.y, p3.z, uMin, vMax, normal.x, normal.y, normal.z, tangent.x, tangent.y, tangent.z, bitangent.x, bitangent.y, bitangent.z);

	// Two triangles: 0-1-2, 0-2-3 (Standard CCW)
	mesh.AddTriangle(base, base + 1, base + 2);
	mesh.AddTriangle(base, base + 2, base + 3);
}

void CityGridNode::AddSidewalkStrip(MeshData& mesh, glm::vec3 roadEdgeStart, glm::vec3 roadEdgeEnd,
	float width, bool side, float texScale)
{
	glm::vec3 dir = glm::normalize(roadEdgeEnd - roadEdgeStart);
	glm::vec3 up(0.0f, 1.0f, 0.0f);
	glm::vec3 right = glm::normalize(glm::cross(dir, up));

	float offset = side ? width : -width;
	float y = roadEdgeStart.y + 0.03f; // Slightly raised above road

	glm::vec3 pA = roadEdgeStart;
	glm::vec3 pB = roadEdgeEnd;
	glm::vec3 pC = roadEdgeEnd + right * offset;
	glm::vec3 pD = roadEdgeStart + right * offset;

	// Universal ordered bounds
	float xMin = std::min({pA.x, pB.x, pC.x, pD.x});
	float xMax = std::max({pA.x, pB.x, pC.x, pD.x});
	float zMin = std::min({pA.z, pB.z, pC.z, pD.z});
	float zMax = std::max({pA.z, pB.z, pC.z, pD.z});

	glm::vec3 p0(xMin, y, zMax); // BL
	glm::vec3 p1(xMax, y, zMax); // BR
	glm::vec3 p2(xMax, y, zMin); // TR
	glm::vec3 p3(xMin, y, zMin); // TL
	
	float len = glm::length(roadEdgeEnd - roadEdgeStart);
	AddRoadQuad(mesh, p0, p1, p2, p3, 0.0f, width * texScale, 0.0f, len * texScale);
}

void CityGridNode::BuildIntersectionQuad(MeshData& mesh, const RoadIntersection& isec)
{
	float half = isec.size * 0.5f;
	float y = isec.center.y;

	float xMin = isec.center.x - half;
	float xMax = isec.center.x + half;
	float zMin = isec.center.z - half;
	float zMax = isec.center.z + half;

	glm::vec3 p0(xMin, y, zMax); // BL
	glm::vec3 p1(xMax, y, zMax); // BR
	glm::vec3 p2(xMax, y, zMin); // TR
	glm::vec3 p3(xMin, y, zMin); // TL

	AddRoadQuad(mesh, p0, p1, p2, p3, 0.0f, 1.0f, 0.0f, 1.0f);
}

// =====================================================================
// Build Complete Road Mesh
// =====================================================================

void CityGridNode::BuildRoadMesh(MeshData& roadOutput, MeshData& sidewalkOutput)
{
	roadOutput.Clear();
	sidewalkOutput.Clear();

	float halfRoad = roadWidth * 0.5f;

	// 1. Road Segments
	for (const auto& seg : roadSegs)
	{
		float len = 0.0f;
		glm::vec3 p0, p1, p2, p3;

		float xMin, xMax, zMin, zMax;
		float y = seg.start.y;

		if (seg.dimX)
		{
			xMin = std::min(seg.start.x, seg.end.x);
			xMax = std::max(seg.start.x, seg.end.x);
			zMin = seg.start.z - halfRoad;
			zMax = seg.start.z + halfRoad;
			len = xMax - xMin;
		}
		else
		{
			xMin = seg.start.x - halfRoad;
			xMax = seg.start.x + halfRoad;
			zMin = std::min(seg.start.z, seg.end.z);
			zMax = std::max(seg.start.z, seg.end.z);
			len = zMax - zMin;
		}

		p0 = glm::vec3(xMin, y, zMax); // BL
		p1 = glm::vec3(xMax, y, zMax); // BR
		p2 = glm::vec3(xMax, y, zMin); // TR
		p3 = glm::vec3(xMin, y, zMin); // TL

		// Texture: tile along length, stretch across width
		float ar = len / roadWidth;
		AddRoadQuad(roadOutput, p0, p1, p2, p3, 0.0f, 1.0f, 0.0f, ar * roadTexScale);

		// Sidewalks on both sides
		if (sidewalkWidth > 0.01f)
		{
			if (seg.dimX)
			{
				glm::vec3 edge0Start(seg.start.x, seg.start.y, seg.start.z - halfRoad);
				glm::vec3 edge0End(seg.end.x, seg.end.y, seg.end.z - halfRoad);
				glm::vec3 edge1Start(seg.start.x, seg.start.y, seg.start.z + halfRoad);
				glm::vec3 edge1End(seg.end.x, seg.end.y, seg.end.z + halfRoad);

				AddSidewalkStrip(sidewalkOutput, edge0Start, edge0End, sidewalkWidth, false, sidewalkTexScale);
				AddSidewalkStrip(sidewalkOutput, edge1Start, edge1End, sidewalkWidth, true, sidewalkTexScale);
			}
			else
			{
				glm::vec3 edge0Start(seg.start.x - halfRoad, seg.start.y, seg.start.z);
				glm::vec3 edge0End(seg.end.x - halfRoad, seg.end.y, seg.end.z);
				glm::vec3 edge1Start(seg.start.x + halfRoad, seg.start.y, seg.start.z);
				glm::vec3 edge1End(seg.end.x + halfRoad, seg.end.y, seg.end.z);

				AddSidewalkStrip(sidewalkOutput, edge0Start, edge0End, sidewalkWidth, false, sidewalkTexScale);
				AddSidewalkStrip(sidewalkOutput, edge1Start, edge1End, sidewalkWidth, true, sidewalkTexScale);
			}
		}
	}

	printf("[CityGridNode] Built meshes: Road(%d tris), Sidewalk(%d tris)\n",
		roadOutput.GetTriangleCount(), sidewalkOutput.GetTriangleCount());
}

// =====================================================================
// Build Plot Ground Mesh (separate from roads for different texture)
// =====================================================================

void CityGridNode::BuildPlotMesh(MeshData& output)
{
	output.Clear();
	float halfRoad = roadWidth * 0.5f;

	for (const auto& plot : plots)
	{
		float halfW = plot.size.x * 0.5f;
		float halfD = plot.size.y * 0.5f;
		float y = plot.center.y - 0.01f; // Slightly below road level

		// Extend plots slightly under roads to eliminate gaps
		float overlapX = roadWidth * 0.5f;
		float overlapZ = roadWidth * 0.5f;

		float xMin = plot.center.x - halfW - overlapX;
		float xMax = plot.center.x + halfW + overlapX;
		float zMin = plot.center.z - halfD - overlapZ;
		float zMax = plot.center.z + halfD + overlapZ;

		glm::vec3 p0(xMin, y, zMax); // BL
		glm::vec3 p1(xMax, y, zMax); // BR
		glm::vec3 p2(xMax, y, zMin); // TR
		glm::vec3 p3(xMin, y, zMin); // TL

		float texU = plot.size.x * 0.15f;
		float texV = plot.size.y * 0.15f;
		// Use p0, p1, p2, p3 which is CCW, AddRoadQuad will make it CW (facing UP)
		AddRoadQuad(output, p0, p1, p2, p3, 0.0f, texU, 0.0f, texV);
	}
}

// =====================================================================
// Build Plot Transforms (output for building placement)
// =====================================================================

void CityGridNode::BuildPlotTransforms(TransformList& output)
{
	output.clear();
	output.reserve(plots.size());

	for (const auto& plot : plots)
	{
		if (plot.isPark) continue; // Parks don't get buildings

		TransformData td;
		td.position = plot.center;
		td.rotation = glm::vec3(0.0f);
		td.normal = glm::vec3(0.0f, 1.0f, 0.0f);

		// Encode plot size in scale (useful for downstream building placement)
		// Scale X = available width, Scale Z = available depth.
		// We must subtract the sidewalk width so buildings/fences don't overlap concrete.
		float totalSetback = sidewalkWidth + buildingSetback;
		float availW = plot.size.x - totalSetback * 2.0f;
		float availD = plot.size.y - totalSetback * 2.0f;
		td.scale = glm::vec3(
			std::max(1.0f, availW),
			plot.isResidential ? 1.0f : 2.0f, // Y scale hint: 1 = residential, 2 = commercial
			std::max(1.0f, availD)
		);

		output.push_back(td);
	}
}

// =====================================================================
// Terrain Height Sampling (basic nearest-point for flat terrain)
// =====================================================================

float CityGridNode::SampleTerrainHeight(const MeshData& terrain, float x, float z)
{
	if (terrain.vertices.empty()) return 0.0f;

	// Simple: find nearest vertex and return its Y
	float bestDist = 1e18f;
	float bestY = 0.0f;
	int count = terrain.GetVertexCount();

	for (int i = 0; i < count; i += 10) // Sample every 10th vertex for speed
	{
		glm::vec3 p = terrain.GetPosition(i);
		float dx = p.x - x;
		float dz = p.z - z;
		float dist = dx * dx + dz * dz;
		if (dist < bestDist) { bestDist = dist; bestY = p.y; }
	}

	return bestY;
}

// =====================================================================
// Execute — spawns road GameObjects directly into the scene
// =====================================================================

void CityGridNode::Execute(SceneManager& scene, NodeProgressCallback progress)
{
	if (progress) progress(0.0f, "Generating city grid...");

	// 1. Generate the grid layout
	GenerateGrid();
	if (progress) progress(30.0f, "Building road meshes...");
	
	// 2. Build road and sidewalk meshes separately
	MeshData roadMesh, sidewalkMesh, plotMesh;
	BuildRoadMesh(roadMesh, sidewalkMesh);
	BuildPlotMesh(plotMesh);

	// 3. If we have a terrain input, offset all geometry to match
	float terrainOffset = 0.0f;
	if (!inputs[0].data.meshData.vertices.empty())
	{
		const MeshData& terrain = inputs[0].data.meshData;
		terrainOffset = SampleTerrainHeight(terrain, 0.0f, 0.0f);

		for (int i = 0; i < roadMesh.GetVertexCount(); i++)
			roadMesh.vertices[i * 14 + 1] += terrainOffset;
		for (int i = 0; i < sidewalkMesh.GetVertexCount(); i++)
			sidewalkMesh.vertices[i * 14 + 1] += terrainOffset;
		for (int i = 0; i < plotMesh.GetVertexCount(); i++)
			plotMesh.vertices[i * 14 + 1] += terrainOffset;

		for (auto& plot : plots)
			plot.center.y += terrainOffset;
	}

	if (progress) progress(50.0f, "Spawning city objects...");

	// ---- Helper: find-or-create a GameObject and assign mesh + texture ----
	std::string idStr = std::to_string(id);

	auto syncCityObject = [&](const std::string& name, MeshData& meshData, const std::string& texPath, float tiling) {
		GameObject* obj = scene.FindObject(name);
		if (!obj) {
			obj = new GameObject(name);
			scene.AddObject(obj);
		}
		obj->GetTransform().SetPosition(glm::vec3(0.0f));
		obj->GetTransform().SetRotation(glm::vec3(0.0f));
		obj->GetTransform().SetScale(glm::vec3(1.0f));

		if (!meshData.vertices.empty()) {
			obj->SetMesh(meshData.ToMesh());
			obj->SetCPUMeshData(meshData);
		}

		// Set up diffuse texture via TextureLayer system
		if (!texPath.empty()) {
			// Clear old layers
			while (obj->GetTextureLayers().size() > 0)
				obj->RemoveTextureLayer(0);

			TextureLayer layer;
			layer.texturePath = texPath;
			layer.texture = new Texture(texPath.c_str());
			layer.texture->LoadTexture();
			layer.blendMode = LayerBlendMode::Normal;
			layer.opacity = 1.0f;
			layer.tiling = tiling;
			obj->AddTextureLayer(layer);
		}
	};

	// Spawn roads, sidewalks and plots as separate GameObjects with their own textures
	syncCityObject("City_Roads_" + idStr, roadMesh, "Assets/Textures/roads/asphalt.jpg", 4.0f);
	syncCityObject("City_Sidewalks_" + idStr, sidewalkMesh, "Assets/Textures/buildings/concrete.jpg", 2.0f);
	syncCityObject("City_Plots_" + idStr, plotMesh, "Assets/Textures/terrain/grass/albedo.jpg", 2.0f);

	if (progress) progress(60.0f, "Generating street lamps...");

	// ---- Phase 3: Street Decoration ----

	// 5a. Street Lamps — placed along road segments at regular intervals
	MeshData lampMesh;
	float lampSpacing = 12.0f;  // One lamp every N world units
	float poleRadius = 0.08f;
	float poleHeight = 4.0f;
	float lampHeadW = 0.4f;
	float lampHeadH = 0.1f;
	float lampHeadD = 0.15f;
	float halfRoad = roadWidth * 0.5f;

	auto addBox = [&](MeshData& m, glm::vec3 center, glm::vec3 half, glm::vec3 n, glm::vec3 t, glm::vec3 b) {
		// Simple 6-face box (all same normal for simplicity — lit as a solid block)
		struct F { glm::vec3 n; glm::vec3 c[4]; };
		float w = half.x, h = half.y, d = half.z;
		F faces[6] = {
			// Front (-Z)
			{ {0,0,-1}, { {center.x-w,center.y-h,center.z-d}, {center.x-w,center.y+h,center.z-d}, {center.x+w,center.y+h,center.z-d}, {center.x+w,center.y-h,center.z-d} } },
			// Back (+Z)
			{ {0,0,1}, { {center.x+w,center.y-h,center.z+d}, {center.x+w,center.y+h,center.z+d}, {center.x-w,center.y+h,center.z+d}, {center.x-w,center.y-h,center.z+d} } },
			// Left (-X)
			{ {-1,0,0}, { {center.x-w,center.y-h,center.z+d}, {center.x-w,center.y+h,center.z+d}, {center.x-w,center.y+h,center.z-d}, {center.x-w,center.y-h,center.z-d} } },
			// Right (+X)
			{ {1,0,0}, { {center.x+w,center.y-h,center.z-d}, {center.x+w,center.y+h,center.z-d}, {center.x+w,center.y+h,center.z+d}, {center.x+w,center.y-h,center.z+d} } },
			// Top (+Y)
			{ {0,1,0}, { {center.x-w,center.y+h,center.z-d}, {center.x-w,center.y+h,center.z+d}, {center.x+w,center.y+h,center.z+d}, {center.x+w,center.y+h,center.z-d} } },
			// Bottom (-Y)
			{ {0,-1,0}, { {center.x-w,center.y-h,center.z+d}, {center.x-w,center.y-h,center.z-d}, {center.x+w,center.y-h,center.z-d}, {center.x+w,center.y-h,center.z+d} } },
		};
		for (int f = 0; f < 6; f++) {
			unsigned int base = m.GetVertexCount();
			for (int v = 0; v < 4; v++)
				m.AddVertex(faces[f].c[v].x, faces[f].c[v].y, faces[f].c[v].z, 0, 0,
					faces[f].n.x, faces[f].n.y, faces[f].n.z, 1, 0, 0, 0, 0, 1);
			m.AddTriangle(base, base+1, base+2);
			m.AddTriangle(base, base+2, base+3);
		}
	};

	for (const auto& seg : roadSegs)
	{
		float len = seg.dimX ? (seg.end.x - seg.start.x) : (seg.end.z - seg.start.z);
		int numLamps = std::max(1, (int)(len / lampSpacing));

		for (int li = 0; li <= numLamps; li++)
		{
			float t = (float)li / (float)numLamps;
			glm::vec3 roadPt = seg.start + (seg.end - seg.start) * t;
			roadPt.y += terrainOffset;

			// Place lamps on both sides of the road
			for (int side = -1; side <= 1; side += 2)
			{
				glm::vec3 lampPos = roadPt;
				if (seg.dimX)
					lampPos.z += (halfRoad + sidewalkWidth * 0.5f) * (float)side;
				else
					lampPos.x += (halfRoad + sidewalkWidth * 0.5f) * (float)side;

				// Pole
				glm::vec3 poleCenter(lampPos.x, lampPos.y + poleHeight * 0.5f, lampPos.z);
				addBox(lampMesh, poleCenter, glm::vec3(poleRadius, poleHeight * 0.5f, poleRadius),
					glm::vec3(0,1,0), glm::vec3(1,0,0), glm::vec3(0,0,1));

				// Lamp head — offset it so it hangs OVER the road instead of being centered on the pole
				glm::vec3 headCenter(lampPos.x, lampPos.y + poleHeight + lampHeadH * 0.5f, lampPos.z);
				glm::vec3 headHalfExtents(lampHeadW, lampHeadH, lampHeadD);
				
				if (seg.dimX) {
					// Road is along X, side is along Z. Head should be long in Z and point towards road (Z=0)
					headHalfExtents = glm::vec3(lampHeadH, lampHeadH, lampHeadW); // Swap W/D for orientation
					headCenter.z -= (headHalfExtents.z) * (float)side; 
				} else {
					// Road is along Z, side is along X. Head should be long in X and point towards road (X=0)
					headCenter.x -= (headHalfExtents.x) * (float)side;
				}

				addBox(lampMesh, headCenter, headHalfExtents,
					glm::vec3(0,1,0), glm::vec3(1,0,0), glm::vec3(0,0,1));
			}
		}
	}

	syncCityObject("City_Lamps_" + idStr, lampMesh, "Assets/Textures/buildings/metal_building.jpg", 1.0f);

	if (progress) progress(75.0f, "Planting trees...");

	// 5b. Trees on park plots — simple procedural trunk + canopy
	MeshData treeMesh;
	float trunkRadius = 0.12f;
	float trunkHeight = 2.5f;
	float canopyRadius = 1.5f;
	float canopyHeight = 2.0f;
	std::mt19937 treeRng(seed + 777);
	std::uniform_real_distribution<float> treePosOff(-0.3f, 0.3f);

	for (const auto& plot : plots)
	{
		if (!plot.isPark) continue;

		// Place a few trees per park
		int numTrees = 2 + (treeRng() % 4);
		float parkW = plot.size.x * 0.4f;
		float parkD = plot.size.y * 0.4f;

		for (int ti = 0; ti < numTrees; ti++)
		{
			float ox = (treeRng() / (float)treeRng.max() - 0.5f) * 2.0f * parkW;
			float oz = (treeRng() / (float)treeRng.max() - 0.5f) * 2.0f * parkD;
			glm::vec3 treeBase(plot.center.x + ox, plot.center.y + terrainOffset, plot.center.z + oz);

			// Trunk
			glm::vec3 trunkCenter(treeBase.x, treeBase.y + trunkHeight * 0.5f, treeBase.z);
			addBox(treeMesh, trunkCenter, glm::vec3(trunkRadius, trunkHeight * 0.5f, trunkRadius),
				glm::vec3(0,1,0), glm::vec3(1,0,0), glm::vec3(0,0,1));

			// Canopy (larger box)
			glm::vec3 canopyCenter(treeBase.x, treeBase.y + trunkHeight + canopyHeight * 0.5f, treeBase.z);
			addBox(treeMesh, canopyCenter, glm::vec3(canopyRadius, canopyHeight * 0.5f, canopyRadius),
				glm::vec3(0,1,0), glm::vec3(1,0,0), glm::vec3(0,0,1));
		}
	}

	syncCityObject("City_Trees_" + idStr, treeMesh, "Assets/Textures/roads/grass_park.jpg", 1.0f);

	if (progress) progress(80.0f, "Building plot data...");

	// 6. Build plot transform list
	TransformList plotTransforms;
	BuildPlotTransforms(plotTransforms);

	// 7. Pass-through terrain mesh on output
	if (!inputs[0].data.meshData.vertices.empty()) {
		outputs[0].data = inputs[0].data;
	} else {
		outputs[0].data.type = PinDataType::Mesh;
		outputs[0].data.meshData = roadMesh;
	}

	outputs[1].data.type = PinDataType::TransformList;
	outputs[1].data.transforms = std::move(plotTransforms);

	if (progress) progress(100.0f, "City grid complete!");

	printf("[CityGridNode] Spawned roads + plots + %d lamps + trees. %d building plots available.\n",
		(int)(lampMesh.GetVertexCount() / 48), (int)outputs[1].data.transforms.size());
}
