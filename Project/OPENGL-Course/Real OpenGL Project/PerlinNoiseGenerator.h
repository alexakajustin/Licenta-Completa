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

	void SetOffset(float x, float z) { offsetX = x; offsetZ = z; }

	// Getters for serialization
	float GetFrequency() const { return frequency; }
	float GetAmplitude() const { return amplitude; }
	int GetOctaves() const { return octaves; }
	float GetPersistence() const { return persistence; }
	int GetSeed() const { return seed; }

	// Setters for serialization
	void SetFrequency(float f) { frequency = f; }
	void SetAmplitude(float a) { amplitude = a; }
	void SetOctaves(int o) { octaves = o; }
	void SetPersistence(float p) { persistence = p; }
	void SetSeed(int s) { seed = s; InitPermutation(); }
	void SetGridSize(int g) { gridSize = g; }
	void SetScale(float s) { scale = s; }

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
	bool useNormalDisplacement; // If true, displace along normal instead of just Y

	// Internal Perlin noise implementation
	int permutation[512];
	void InitPermutation();

	float Fade(float t);
	float Lerp(float a, float b, float t);
	float Grad(int hash, float x, float y);
	float PerlinNoise2D(float x, float y);
	float FractalNoise(float x, float y);
};
