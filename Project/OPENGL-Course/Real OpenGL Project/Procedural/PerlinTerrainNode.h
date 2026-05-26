#pragma once

#include "Nodes/NodeGraph.h"
#include "Procedural/PerlinNoiseGenerator.h"

// Generates terrain mesh using Perlin noise.
// No inputs, one Mesh output.
class PerlinTerrainNode : public GraphNode
{
public:
	PerlinTerrainNode(NodeGraph& graph)
	{
		id = graph.NextNodeId();
		title = "Perlin Terrain";

		// No inputs
		// One output: Mesh
		Pin meshOut(graph.NextPinId(), PinDataType::Mesh, "Mesh");
		outputs.push_back(meshOut);
	}

	void RenderContent(SceneManager* scene) override
	{
		generator.RenderUI();
	}

	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override
	{
		MeshData data = generator.Generate(nullptr);
		outputs[0].data.type = PinDataType::Mesh;
		outputs[0].data.meshData = data;
	}

private:
	PerlinNoiseGenerator generator;
};
