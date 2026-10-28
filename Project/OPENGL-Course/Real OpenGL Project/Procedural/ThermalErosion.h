#pragma once

// Thermal erosion (talus / material slippage) on a square heightmap.
//
// Wherever the drop to a neighbour is steeper than the talus angle (the angle of repose of loose
// material, ~30-40 degrees in nature), part of the excess slides down to the lower neighbours.
// Iterated, this turns vertical walls and knife-edge fins into scree slopes at the talus angle,
// while slopes already below it are left untouched. Mass-conserving.
//
// Works in real units: cellSize is the horizontal grid spacing in the same units as the heights,
// so the talus angle is an actual slope angle.
//
// Header-only so it can be unit-tested / previewed outside the engine.

#include <vector>
#include <cmath>
#include <algorithm>
#include <functional>

inline void RunThermalErosion(std::vector<float>& heights, int res, float cellSize, float talusAngleDeg,
	int iterations, const std::function<void(float)>& progress = nullptr)
{
	if (res < 3 || (int)heights.size() != res * res || iterations <= 0 || cellSize <= 0.0f) return;

	static const int offX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	static const int offZ[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
	static const float dist[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.41421356f, 1.41421356f, 1.41421356f, 1.41421356f };

	const float tanTalus = std::tan(std::clamp(talusAngleDeg, 1.0f, 89.0f) * 3.14159265f / 180.0f);
	float threshold[8];
	for (int k = 0; k < 8; k++) threshold[k] = tanTalus * cellSize * dist[k];

	std::vector<float> delta(heights.size());
	for (int it = 0; it < iterations; it++)
	{
		if (progress && (it % 8 == 0)) progress((float)it / (float)iterations);
		std::fill(delta.begin(), delta.end(), 0.0f);

		for (int z = 0; z < res; z++)
		{
			for (int x = 0; x < res; x++)
			{
				int i = z * res + x;
				float hc = heights[i];
				float excess[8];
				float excessSum = 0.0f, excessMax = 0.0f;
				for (int k = 0; k < 8; k++)
				{
					int nx = x + offX[k], nz = z + offZ[k];
					excess[k] = 0.0f;
					if (nx < 0 || nx >= res || nz < 0 || nz >= res) continue;
					float e = hc - heights[nz * res + nx] - threshold[k];
					if (e > 0.0f) {
						excess[k] = e;
						excessSum += e;
						excessMax = std::max(excessMax, e);
					}
				}
				if (excessSum <= 0.0f) continue;

				// Move half of the largest excess (never overshoots below the talus slope),
				// shared between the unstable neighbours in proportion to their excess.
				float move = excessMax * 0.5f;
				delta[i] -= move;
				for (int k = 0; k < 8; k++)
				{
					if (excess[k] > 0.0f)
						delta[(z + offZ[k]) * res + (x + offX[k])] += move * excess[k] / excessSum;
				}
			}
		}

		for (size_t i = 0; i < heights.size(); i++) heights[i] += delta[i];
	}
	if (progress) progress(1.0f);
}
