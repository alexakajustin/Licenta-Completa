#include "RiverNode.h"
#include "imgui.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "Mesh.h"
#include "Material.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <thread>
#include <glm/gtc/matrix_transform.hpp>

RiverNode::RiverNode(NodeGraph& graph)
{
	id = graph.NextNodeId();
	title = "River";

	Pin meshIn(graph.NextPinId(), PinDataType::Mesh, "Mesh");
	inputs.push_back(meshIn);

	Pin meshOut(graph.NextPinId(), PinDataType::Mesh, "Mesh");
	outputs.push_back(meshOut);

	springCount = 8;
	maxSteps = 800;
	baseDepth = 5.0f;
	baseWidth = 15.0f;
	waterOffset = 3.5f;
	smoothPasses = 8;
}

json RiverNode::Serialize() const
{
	json j = GraphNode::Serialize();
	j["springCount"] = springCount;
	j["maxSteps"] = maxSteps;
	j["baseDepth"] = baseDepth;
	j["baseWidth"] = baseWidth;
	j["waterOffset"] = waterOffset;
	j["smoothPasses"] = smoothPasses;
	return j;
}

void RiverNode::Deserialize(const json& j)
{
	GraphNode::Deserialize(j);
	springCount = j.value("springCount", 5);
	maxSteps = j.value("maxSteps", 500);
	baseDepth = j.value("baseDepth", 4.0f);
	baseWidth = j.value("baseWidth", 15.0f);
	waterOffset = j.value("waterOffset", 3.5f);
	smoothPasses = j.value("smoothPasses", 8);
}

void RiverNode::RenderContent(SceneManager* scene)
{
	ImGui::DragInt("Springs", &springCount, 1, 1, 50);
	ImGui::DragInt("Max Length", &maxSteps, 10, 10, 2000);
	ImGui::DragFloat("Base Depth", &baseDepth, 0.1f, 0.0f, 50.0f);
	ImGui::DragFloat("Base Width", &baseWidth, 0.1f, 0.1f, 100.0f);
	ImGui::DragFloat("Water Offset", &waterOffset, 0.1f, -10.0f, 10.0f);
	ImGui::DragInt("Smooth Passes", &smoothPasses, 1, 0, 30);
}

