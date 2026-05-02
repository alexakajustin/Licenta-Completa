#include "HydraulicErosionNode.h"
#include "FluidSimulation.h"
#include "imgui.h"
#include <cmath>
#include <thread>
#include <vector>
#include <random>

HydraulicErosionNode::HydraulicErosionNode(NodeGraph& graph)
{
	id = graph.NextNodeId();
	title = "Hydraulic Erosion";

	Pin meshIn(graph.NextPinId(), PinDataType::Mesh, "Mesh");
	inputs.push_back(meshIn);

	Pin meshOut(graph.NextPinId(), PinDataType::Mesh, "Mesh");
	outputs.push_back(meshOut);
}

void HydraulicErosionNode::RenderContent(SceneManager* scene)
{
	ImGui::PushItemWidth(100.0f);
	ImGui::DragInt("Steps", &simulationSteps, 1, 0, 2000);
	ImGui::DragFloat("Rain", &rainRate, 0.001f, 0.0f, 2.0f);
	ImGui::DragFloat("Capacity", &sedimentCapacity, 0.1f, 0.1f, 200.0f);
	ImGui::DragFloat("Dissolve (Ks)", &dissolvingConstant, 0.001f, 0.0f, 1.0f);
	ImGui::DragFloat("Deposit (Kd)", &depositionConstant, 0.001f, 0.0f, 1.0f);
	ImGui::DragFloat("Evaporate", &evaporationConstant, 0.00001f, 0.0f, 0.5f, "%.5f");
	ImGui::DragFloat("Smooth Max D", &maxDelta, 0.1f, 0.0f, 10.0f);
	ImGui::DragInt("Smooth Passes", &smoothPasses, 1, 0, 20);
	ImGui::PopItemWidth();
}

json HydraulicErosionNode::Serialize() const
{
	json j = GraphNode::Serialize();
	j["simulationSteps"] = simulationSteps;
	j["rainRate"] = rainRate;
	j["sedimentCapacity"] = sedimentCapacity;
	j["dissolvingConstant"] = dissolvingConstant;
	j["depositionConstant"] = depositionConstant;
	j["evaporationConstant"] = evaporationConstant;
	j["maxDelta"] = maxDelta;
	j["smoothPasses"] = smoothPasses;
	return j;
}

void HydraulicErosionNode::Deserialize(const json& j)
{
	GraphNode::Deserialize(j);
	simulationSteps = j.value("simulationSteps", 60);
	rainRate = j.value("rainRate", 0.03f);
	sedimentCapacity = j.value("sedimentCapacity", 30.0f);
	dissolvingConstant = j.value("dissolvingConstant", 0.03f);
	depositionConstant = j.value("depositionConstant", 0.02f);
	evaporationConstant = j.value("evaporationConstant", 0.0002f);
	maxDelta = j.value("maxDelta", 1.0f);
	smoothPasses = j.value("smoothPasses", 0);
}

