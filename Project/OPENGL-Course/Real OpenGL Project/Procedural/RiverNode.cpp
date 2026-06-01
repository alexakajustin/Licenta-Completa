#include "Procedural/RiverNode.h"
#include "imgui.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "Rendering/Mesh.h"
#include "Rendering/Material.h"
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
	maxSteps = 1500;
	baseDepth = 1.0f;
	baseWidth = 8.0f;
	waterOffset = 0.005f;
	smoothPasses = 8;
	lakeVolumeMultiplier = 500.0f;
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
	j["lakeVolumeMultiplier"] = lakeVolumeMultiplier;
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
	lakeVolumeMultiplier = j.value("lakeVolumeMultiplier", 500.0f);
}

void RiverNode::RenderContent(SceneManager* scene)
{
	ImGui::DragInt("Springs", &springCount, 1, 1, 50);
	ImGui::DragInt("Max Length", &maxSteps, 10, 10, 2000);
	ImGui::DragFloat("Base Depth", &baseDepth, 0.1f, 0.0f, 50.0f);
	ImGui::DragFloat("Base Width", &baseWidth, 0.1f, 0.1f, 100.0f);
	ImGui::DragFloat("Water Offset", &waterOffset, 0.1f, -10.0f, 10.0f);
	ImGui::DragInt("Smooth Passes", &smoothPasses, 1, 0, 30);
	ImGui::Separator();
	ImGui::Text("Appearance");
	ImGui::SliderFloat("Lake Volume", &lakeVolumeMultiplier, 10.0f, 5000.0f);
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
	if (terrainObj) {
		terrainScale = terrainObj->GetTransform().GetScale();
	}
	if (!inputs[0].data.transforms.empty()) {
		terrainScale = inputs[0].data.transforms[0].scale;
	}

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
					path.push_back({ currX, currZ }); 
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
				int jumpX = -1, jumpZ = -1;
				for (int r = 2; r <= 12 && !foundLower; r++) {
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
				if (foundLower) {
					if (!merged) {
						int stepCount = std::max(std::abs(jumpX - currX), std::abs(jumpZ - currZ));
						for (int s = 1; s < stepCount; s++) {
							path.push_back({ currX + (jumpX - currX) * s / stepCount, currZ + (jumpZ - currZ) * s / stepCount });
						}
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
		
		if (!trapped) {
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

	std::vector<float> originalHeights(totalVerts);
	for (int i = 0; i < totalVerts; i++) {
		originalHeights[i] = data.vertices[i * 14 + 1];
	}

	std::vector<float> lakeWaterLevel(totalVerts, -1.0f);
	std::vector<bool> lakeMask(totalVerts, false);
	std::vector<bool> sinkHandled(totalVerts, false);

	std::sort(sinks.begin(), sinks.end(), [&](const Sink& a, const Sink& b) {
		return data.vertices[(a.z * gridRes + a.x) * 14 + 1] < data.vertices[(b.z * gridRes + b.x) * 14 + 1];
	});

	for (const auto& sink : sinks)
	{
		int sinkIdx = sink.z * gridRes + sink.x;
		if (sinkHandled[sinkIdx]) continue;

		// Use the user-defined multiplier for lake capacity
		float targetVolume = sink.volume * baseWidth * lakeVolumeMultiplier; 
		float currentVolume = 0.0f;
		float sinkHeight = data.vertices[sinkIdx * 14 + 1];
		float currentWaterLevel = sinkHeight;

		std::vector<int> lakePixels;
		std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>, std::greater<std::pair<float, int>>> pq;
		
		pq.push({currentWaterLevel, sinkIdx});
		std::vector<bool> visited(totalVerts, false);
		visited[sinkIdx] = true;
		sinkHandled[sinkIdx] = true;

		while (!pq.empty())
		{
			auto [h, idx] = pq.top();
			
			// UNLIMITED fill depth: Fill until the target volume or spill point is reached
			if (currentVolume >= targetVolume) break;
			
			pq.pop();

			if (isSink[idx] && !sinkHandled[idx]) {
				targetVolume += flowVolume[idx] * baseWidth * lakeVolumeMultiplier;
				sinkHandled[idx] = true;
			}

			if (h > currentWaterLevel) {
				float diff = h - currentWaterLevel;
				currentVolume += diff * (float)lakePixels.size();
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

		// Minimum size for a realistic lake (Increased for realism)
		if (lakePixels.size() < 50) {
			for (int idx : lakePixels) lakeMask[idx] = false;
			continue;
		}

		for (int idx : lakePixels) {
			lakeWaterLevel[idx] = currentWaterLevel;
			float terrainH = data.vertices[idx * 14 + 1];
			float bedDepth = currentWaterLevel - baseDepth;
			if (terrainH > bedDepth) {
				data.vertices[idx * 14 + 1] = bedDepth;
			}
		}
	}

	struct SplinePoint { glm::vec2 pos; float volume; float lakeLevel; float height; };
	struct RiverData { std::vector<SplinePoint> path; bool isMerged; };
	std::vector<RiverData> fineRivers;

	auto clampGrid = [&](int v) { return std::max(0, std::min(v, gridRes - 1)); };
	auto catmullRom = [](glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, float t) -> glm::vec2 {
		float t2 = t * t, t3 = t2 * t;
		return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
	};

	for (auto& path : riverPaths) {
		if (path.size() < 2) continue;
		std::vector<glm::vec2> coarsePath;
		std::vector<float> coarseVolume;
		for (size_t pi = 0; pi < path.size(); pi++) {
			coarsePath.push_back(glm::vec2((float)path[pi].x, (float)path[pi].z));
			int idx = clampGrid(path[pi].z) * gridRes + clampGrid(path[pi].x);
			coarseVolume.push_back((float)std::max(1, flowVolume[idx]));
		}
		for (int s = 0; s < 10; s++) { 
			for (size_t i = 1; i < coarsePath.size() - 1; i++) coarsePath[i] = (coarsePath[i - 1] + coarsePath[i] + coarsePath[i + 1]) / 3.0f;
		}

		int lakeHitIdx = -1; float lakeLevel = -1.0f;
		for (size_t i = 0; i < coarsePath.size(); i++) {
			int gx = clampGrid((int)coarsePath[i].x); int gz = clampGrid((int)coarsePath[i].y);
			if (lakeMask[gz * gridRes + gx]) { lakeHitIdx = (int)i; lakeLevel = lakeWaterLevel[gz * gridRes + gx]; break; }
		}

		if (lakeHitIdx >= 0 && lakeHitIdx < (int)coarsePath.size()) {
			coarsePath.resize(lakeHitIdx + 1); coarseVolume.resize(lakeHitIdx + 1);
			glm::vec2 lastDir(0, 0);
			if (coarsePath.size() >= 2) lastDir = glm::normalize(coarsePath.back() - coarsePath[coarsePath.size() - 2]);
			for (int e = 1; e <= 8; e++) { 
				coarsePath.push_back(coarsePath[lakeHitIdx] + lastDir * (float)e);
				coarseVolume.push_back(coarseVolume.back());
			}
		}

		if (coarsePath.size() < 2) continue;

		const int subdivisions = 12;
		std::vector<SplinePoint> finePath;
		for (size_t seg = 0; seg < coarsePath.size() - 1; seg++) {
			int i0 = (int)std::max((int)seg - 1, 0); int i1 = (int)seg;
			int i2 = (int)std::min(seg + 1, coarsePath.size() - 1); int i3 = (int)std::min(seg + 2, coarsePath.size() - 1);

			for (int sub = 0; sub < subdivisions; sub++) {
				float t = (float)sub / (float)subdivisions;
				glm::vec2 pt = catmullRom(coarsePath[i0], coarsePath[i1], coarsePath[i2], coarsePath[i3], t);
				float vol = glm::mix(coarseVolume[i1], coarseVolume[i2], t);
				int gx = clampGrid((int)pt.x); int gz = clampGrid((int)pt.y);
				float lvl = -1.0f;
				if (lakeHitIdx >= 0 && lakeMask[gz * gridRes + gx]) lvl = lakeLevel;
				finePath.push_back({ pt, vol, lvl, 0.0f });
			}
		}
		glm::vec2 pt = coarsePath.back();
		int gx = clampGrid((int)pt.x); int gz = clampGrid((int)pt.y);
		float lvl = -1.0f; if (lakeHitIdx >= 0 && lakeMask[gz * gridRes + gx]) lvl = lakeLevel;
		finePath.push_back({ pt, coarseVolume.back(), lvl, 0.0f });

		auto getOriginalH = [&](int x, int z) -> float { return originalHeights[clampGrid(z) * gridRes + clampGrid(x)]; };
		for (auto& fp : finePath) {
			int x0 = clampGrid((int)std::floor(fp.pos.x)); int x1 = clampGrid(x0 + 1);
			int z0 = clampGrid((int)std::floor(fp.pos.y)); int z1 = clampGrid(z0 + 1);
			float tx = fp.pos.x - (float)x0; float tz = fp.pos.y - (float)z0;
			fp.height = glm::mix(glm::mix(getOriginalH(x0, z0), getOriginalH(x1, z0), tx), glm::mix(getOriginalH(x0, z1), getOriginalH(x1, z1), tx), tz);
		}

		for (int s = 0; s < 50; s++) {
			for (size_t i = 1; i < finePath.size() - 1; i++) {
				if (finePath[i].lakeLevel == -1.0f) {
					finePath[i].height = (finePath[i - 1].height + finePath[i].height + finePath[i + 1].height) / 3.0f;
				} else {
					finePath[i].height = finePath[i].lakeLevel;
				}
			}
		}

		// --- EXTREME SOURCE SINK ---
		// Sink the first point 25 meters deep to guarantee it starts INSIDE the mountain.
		float lastH = finePath[0].height - 25.0f; 
		finePath[0].height = lastH;

		for (size_t i = 1; i < finePath.size(); i++) {
			if (finePath[i].lakeLevel > -1.0f) {
				finePath[i].height = finePath[i].lakeLevel;
			} else if (finePath[i].height > lastH) {
				finePath[i].height = lastH;
			}
			lastH = finePath[i].height;
		}

		bool isMerged = false;
		int lastIdx = (int)path.size() - 1;
		int idx = clampGrid(path[lastIdx].z) * gridRes + clampGrid(path[lastIdx].x);
		if (pathID[idx] != currentPathID) isMerged = true;

		fineRivers.push_back({finePath, isMerged});
		currentPathID++;
	}

	for (const auto& riverData : fineRivers) {
		for (const auto& pt : riverData.path) {
			if (pt.lakeLevel > -1.0f) continue;

			float volume = pt.volume;
			float currentDepth = baseDepth * std::pow(volume, 0.35f);
			float currentWidth = baseWidth * std::pow(volume, 0.35f);

			int cx = (int)pt.pos.x;
			int cz = (int)pt.pos.y;
			
			float stepSize = terrainScale.x / (float)gridRes;
			int gridRadius = 0;
			if (stepSize > 1e-5f) {
				gridRadius = (int)std::ceil(currentWidth / stepSize);
			}
			// Safety cap to prevent massive/infinite loops if scale is invalid or extremely small
			if (gridRadius > gridRes / 4) {
				gridRadius = gridRes / 4;
			}

			for (int rz = -gridRadius; rz <= gridRadius; rz++) {
				for (int rx = -gridRadius; rx <= gridRadius; rx++) {
					int nx = cx + rx;
					int nz = cz + rz;
					if (nx >= 0 && nx < gridRes && nz >= 0 && nz < gridRes && !lakeMask[nz * gridRes + nx]) {
						float dx = (float)nx - pt.pos.x;
						float dz = (float)nz - pt.pos.y;
						float dist = std::sqrt(dx * dx + dz * dz);
						float worldDist = dist * (terrainScale.x / gridRes);

						if (worldDist <= currentWidth) {
							float t = glm::clamp(worldDist / currentWidth, 0.0f, 1.0f);
							t = t * t * (3.0f - 2.0f * t);
							
							float bankHeight = originalHeights[nz * gridRes + nx];
							float riverBedHeight = pt.height - currentDepth;
							float targetHeight = glm::mix(riverBedHeight, bankHeight, t);
							
							if (targetHeight < data.vertices[(nz * gridRes + nx) * 14 + 1]) {
								data.vertices[(nz * gridRes + nx) * 14 + 1] = targetHeight;
							}
						}
					}
				}
			}
		}
	}

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

	MeshData riverMesh;
	MeshData lakeMesh;
	glm::vec3 up(0, 1, 0);
	float waterMeshWidthMultiplier = 1.25f;
	float yOffset = waterOffset;

	for (const auto& riverData : fineRivers)
	{
		int baseIdx = riverMesh.GetVertexCount();
		int emittedVerts = 0;

		for (size_t i = 0; i < riverData.path.size(); i++)
		{
			float fx = riverData.path[i].pos.x;
			float fz = riverData.path[i].pos.y;
			fx = glm::clamp(fx, 0.0f, (float)(gridRes - 1));
			fz = glm::clamp(fz, 0.0f, (float)(gridRes - 1));

			float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
			int x0 = clampGrid((int)std::floor(fx)); int x1 = clampGrid(x0 + 1);
			int z0 = clampGrid((int)std::floor(fz)); int z1 = clampGrid(z0 + 1);
			float tx = fx - (float)x0; float tz = fz - (float)z0;
			tx = glm::clamp(tx, 0.0f, 1.0f); tz = glm::clamp(tz, 0.0f, 1.0f);

			posX = glm::mix(data.vertices[(z0 * gridRes + x0) * 14], data.vertices[(z0 * gridRes + x1) * 14], tx);
			posZ = glm::mix(data.vertices[(z0 * gridRes + x0) * 14 + 2], data.vertices[(z1 * gridRes + x0) * 14 + 2], tz);

			float currentDepth = baseDepth * std::pow(riverData.path[i].volume, 0.35f);
			if (riverData.path[i].lakeLevel > -1.0f) posY = riverData.path[i].lakeLevel;
			else posY = riverData.path[i].height - (currentDepth * 0.5f);

			glm::vec3 dir;
			if (i == 0) {
				glm::vec2 d = riverData.path[i + 1].pos - riverData.path[i].pos;
				dir = glm::normalize(glm::vec3(d.x, 0, d.y));
			} else if (i == riverData.path.size() - 1) {
				glm::vec2 d = riverData.path[i].pos - riverData.path[i - 1].pos;
				dir = glm::normalize(glm::vec3(d.x, 0, d.y));
			} else {
				glm::vec2 d = riverData.path[i + 1].pos - riverData.path[i - 1].pos;
				float len = glm::length(d);
				if (len > 0.0001f) dir = glm::normalize(glm::vec3(d.x, 0, d.y));
				else dir = glm::vec3(1, 0, 0);
			}
			
			glm::vec3 right = glm::normalize(glm::cross(dir, up));
			glm::vec3 center(posX, posY + yOffset, posZ);
			
			// --- TERRAIN-AWARE MESH BURIAL ---
			// Push segments backward horizontally.
			// This ensures the buried part of the mesh starts inside the mountain behind the source.
			if (i < 8 && riverData.path.size() >= 2) {
				float pushDist = 20.0f * (1.0f - (float)i / 8.0f);
				center -= dir * pushDist;
			}

			float volume = riverData.path[i].volume;
			float currentWidth = baseWidth * std::pow(volume, 0.35f);
			
			if (i < 8) {
				float t = (float)i / 8.0f;
				float smoothT = t * t * (3.0f - 2.0f * t);
				currentWidth *= smoothT;
			}
			
			if (riverData.isMerged && i > riverData.path.size() - 6) {
				float t = (float)(riverData.path.size() - 1 - i) / 5.0f;
				currentWidth *= t;
			}
			
			float localWidth = currentWidth * waterMeshWidthMultiplier;
			glm::vec3 pL = center - right * localWidth;
			glm::vec3 pR = center + right * localWidth;

			float vCoord = (float)i * 0.025f; // V texture coordinate for river flow direction
			riverMesh.AddVertex(pL.x, pL.y, pL.z, 0, vCoord, up.x, up.y, up.z, right.x, right.y, right.z, dir.x, dir.y, dir.z);
			riverMesh.AddVertex(pR.x, pR.y, pR.z, 1, vCoord, up.x, up.y, up.z, right.x, right.y, right.z, dir.x, dir.y, dir.z);

			if (emittedVerts > 0)
			{
				int currL = baseIdx + emittedVerts * 2; int currR = baseIdx + emittedVerts * 2 + 1;
				int prevL = baseIdx + (emittedVerts - 1) * 2; int prevR = baseIdx + (emittedVerts - 1) * 2 + 1;
				riverMesh.AddTriangle(prevL, currR, currL);
				riverMesh.AddTriangle(prevL, prevR, currR);
			}
			emittedVerts++;
		}
	}

	// 6b. Lake Meshes (Static)
	std::vector<int> lakePixelList;
	for (int i = 0; i < totalVerts; i++) if (lakeWaterLevel[i] > -1.0f) lakePixelList.push_back(i);
	std::vector<int> expandedPixels = lakePixelList;
	std::vector<bool> isExpanded(totalVerts, false);
	for (int idx : lakePixelList) isExpanded[idx] = true;
	for(int pass=0; pass<5; pass++) {
		std::vector<int> currentPass = expandedPixels;
		for (int idx : currentPass) {
			for (int dz = -1; dz <= 1; dz++) {
				for (int dx = -1; dx <= 1; dx++) {
					int nx = (idx % gridRes) + dx; int nz = (idx / gridRes) + dz;
					if (nx >= 0 && nx < gridRes && nz >= 0 && nz < gridRes) {
						int nidx = nz * gridRes + nx;
						if (!isExpanded[nidx]) { 
							float terrainH = originalHeights[nidx];
							float waterLevel = lakeWaterLevel[idx];
							// Correct Expansion: Only expand into terrain that is BELOW or at the water level
							// This allows lakes to fill deep basins while preventing "Staircases" in the air
							if (terrainH < waterLevel + 0.5f) {
								expandedPixels.push_back(nidx); isExpanded[nidx] = true; 
								if (lakeWaterLevel[nidx] < 0) lakeWaterLevel[nidx] = waterLevel;
							}
						}
					}
				}
			}
		}
	}
	std::map<int, int> terrainToWaterIdx;
	for (int idx : expandedPixels) {
		float x_pos = data.vertices[idx * 14]; float z_pos = data.vertices[idx * 14 + 2];
		float y_pos = lakeWaterLevel[idx] + yOffset;
		lakeMesh.AddVertex(x_pos, y_pos, z_pos, 0.5f, 0.5f, 0, 1, 0, 1, 0, 0, 0, 0, 1);
		terrainToWaterIdx[idx] = lakeMesh.GetVertexCount() - 1;
	}
	for (int idx : expandedPixels) {
		int cx = idx % gridRes; int cz = idx / gridRes;
		int r = cz * gridRes + (cx + 1); int b = (cz + 1) * gridRes + cx; int br = (cz + 1) * gridRes + (cx + 1);
		if (cx < gridRes - 1 && cz < gridRes - 1) {
			if (terrainToWaterIdx.count(idx) && terrainToWaterIdx.count(r) && terrainToWaterIdx.count(b) && terrainToWaterIdx.count(br)) {
				lakeMesh.AddTriangle(terrainToWaterIdx[idx], terrainToWaterIdx[b], terrainToWaterIdx[r]);
				lakeMesh.AddTriangle(terrainToWaterIdx[r], terrainToWaterIdx[b], terrainToWaterIdx[br]);
			}
		}
	}

	// 7. Sync Objects & Materials
	auto syncWaterObject = [&](const std::string& name, MeshData& meshData, const std::string& matPath) {
		GameObject* obj = scene.FindObject(name);
		if (!obj) {
			obj = new GameObject(name);
			scene.AddObject(obj);
		}
		if (terrainObj) {
			obj->GetTransform().SetPosition(terrainObj->GetTransform().GetPosition());
			obj->GetTransform().SetRotation(glm::vec3(0.0f));
			obj->GetTransform().SetScale(glm::vec3(1.0f));
		}
		if (!obj->GetMaterial()) {
			Material* mat = Material::LoadFromFile(matPath);
			if (mat) obj->SetMaterial(mat);
		}
		if (!meshData.vertices.empty()) {
			obj->SetMesh(meshData.ToMesh());
			obj->SetCPUMeshData(meshData);
		}
	};

	syncWaterObject("River_Water_" + std::to_string(id), riverMesh, "Assets/Materials/River.mat");
	syncWaterObject("Lake_Water_" + std::to_string(id), lakeMesh, "Assets/Materials/Water.mat");

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