void RiverNode::Execute(SceneManager& scene, NodeProgressCallback progress)
{
	outputs[0].data.Clear();
	if (inputs[0].data.type != PinDataType::Mesh) return;

	MeshData data = inputs[0].data.meshData;
	if (data.vertices.empty()) return;

	int totalVerts = (int)data.vertices.size() / 14;
	int gridRes = (int)std::sqrt(totalVerts);

	if (gridRes * gridRes != totalVerts)
	{
		outputs[0].data = inputs[0].data;
		return;
	}

	glm::vec3 terrainScale(1.0f);
	GameObject* terrainObj = scene.FindObject(inputs[0].data.sourceObjectName);
	if (terrainObj) terrainScale = terrainObj->GetTransform().GetScale();

	// 1. Identify "Springs"
	struct Spring { int x, z; float height; };
	std::vector<Spring> allVerts;
	for (int z = 0; z < gridRes; z++)
	{
		for (int x = 0; x < gridRes; x++)
		{
			allVerts.push_back({ x, z, data.vertices[(z * gridRes + x) * 14 + 1] });
		}
	}

	std::sort(allVerts.begin(), allVerts.end(), [](const Spring& a, const Spring& b) {
		return a.height > b.height;
	});

	std::vector<Spring> springs;
	float minDist = (float)gridRes / 10.0f;
	for (const auto& s : allVerts)
	{
		bool farEnough = true;
		for (const auto& existing : springs)
		{
			float dx = (float)(s.x - existing.x);
			float dz = (float)(s.z - existing.z);
			if (std::sqrt(dx * dx + dz * dz) < minDist) { farEnough = false; break; }
		}
		if (farEnough) {
			springs.push_back(s);
			if ((int)springs.size() >= springCount) break;
		}
	}

	if (progress) progress(10.0f, "Seeding Springs...");

	// 2. Trace Paths
	std::vector<int> flowVolume(totalVerts, 0);
	struct Sink { int x, z; float volume; };
	std::vector<Sink> sinks;
	struct PathStep { int x, z; };
	std::vector<std::vector<PathStep>> riverPaths;

	for (const auto& spring : springs)
	{
		int currX = spring.x;
		int currZ = spring.z;
		std::vector<PathStep> path;

		bool trapped = false;
		for (int step = 0; step < maxSteps; step++)
		{
			flowVolume[currZ * gridRes + currX]++;
			path.push_back({ currX, currZ });

			int bestX = currX, bestZ = currZ;
			float lowestH = data.vertices[(currZ * gridRes + currX) * 14 + 1];
			bool foundLower = false;

			for (int dz = -1; dz <= 1; dz++)
			{
				for (int dx = -1; dx <= 1; dx++)
				{
					if (dx == 0 && dz == 0) continue;
					int nx = currX + dx;
					int nz = currZ + dz;
					if (nx >= 0 && nx < gridRes && nz >= 0 && nz < gridRes)
					{
						float nh = data.vertices[(nz * gridRes + nx) * 14 + 1];
						if (nh < lowestH) { lowestH = nh; bestX = nx; bestZ = nz; foundLower = true; }
					}
				}
			}

			if (!foundLower) { sinks.push_back({ currX, currZ, (float)flowVolume[currZ * gridRes + currX] }); trapped = true; break; }
			currX = bestX;
			currZ = bestZ;
		}
		if (!trapped) sinks.push_back({ currX, currZ, (float)flowVolume[currZ * gridRes + currX] });
		riverPaths.push_back(path);
	}

	if (progress) progress(30.0f, "Calculating Flow...");

	// 3. Pre-calculate Lake Zones to prevent double-carving
	struct LakeZone { int sx, sz; float radius; };
	std::vector<LakeZone> lakeZones;
	for (const auto& sink : sinks) {
		lakeZones.push_back({ sink.x, sink.z, 20.0f * (float)std::sqrt(sink.volume) });
	}

	// 4. Carve Rivers
	for (int i = 0; i < totalVerts; i++)
	{
		if (flowVolume[i] > 0)
		{
			int cz = i / gridRes;
			int cx = i % gridRes;
			
			// Skip river carving if we are deep inside a lake zone
			bool inLake = false;
			float gridUnitSize = terrainScale.x / gridRes;
			for (const auto& zone : lakeZones) {
				float dx = (float)(cx - zone.sx);
				float dz = (float)(cz - zone.sz);
				if (std::sqrt(dx * dx + dz * dz) * gridUnitSize < zone.radius * 0.4f) {
					inLake = true; break;
				}
			}
			if (inLake) continue;

			float volume = (float)flowVolume[i];
			float currentDepth = baseDepth * std::pow(volume, 0.35f);
			float currentWidth = 8.0f * std::pow(volume, 0.35f);

			int gridRadius = (int)std::ceil(currentWidth / gridUnitSize);

			for (int rz = -gridRadius; rz <= gridRadius; rz++)
			{
				for (int rx = -gridRadius; rx <= gridRadius; rx++)
				{
					int nx = cx + rx;
					int nz = cz + rz;
					if (nx >= 0 && nx < gridRes && nz >= 0 && nz < gridRes)
					{
						float dist = std::sqrt((float)(rx * rx + rz * rz));
						float worldDist = dist * gridUnitSize;

						if (worldDist <= currentWidth)
						{
							float falloff = 1.0f - (worldDist / currentWidth);
							data.vertices[(nz * gridRes + nx) * 14 + 1] -= currentDepth * falloff;
						}
					}
				}
			}
		}
	}

	// 5. Carve Lake Basins (Automated Organic Shapes)
	auto getLakeJitter = [&](float angle, int sx, int sz) {
		return std::sin(angle * 4.0f + (float)sx) * 0.4f + std::sin(angle * 9.0f - (float)sz) * 0.15f;
	};

	std::vector<bool> lakeMask(totalVerts, false);

	for (const auto& zone : lakeZones)
	{
		float baseRadius = zone.radius;
		float targetDepth = baseDepth * 2.0f; 
		float targetH = data.vertices[(zone.sz * gridRes + zone.sx) * 14 + 1] - targetDepth;
		
		float gridUnitSize = terrainScale.x / gridRes;
		float lakeNoise = 0.25f; 
		int gridSearchRadius = (int)std::ceil((baseRadius * (1.0f + lakeNoise) * 2.0f) / gridUnitSize) + 4;

		for (int rz = -gridSearchRadius; rz <= gridSearchRadius; rz++)
		{
			for (int rx = -gridSearchRadius; rx <= gridSearchRadius; rx++)
			{
				int nx = zone.sx + rx;
				int nz = zone.sz + rz;
				if (nx >= 0 && nx < gridRes && nz >= 0 && nz < gridRes)
				{
					float dx = (float)rx;
					float dz = (float)rz;
					float dist = std::sqrt(dx * dx + dz * dz) * gridUnitSize;
					float angle = std::atan2(dz, dx);
					
					float noise = getLakeJitter(angle, zone.sx, zone.sz);
					float jitteredRadius = baseRadius * (1.0f + noise * lakeNoise);
					float totalRadius = jitteredRadius * 1.5f; 

					if (dist <= totalRadius)
					{
						float t = dist / totalRadius;
						float bowlT = std::pow(t, 2.5f); 
						
						float currentH = data.vertices[(nz * gridRes + nx) * 14 + 1];
						float lakeH = targetH + bowlT * (currentH - targetH);
						
						if (lakeH < currentH) {
							data.vertices[(nz * gridRes + nx) * 14 + 1] = lakeH;
							lakeMask[nz * gridRes + nx] = true;
						}
					}
				}
			}
		}
	}

	// 5. Smoothing
	if (smoothPasses > 0)
	{
		std::vector<float> heightBuffer(totalVerts);
		for (int p = 0; p < smoothPasses; p++)
		{
			for (int i = 0; i < totalVerts; i++) heightBuffer[i] = data.vertices[i * 14 + 1];
			for (int z = 1; z < gridRes - 1; z++)
			{
				for (int x = 1; x < gridRes - 1; x++)
				{
					int idx = z * gridRes + x;
					if (flowVolume[idx] > 0 || lakeMask[idx])
					{
						float avg = (heightBuffer[idx] + heightBuffer[idx - 1] + heightBuffer[idx + 1] +
							heightBuffer[idx - gridRes] + heightBuffer[idx + gridRes]) / 5.0f;
						data.vertices[idx * 14 + 1] = avg;
					}
				}
			}
		}
	}
	RecomputeNormals(data, gridRes);

	if (progress) progress(80.0f, "Generating Water Meshes...");

	// 6. Generate Water Mesh
	MeshData waterMesh;
	glm::vec3 up(0, 1, 0);
	float waterMeshWidthMultiplier = 1.25f; // Bigger for clear visibility

	for (auto& path : riverPaths)
	{
		if (path.size() < 2) continue;

		// Smooth the river path for more natural curves (Laplacian smoothing)
		std::vector<glm::vec2> smoothPath;
		for (auto& p : path) smoothPath.push_back(glm::vec2((float)p.x, (float)p.z));

		for (int s = 0; s < 5; s++) { // 5 smoothing passes
			for (size_t i = 1; i < smoothPath.size() - 1; i++) {
				smoothPath[i] = (smoothPath[i - 1] + smoothPath[i] + smoothPath[i + 1]) / 3.0f;
			}
		}

		int baseIdx = waterMesh.GetVertexCount();
		for (size_t i = 0; i < smoothPath.size(); i++)
		{
			float fx = smoothPath[i].x;
			float fz = smoothPath[i].y;
			
			// Bilinear interpolation for height
			int x0 = (int)std::floor(fx); int x1 = std::min(x0 + 1, gridRes - 1);
			int z0 = (int)std::floor(fz); int z1 = std::min(z0 + 1, gridRes - 1);
			float tx = fx - x0; float tz = fz - z0;
			
			auto getH = [&](int x, int z) { return data.vertices[(z * gridRes + x) * 14 + 1]; };
			float h00 = getH(x0, z0); float h10 = getH(x1, z0);
			float h01 = getH(x0, z1); float h11 = getH(x1, z1);
			float lerpH = glm::mix(glm::mix(h00, h10, tx), glm::mix(h01, h11, tx), tz);

			float x0_pos = data.vertices[(z0 * gridRes + x0) * 14];
			float x1_pos = data.vertices[(z0 * gridRes + x1) * 14];
			float z0_pos = data.vertices[(z0 * gridRes + x0) * 14 + 2];
			float z1_pos = data.vertices[(z1 * gridRes + x0) * 14 + 2];

			glm::vec3 pos(glm::mix(x0_pos, x1_pos, tx), 
						  lerpH, 
						  glm::mix(z0_pos, z1_pos, tz));


			glm::vec3 dir;
			if (i == 0) dir = glm::normalize(glm::vec3(smoothPath[i + 1].x - smoothPath[i].x, 0, smoothPath[i + 1].y - smoothPath[i].y));
			else if (i == smoothPath.size() - 1) dir = glm::normalize(glm::vec3(smoothPath[i].x - smoothPath[i - 1].x, 0, smoothPath[i].y - smoothPath[i - 1].y));
			else dir = glm::normalize(glm::vec3(smoothPath[i + 1].x - smoothPath[i - 1].x, 0, smoothPath[i + 1].y - smoothPath[i - 1].y));

			glm::vec3 right = glm::normalize(glm::cross(dir, up));
			
			// Use original path volume to ensure consistent width during smoothing
			float pathVolume = (float)flowVolume[path[i].z * gridRes + path[i].x];
			float worldWidth = 8.0f * std::pow(pathVolume, 0.35f) * waterMeshWidthMultiplier;
			float localWidth = worldWidth / terrainScale.x;

			glm::vec3 pL = pos - right * localWidth + up * (waterOffset / terrainScale.y);
			glm::vec3 pR = pos + right * localWidth + up * (waterOffset / terrainScale.y);

			// TBN Generation: Tangent = Right, Bitangent = Forward (dir), Normal = Up
			waterMesh.AddVertex(pL.x, pL.y, pL.z, 0, (float)i * 0.1f, up.x, up.y, up.z, right.x, right.y, right.z, dir.x, dir.y, dir.z);
			waterMesh.AddVertex(pR.x, pR.y, pR.z, 1, (float)i * 0.1f, up.x, up.y, up.z, right.x, right.y, right.z, dir.x, dir.y, dir.z);

			if (i > 0)
			{
				int currL = baseIdx + (int)i * 2; int currR = baseIdx + (int)i * 2 + 1;
				int prevL = baseIdx + (int)(i - 1) * 2; int prevR = baseIdx + (int)(i - 1) * 2 + 1;
				waterMesh.AddTriangle(prevL, currR, currL);
				waterMesh.AddTriangle(prevL, prevR, currR);
			}
		}
	}

	for (const auto& sink : sinks)
	{
		float baseRadius = 20.0f * std::sqrt(sink.volume);
		float localRadius = baseRadius / terrainScale.x;
		glm::vec3 center(data.vertices[(sink.z * gridRes + sink.x) * 14], 
						 data.vertices[(sink.z * gridRes + sink.x) * 14 + 1] + (waterOffset / terrainScale.y),
						 data.vertices[(sink.z * gridRes + sink.x) * 14 + 2]);

		int rings = 12; 
		int segments = 64; 
		int startIdx = waterMesh.GetVertexCount();
		float lakeNoise = 0.25f;
		
		waterMesh.AddVertex(center.x, center.y, center.z, 0.5f, 0.5f, 0, 1, 0, 1, 0, 0, 0, 0, 1);

		for (int r = 1; r <= rings; r++)
		{
			float rFactor = (float)r / (float)rings;
			for (int s = 0; s < segments; s++)
			{
				float ang = (float)s / (float)segments * 2.0f * 3.14159f;
				float noise = getLakeJitter(ang, sink.x, sink.z);
				
				float meshRadius = localRadius * (1.0f + noise * lakeNoise) * 1.05f; 
				float currentRadius = meshRadius * rFactor;

				glm::vec3 p = center + glm::vec3(std::cos(ang), 0, std::sin(ang)) * currentRadius;
				float u = (std::cos(ang) * rFactor + 1.0f) * 0.5f;
				float v = (std::sin(ang) * rFactor + 1.0f) * 0.5f;
				waterMesh.AddVertex(p.x, p.y, p.z, u, v, 0, 1, 0, 1, 0, 0, 0, 0, 1);
			}
		}

		// 3. Add triangles for the inner circle (center to first ring)
		for (int s = 0; s < segments; s++)
		{
			int nextS = (s + 1) % segments;
			waterMesh.AddTriangle(startIdx, startIdx + 1 + nextS, startIdx + 1 + s);
		}

		// 4. Add triangles for subsequent rings
		for (int r = 1; r < rings; r++)
		{
			int innerRingStart = startIdx + 1 + (r - 1) * segments;
			int outerRingStart = startIdx + 1 + r * segments;
			for (int s = 0; s < segments; s++)
			{
				int nextS = (s + 1) % segments;
				int iL = innerRingStart + s;
				int iR = innerRingStart + nextS;
				int oL = outerRingStart + s;
				int oR = outerRingStart + nextS;
				waterMesh.AddTriangle(iL, oR, oL);
				waterMesh.AddTriangle(iL, iR, oR);
			}
		}
	}

	// 7. Sync Transform & Material
	std::string waterName = "River_Water_" + std::to_string(id);
	GameObject* waterObj = scene.FindObject(waterName);
	if (!waterObj) { waterObj = new GameObject(waterName); scene.AddObject(waterObj); }
	if (terrainObj)
	{
		waterObj->GetTransform().SetPosition(terrainObj->GetTransform().GetPosition());
		waterObj->GetTransform().SetRotation(terrainObj->GetTransform().GetRotation());
		waterObj->GetTransform().SetScale(terrainObj->GetTransform().GetScale());
	}

	if (!waterObj->GetMaterial())
	{
		Material* waterMat = Material::LoadFromFile("Assets/Materials/Water.mat");
		if (waterMat) waterObj->SetMaterial(waterMat);
	}

	if (!waterMesh.vertices.empty())
	{
		Mesh* m = waterMesh.ToMesh();
		waterObj->SetMesh(m);
		waterObj->SetCPUMeshData(waterMesh);
	}

	if (progress) progress(100.0f, "Done!");
	outputs[0].data = inputs[0].data;
	outputs[0].data.meshData = data;
}

