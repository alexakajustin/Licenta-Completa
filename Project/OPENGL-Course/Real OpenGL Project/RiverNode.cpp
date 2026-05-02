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
	baseDepth = 5.0f;
	baseWidth = 40.0f;
	waterOffset = 0.008f;
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
	waterOffset = j.value("waterOffset", 0.003f);
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
			float bedDepth = currentWaterLevel - waterOffset;
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

	// 6. Generate Water Mesh
	MeshData waterMesh;
	glm::vec3 up(0, 1, 0);
	float waterMeshWidthMultiplier = 1.25f;

	// Helpers for bilinear terrain sampling
	// Water Y is sampled from originalHeights (pre-carve) so it sits at bank level
	// X/Z positions come from data (unchanged by carving)
	auto clampGrid = [&](int v) { return std::max(0, std::min(v, gridRes - 1)); };
	auto getOriginalH = [&](int x, int z) -> float {
		return originalHeights[clampGrid(z) * gridRes + clampGrid(x)];
	};
	auto getTerrainPos = [&](float fx, float fz, float& outX, float& outY, float& outZ) {
		int x0 = clampGrid((int)std::floor(fx));
		int x1 = clampGrid(x0 + 1);
		int z0 = clampGrid((int)std::floor(fz));
		int z1 = clampGrid(z0 + 1);
		float tx = fx - (float)x0; float tz = fz - (float)z0;
		tx = glm::clamp(tx, 0.0f, 1.0f); tz = glm::clamp(tz, 0.0f, 1.0f);

		float px0 = data.vertices[(z0 * gridRes + x0) * 14];
		float px1 = data.vertices[(z0 * gridRes + x1) * 14];
		float pz0 = data.vertices[(z0 * gridRes + x0) * 14 + 2];
		float pz1 = data.vertices[(z1 * gridRes + x0) * 14 + 2];

		outX = glm::mix(px0, px1, tx);
		outZ = glm::mix(pz0, pz1, tz);
		// Sample Y from ORIGINAL (pre-carve) heights so water sits at bank level
		outY = glm::mix(
			glm::mix(getOriginalH(x0, z0), getOriginalH(x1, z0), tx),
			glm::mix(getOriginalH(x0, z1), getOriginalH(x1, z1), tx), tz);
	};

	// Catmull-Rom spline helper
	auto catmullRom = [](glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, float t) -> glm::vec2 {
		float t2 = t * t, t3 = t2 * t;
		return 0.5f * ((2.0f * p1) +
			(-p0 + p2) * t +
			(2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
			(-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
	};

	for (auto& path : riverPaths)
	{
		if (path.size() < 2) continue;

		// Build coarse path points with flow volume for each
		std::vector<glm::vec2> coarsePath;
		std::vector<float> coarseVolume;
		for (size_t pi = 0; pi < path.size(); pi++) {
			coarsePath.push_back(glm::vec2((float)path[pi].x, (float)path[pi].z));
			int idx = clampGrid(path[pi].z) * gridRes + clampGrid(path[pi].x);
			coarseVolume.push_back((float)std::max(1, flowVolume[idx]));
		}

		// Laplacian smoothing on coarse path (10 passes for extra smoothness)
		for (int s = 0; s < 10; s++) {
			for (size_t i = 1; i < coarsePath.size() - 1; i++) {
				coarsePath[i] = (coarsePath[i - 1] + coarsePath[i] + coarsePath[i + 1]) / 3.0f;
			}
		}

		// Find where the river first hits a lake (if at all) and record the lake water level
		int lakeHitIdx = -1;
		float lakeLevel = 0.0f;
		for (size_t i = 0; i < coarsePath.size(); i++) {
			int gx = clampGrid((int)coarsePath[i].x);
			int gz = clampGrid((int)coarsePath[i].y);
			if (lakeMask[gz * gridRes + gx]) {
				lakeHitIdx = (int)i;
				lakeLevel = lakeWaterLevel[gz * gridRes + gx];
				break;
			}
		}

		// If path hits a lake, extend a few extra points INTO the lake for seamless blending
		int extraLakeVerts = 6;
		if (lakeHitIdx >= 0 && lakeHitIdx < (int)coarsePath.size()) {
			// Keep path up to and including the lake hit point
			coarsePath.resize(lakeHitIdx + 1);
			coarseVolume.resize(lakeHitIdx + 1);
			
			// Extend the path into the lake along the last direction
			glm::vec2 lastDir(0, 0);
			if (coarsePath.size() >= 2) {
				lastDir = glm::normalize(coarsePath.back() - coarsePath[coarsePath.size() - 2]);
			}
			for (int e = 1; e <= extraLakeVerts; e++) {
				coarsePath.push_back(coarsePath[lakeHitIdx] + lastDir * (float)e);
				coarseVolume.push_back(coarseVolume.back());
			}
		}

		if (coarsePath.size() < 2) continue;

		// Catmull-Rom subdivision: 4 sub-steps per segment for butter-smooth curves
		const int subdivisions = 4;
		std::vector<glm::vec2> finePath;
		std::vector<float> fineVolume;

		for (size_t seg = 0; seg < coarsePath.size() - 1; seg++) {
			int i0 = (int)std::max((int)seg - 1, 0);
			int i1 = (int)seg;
			int i2 = (int)std::min(seg + 1, coarsePath.size() - 1);
			int i3 = (int)std::min(seg + 2, coarsePath.size() - 1);

			for (int sub = 0; sub < subdivisions; sub++) {
				float t = (float)sub / (float)subdivisions;
				glm::vec2 pt = catmullRom(coarsePath[i0], coarsePath[i1], coarsePath[i2], coarsePath[i3], t);
				float vol = glm::mix(coarseVolume[i1], coarseVolume[i2], t);
				finePath.push_back(pt);
				fineVolume.push_back(vol);
			}
		}
		// Add the last point
		finePath.push_back(coarsePath.back());
		fineVolume.push_back(coarseVolume.back());

		if (finePath.size() < 2) continue;

		// Build the ribbon mesh from the fine path
		int baseIdx = waterMesh.GetVertexCount();
		float lastH = 999999.0f;
		int emittedVerts = 0;

		for (size_t i = 0; i < finePath.size(); i++)
		{
			float fx = finePath[i].x;
			float fz = finePath[i].y;

			// Clamp to grid bounds
			fx = glm::clamp(fx, 0.0f, (float)(gridRes - 1));
			fz = glm::clamp(fz, 0.0f, (float)(gridRes - 1));

			// Sample terrain height via bilinear interpolation
			float posX, posY, posZ;
			getTerrainPos(fx, fz, posX, posY, posZ);

			// Check if this point is inside a lake
			int gx = clampGrid((int)fx);
			int gz = clampGrid((int)fz);
			bool inLake = lakeMask[gz * gridRes + gx];

			// If inside lake, blend height towards the lake water level
			if (inLake && lakeHitIdx >= 0) {
				posY = lakeLevel;
			}

			// Enforce monotonically decreasing water surface
			if (emittedVerts == 0) lastH = posY;
			else if (posY > lastH) posY = lastH;
			lastH = posY;

			// Compute tangent direction from neighbors
			glm::vec3 dir;
			if (i == 0) {
				glm::vec2 d = finePath[i + 1] - finePath[i];
				dir = glm::normalize(glm::vec3(d.x, 0, d.y));
			} else if (i == finePath.size() - 1) {
				glm::vec2 d = finePath[i] - finePath[i - 1];
				dir = glm::normalize(glm::vec3(d.x, 0, d.y));
			} else {
				glm::vec2 d = finePath[i + 1] - finePath[i - 1];
				float len = glm::length(d);
				if (len > 0.0001f) dir = glm::normalize(glm::vec3(d.x, 0, d.y));
				else dir = glm::vec3(1, 0, 0);
			}

			glm::vec3 right = glm::normalize(glm::cross(dir, up));

			// Width from interpolated flow volume
			float vol = fineVolume[i];
			float worldWidth = baseWidth * std::pow(vol, 0.35f) * waterMeshWidthMultiplier;
			float localWidth = worldWidth / terrainScale.x;

			float yOffset = waterOffset / terrainScale.y;
			glm::vec3 center(posX, posY + yOffset, posZ);
			glm::vec3 pL = center - right * localWidth;
			glm::vec3 pR = center + right * localWidth;

			float vCoord = (float)i * 0.025f; // Finer UV stepping for subdivided mesh

			waterMesh.AddVertex(pL.x, pL.y, pL.z, 0, vCoord, up.x, up.y, up.z, right.x, right.y, right.z, dir.x, dir.y, dir.z);
			waterMesh.AddVertex(pR.x, pR.y, pR.z, 1, vCoord, up.x, up.y, up.z, right.x, right.y, right.z, dir.x, dir.y, dir.z);

			if (emittedVerts > 0)
			{
				int currL = baseIdx + emittedVerts * 2;
				int currR = baseIdx + emittedVerts * 2 + 1;
				int prevL = baseIdx + (emittedVerts - 1) * 2;
				int prevR = baseIdx + (emittedVerts - 1) * 2 + 1;
				waterMesh.AddTriangle(prevL, currR, currL);
				waterMesh.AddTriangle(prevL, prevR, currR);
			}
			emittedVerts++;
		}
	}

	// Lakes (Global deduplicated mesh)
	std::map<int, int> terrainToWaterIdx;
	std::vector<int> lakePixelList;
	for (int i = 0; i < totalVerts; i++) if (lakeWaterLevel[i] > -1.0f) lakePixelList.push_back(i);

	// Expand for skirt
	std::vector<int> expandedPixels = lakePixelList;
	std::vector<bool> isExpanded(totalVerts, false);
	for (int idx : lakePixelList) isExpanded[idx] = true;
	for (int idx : lakePixelList) {
		for (int dz = -1; dz <= 1; dz++) {
			for (int dx = -1; dx <= 1; dx++) {
				int nx = (idx % gridRes) + dx; int nz = (idx / gridRes) + dz;
				if (nx >= 0 && nx < gridRes && nz >= 0 && nz < gridRes) {
					int nidx = nz * gridRes + nx;
					if (!isExpanded[nidx]) { expandedPixels.push_back(nidx); isExpanded[nidx] = true; }
				}
			}
		}
	}

	for (int idx : expandedPixels) {
		float x_pos = data.vertices[idx * 14];
		float z_pos = data.vertices[idx * 14 + 2];
		float y_pos = -1.0f;
		if (lakeWaterLevel[idx] > -1.0f) y_pos = lakeWaterLevel[idx];
		else {
			// Skirt vertex: find water level from nearest lake pixel
			for (int dz = -1; dz <= 1 && y_pos < 0; dz++) {
				for (int dx = -1; dx <= 1 && y_pos < 0; dx++) {
					int nidx = (idx / gridRes + dz) * gridRes + (idx % gridRes + dx);
					if (nidx >= 0 && nidx < totalVerts && lakeWaterLevel[nidx] > -1.0f) y_pos = lakeWaterLevel[nidx];
				}
			}
		}
		// TBN Generation for lakes
		waterMesh.AddVertex(x_pos, y_pos, z_pos, 0.5f, 0.5f, 0, 1, 0, 1, 0, 0, 0, 0, 1);
		terrainToWaterIdx[idx] = waterMesh.GetVertexCount() - 1;
	}

	for (int idx : expandedPixels) {
		int cx = idx % gridRes; int cz = idx / gridRes;
		int r = cz * gridRes + (cx + 1); int b = (cz + 1) * gridRes + cx; int br = (cz + 1) * gridRes + (cx + 1);
		if (cx < gridRes - 1 && cz < gridRes - 1) {
			if (terrainToWaterIdx.count(idx) && terrainToWaterIdx.count(r) && terrainToWaterIdx.count(b) && terrainToWaterIdx.count(br)) {
				waterMesh.AddTriangle(terrainToWaterIdx[idx], terrainToWaterIdx[b], terrainToWaterIdx[r]);
				waterMesh.AddTriangle(terrainToWaterIdx[r], terrainToWaterIdx[b], terrainToWaterIdx[br]);
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
