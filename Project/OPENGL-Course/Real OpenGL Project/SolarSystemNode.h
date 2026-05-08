#pragma once

#include "NodeGraph.h"
#include <set>
#include <string>

class SolarSystemNode : public GraphNode
{
public:
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

	void Execute(class SceneManager& scene, NodeProgressCallback progress = nullptr) override;
	void RenderContent(class SceneManager* scene = nullptr) override;
	void OnRemove(class SceneManager& scene) override;

	json Serialize() const override;
	void Deserialize(const json& j) override;

	// Keep track of spawned objects for cleanup
	void AddSpawnedObjectName(const std::string& name) { spawnedObjects.insert(name); }

private:
	int planetCount = 5;
	float minRadius = 150.0f;
	float maxRadius = 1500.0f;
	float minScale = 5.0f;
	float maxScale = 30.0f;
	float timeSpeed = 1.0f;
	int seed = 12345;
	bool generateSun = true;
	float sunScale = 100.0f;

	std::set<std::string> spawnedObjects;
};
