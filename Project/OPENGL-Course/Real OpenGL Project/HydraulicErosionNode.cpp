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
	int stripStride = 14;
	
	for (int z = 0; z < resZ - 1; z++)
	{
		for (int x = 0; x < resX - 1; x++)
		{
			unsigned int v0_idx = (z * resX + x) * stripStride;
			unsigned int v1_idx = (z * resX + (x + 1)) * stripStride;
			unsigned int v2_idx = ((z + 1) * resX + x) * stripStride;

			glm::vec3 v0(data.vertices[v0_idx], data.vertices[v0_idx + 1], data.vertices[v0_idx + 2]);
			glm::vec3 v1(data.vertices[v1_idx], data.vertices[v1_idx + 1], data.vertices[v1_idx + 2]);
			glm::vec3 v2(data.vertices[v2_idx], data.vertices[v2_idx + 1], data.vertices[v2_idx + 2]);

			glm::vec3 e1 = v1 - v0;
			glm::vec3 e2 = v2 - v0;
			glm::vec3 normal = glm::normalize(glm::cross(e1, e2));
			
			for (int i=0; i<3; ++i) {
				data.vertices[v0_idx + 5 + i] = normal[i];
			}
		}
	}
}
