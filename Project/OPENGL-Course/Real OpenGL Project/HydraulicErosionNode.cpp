#include "HydraulicErosionNode.h"
#include "imgui.h"
#include <cmath>
#include <thread>
#include <vector>
#include <random>
#include <iostream>

// --- INSPIRATION PROJECT CONSTANTS ---
const float Kq = 10.0f;        // Sediment capacity factor
const float Kw = 0.001f;       // Water evaporation speed
const float Kr = 0.9f;         // Erosion speed
const float Kd = 0.02f;        // Deposition speed
const float Ki = 0.1f;         // Direction inertia
const float minSlope = 0.05f;
const float g = 20.0f;
const float Kg = g * 2.0f;

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
	ImGui::DragInt("Steps (x1k)", &simulationSteps, 1, 0, 10000);
	ImGui::DragFloat("Erosion (Kr)", &dissolvingConstant, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Deposit (Kd)", &depositionConstant, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("Rain Intensity", &rainRate, 0.01f, 0.0f, 5.0f);
	ImGui::DragInt("Smooth Passes", &smoothPasses, 1, 0, 20);
	ImGui::PopItemWidth();
}

json HydraulicErosionNode::Serialize() const
{
	json j = GraphNode::Serialize();
	j["simulationSteps"] = simulationSteps;
	j["rainRate"] = rainRate;
	j["dissolvingConstant"] = dissolvingConstant;
	j["depositionConstant"] = depositionConstant;
	j["smoothPasses"] = smoothPasses;
	return j;
}