void HydraulicErosionNode::Execute(SceneManager& scene, NodeProgressCallback progress)
{
	outputs[0].data.Clear();

	PinData inputData = inputs[0].data;
	if (inputData.type == PinDataType::Mesh)
	{
		MeshData data = inputData.meshData;
		if (data.vertices.empty()) return;

		// Determine grid size assuming a perfectly square grid layout (like PrimitiveGenerator::Plane)
		int totalVerts = (int)data.vertices.size() / 14;
		int gridRes = (int)std::sqrt(totalVerts);

		// If it doesn't form a perfect square, we can't reliably erode it with this structured grid method
		if (gridRes * gridRes != totalVerts)
		{
			// Bypass
			outputs[0].data = inputData;
			return;
		}

		// === DROPLET-BASED HYDRAULIC EROSION ===
		// Extracted parameters
		int numDrops = simulationSteps * 1000;
		float dropEvap = evaporationConstant; 
		float dropDeposition = depositionConstant; 
		float dropErosion = dissolvingConstant; 
		float dropCapacity = sedimentCapacity; 
		int maxLifetime = 100;
		
		std::vector<float> hmap(gridRes * gridRes);
		for (int z = 0; z < gridRes; z++)
			for (int x = 0; x < gridRes; x++)
				hmap[z * gridRes + x] = data.vertices[(z * gridRes + x) * 14 + 1];

		auto GetHeight = [&](float x, float z) -> float {
			int ix = (int)x;
			int iz = (int)z;
			float u = x - ix;
			float v = z - iz;
			int ix1 = std::min(ix + 1, gridRes - 1);
			int iz1 = std::min(iz + 1, gridRes - 1);
			float h00 = hmap[iz * gridRes + ix];
			float h10 = hmap[iz * gridRes + ix1];
			float h01 = hmap[iz1 * gridRes + ix];
			float h11 = hmap[iz1 * gridRes + ix1];
			float h0 = h00 * (1.0f - u) + h10 * u;
			float h1 = h01 * (1.0f - u) + h11 * u;
			return h0 * (1.0f - v) + h1 * v;
		};

		auto GetGradient = [&](float x, float z, float& gx, float& gz) {
			int ix = (int)x;
			int iz = (int)z;
			float u = x - ix;
			float v = z - iz;
			int ix1 = std::min(ix + 1, gridRes - 1);
			int iz1 = std::min(iz + 1, gridRes - 1);
			float h00 = hmap[iz * gridRes + ix];
			float h10 = hmap[iz * gridRes + ix1];
			float h01 = hmap[iz1 * gridRes + ix];
			float h11 = hmap[iz1 * gridRes + ix1];
			gx = (h10 - h00) * (1.0f - v) + (h11 - h01) * v;
			gz = (h01 - h00) * (1.0f - u) + (h11 - h10) * u;
		};

		std::mt19937 rng(42);
		std::uniform_real_distribution<float> randX(0.0f, (float)(gridRes - 2));
		
		for (int d = 0; d < numDrops; d++) {
			if (progress && (d % 10000 == 0 || d == numDrops - 1)) {
				float pct = ((float)(d + 1) / numDrops) * 100.0f;
				progress(pct, "Droplet Erosion... " + std::to_string(d + 1) + "/" + std::to_string(numDrops));
			}

			float posX = randX(rng);
			float posZ = randX(rng);
			float dirX = 0.0f;
			float dirZ = 0.0f;
			float speed = 1.0f;
			float water = rainRate;
			float sediment = 0.0f;

			for (int step = 0; step < maxLifetime; step++) {
				int ix = (int)posX;
				int iz = (int)posZ;
				float u = posX - ix;
				float v = posZ - iz;
				
				float gx, gz;
				GetGradient(posX, posZ, gx, gz);

				dirX = (dirX * 0.1f - gx * 0.9f);
				dirZ = (dirZ * 0.1f - gz * 0.9f);
				
				float len = std::sqrt(dirX * dirX + dirZ * dirZ);
				if (len != 0.0f) {
					dirX /= len;
					dirZ /= len;
				}
				
				posX += dirX;
				posZ += dirZ;

				if (posX < 0.0f || posX >= gridRes - 1 || posZ < 0.0f || posZ >= gridRes - 1)
					break; 

				float newH = GetHeight(posX, posZ);
				float deltaH = newH - GetHeight(posX - dirX, posZ - dirZ);

				float capacity = std::max(-deltaH, 0.01f) * speed * water * dropCapacity;

				if (sediment > capacity || deltaH > 0.0f) {
					float depositAmount = (deltaH > 0.0f) ? std::min(deltaH, sediment) : (sediment - capacity) * dropDeposition;
					sediment -= depositAmount;
					
					hmap[iz * gridRes + ix] += depositAmount * (1.0f - u) * (1.0f - v);
					hmap[iz * gridRes + ix + 1] += depositAmount * u * (1.0f - v);
					hmap[(iz + 1) * gridRes + ix] += depositAmount * (1.0f - u) * v;
					hmap[(iz + 1) * gridRes + ix + 1] += depositAmount * u * v;
				} else {
					float erodeAmount = std::min((capacity - sediment) * dropErosion, -deltaH);
					sediment += erodeAmount;
					
					hmap[iz * gridRes + ix] -= erodeAmount * (1.0f - u) * (1.0f - v);
					hmap[iz * gridRes + ix + 1] -= erodeAmount * u * (1.0f - v);
					hmap[(iz + 1) * gridRes + ix] -= erodeAmount * (1.0f - u) * v;
					hmap[(iz + 1) * gridRes + ix + 1] -= erodeAmount * u * v;
				}

				speed = std::sqrt(std::max(0.0f, speed * speed + deltaH * 1.0f));
				water *= (1.0f - dropEvap);
				if (water < 0.01f) break;
			}
		}

		for (int z = 0; z < gridRes; z++)
			for (int x = 0; x < gridRes; x++)
				data.vertices[(z * gridRes + x) * 14 + 1] = hmap[z * gridRes + x];

		// Post-erosion Gaussian smoothing — eliminates remaining jagged spikes
		if (smoothPasses > 0)
		{
			std::vector<float> heightBuffer(gridRes * gridRes);
			for (int pass = 0; pass < smoothPasses; pass++)
			{
				// Extract heights
				for (int i = 0; i < gridRes * gridRes; i++)
					heightBuffer[i] = data.vertices[i * 14 + 1];

				// 3x3 Gaussian kernel (approximation: 1-2-1 / 4)
				for (int z = 1; z < gridRes - 1; z++)
				{
					for (int x = 1; x < gridRes - 1; x++)
					{
						int idx = z * gridRes + x;
						float center = heightBuffer[idx] * 4.0f;
						float cross = heightBuffer[idx - 1] + heightBuffer[idx + 1]
							+ heightBuffer[idx - gridRes] + heightBuffer[idx + gridRes];
						float diag = heightBuffer[idx - gridRes - 1] + heightBuffer[idx - gridRes + 1]
							+ heightBuffer[idx + gridRes - 1] + heightBuffer[idx + gridRes + 1];
						data.vertices[idx * 14 + 1] = (center + cross * 2.0f + diag) / 16.0f;
					}
				}

				if (progress) {
					progress(100.0f, "Smoothing... Pass " + std::to_string(pass + 1) + "/" + std::to_string(smoothPasses));
				}
			}
		}

		// Recompute Normals
		RecomputeNormals(data, gridRes - 1, gridRes - 1); // Resolution is gridRes - 1

		outputs[0].data = inputData; // Keep source objects, materials etc.
		outputs[0].data.meshData = data;
	}
}

