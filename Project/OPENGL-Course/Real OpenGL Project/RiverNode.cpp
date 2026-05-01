#include "RiverNode.h"
#include "imgui.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "Mesh.h"
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
}

json RiverNode::Serialize() const
{
	json j = GraphNode::Serialize();
	j["springCount"] = springCount;
	j["maxSteps"] = maxSteps;
	j["baseDepth"] = baseDepth;
	j["baseWidth"] = baseWidth;
	j["lakeSize"] = lakeSize;
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
	baseWidth = j.value("baseWidth", 5.0f);
	lakeSize = j.value("lakeSize", 4.0f);
	waterOffset = j.value("waterOffset", 0.5f);
	smoothPasses = j.value("smoothPasses", 4);
}

void RiverNode::RenderContent(SceneManager* scene)
{
	ImGui::DragInt("Springs", &springCount, 1, 1, 50);
	ImGui::DragInt("Max Length", &maxSteps, 10, 10, 2000);
	ImGui::DragFloat("Base Depth", &baseDepth, 0.1f, 0.0f, 50.0f);
	ImGui::DragFloat("Base Width", &baseWidth, 0.1f, 0.1f, 50.0f);
	ImGui::DragFloat("Lake Size", &lakeSize, 0.1f, 0.0f, 100.0f);
	ImGui::DragFloat("Water Offset", &waterOffset, 0.1f, -10.0f, 10.0f);
	ImGui::DragInt("Smooth Passes", &smoothPasses, 1, 0, 20);
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

	// 3. Carve Rivers
	for (int i = 0; i < totalVerts; i++)
	{
		if (flowVolume[i] > 0)
		{
			int cz = i / gridRes;
			int cx = i % gridRes;
			float volume = (float)flowVolume[i];
			float currentDepth = baseDepth * std::sqrt(volume);
			float currentWidth = baseWidth * std::sqrt(volume);

			int radius = (int)std::ceil(currentWidth);
			for (int rz = -radius; rz <= radius; rz++)
			{
				for (int rx = -radius; rx <= radius; rx++)
				{
					int nx = cx + rx;
					int nz = cz + rz;
					if (nx >= 0 && nx < gridRes && nz >= 0 && nz < gridRes)
					{
						float dist = std::sqrt((float)(rx * rx + rz * rz));
						float gridUnitSize = terrainScale.x / gridRes;
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

	// 4. Carve Lake Basins
	if (lakeSize > 0)
	{
		for (const auto& sink : sinks)
		{
			float worldRadius = lakeSize * std::sqrt(sink.volume);
			float targetH = data.vertices[(sink.z * gridRes + sink.x) * 14 + 1] - 2.0f;
			float gridUnitSize = terrainScale.x / gridRes;
			int gridRadius = (int)std::ceil(worldRadius / gridUnitSize);

			for (int rz = -gridRadius * 2; rz <= gridRadius * 2; rz++)
			{
				for (int rx = -gridRadius * 2; rx <= gridRadius * 2; rx++)
				{
					int nx = sink.x + rx;
					int nz = sink.z + rz;
					if (nx >= 0 && nx < gridRes && nz >= 0 && nz < gridRes)
					{
						float dist = std::sqrt((float)(rx * rx + rz * rz));
						float worldDist = dist * gridUnitSize;
						if (worldDist <= worldRadius)
						{
							data.vertices[(nz * gridRes + nx) * 14 + 1] = targetH;
						}
						else if (worldDist <= worldRadius * 1.5f)
						{
							float t = (worldDist - worldRadius) / (worldRadius * 0.5f);
							float currentH = data.vertices[(nz * gridRes + nx) * 14 + 1];
							data.vertices[(nz * gridRes + nx) * 14 + 1] = targetH + t * (currentH - targetH);
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
					if (flowVolume[idx] > 0)
					{
						float avg = (heightBuffer[idx] + heightBuffer[idx-1] + heightBuffer[idx+1] + 
									 heightBuffer[idx-gridRes] + heightBuffer[idx+gridRes]) / 5.0f;
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

	for (const auto& path : riverPaths)
	{
		if (path.size() < 2) continue;
		int baseIdx = waterMesh.GetVertexCount();
		for (size_t i = 0; i < path.size(); i++)
		{
			int vx = path[i].x; int vz = path[i].z;
			glm::vec3 pos(data.vertices[(vz * gridRes + vx) * 14],
						  data.vertices[(vz * gridRes + vx) * 14 + 1],
						  data.vertices[(vz * gridRes + vx) * 14 + 2]);

			glm::vec3 dir;
			if (i == 0) dir = glm::normalize(glm::vec3(path[i+1].x - path[i].x, 0, path[i+1].z - path[i].z));
			else if (i == path.size() - 1) dir = glm::normalize(glm::vec3(path[i].x - path[i-1].x, 0, path[i].z - path[i-1].z));
			else dir = glm::normalize(glm::vec3(path[i+1].x - path[i-1].x, 0, path[i+1].z - path[i-1].z));

			glm::vec3 right = glm::normalize(glm::cross(dir, up));
			
			float worldWidth = baseWidth * std::sqrt((float)flowVolume[vz * gridRes + vx]);
			float localWidth = worldWidth / terrainScale.x;

			glm::vec3 pL = pos - right * localWidth + up * (waterOffset / terrainScale.y);
			glm::vec3 pR = pos + right * localWidth + up * (waterOffset / terrainScale.y);

			waterMesh.AddVertex(pL.x, pL.y, pL.z, 0, (float)i / path.size(), 0, 1, 0, 1, 0, 0, 0, 0, 1);
			waterMesh.AddVertex(pR.x, pR.y, pR.z, 1, (float)i / path.size(), 0, 1, 0, 1, 0, 0, 0, 0, 1);

			if (i > 0)
			{
				int currL = baseIdx + (int)i * 2; int currR = baseIdx + (int)i * 2 + 1;
				int prevL = baseIdx + (int)(i - 1) * 2; int prevR = baseIdx + (int)(i - 1) * 2 + 1;
				
				// FIXED WINDING ORDER (CCW)
				waterMesh.AddTriangle(prevL, currR, currL);
				waterMesh.AddTriangle(prevL, prevR, currR);
			}
		}
	}

	for (const auto& sink : sinks)
	{
		float worldRadius = lakeSize * std::sqrt(sink.volume);
		float localRadius = worldRadius / terrainScale.x;
		glm::vec3 center(data.vertices[(sink.z * gridRes + sink.x) * 14], 
						 data.vertices[(sink.z * gridRes + sink.x) * 14 + 1] + (waterOffset / terrainScale.y) + 0.1f,
						 data.vertices[(sink.z * gridRes + sink.x) * 14 + 2]);

		int segments = 32; int baseIdx = waterMesh.GetVertexCount();
		waterMesh.AddVertex(center.x, center.y, center.z, 0.5f, 0.5f, 0, 1, 0, 1, 0, 0, 0, 0, 1);
		for (int i = 0; i <= segments; i++)
		{
			float ang = (float)i / (float)segments * 2.0f * 3.14159f;
			glm::vec3 p = center + glm::vec3(std::cos(ang), 0, std::sin(ang)) * localRadius;
			waterMesh.AddVertex(p.x, p.y, p.z, (std::cos(ang) + 1.0f) * 0.5f, (std::sin(ang) + 1.0f) * 0.5f, 0, 1, 0, 1, 0, 0, 0, 0, 1);
			
			if (i > 0) 
			{
				// FIXED WINDING ORDER (CCW)
				waterMesh.AddTriangle(baseIdx, baseIdx + i + 1, baseIdx + i);
			}
		}
	}

	// 7. Sync Transform
	std::string waterName = "River_Water_" + std::to_string(id);
	GameObject* waterObj = scene.FindObject(waterName);
	if (!waterObj) { waterObj = new GameObject(waterName); scene.AddObject(waterObj); }
	if (terrainObj)
	{
		waterObj->GetTransform().SetPosition(terrainObj->GetTransform().GetPosition());
		waterObj->GetTransform().SetRotation(terrainObj->GetTransform().GetRotation());
		waterObj->GetTransform().SetScale(terrainObj->GetTransform().GetScale());
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
