#pragma once

#include "Nodes/NodeGraph.h"
#include "Scene/SceneManager.h"
#include "Procedural/InteriorStructure.h"
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

/**
 * @class InteriorGenNode
 * @brief Procedural building interior generator node that divides space recursively to place rooms, hallways, doors, and furniture props.
 */
class InteriorGenNode : public GraphNode
{
public:
	/**
	 * @brief Constructor.
	 * @param graph NodeGraph that owns this node.
	 */
	InteriorGenNode(NodeGraph& graph);

	/**
	 * @brief Renders the editor UI panel for configuring room counts, door sizes, and furniture toggles.
	 * @param scene Pointer to active scene manager.
	 */
	void RenderContent(SceneManager* scene) override;

	/**
	 * @brief Executes the interior layout generation process, producing walls, floors, and furniture GameObjects in the scene.
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

private:
	// Generation parameters (exposed in ImGui)
	float floorHeight    = 3.0f;    ///< Height per floor.
	float wallThickness  = 0.15f;   ///< Thickness of interior walls.
	float floorThick     = 0.1f;    ///< Thickness of floor slabs.
	float doorWidth      = 0.8f;    ///< Width of door openings.
	float doorHeight     = 2.1f;    ///< Height of door openings.
	float hallwayWidth   = 1.8f;    ///< Width of primary transit hallways.
	float wallInset      = 0.5f;    ///< Building envelope setback distance.
	int   seed           = 42;      ///< Random seed.
	bool  generateFurniture = false; ///< If true, spawns furniture models inside decorated rooms.
	float singleWidth    = 20.0f;   ///< Standalone width when executing without city plots.
	float singleDepth    = 20.0f;   ///< Standalone depth when executing without city plots.
	bool  generateWalls  = true;    ///< If true, generates wall meshes.
	bool  generateCeiling = false;  ///< If true, generates ceiling slabs.
	int   numBedrooms    = 1;       ///< Requested bedroom count.
	int   numBathrooms   = 1;       ///< Requested bathroom count.
	int   numKitchens    = 1;       ///< Requested kitchen count.
	int   numLivingRooms = 1;       ///< Requested living room count.

	// Core generation pipeline
	
	/**
	 * @brief Master algorithm running structural and decoration steps.
	 */
	BuildingInterior GenerateBuildingInterior(
		const TransformData& plot, std::mt19937& rng,
		const FurnitureSpecs& furniture) const;

	/**
	 * @brief Computes room sizes dynamically based on required furniture dimensions.
	 */
	glm::vec2 ComputeRoomSize(RoomType type, const FurnitureSpecs& furniture) const;

	/**
	 * @brief Assembles rooms around primary hallway channels.
	 */
	void AssembleFloorplan(
		BuildingInterior& interior,
		glm::vec3 origin, float floorY, float ceilY,
		std::mt19937& rng,
		const FurnitureSpecs& furniture) const;

	/**
	 * @brief Automatically places doors while avoiding wall corners.
	 */
	void PlaceDoors(
		BuildingInterior& interior,
		std::mt19937& rng) const;

	// Mesh generation helpers
	
	/**
	 * @brief Creates a 3D box representing a single wall.
	 */
	MeshData MakeWallBox(glm::mat4 plotMat, glm::vec3 center, glm::vec3 halfExtents, float uvScale = 1.0f) const;
	
	/**
	 * @brief Assembles and optimizes mesh geometry buckets for structural components.
	 */
	void BuildStructuralMesh(
		const BuildingInterior& interior,
		glm::mat4 plotMat,
		std::map<int, MeshData>& meshBuckets) const;
};