void RiverNode::RecomputeNormals(MeshData& data, int gridRes)
{
	const int stride = 14;
	for (int i = 0; i < gridRes * gridRes; i++) { data.vertices[i * stride + 5] = 0; data.vertices[i * stride + 6] = 0; data.vertices[i * stride + 7] = 0; }
	for (size_t i = 0; i + 2 < data.indices.size(); i += 3)
	{
		int i0 = data.indices[i], i1 = data.indices[i + 1], i2 = data.indices[i + 2];
		glm::vec3 v0(data.vertices[i0 * 14], data.vertices[i0 * 14 + 1], data.vertices[i0 * 14 + 2]);
		glm::vec3 v1(data.vertices[i1 * 14], data.vertices[i1 * 14 + 1], data.vertices[i1 * 14 + 2]);
		glm::vec3 v2(data.vertices[i2 * 14], data.vertices[i2 * 14 + 1], data.vertices[i2 * 14 + 2]);
		glm::vec3 n = glm::cross(v1 - v0, v2 - v0);
		for (int j = 0; j < 3; j++) { data.vertices[i0 * 14 + 5 + j] += n[j]; data.vertices[i1 * 14 + 5 + j] += n[j]; data.vertices[i2 * 14 + 5 + j] += n[j]; }
	}
	for (int i = 0; i < gridRes * gridRes; i++)
	{
		glm::vec3 n(data.vertices[i * 14 + 5], data.vertices[i * 14 + 6], data.vertices[i * 14 + 7]);
		if (glm::length(n) > 0.0001f) n = glm::normalize(n); else n = glm::vec3(0, 1, 0);
		data.vertices[i * 14 + 5] = n.x; data.vertices[i * 14 + 6] = n.y; data.vertices[i * 14 + 7] = n.z;
	}
}