void HydraulicErosionNode::RecomputeNormals(MeshData& data, int resX, int resZ)
{
	const int stride = 14;
	int gridW = resX + 1; // Number of vertices per row
	int gridH = resZ + 1; // Number of vertices per column
	int totalVerts = gridW * gridH;

	// Zero out all normals
	for (int i = 0; i < totalVerts; i++)
	{
		int base = i * stride;
		data.vertices[base + 5] = 0.0f; // nx
		data.vertices[base + 6] = 0.0f; // ny
		data.vertices[base + 7] = 0.0f; // nz
	}

	// Accumulate face normals from the index buffer (triangles)
	for (size_t i = 0; i + 2 < data.indices.size(); i += 3)
	{
		unsigned int i0 = data.indices[i];
		unsigned int i1 = data.indices[i + 1];
		unsigned int i2 = data.indices[i + 2];

		int b0 = i0 * stride;
		int b1 = i1 * stride;
		int b2 = i2 * stride;

		glm::vec3 v0(data.vertices[b0], data.vertices[b0 + 1], data.vertices[b0 + 2]);
		glm::vec3 v1(data.vertices[b1], data.vertices[b1 + 1], data.vertices[b1 + 2]);
		glm::vec3 v2(data.vertices[b2], data.vertices[b2 + 1], data.vertices[b2 + 2]);

		glm::vec3 e1 = v1 - v0;
		glm::vec3 e2 = v2 - v0;
		glm::vec3 faceNormal = glm::cross(e1, e2); // Unnormalized — area-weighted

		// Accumulate to all 3 vertices
		for (int j = 0; j < 3; j++)
		{
			data.vertices[b0 + 5 + j] += faceNormal[j];
			data.vertices[b1 + 5 + j] += faceNormal[j];
			data.vertices[b2 + 5 + j] += faceNormal[j];
		}
	}

	// Normalize all normals and recompute tangent/bitangent — MULTITHREADED
	unsigned int numThreads = std::thread::hardware_concurrency();
	if (numThreads == 0) numThreads = 4;
	if (totalVerts > 1000 && numThreads > 1)
	{
		if (numThreads > (unsigned int)totalVerts) numThreads = (unsigned int)totalVerts;
		std::vector<std::thread> threads;
		int perThread = totalVerts / numThreads;

		for (unsigned int t = 0; t < numThreads; t++)
		{
			int startV = t * perThread;
			int endV = (t == numThreads - 1) ? totalVerts : (t + 1) * perThread;

			threads.emplace_back([&data, stride, startV, endV]() {
				for (int i = startV; i < endV; i++)
				{
					int base = i * stride;
					glm::vec3 n(data.vertices[base + 5], data.vertices[base + 6], data.vertices[base + 7]);
					float len = glm::length(n);
					if (len > 0.0001f) n /= len;
					else n = glm::vec3(0.0f, 1.0f, 0.0f);

					data.vertices[base + 5] = n.x;
					data.vertices[base + 6] = n.y;
					data.vertices[base + 7] = n.z;

					glm::vec3 ref = (std::abs(n.y) < 0.999f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
					glm::vec3 tangent = glm::normalize(ref - n * glm::dot(ref, n));
					glm::vec3 bitangent = glm::cross(n, tangent);

					data.vertices[base + 8] = tangent.x;
					data.vertices[base + 9] = tangent.y;
					data.vertices[base + 10] = tangent.z;
					data.vertices[base + 11] = bitangent.x;
					data.vertices[base + 12] = bitangent.y;
					data.vertices[base + 13] = bitangent.z;
				}
			});
		}
		for (auto& th : threads) th.join();
	}
	else
	{
		for (int i = 0; i < totalVerts; i++)
		{
			int base = i * stride;
			glm::vec3 n(data.vertices[base + 5], data.vertices[base + 6], data.vertices[base + 7]);
			float len = glm::length(n);
			if (len > 0.0001f) n /= len;
			else n = glm::vec3(0.0f, 1.0f, 0.0f);

			data.vertices[base + 5] = n.x;
			data.vertices[base + 6] = n.y;
			data.vertices[base + 7] = n.z;

			glm::vec3 ref = (std::abs(n.y) < 0.999f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
			glm::vec3 tangent = glm::normalize(ref - n * glm::dot(ref, n));
			glm::vec3 bitangent = glm::cross(n, tangent);

			data.vertices[base + 8] = tangent.x;
			data.vertices[base + 9] = tangent.y;
			data.vertices[base + 10] = tangent.z;
			data.vertices[base + 11] = bitangent.x;
			data.vertices[base + 12] = bitangent.y;
			data.vertices[base + 13] = bitangent.z;
		}
	}
}
