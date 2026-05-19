#pragma once

#include "NodeGraph.h"
#include "SceneManager.h"
#include "InteriorStructure.h"
#include "imgui.h"
#include <string>
#include <vector>
#include <random>

// =====================================================================
// InteriorGenNode — Procedural Building Interior Generator
//
// Takes building plot data and generates fully detailed interiors:
//   - Recursive floorplan subdivision (rooms, hallways)
//   - Door placement with hinge-swing collision avoidance
//   - Wall/window mullion alignment
//   - Modular room decoration (furniture, fixtures, props)
//   - Material-batched mega-mesh output for minimal draw calls
//
// Inputs:
//   [0] Plots (TransformList) — building positions + sizes from CityGridNode
//
// Outputs:
//   (none — spawns GameObjects directly, matching BuildingGenNode pattern)
// =====================================================================

class InteriorGenNode : public GraphNode
{
public:
	InteriorGenNode(NodeGraph& graph);

	void RenderContent(SceneManager* scene) override;
	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override;

	json Serialize() const override;
	void Deserialize(const json& j) override;

private:
	// Generation parameters (exposed in ImGui)
	float floorHeight    = 3.0f;    // Height per floor
	float wallThickness  = 0.15f;   // Interior wall thickness
	float floorThick     = 0.1f;    // Floor/ceiling slab thickness
	float minRoomArea    = 1.0f;    // Minimum room area before stopping subdivision
	float doorWidth      = 0.8f;    // Standard door width
	float doorHeight     = 2.1f;    // Standard door height
	float hallwayWidth   = 2.0f;    // Central hallway width
	float wallInset      = 0.5f;    // Building shell inset (must match BuildingGenNode)
	int   seed           = 42;
	bool  generateFurniture = false; // Toggle furniture decoration
	float singleWidth    = 20.0f;   // Stanadalone building width (when no plots are connected)
	float singleDepth    = 20.0f;   // Standalone building depth (when no plots are connected)
	bool  generateWalls  = true;    // Toggle interior wall drawing
	bool  generateCeiling = false;  // Toggle ceiling slab drawing
	int   numBedrooms    = 1;
	int   numBathrooms   = 1;
	int   numKitchens    = 1;
	int   numLivingRooms = 1;
	float minRoomSize    = 3.0f;
	float maxRoomSize    = 6.0f;

	// Core generation pipeline
	BuildingInterior GenerateBuildingInterior(
		const TransformData& plot, std::mt19937& rng) const;

	void SubdivideFloor(
		BuildingInterior& interior,
		glm::vec3 floorMin, glm::vec3 floorMax,
		int floorIndex, bool isCommercial,
		std::mt19937& rng,
		int targetRooms) const;

	void AssignRoomTypes(
		BuildingInterior& interior,
		bool isCommercial,
		std::mt19937& rng) const;

	void PlaceDoors(
		BuildingInterior& interior,
		std::mt19937& rng) const;

	// Mesh generation helpers
	MeshData MakeWallBox(glm::mat4 plotMat, glm::vec3 center, glm::vec3 halfExtents, float uvScale = 1.0f) const;
	void BuildStructuralMesh(
		const BuildingInterior& interior,
		glm::mat4 plotMat,
		std::map<int, MeshData>& meshBuckets) const;
};
