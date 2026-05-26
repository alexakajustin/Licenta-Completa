#pragma once

#include "Nodes/Operation.h"
#include "Nodes/OperationRegistry.h"
#include "Simulation/FluidSimulation.h"
#include <cmath>

// =====================================================================
//  MeshOp_HydraulicErosion — Composable hydraulic erosion operation
// =====================================================================

class MeshOp_HydraulicErosion : public Operation
{
public:
	std::string GetName() const override { return "Hydraulic Erosion"; }
	std::string GetCategory() const override { return "Nature"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Int("Steps", 120, 1, 1000),
			ParamDef::Float("Rain Rate", 0.05f, 0.0f, 0.5f),
			ParamDef::Float("Sediment Capacity", 40.0f, 1.0f, 200.0f),
			ParamDef::Float("Ks (Dissolve)", 0.08f, 0.0f, 0.5f),
			ParamDef::Float("Kd (Deposit)", 0.08f, 0.0f, 0.5f),
			ParamDef::Float("Evaporation", 0.002f, 0.0f, 0.1f),
			ParamDef::Float("Max Delta", 2.0f, 0.01f, 10.0f)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty()) return;

		int totalVerts = ctx.mesh.GetVertexCount();
		int gridRes = (int)std::sqrt(totalVerts);

		// Must be a square grid for this operation
		if (gridRes * gridRes != totalVerts) return;

		int steps = GetInt("Steps");
		float rain = GetFloat("Rain Rate");
		float cap = GetFloat("Sediment Capacity");
		float ks = GetFloat("Ks (Dissolve)");
		float kd = GetFloat("Kd (Deposit)");
		float evap = GetFloat("Evaporation");
		float maxD = GetFloat("Max Delta");

		FluidSimulation sim(gridRes, gridRes);
		sim.rainRate = rain;
		sim.sedimentCapacityConstant = cap;
		sim.dissolvingConstant = ks;
		sim.depositionConstant = kd;
		sim.evaporationConstant = evap;
		sim.maxDelta = maxD;

		// 1. Extract Heightmap
		for (int z = 0; z < gridRes; z++)
		{
			for (int x = 0; x < gridRes; x++)
			{
				int vIndex = (z * gridRes + x) * 14;
				sim.terrain(z, x) = ctx.mesh.vertices[vIndex + 1];
			}
		}

		// 2. Run Sim
		double dt = 0.02; 
		for (int i = 0; i < steps; i++)
		{
			sim.update(dt, true, false);
		}

		// 3. Write back Heightmap
		for (int z = 0; z < gridRes; z++)
		{
			for (int x = 0; x < gridRes; x++)
			{
				int vIndex = (z * gridRes + x) * 14;
				ctx.mesh.vertices[vIndex + 1] = sim.terrain(z, x);
			}
		}

		// 4. Recalculate Normals (Basic grid normal update)
		UpdateGridNormals(ctx.mesh, gridRes);
	}

private:
	void UpdateGridNormals(MeshData& data, int res)
	{
		// Simple cross-product based normal update for the grid
		for (int z = 0; z < res - 1; z++)
		{
			for (int x = 0; x < res - 1; x++)
			{
				int i0 = (z * res + x) * 14;
				int i1 = (z * res + (x + 1)) * 14;
				int i2 = ((z + 1) * res + x) * 14;

				glm::vec3 v0(data.vertices[i0], data.vertices[i0 + 1], data.vertices[i0 + 2]);
				glm::vec3 v1(data.vertices[i1], data.vertices[i1 + 1], data.vertices[i1 + 2]);
				glm::vec3 v2(data.vertices[i2], data.vertices[i2 + 1], data.vertices[i2 + 2]);

				glm::vec3 n = glm::normalize(glm::cross(v1 - v0, v2 - v0));
				
				// Set normal for v0
				data.vertices[i0 + 5] = n.x;
				data.vertices[i0 + 6] = n.y;
				data.vertices[i0 + 7] = n.z;
			}
		}
	}
};

REGISTER_OPERATION(MeshOp_HydraulicErosion, "Hydraulic Erosion", "Nature")
