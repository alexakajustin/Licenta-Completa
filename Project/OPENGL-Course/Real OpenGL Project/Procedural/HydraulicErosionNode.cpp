#include "Procedural/HydraulicErosionNode.h"
#include "Nodes/NodeRandom.h"
#include "Procedural/DropletErosion.h"
#include "Procedural/ThermalErosion.h"
#include <algorithm>
#include <cstdio>
#include "imgui.h"
#include <cmath>
#include <vector>
#include <string>

HydraulicErosionNode::HydraulicErosionNode(NodeGraph& graph) : GraphNode(graph), seed(NodeRandom::TrueRandomSeed())
{
	AddInput(PinType::Mesh, "Mesh", "mesh");
	AddOutput(PinType::Mesh, "Mesh", "mesh");
	AddFloat("dropletsPerCell", "Droplets / cell", &dropletsPerCell, 0.1f, 0.0f, 100.0f, "%.1f").Tip("Rain: droplets simulated per heightmap cell");
	AddFloat("erodeSpeed", "Erosion", &erodeSpeed, 0.01f, 0.0f, 1.0f);
	AddFloat("depositSpeed", "Deposition", &depositSpeed, 0.01f, 0.0f, 1.0f);
	AddFloat("sedimentCapacity", "Capacity", &sedimentCapacity, 0.1f, 0.1f, 32.0f, "%.2f").Tip("Sediment a droplet carries per unit of slope, speed and water");
	AddInt("brushRadius", "Brush radius", &brushRadius, 0.1f, 1, 8).Tip("Wider: smoother gullies");
	AddFloat("inertia", "Inertia", &inertia, 0.01f, 0.0f, 0.95f);
	AddFloat("evaporation", "Evaporation", &evaporation, 0.001f, 0.0f, 0.2f, "%.3f");
	AddFloat("talusAngle", "Talus angle", &talusAngle, 0.5f, 15.0f, 70.0f, "%.1f deg").Tip("Thermal erosion: the steepest stable slope; steeper material slides down");
	AddInt("thermalIterations", "Thermal iterations", &thermalIterations, 0.5f, 0, 200);
	AddFloat("edgeFadeCells", "Edge fade", &edgeFadeCells, 1.0f, 0.0f, 200.0f, "%.0f cells");
	AddInt("smoothPasses", "Smooth passes", &smoothPasses, 0.1f, 0, 20);
	AddSeed("seed", "Seed", &seed);
}

void HydraulicErosionNode::LoadState(const json& j)
{
	// Before it had a seed, every erosion used 42: same terrain as before
	if (!j.contains("seed")) seed = 42;
}

void HydraulicErosionNode::Execute(NodeContext& ctx)
{
	const NodeValue& input = In("mesh");
	if (!input.mesh.Empty())
	{
		MeshData data = input.mesh.Data();

		int totalVerts = (int)data.vertices.size() / 14;
		int gridRes = (int)std::sqrt(totalVerts);

		if (gridRes * gridRes != totalVerts)
		{
			ctx.Warn("Needs a square grid mesh (a subdivided plane): passed through unchanged");
			Out(0, input);
			return;
		}

		std::vector<float> hmap(gridRes * gridRes);
		for (int i = 0; i < gridRes * gridRes; i++)
			hmap[i] = data.vertices[i * 14 + 1];

		DropletErosionParams params;
		const int totalDroplets = (int)(dropletsPerCell * (float)(gridRes * gridRes));
		params.erodeSpeed = erodeSpeed;
		params.depositSpeed = depositSpeed;
		params.sedimentCapacityFactor = sedimentCapacity;
		params.evaporateSpeed = evaporation;
		params.brushRadius = brushRadius;
		params.inertia = inertia;
		params.edgeFadeCells = edgeFadeCells;

		// Thermal erosion needs the real horizontal spacing (talus is an actual slope angle).
		// SceneInput bakes the object's scale into the vertices, so x is in world units here.
		float minX = data.vertices[0], maxX = data.vertices[0];
		for (int i = 0; i < gridRes * gridRes; i++) {
			minX = std::min(minX, data.vertices[i * 14]);
			maxX = std::max(maxX, data.vertices[i * 14]);
		}
		const float cellSize = (maxX - minX) / (float)(gridRes - 1);
		auto mm = std::minmax_element(hmap.begin(), hmap.end());
		const float heightRange = *mm.second - *mm.first;
		// A terrain taller than half its width means the grid is in unscaled local units:
		// a talus threshold in those units would flatten everything, so skip thermal.
		const bool thermal = thermalIterations > 0 && cellSize > 0.0f && (maxX - minX) > heightRange * 0.5f;
		if (thermalIterations > 0 && !thermal)
			printf("[HydraulicErosion] Grid looks unscaled (width %.3f, height range %.1f): thermal erosion skipped\n", maxX - minX, heightRange);

		// Pipeline: thermal (settle the base noise) -> rounds of [droplets -> thermal]
		// -> final thermal. Hydraulic carving alone leaves vertical gully walls and knife-edge
		// fins (fine from above, walls from the side); thermal relaxes them to the talus angle
		// after every round, so the gullies deepen as V-shaped valleys with scree slopes.
		const int rounds = 4;
		auto report = [&](float t, const std::string& what) { ctx.Progress(t * 100.0f, what); };

		if (thermal) RunThermalErosion(hmap, gridRes, cellSize, talusAngle, thermalIterations * 3,
			[&](float t) { report(t * 0.05f, "Thermal Erosion (settling)..."); });

		for (int r = 0; r < rounds; r++)
		{
			params.numDroplets = totalDroplets / rounds;
			params.seed = (unsigned int)seed + (unsigned int)r;
			float base = 0.05f + 0.9f * (float)r / (float)rounds;
			float span = 0.9f / (float)rounds;
			RunDropletErosion(hmap, gridRes, params, [&](float t) {
				report(base + span * 0.85f * t, "Hydraulic Erosion... round " + std::to_string(r + 1) + "/" + std::to_string(rounds) + " (" + std::to_string(totalDroplets) + " droplets)");
			});
			if (thermal) RunThermalErosion(hmap, gridRes, cellSize, talusAngle, thermalIterations,
				[&](float t) { report(base + span * (0.85f + 0.15f * t), "Thermal Erosion..."); });
		}

		if (thermal) RunThermalErosion(hmap, gridRes, cellSize, talusAngle, thermalIterations,
			[&](float t) { report(0.95f + 0.05f * t, "Thermal Erosion (final)..."); });

		for (int i = 0; i < gridRes * gridRes; i++)
			data.vertices[i * 14 + 1] = hmap[i];

		// Optional post-erosion smoothing (3x3 binomial)
		if (smoothPasses > 0)
		{
			std::vector<float> heightBuffer(gridRes * gridRes);
			for (int pass = 0; pass < smoothPasses; pass++)
			{
				for (int i = 0; i < gridRes * gridRes; i++) heightBuffer[i] = data.vertices[i * 14 + 1];
				for (int z = 1; z < gridRes - 1; z++)
				{
					for (int x = 1; x < gridRes - 1; x++)
					{
						int idx = z * gridRes + x;
						float center = heightBuffer[idx] * 4.0f;
						float cross = heightBuffer[idx - 1] + heightBuffer[idx + 1] + heightBuffer[idx - gridRes] + heightBuffer[idx + gridRes];
						float diag = heightBuffer[idx - gridRes - 1] + heightBuffer[idx - gridRes + 1] + heightBuffer[idx + gridRes - 1] + heightBuffer[idx + gridRes + 1];
						data.vertices[idx * 14 + 1] = (center + cross * 2.0f + diag) / 16.0f;
					}
				}
			}
		}

		RecomputeNormals(data, gridRes - 1, gridRes - 1);
		MeshValue out = input.mesh;
		out.Set(std::move(data));
		Out(0, NodeValue::FromMesh(std::move(out)));
	}
}

