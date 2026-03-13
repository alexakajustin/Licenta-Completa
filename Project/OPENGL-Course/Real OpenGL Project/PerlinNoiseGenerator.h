#pragma once

#include "IGenerator.h"
#include <vector>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Perlin Noise terrain generator.
// Generates a subdivided grid with noise-based Y displacement.
class PerlinNoiseGenerator : public IGenerator
{
public:
	PerlinNoiseGenerator();
	~PerlinNoiseGenerator() override = default;

	std::string GetName() const override { return "Perlin Noise"; }
	void RenderUI() override;
	MeshData Generate(const MeshData* input) override;

private:
	// Configurable parameters
	int gridSize;         // Grid resolution (gridSize x gridSize quads)
	float scale;          // World-space size
	float amplitude;      // Max height displacement
	float frequency;      // Noise sampling frequency
	int octaves;          // Fractal noise octaves
	float persistence;    // Amplitude decay per octave
	float offsetX;        // Sampling offset X
	float offsetZ;        // Sampling offset Z
	int seed;             // Random seed

	// Internal Perlin noise implementation
	int permutation[512];
	void InitPermutation();

	float Fade(float t);
	float Lerp(float a, float b, float t);
	float Grad(int hash, float x, float y);
	float PerlinNoise2D(float x, float y);
	float FractalNoise(float x, float y);
};
