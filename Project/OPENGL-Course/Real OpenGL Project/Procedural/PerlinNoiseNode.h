#pragma once

#include "Nodes/NodeGraph.h"
#include "Nodes/NodeRandom.h"
#include "Procedural/PerlinNoiseGenerator.h"

// Displaces a mesh with fractal Perlin noise (terrain from a plane)
class PerlinNoiseNode : public GraphNode
{
public:
	explicit PerlinNoiseNode(NodeGraph& graph) : GraphNode(graph), seed(NodeRandom::TrueRandomSeed())
	{
		AddInput(PinType::Mesh, "Mesh", "mesh");
		AddOutput(PinType::Mesh, "Mesh", "mesh");
		AddFloat("amplitude", "Amplitude", &amplitude, 0.5f, 0.0f, 10000.0f, "%.1f").Tip("Height of the noise, in world units");
		AddFloat("frequency", "Frequency", &frequency, 0.01f, 0.0001f, 10.0f).Tip("Higher: smaller, more frequent hills");
		AddInt("octaves", "Octaves", &octaves, 0.1f, 1, 12).Tip("Layers of finer detail");
		AddFloat("persistence", "Persistence", &persistence, 0.01f, 0.01f, 1.0f, "%.3f").Tip("How much each finer layer keeps of the previous one's height");
		AddBool("ridged", "Ridged", &ridged).Tip("Sharp ridges (mountains) instead of rounded hills");
		AddBool("useNormalDisplacement", "Along normals", &useNormalDisplacement).Tip("Displace along the surface normals instead of straight up");
		AddFloat("offsetX", "Offset X", &offsetX, 0.1f);
		AddFloat("offsetZ", "Offset Z", &offsetZ, 0.1f);
		AddSeed("seed", "Seed", &seed);
	}

	void Execute(NodeContext&) override
	{
		const NodeValue& in = In("mesh");
		if (in.mesh.Empty()) return;
		generator.SetAmplitude(amplitude);
		generator.SetFrequency(frequency);
		generator.SetOctaves(octaves);
		generator.SetPersistence(persistence);
		generator.SetRidged(ridged);
		generator.SetUseNormalDisplacement(useNormalDisplacement);
		generator.SetOffset(offsetX, offsetZ);
		generator.SetSeed(seed);

		MeshValue out = in.mesh;
		out.Set(generator.Generate(&in.mesh.Data()));
		Out(0, NodeValue::FromMesh(std::move(out)));
	}

private:
	PerlinNoiseGenerator generator;
	float amplitude = 15.0f;
	float frequency = 0.028f;
	int octaves = 10;
	float persistence = 1.0f;
	bool ridged = false;
	bool useNormalDisplacement = false;
	float offsetX = 0.0f, offsetZ = 0.0f;
	int seed;
};
