#include "HydraulicErosionNode.h"
#include "FluidSimulation.h"
#include "imgui.h"
#include <cmath>

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
	return j;
}

void HydraulicErosionNode::Deserialize(const json& j)
{
	GraphNode::Deserialize(j);
	simulationSteps = j.value("simulationSteps", 100);
	rainRate = j.value("rainRate", 0.05f);
	sedimentCapacity = j.value("sedimentCapacity", 40.0f);
	dissolvingConstant = j.value("dissolvingConstant", 0.08f);
	depositionConstant = j.value("depositionConstant", 0.08f);
	evaporationConstant = j.value("evaporationConstant", 0.0001f);
	maxDelta = j.value("maxDelta", 2.0f);
}

void HydraulicErosionNode::Execute(SceneManager& scene)
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

		FluidSimulation sim(gridRes, gridRes);

		// Apply user settings
		sim.rainRate = rainRate;
		sim.sedimentCapacityConstant = sedimentCapacity;
		sim.dissolvingConstant = dissolvingConstant;
		sim.depositionConstant = depositionConstant;
		sim.evaporationConstant = evaporationConstant;
		sim.maxDelta = maxDelta;

		// Extract Y coordinates to simulator terrain
		for (int z = 0; z < gridRes; z++)
		{
			for (int x = 0; x < gridRes; x++)
			{
				int vIndex = (z * gridRes + x) * 14;
				sim.terrain(z, x) = data.vertices[vIndex + 1]; // Y
			}
		}

		// Run Euler simulation
		double dt = 0.016; // Fixed timestep approx 60fps
		for (int i = 0; i < simulationSteps; i++)
		{
			sim.update(dt, true, false);
		}

		// Push back Y coordinates
		for (int z = 0; z < gridRes; z++)
		{
			for (int x = 0; x < gridRes; x++)
			{
				int vIndex = (z * gridRes + x) * 14;
				data.vertices[vIndex + 1] = sim.terrain(z, x);
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

	// Normalize all normals and recompute tangent/bitangent
	for (int i = 0; i < totalVerts; i++)
	{
		int base = i * stride;
		glm::vec3 n(data.vertices[base + 5], data.vertices[base + 6], data.vertices[base + 7]);
		float len = glm::length(n);
		if (len > 0.0001f)
		{
			n /= len;
		}
		else
		{
			n = glm::vec3(0.0f, 1.0f, 0.0f);
		}
		data.vertices[base + 5] = n.x;
		data.vertices[base + 6] = n.y;
		data.vertices[base + 7] = n.z;

		// Recompute tangent from normal (use Gram-Schmidt with a reference direction)
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
