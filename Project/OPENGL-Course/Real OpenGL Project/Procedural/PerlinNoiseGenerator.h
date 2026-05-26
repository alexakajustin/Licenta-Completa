#pragma once

#include "Nodes/IGenerator.h"
#include <vector>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Perlin Noise terrain generator.
// Generates a subdivided grid with noise-based Y displacement.
/**
 * @class PerlinNoiseGenerator
 * @brief Procedural terrain heightmap generator using 2D Perlin noise and fractal octaves.
 */
class PerlinNoiseGenerator : public IGenerator
{
public:
	/**
	 * @brief Constructor setting default grid resolution, octaves, and amplitudes.
	 */
	PerlinNoiseGenerator();

	~PerlinNoiseGenerator() override = default;

	std::string GetName() const override { return "Perlin Noise"; }
	void RenderUI() override;

	/**
	 * @brief Processes the input mesh data, displacing vertices based on 2D fractal noise.
	 * @param input Optional input MeshData.
	 * @return Modified or newly generated MeshData.
	 */
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

	void SetRidged(bool r) { useRidged = r; }
	bool GetRidged() const { return useRidged; }

	float GetOffsetX() const { return offsetX; }
	float GetOffsetZ() const { return offsetZ; }
	bool GetUseNormalDisplacement() const { return useNormalDisplacement; }
	void SetUseNormalDisplacement(bool u) { useNormalDisplacement = u; }

private:
	// Configurable parameters
	int gridSize;         ///< Grid resolution size (gridSize x gridSize quads).
	float scale;          ///< World-space scale dimensions of the plane.
	float amplitude;      ///< Maximum height displacement magnitude.
	float frequency;      ///< Noise sampling scale frequency.
	int octaves;          ///< Total number of summation octaves.
	float persistence;    ///< Amplitude damping factor per octave.
	float offsetX;        ///< Coordinate X sampling offset.
	float offsetZ;        ///< Coordinate Z sampling offset.
	int seed;             ///< Randomization seed.
	bool useNormalDisplacement; ///< If true, vertices displace along normal directions rather than strictly Y.
	bool useRidged;             ///< If true, outputs ridged multifractal noise (1.0 - abs(noise)).

	// Internal Perlin noise implementation
	int permutation[512]; ///< Permutation lookup table for gradient selection.
	
	/**
	 * @brief Pre-calculates the permutation table from the active seed.
	 */
	void InitPermutation();

	float Fade(float t);
	float Lerp(float a, float b, float t);
	float Grad(int hash, float x, float y);
	
	/**
	 * @brief Computes raw single-octave 2D Perlin noise.
	 */
	float PerlinNoise2D(float x, float y);

	/**
	 * @brief Combines multiple octaves to construct the final fractal height offset.
	 */
	float FractalNoise(float x, float y);
};
