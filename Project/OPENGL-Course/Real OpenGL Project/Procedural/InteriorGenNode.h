#pragma once

#include "Nodes/NodeGraph.h"
#include "Scene/SceneManager.h"
#include "Procedural/InteriorStructure.h"
#include "imgui.h"
#include <string>
#include <vector>
#include <map>
#include <random>

// =====================================================================
// InteriorGenNode — Procedural Building Interior Generator
//
// Takes building plots (or a single standalone footprint) and generates interiors:
//   - Recursive floorplan subdivision (rooms, hallways)
//   - Door placement with hinge-swing collision avoidance
//   - Wall/window mullion alignment
//   - Room decoration with furniture
//   - Material-batched meshes for few draw calls
//
// Inputs:
//   Plots      (Points)  building positions + sizes, e.g. from City Grid
//   Furniture  (Objects) any number of furniture objects. Each is sorted into a category (bed,
//              desk, TV, stove...) by its label, or by its name, or by the category table on the
//              node; a category with several objects picks one per placement (variety).
// =====================================================================

/**
 * @class InteriorGenNode
 * @brief Procedural building interior generator node that divides space recursively to place rooms, hallways, doors, and furniture props.
 */
class InteriorGenNode : public GraphNode
{
public:
	explicit InteriorGenNode(NodeGraph& graph);
	void Execute(NodeContext& ctx) override;
	void RenderContent(SceneManager* scene) override;
	void SaveState(json& j) const override;
	void LoadState(const json& j) override;

	// Older saves: plots, then one mesh pin per furniture category
	std::vector<std::string> LegacyInputKeys() const override;

	// The furniture categories (key, label)
	static const std::vector<std::pair<std::string, std::string>>& Categories();
	// Objects assigned to a category by hand (or by the conversion of an older save)
	void SetFurnitureOverride(const std::string& category, const std::vector<std::string>& objects) { furnitureOverrides[category] = objects; }

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

	std::map<std::string, std::vector<std::string>> furnitureOverrides; ///< Category -> objects, set by hand
	std::map<std::string, std::vector<std::string>> lastAssignment;     ///< What the last run used, for the UI

	// Category -> furniture objects: the table first, then labels, then names
	std::map<std::string, std::vector<GameObject*>> ResolveFurniture(NodeContext& ctx);

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
