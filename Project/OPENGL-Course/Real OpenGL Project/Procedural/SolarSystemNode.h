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
	/**
	 * @brief Constructor setup of center inputs and planetary transform outputs.
	 * @param graph NodeGraph that owns this node.
	 */
	SolarSystemNode(NodeGraph& graph)
	{
		id = graph.NextNodeId();
		title = "Solar System";
		
		// Input 0: Optional surface/center point
		inputs.push_back(Pin(graph.NextPinId(), PinDataType::Mesh, "Center"));
		
		// Output 0: Sun pass-through
		outputs.push_back(Pin(graph.NextPinId(), PinDataType::Mesh, "Sun Output"));
		// Output 1: Planet Transforms
		outputs.push_back(Pin(graph.NextPinId(), PinDataType::TransformList, "Planet Transforms"));
	}

	virtual ~SolarSystemNode() = default;

	/**
	 * @brief Computes orbits and spawns sun and planet GameObjects in the scene.
	 */
	void Execute(class SceneManager& scene, NodeProgressCallback progress = nullptr) override;

	/**
	 * @brief Renders the editor UI panel for configuring orbit boundaries, count of planets, and scaling factors.
	 */
	void RenderContent(class SceneManager* scene = nullptr) override;

	/**
	 * @brief Cleans up spawned system bodies upon node removal.
	 */
	void OnRemove(class SceneManager& scene) override;

	json Serialize() const override;
	void Deserialize(const json& j) override;

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
