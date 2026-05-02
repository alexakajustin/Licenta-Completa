#include "RiverNode.h"
#include "imgui.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "Mesh.h"
#include "Material.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
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
	baseDepth = 0.08f;
	baseWidth = 15.0f;
	waterOffset = -0.005f;
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
	baseDepth = j.value("baseDepth", 0.08f);
	baseWidth = j.value("baseWidth", 15.0f);
	waterOffset = j.value("waterOffset", -0.005f);
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
	std::vector<bool> isSink(totalVerts, false);
	std::vector<int> pathID(totalVerts, -1);
	int currentPathID = 0;

	struct PathStep { int x, z; };
	std::vector<std::vector<PathStep>> riverPaths;

	for (const auto& spring : springs)
	{
		int currX = spring.x;
		int currZ = spring.z;
		std::vector<PathStep> path;

		bool trapped = false;
		bool merged = false;

		for (int step = 0; step < maxSteps; step++)
		{
			int idx = currZ * gridRes + currX;
			flowVolume[idx]++;
			
			if (pathID[idx] == -1 || pathID[idx] == currentPathID) {
				pathID[idx] = currentPathID;
				if (!merged) path.push_back({ currX, currZ });
			} else {
				if (!merged) {
					path.push_back({ currX, currZ }); // Connect to the main river
					merged = true;
				}
			}

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

			if (!foundLower) { 
			// Puddle Jump: search up to radius 5 for a lower point to spill over
			int jumpX = -1, jumpZ = -1;
			for (int r = 2; r <= 5 && !foundLower; r++) {
				for (int dz = -r; dz <= r; dz++) {
					for (int dx = -r; dx <= r; dx++) {
						if (std::abs(dx) != r && std::abs(dz) != r) continue;
						int nx = currX + dx;
						int nz = currZ + dz;
						if (nx >= 0 && nx < gridRes && nz >= 0 && nz < gridRes) {
							float nh = data.vertices[(nz * gridRes + nx) * 14 + 1];
							if (nh < lowestH) { 
								lowestH = nh; 
								jumpX = nx; jumpZ = nz; 
								foundLower = true; 
							}
						}
					}
				}
			}
			// Interpolate ALL intermediate cells to keep the path continuous
			if (foundLower) {
				int stepCount = std::max(std::abs(jumpX - currX), std::abs(jumpZ - currZ));
				for (int s = 1; s < stepCount; s++) {
					int mx = currX + (jumpX - currX) * s / stepCount;
					int mz = currZ + (jumpZ - currZ) * s / stepCount;
					mx = std::max(0, std::min(mx, gridRes - 1));
					mz = std::max(0, std::min(mz, gridRes - 1));
					path.push_back({ mx, mz });
				}
				bestX = jumpX; bestZ = jumpZ;
			}
		}

			if (!foundLower) { 
				isSink[currZ * gridRes + currX] = true; 
				trapped = true; 
				break; 
			}
			currX = bestX;
			currZ = bestZ;
		}
		
		if (!trapped && !merged) {
			isSink[currZ * gridRes + currX] = true;
		}
		riverPaths.push_back(path);
		currentPathID++;
	}

	struct Sink { int x, z; float volume; };
	std::vector<Sink> sinks;
	for (int i = 0; i < totalVerts; i++) {
		if (isSink[i]) {
			sinks.push_back({ i % gridRes, i / gridRes, (float)flowVolume[i] });
		}
	}

	if (progress) progress(30.0f, "Calculating Flow...");

	// Save original terrain heights BEFORE any carving
	// Water surfaces will sample from this so they sit at bank level
	std::vector<float> originalHeights(totalVerts);
	for (int i = 0; i < totalVerts; i++) {
		originalHeights[i] = data.vertices[i * 14 + 1];
	}

	// 3. Hydrological Lake Filling (Flood-Fill)
	std::vector<float> lakeWaterLevel(totalVerts, -1.0f);
	std::vector<bool> lakeMask(totalVerts, false);
	std::vector<bool> sinkHandled(totalVerts, false);

	// Sort sinks by height to fill from the bottom up
	std::sort(sinks.begin(), sinks.end(), [&](const Sink& a, const Sink& b) {
		return data.vertices[(a.z * gridRes + a.x) * 14 + 1] < data.vertices[(b.z * gridRes + b.x) * 14 + 1];
	});

	for (const auto& sink : sinks)
	{
		int sinkIdx = sink.z * gridRes + sink.x;
		if (sinkHandled[sinkIdx]) continue;

		float targetVolume = sink.volume * baseWidth * 1.5f; 
		float currentVolume = 0.0f;
		float currentWaterLevel = data.vertices[sinkIdx * 14 + 1];

		std::vector<int> lakePixels;
		std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>, std::greater<std::pair<float, int>>> pq;
		
		pq.push({currentWaterLevel, sinkIdx});
		std::vector<bool> visited(totalVerts, false);
		visited[sinkIdx] = true;
		sinkHandled[sinkIdx] = true;

		while (!pq.empty() && currentVolume < targetVolume)
		{
			auto [h, idx] = pq.top();
			pq.pop();

			// If we hit another sink during the fill, merge its volume into this basin
			if (isSink[idx] && !sinkHandled[idx]) {
				targetVolume += flowVolume[idx] * baseWidth * 1.5f;
				sinkHandled[idx] = true;
			}

			if (h > currentWaterLevel) {
				float diff = h - currentWaterLevel;
				currentVolume += diff * lakePixels.size();
				currentWaterLevel = h;
			}

			lakePixels.push_back(idx);
			lakeMask[idx] = true;

			for (int dz = -1; dz <= 1; dz++) {
				for (int dx = -1; dx <= 1; dx++) {
					if (dx == 0 && dz == 0) continue;
					int nx = (idx % gridRes) + dx;
					int nz = (idx / gridRes) + dz;
					if (nx >= 0 && nx < gridRes && nz >= 0 && nz < gridRes) {
						int nidx = nz * gridRes + nx;
						if (!visited[nidx]) {
							visited[nidx] = true;
							pq.push({data.vertices[nidx * 14 + 1], nidx});
						}
					}
				}
			}
		}

		if (lakePixels.size() < 5) {
			for (int idx : lakePixels) lakeMask[idx] = false;
			continue;
		}

		// Store water level for this basin and flatten bed
		for (int idx : lakePixels) {
			lakeWaterLevel[idx] = currentWaterLevel;
			float terrainH = data.vertices[idx * 14 + 1];
			float bedDepth = currentWaterLevel - baseDepth;
			if (terrainH > bedDepth) {
				data.vertices[idx * 14 + 1] = bedDepth;
			}
		}
	}

	// 4. Natural River Carving (Cascading)
	for (int i = 0; i < totalVerts; i++)
	{
		if (flowVolume[i] > 0 && !lakeMask[i])
		{
			int cz = i / gridRes;
			int cx = i % gridRes;
			
			float volume = (float)flowVolume[i];
			float currentDepth = baseDepth * std::pow(volume, 0.35f);
			float currentWidth = baseWidth * std::pow(volume, 0.35f);

			int gridRadius = (int)std::ceil(currentWidth / (terrainScale.x / gridRes));

			for (int rz = -gridRadius; rz <= gridRadius; rz++)
			{
				for (int rx = -gridRadius; rx <= gridRadius; rx++)
				{
					int nx = cx + rx;
					int nz = cz + rz;
					if (nx >= 0 && nx < gridRes && nz >= 0 && nz < gridRes && !lakeMask[nz * gridRes + nx])
					{
						float dist = std::sqrt((float)(rx * rx + rz * rz));
						float worldDist = dist * (terrainScale.x / gridRes);

						if (worldDist <= currentWidth)
						{
							float falloff = std::exp(-(worldDist * worldDist) / (currentWidth * currentWidth * 0.2f));
							data.vertices[(nz * gridRes + nx) * 14 + 1] -= currentDepth * falloff;
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

	// 6. Build Unified Water Height Map (rivers + lakes)
	// Every cell with water gets a height; the terrain below was already carved
	std::vector<float> waterHeight(totalVerts, -9999.0f);
	std::vector<bool> waterMask(totalVerts, false);

	// 6a. Lake water heights (already computed)
	for (int i = 0; i < totalVerts; i++) {
		if (lakeWaterLevel[i] > -1.0f) {
			waterHeight[i] = lakeWaterLevel[i];
			waterMask[i] = true;
		}
	}

	// 6b. River water heights: use original (pre-carve) terrain height
	// This makes water sit at bank level while carved terrain forms the riverbed below
	for (int i = 0; i < totalVerts; i++) {
		if (flowVolume[i] > 0 && !lakeMask[i]) {
			waterHeight[i] = originalHeights[i];
			waterMask[i] = true;
		}
	}

	// 6c. Expand water mask by 2 cells for smooth shoreline blending
	std::vector<bool> expandedWater(totalVerts, false);
	for (int i = 0; i < totalVerts; i++) expandedWater[i] = waterMask[i];

	for (int pass = 0; pass < 2; pass++) {
		std::vector<bool> nextExpand = expandedWater;
		for (int z = 0; z < gridRes; z++) {
			for (int x = 0; x < gridRes; x++) {
				int idx = z * gridRes + x;
				if (!expandedWater[idx]) continue;
				for (int dz = -1; dz <= 1; dz++) {
					for (int dx = -1; dx <= 1; dx++) {
						int nx = x + dx, nz = z + dz;
						if (nx >= 0 && nx < gridRes && nz >= 0 && nz < gridRes) {
							int nidx = nz * gridRes + nx;
							if (!nextExpand[nidx]) {
								nextExpand[nidx] = true;
								// Skirt vertex: find nearest water height
								float nearest = -9999.0f;
								for (int sz = -1; sz <= 1; sz++) {
									for (int sx = -1; sx <= 1; sx++) {
										int snx = nx + sx, snz = nz + sz;
										if (snx >= 0 && snx < gridRes && snz >= 0 && snz < gridRes) {
											int snidx = snz * gridRes + snx;
											if (waterMask[snidx] && waterHeight[snidx] > nearest) {
												nearest = waterHeight[snidx];
											}
										}
									}
								}
								waterHeight[nidx] = nearest;
							}
						}
					}
				}
			}
		}
		expandedWater = nextExpand;
	}

	// 6d. Smooth water heights along rivers for butter-smooth surface
	for (int pass = 0; pass < smoothPasses; pass++) {
		std::vector<float> smoothed = waterHeight;
		for (int z = 1; z < gridRes - 1; z++) {
			for (int x = 1; x < gridRes - 1; x++) {
				int idx = z * gridRes + x;
				if (!waterMask[idx]) continue;
				if (lakeMask[idx]) continue;

				// Only average with neighbors that have valid water
				float sum = waterHeight[idx] * 2.0f;
				float weight = 2.0f;
				int neighbors[] = { idx - 1, idx + 1, idx - gridRes, idx + gridRes };
				for (int ni : neighbors) {
					if (ni >= 0 && ni < totalVerts && waterHeight[ni] > -9000.0f) {
						sum += waterHeight[ni];
						weight += 1.0f;
					}
				}
				smoothed[idx] = sum / weight;
			}
		}
		waterHeight = smoothed;
	}

	// 6e. Generate unified grid-based water mesh
	MeshData waterMesh;
	std::map<int, int> terrainToWaterIdx;

	float yOffset = waterOffset / terrainScale.y;

	for (int i = 0; i < totalVerts; i++) {
		if (!expandedWater[i]) continue;
		if (waterHeight[i] < -9000.0f) continue;

		float x_pos = data.vertices[i * 14];
		float z_pos = data.vertices[i * 14 + 2];
		float y_pos = waterHeight[i] + yOffset;

		// TBN: water surface faces up
		waterMesh.AddVertex(x_pos, y_pos, z_pos, 0.5f, 0.5f, 0, 1, 0, 1, 0, 0, 0, 0, 1);
		terrainToWaterIdx[i] = waterMesh.GetVertexCount() - 1;
	}

	// Triangulate: connect adjacent water cells
	for (int z = 0; z < gridRes - 1; z++) {
		for (int x = 0; x < gridRes - 1; x++) {
			int tl = z * gridRes + x;
			int tr = tl + 1;
			int bl = (z + 1) * gridRes + x;
			int br = bl + 1;

			if (terrainToWaterIdx.count(tl) && terrainToWaterIdx.count(tr) &&
				terrainToWaterIdx.count(bl) && terrainToWaterIdx.count(br)) {
				waterMesh.AddTriangle(terrainToWaterIdx[tl], terrainToWaterIdx[bl], terrainToWaterIdx[tr]);
				waterMesh.AddTriangle(terrainToWaterIdx[tr], terrainToWaterIdx[bl], terrainToWaterIdx[br]);
			}
		}
	}

	// 7. Sync Transform & Material
	std::string waterName = "River_Water_" + std::to_string(id);
	
	// NUCLEAR CLEANUP: Explicitly find and delete ALL objects with this name to prevent "ghost" meshes
	// from previous failed runs or duplicate nodes from causing Z-fighting/overlapping water.
	while (scene.FindObject(waterName) != nullptr) {
		scene.RemoveObject(waterName);
	}

	GameObject* waterObj = new GameObject(waterName);
	scene.AddObject(waterObj);

	if (terrainObj)
	{
		waterObj->GetTransform().SetPosition(terrainObj->GetTransform().GetPosition());
		waterObj->GetTransform().SetRotation(terrainObj->GetTransform().GetRotation());
		waterObj->GetTransform().SetScale(terrainObj->GetTransform().GetScale());
	}

	Material* waterMat = Material::LoadFromFile("Assets/Materials/Water.mat");
	if (waterMat) waterObj->SetMaterial(waterMat);

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