void HydraulicErosionNode::Deserialize(const json& j)
{
	GraphNode::Deserialize(j);
	simulationSteps = j.value("simulationSteps", 250);
	rainRate = j.value("rainRate", 1.0f);
	dissolvingConstant = j.value("dissolvingConstant", 0.9f);
	depositionConstant = j.value("depositionConstant", 0.02f);
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

		int totalVerts = (int)data.vertices.size() / 14;
		int gridRes = (int)std::sqrt(totalVerts);

		if (gridRes * gridRes != totalVerts)
		{
			outputs[0].data = inputData;
			return;
		}

		// === INSPIRATION DROPLET EROSION ===
		int numDrops = simulationSteps * 1000;
		float currentErodeAmount = rainRate; 
		float currentKr = dissolvingConstant;
		float currentKd = depositionConstant;

		std::vector<float> hmap(gridRes * gridRes);
		for (int i = 0; i < gridRes * gridRes; i++)
			hmap[i] = data.vertices[i * 14 + 1];

		std::mt19937 rng(42);
		std::uniform_real_distribution<float> randCoord(1.0f, (float)(gridRes - 3));

		for (int d = 0; d < numDrops; d++) {
			if (progress && (d % 10000 == 0 || d == numDrops - 1)) {
				float pct = ((float)(d + 1) / numDrops) * 100.0f;
				progress(pct, "Inspiration Erosion... " + std::to_string(d + 1) + "/" + std::to_string(numDrops));
			}

			float xp = randCoord(rng);
			float zp = randCoord(rng);
			float dx = 0, dz = 0, s = 0, v = 0, w = 1;
			int xi = (int)xp;
			int zi = (int)zp;
			float h = hmap[zi * gridRes + xi];

			for (int step = 0; step < 100; step++) {
				// Calculate gradient using neighbor bilinear interpolation
				int ix = (int)xp;
				int iz = (int)zp;
				float xf = xp - ix;
				float zf = zp - iz;

				float h00 = hmap[iz * gridRes + ix];
				float h10 = hmap[iz * gridRes + (ix + 1)];
				float h01 = hmap[(iz + 1) * gridRes + ix];
				float h11 = hmap[(iz + 1) * gridRes + (ix + 1)];

				float gx = h00 + h01 - h10 - h11;
				float gz = h00 + h10 - h01 - h11;

				// Next position with inertia
				dx = (dx - gx) * Ki + gx;
				dz = (dz - gz) * Ki + gz;

				float dl = std::sqrt(dx * dx + dz * dz);
				if (dl <= 1e-6f) {
					// Random direction if gradient is zero
					std::uniform_real_distribution<float> randAngle(0, 2.0f * 3.14159f);
					float a = randAngle(rng);
					dx = std::cos(a); dz = std::sin(a);
				} else {
					dx /= dl; dz /= dl;
				}

				float nxp = xp + dx;
				float nzp = zp + dz;
				int nxi = (int)nxp;
				int nzi = (int)nzp;

				if (nxi < 0 || nxi >= gridRes - 1 || nzi < 0 || nzi >= gridRes - 1) break;

				float nxf = nxp - nxi;
				float nzf = nzp - nzi;
				float nh00 = hmap[nzi * gridRes + nxi];
				float nh10 = hmap[nzi * gridRes + (nxi + 1)];
				float nh01 = hmap[(nzi + 1) * gridRes + nxi];
				float nh11 = hmap[(nzi + 1) * gridRes + (nxi + 1)];
				float nh = (nh00 * (1 - nxf) + nh10 * nxf) * (1 - nzf) + (nh01 * (1 - nxf) + nh11 * nxf) * nzf;

				// Deposit/Erode logic from Inspiration project
				if (nh >= h) {
					float ds = (nh - h) + 0.001f;
					if (ds >= s) {
						// Deposit all
						float dep = s;
						hmap[iz * gridRes + ix] += dep * (1 - xf) * (1 - zf);
						hmap[iz * gridRes + ix + 1] += dep * xf * (1 - zf);
						hmap[(iz + 1) * gridRes + ix] += dep * (1 - xf) * zf;
						hmap[(iz + 1) * gridRes + ix + 1] += dep * xf * zf;
						s = 0;
						break;
					}
					float dep = ds;
					hmap[iz * gridRes + ix] += dep * (1 - xf) * (1 - zf);
					hmap[iz * gridRes + ix + 1] += dep * xf * (1 - zf);
					hmap[(iz + 1) * gridRes + ix] += dep * (1 - xf) * zf;
					hmap[(iz + 1) * gridRes + ix + 1] += dep * xf * zf;
					s -= ds;
					v = 0;
				} else {
					float dh = h - nh;
					float slope = dh;
					float q = std::max(slope, minSlope) * v * w * Kq;
					float ds = s - q;

					if (ds >= 0) { // Deposit
						ds *= currentKd;
						float dep = ds;
						hmap[iz * gridRes + ix] += dep * (1 - xf) * (1 - zf);
						hmap[iz * gridRes + ix + 1] += dep * xf * (1 - zf);
						hmap[(iz + 1) * gridRes + ix] += dep * (1 - xf) * zf;
						hmap[(iz + 1) * gridRes + ix + 1] += dep * xf * zf;
						s -= ds;
					} else { // Erode with 4x4 Radius Kernel
						ds *= -currentKr;
						ds = std::min(ds, dh * 0.99f);
						
						for (int rz = zi - 1; rz <= zi + 2; rz++) {
							for (int rx = xi - 1; rx <= xi + 2; rx++) {
								if (rx < 0 || rx >= gridRes || rz < 0 || rz >= gridRes) continue;
								float xo = (float)rx - xp;
								float zo = (float)rz - zp;
								float weight = 1.0f - (xo * xo + zo * zo) * 0.25f;
								if (weight <= 0) continue;
								weight *= 0.159154943f; // 1/(2*PI) normalization
								hmap[rz * gridRes + rx] -= ds * currentErodeAmount * weight;
							}
						}
						dh -= ds;
						s += ds;
					}
					v = std::sqrt(v * v + Kg * dh);
					w *= (1.0f - Kw);
				}

				xp = nxp; zp = nzp; xi = nxi; zi = nzi; xf = nxf; zf = nzf;
				h = nh;
			}
		}

		for (int i = 0; i < gridRes * gridRes; i++)
			data.vertices[i * 14 + 1] = hmap[i];

		// Smoothing and normal recomputation remains the same
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
		outputs[0].data = inputData;
		outputs[0].data.meshData = data;
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
		glm::vec3 ref = (std::abs(n.y) < 0.999f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
		glm::vec3 tangent = glm::normalize(ref - n * glm::dot(ref, n));
		glm::vec3 bitangent = glm::cross(n, tangent);
		data.vertices[base + 8] = tangent.x; data.vertices[base + 9] = tangent.y; data.vertices[base + 10] = tangent.z;
		data.vertices[base + 11] = bitangent.x; data.vertices[base + 12] = bitangent.y; data.vertices[base + 13] = bitangent.z;
	}
}
