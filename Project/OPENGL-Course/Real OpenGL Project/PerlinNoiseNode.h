#pragma once

#include "NodeGraph.h"
#include "PerlinNoiseGenerator.h"

// Applies Perlin noise displacement to an input mesh.
// Input: Mesh, Output: Mesh
class PerlinNoiseNode : public GraphNode
{
public:
	PerlinNoiseNode(NodeGraph& graph)
	{
		id = graph.NextNodeId();
		title = "Perlin Noise";

		// One input: Mesh
		Pin meshIn(graph.NextPinId(), PinDataType::Mesh, "Mesh");
		inputs.push_back(meshIn);

		// One output: Mesh
		Pin meshOut(graph.NextPinId(), PinDataType::Mesh, "Mesh");
		outputs.push_back(meshOut);
	}

	void RenderContent(SceneManager* scene) override
	{
		generator.RenderUI();
	}

	void Execute(SceneManager& scene) override
	{
		// Process input
		MeshData* inputMesh = nullptr;
		if (inputs[0].data.type == PinDataType::Mesh)
		{
			inputMesh = &inputs[0].data.meshData;
		}

		MeshData result = generator.Generate(inputMesh);
		
		outputs[0].data.type = PinDataType::Mesh;
		outputs[0].data.meshData = result;
	}

private:
	PerlinNoiseGenerator generator;
};
