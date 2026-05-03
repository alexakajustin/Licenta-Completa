#pragma once

#include "NodeGraph.h"
#include "SceneManager.h"
#include "imgui.h"
#include <string>
#include <vector>

// =====================================================================
// CityGridNode — Procedural City Road Grid & Plot Layout Generator
//
// Inspired by 3DWorld (Frank Gennari) city_gen.cpp road grid algorithm.
// Generates a road grid with intersections, sidewalks, and building plots.
//
// Inputs:
//   [0] Surface (Mesh) — optional terrain to place city on
//
// Outputs:
//   [0] Roads (Mesh) — combined road + intersection + sidewalk geometry
//   [1] Plots (TransformList) — building plot center positions + extents
// =====================================================================

// Internal data for a city plot (block between roads)
struct CityPlot
{
	glm::vec3 center;
	glm::vec2 size; // width (x) and depth (z)
	bool isResidential = true;
	bool isPark = false;
	int gridX = 0, gridY = 0;
};

// Internal data for a road segment
struct RoadSegment
{
	glm::vec3 start, end;
	float width;
	bool dimX; // true = runs along X axis, false = Z axis
};

// Internal data for an intersection
struct RoadIntersection
{
	glm::vec3 center;
	float size; // square intersection size (= road width)
	int numConnections; // 2, 3, or 4 way
	uint8_t connectionMask; // bit flags: -X, +X, -Z, +Z
};

class CityGridNode : public GraphNode
{
public:
	CityGridNode(NodeGraph& graph);

	void RenderContent(SceneManager* scene) override;
	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override;

	json Serialize() const override;
	void Deserialize(const json& j) override;

private:
	// City Parameters
	float citySize = 100.0f;          // Total city extent (square, world units)
	float roadWidth = 2.0f;           // Width of each road
	float roadSpacingX = 20.0f;       // Distance between roads in X
	float roadSpacingZ = 20.0f;       // Distance between roads in Z
	float sidewalkWidth = 0.4f;       // Sidewalk width on each side of road
	float roadHeight = 0.02f;         // Slight elevation above terrain
	float buildingSetback = 1.0f;     // How far buildings sit back from roads
	float residentialProbability = 0.5f; // 0..1, probability of residential vs commercial
	int parkRate = 8;                 // 1 in N plots become parks (0 = no parks)
	int seed = 42;                    // Random seed

	// Texture tiling
	float roadTexScale = 1.0f;
	float sidewalkTexScale = 2.0f;

	// Generated data (cached for output)
	std::vector<CityPlot> plots;
	std::vector<RoadSegment> roadSegs;
	std::vector<RoadIntersection> intersections;

	// Mesh generation helpers
	void GenerateGrid();
	void BuildRoadMesh(MeshData& output);
	void AddRoadQuad(MeshData& mesh, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
		float uMin, float uMax, float vMin, float vMax);
	void AddSidewalkStrip(MeshData& mesh, glm::vec3 roadEdgeStart, glm::vec3 roadEdgeEnd,
		float width, bool side, float texScale);
	void BuildIntersectionQuad(MeshData& mesh, const RoadIntersection& isec);
	void BuildPlotTransforms(TransformList& output);

	// Terrain height sampling (if surface mesh provided)
	float SampleTerrainHeight(const MeshData& terrain, float x, float z);
};