void HydraulicErosionNode::RecomputeNormals(MeshData& data, int resX, int resZ)
{
	const int stride = 14;
	int gridW = resX + 1;
	int gridH = resZ + 1;
	int totalVerts = gridW * gridH;

	for (int i = 0; i < totalVerts; i++)
	{
		int base = i * stride;
		data.vertices[base + 5] = 0.0f;
		data.vertices[base + 6] = 0.0f;
		data.vertices[base + 7] = 0.0f;
	}

	for (size_t i = 0; i + 2 < data.indices.size(); i += 3)
	{
		unsigned int i0 = data.indices[i];
		unsigned int i1 = data.indices[i + 1];
		unsigned int i2 = data.indices[i + 2];
		int b0 = i0 * stride; int b1 = i1 * stride; int b2 = i2 * stride;
		glm::vec3 v0(data.vertices[b0], data.vertices[b0 + 1], data.vertices[b0 + 2]);
		glm::vec3 v1(data.vertices[b1], data.vertices[b1 + 1], data.vertices[b1 + 2]);
		glm::vec3 v2(data.vertices[b2], data.vertices[b2 + 1], data.vertices[b2 + 2]);
		glm::vec3 faceNormal = glm::cross(v1 - v0, v2 - v0);
		for (int j = 0; j < 3; j++) {
			data.vertices[b0 + 5 + j] += faceNormal[j];
			data.vertices[b1 + 5 + j] += faceNormal[j];
			data.vertices[b2 + 5 + j] += faceNormal[j];
		}
	}

	for (int i = 0; i < totalVerts; i++)
	{
		int base = i * stride;
		glm::vec3 n(data.vertices[base + 5], data.vertices[base + 6], data.vertices[base + 7]);
		float len = glm::length(n);
		n = (len > 0.0001f) ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
		data.vertices[base + 5] = n.x; data.vertices[base + 6] = n.y; data.vertices[base + 7] = n.z;
		// Tangent follows the plane's U direction (+X), bitangent its V direction (+Z), like
		// PrimitiveGenerator's plane. (Building it from +Y made it point up-slope, so UV normal
		// maps were rotated differently on every slope.)
		glm::vec3 ref = (std::abs(n.x) < 0.999f) ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
		glm::vec3 tangent = glm::normalize(ref - n * glm::dot(ref, n));
		glm::vec3 bitangent = glm::cross(tangent, n);
		data.vertices[base + 8] = tangent.x; data.vertices[base + 9] = tangent.y; data.vertices[base + 10] = tangent.z;
		data.vertices[base + 11] = bitangent.x; data.vertices[base + 12] = bitangent.y; data.vertices[base + 13] = bitangent.z;
	}
}
