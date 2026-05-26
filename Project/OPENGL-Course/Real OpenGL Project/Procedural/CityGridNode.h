#pragma once

#include "Nodes/NodeGraph.h"
#include "Scene/SceneManager.h"
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

/**
 * @struct CityPlot
 * @brief Represents a block or parcel of land between roads in the city grid layout.
 */
struct CityPlot
{
	glm::vec3 center; ///< Center position vector of the plot.
	glm::vec2 size; ///< Width (x) and depth (z) extents of the plot.
	bool isResidential = true; ///< If true, residential zoning; commercial otherwise.
	bool isPark = false; ///< If true, designated as a public park zone.
	int gridX = 0; ///< X coordinate index in the macro city grid.
	int gridY = 0; ///< Y coordinate index in the macro city grid.
};

/**
 * @struct RoadSegment
 * @brief Represents a linear road section connecting two intersections.
 */
struct RoadSegment
{
	glm::vec3 start; ///< Start 3D coordinate of the segment.
	glm::vec3 end; ///< End 3D coordinate of the segment.
	float width; ///< Width of the road section.
	bool dimX; ///< True if segment runs along the X axis; false if along Z.
};

/**
 * @struct RoadIntersection
 * @brief Represents an intersection joining multiple road segments.
 */
struct RoadIntersection
{
	glm::vec3 center; ///< Center 3D coordinate of the intersection.
	float size; ///< Size of the square intersection boundary (= road width).
	int numConnections; ///< Count of connected road directions (2, 3, or 4-way).
	uint8_t connectionMask; ///< Bitmask flags representing connections (-X, +X, -Z, +Z).
};

/**
 * @class CityGridNode
 * @brief Procedural road grid and plot layout generator node inspired by classic street layouts.
 */
class CityGridNode : public GraphNode
{
public:
	/**
	 * @brief Constructor.
	 * @param graph NodeGraph that owns this node.
	 */
	CityGridNode(NodeGraph& graph);

	/**
	 * @brief Renders the editor UI panel for configuring road parameters.
	 * @param scene Pointer to active scene manager.
	 */
	void RenderContent(SceneManager* scene) override;

	/**
	 * @brief Executes the city grid generator, calculating street segments, intersections, and building plots.
	 * @param scene Active scene manager.
	 * @param progress Callback for updating execution progress.
	 */
	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override;

	/**
	 * @brief Serializes node properties to JSON.
	 * @return JSON object.
	 */
	json Serialize() const override;

	/**
	 * @brief Deserializes node properties from JSON.
	 * @param j JSON object.
	 */
	void Deserialize(const json& j) override;

	// City Parameters (public for template configuration)
	float citySize = 100.0f;          ///< Total extent size of the city grid.
	float roadWidth = 6.0f;           ///< Width of generated roads.
	float roadSpacingX = 30.0f;       ///< Spacing distance between roads along the X axis.
	float roadSpacingZ = 30.0f;       ///< Spacing distance between roads along the Z axis.
	float sidewalkWidth = 1.5f;       ///< Sidewalk border width alongside roads.
	float roadHeight = 0.02f;         ///< Slight elevation offset to avoid z-fighting with terrain.
	float buildingSetback = 1.0f;     ///< Offset distance of buildings from roads.
	float residentialProbability = 0.5f; ///< Zoning probability distribution factor.
	int parkRate = 8;                 ///< Ratio of park placement (1 in N plots).
	int seed = 42;                    ///< Random seed.

private:
	// Texture tiling
	float roadTexScale = 1.0f;
	float sidewalkTexScale = 2.0f;

	// Generated data (cached for output)
	std::vector<CityPlot> plots;
	std::vector<RoadSegment> roadSegs;
	std::vector<RoadIntersection> intersections;

	// Mesh generation helpers
	
	/**
	 * @brief Procedurally calculates coordinates for all streets and plots.
	 */
	void GenerateGrid();

	/**
	 * @brief Combines segments to form road and sidewalk meshes.
	 */
	void BuildRoadMesh(MeshData& roadOutput, MeshData& sidewalkOutput);

	/**
	 * @brief Appends a quad for a single road piece.
	 */
	void AddRoadQuad(MeshData& mesh, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
		float uMin, float uMax, float vMin, float vMax);

	/**
	 * @brief Generates sidewalk border meshes along a road edge.
	 */
	void AddSidewalkStrip(MeshData& mesh, glm::vec3 roadEdgeStart, glm::vec3 roadEdgeEnd,
		float width, bool side, float texScale);

	/**
	 * @brief Generates intersection mesh geometry.
	 */
	void BuildIntersectionQuad(MeshData& mesh, const RoadIntersection& isec);

	/**
	 * @brief Builds debug mesh representations of building plots.
	 */
	void BuildPlotMesh(MeshData& output);

	/**
	 * @brief Generates transformation structures to output building plots to downstream generators.
	 */
	void BuildPlotTransforms(TransformList& output);

	/**
	 * @brief Samples underlying terrain height if a surface mesh is connected.
	 */
	float SampleTerrainHeight(const MeshData& terrain, float x, float z);
};
