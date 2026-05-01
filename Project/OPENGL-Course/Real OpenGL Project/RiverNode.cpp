#include "RiverNode.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <thread>

RiverNode::RiverNode(NodeGraph& graph)
{
	id = graph.NextNodeId();
	title = "River";

	// One input: Mesh
	Pin meshIn(graph.NextPinId(), PinDataType::Mesh, "Mesh");
	inputs.push_back(meshIn);

	// One output: Mesh
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
	smoothPasses = j.value("smoothPasses", 4);
}

void RiverNode::RenderContent(SceneManager* scene)
{
	ImGui::DragInt("Springs", &springCount, 1, 1, 50);
	ImGui::DragInt("Max Length", &maxSteps, 10, 10, 2000);
	ImGui::DragFloat("Base Depth", &baseDepth, 0.1f, 0.0f, 50.0f);
	ImGui::DragFloat("Base Width", &baseWidth, 0.1f, 0.1f, 50.0f);
	ImGui::DragFloat("Lake Size", &lakeSize, 0.1f, 0.0f, 100.0f);
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

	// 1. Identify "Springs" (High points)
	struct Spring { int x, z; float height; };
	std::vector<Spring> allVerts;
	allVerts.reserve(totalVerts);
	for (int z = 0; z < gridRes; z++)
	{
		for (int x = 0; x < gridRes; x++)
		{
			allVerts.push_back({ x, z, data.vertices[(z * gridRes + x) * 14 + 1] });
		}
	}

	// Sort by height descending
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
			if (std::sqrt(dx * dx + dz * dz) < minDist)
			{
				farEnough = false;
				break;
			}
		}
		if (farEnough)
		{
			springs.push_back(s);
			if ((int)springs.size() >= springCount) break;
		}
	}

	if (progress) progress(15.0f, "Seeding Springs...");

	// 2. Trace Paths (Steepest Descent)
	std::vector<int> flowVolume(totalVerts, 0);
	struct Sink { int x, z; float volume; };
	std::vector<Sink> sinks;

	for (const auto& spring : springs)
	{
		int currX = spring.x;
		int currZ = spring.z;

		bool trapped = false;
		for (int step = 0; step < maxSteps; step++)
		{
			flowVolume[currZ * gridRes + currX]++;

			// Find lowest neighbor
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
						if (nh < lowestH)
						{
							lowestH = nh;
							bestX = nx;
							bestZ = nz;
							foundLower = true;
						}
					}
				}
			}

			if (!foundLower) 
			{
				sinks.push_back({ currX, currZ, (float)flowVolume[currZ * gridRes + currX] });
				trapped = true;
				break; 
			}
			currX = bestX;
			currZ = bestZ;
		}
		if (!trapped) {
			sinks.push_back({ currX, currZ, (float)flowVolume[currZ * gridRes + currX] });
		}
	}

	if (progress) progress(40.0f, "Calculating Flow...");

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
						if (dist <= currentWidth)
						{
							float falloff = 1.0f - (dist / currentWidth);
							data.vertices[(nz * gridRes + nx) * 14 + 1] -= currentDepth * falloff;
						}
					}
				}
			}
		}
	}

	if (progress) progress(70.0f, "Carving Rivers...");

	// 4. Carve Lake Basins (Water Accumulation)
	if (lakeSize > 0)
	{
		for (const auto& sink : sinks)
		{
			float radius = lakeSize * std::pow(sink.volume, 0.7f);
			float targetH = data.vertices[(sink.z * gridRes + sink.x) * 14 + 1];

			targetH -= 2.0f;

			int iradius = (int)std::ceil(radius * 2.0f);
			for (int rz = -iradius; rz <= iradius; rz++)
			{
				for (int rx = -iradius; rx <= iradius; rx++)
				{
					int nx = sink.x + rx;
					int nz = sink.z + rz;
					if (nx >= 0 && nx < gridRes && nz >= 0 && nz < gridRes)
					{
						float dist = std::sqrt((float)(rx * rx + rz * rz));
						if (dist <= radius)
						{
							float noise = std::sin((float)nx * 0.5f) * std::cos((float)nz * 0.5f) * 0.5f;
							data.vertices[(nz * gridRes + nx) * 14 + 1] = targetH + noise;
						}
						else if (dist <= radius * 2.0f)
						{
							float t = (dist - radius) / radius;
							float currentH = data.vertices[(nz * gridRes + nx) * 14 + 1];
							data.vertices[(nz * gridRes + nx) * 14 + 1] = targetH + t * (currentH - targetH);
						}
					}
				}
			}
		}
	}

	if (progress) progress(85.0f, "Accumulating Water...");

	// 5. Smooth
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

	// 6. Normals
	RecomputeNormals(data, gridRes);

	if (progress) progress(100.0f, "Finalizing...");

	outputs[0].data = inputs[0].data;
	outputs[0].data.meshData = data;
}

void RiverNode::RecomputeNormals(MeshData& data, int gridRes)
{
	const int stride = 14;
	for (int i = 0; i < gridRes * gridRes; i++)
	{
		data.vertices[i * stride + 5] = 0;
		data.vertices[i * stride + 6] = 0;
		data.vertices[i * stride + 7] = 0;
	}

	for (size_t i = 0; i + 2 < data.indices.size(); i += 3)
	{
		int i0 = data.indices[i], i1 = data.indices[i + 1], i2 = data.indices[i + 2];
		glm::vec3 v0(data.vertices[i0 * 14], data.vertices[i0 * 14 + 1], data.vertices[i0 * 14 + 2]);
		glm::vec3 v1(data.vertices[i1 * 14], data.vertices[i1 * 14 + 1], data.vertices[i1 * 14 + 2]);
		glm::vec3 v2(data.vertices[i2 * 14], data.vertices[i2 * 14 + 1], data.vertices[i2 * 14 + 2]);
		glm::vec3 n = glm::cross(v1 - v0, v2 - v0);
		for (int j = 0; j < 3; j++) {
			data.vertices[i0 * 14 + 5 + j] += n[j];
			data.vertices[i1 * 14 + 5 + j] += n[j];
			data.vertices[i2 * 14 + 5 + j] += n[j];
		}
	}

	for (int i = 0; i < gridRes * gridRes; i++)
	{
		glm::vec3 n(data.vertices[i * 14 + 5], data.vertices[i * 14 + 6], data.vertices[i * 14 + 7]);
		if (glm::length(n) > 0.0001f) n = glm::normalize(n);
		else n = glm::vec3(0, 1, 0);
		data.vertices[i * 14 + 5] = n.x;
		data.vertices[i * 14 + 6] = n.y;
		data.vertices[i * 14 + 7] = n.z;
	}
}
