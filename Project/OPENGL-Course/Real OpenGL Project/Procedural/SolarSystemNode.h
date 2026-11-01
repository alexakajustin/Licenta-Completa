#pragma once

#include "Nodes/NodeGraph.h"
#include <set>
#include <string>

/**
 * @class SolarSystemNode
 * @brief Generates a procedural solar system layout, spawning planetary GameObjects with orbits, scale ratios, and custom speeds.
 */
class SolarSystemNode : public GraphNode
{
public:
	explicit SolarSystemNode(NodeGraph& graph);
	// A sun and planets as Planet objects (their materials and turned rotations survive a re-run)
	void Execute(NodeContext& ctx) override;
	void SaveState(json& j) const override;
	void LoadState(const json& j) override;

	void AddSpawnedObjectName(const std::string& name) { spawnedObjects.insert(name); }

private:
	int planetCount = 5; ///< Number of procedural planets to generate.
	float minRadius = 150.0f; ///< Inner orbit radius boundary.
	float maxRadius = 1500.0f; ///< Outer orbit radius boundary.
	float minScale = 5.0f; ///< Minimum scale multiplier for planets.
	float maxScale = 30.0f; ///< Maximum scale multiplier for planets.
	float timeSpeed = 1.0f; ///< Simulation speed multiplier for orbits.
	int seed = 12345; ///< Random seed.
	bool generateSun = true; ///< If true, creates a central illuminated sun sphere.
	float sunScale = 100.0f; ///< Scale size of the generated sun.

	std::set<std::string> spawnedObjects; ///< Tracks spawned body names for cleanups.
};
