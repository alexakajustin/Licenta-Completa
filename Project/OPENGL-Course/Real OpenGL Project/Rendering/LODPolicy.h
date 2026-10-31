#pragma once

#include <algorithm>

/**
 * @namespace LODPolicy
 * @brief Screen-size level of detail, shared by the GPU culler (compute_cull.glsl) and the CPU
 *        path (GameObject::RenderSingle).
 *
 * An object's screen size is the fraction of the screen height covered by its bounding sphere,
 * radius * projection[1][1] / distance — the metric of Unity's LODGroup and Unreal's LOD screen
 * sizes. A 20 m tree therefore keeps its detail ten times further away than a 2 m rock, and a
 * zoomed-in camera keeps detail further away too. The LOD bias (Graphics Settings) scales the
 * measured size; the thresholds below are the same for every pass.
 */
namespace LODPolicy
{
	// Below LOD_SCREEN_SIZE[i] the object uses LOD i + 1
	constexpr float LOD_SCREEN_SIZE[3] = { 0.25f, 0.08f, 0.03f };
	// Below this the object is not drawn at all (about 2 px at 1080p)
	constexpr float CULL_SCREEN_SIZE = 0.002f;
	// Width of the dithered transition band above each threshold, relative to the threshold
	constexpr float CROSSFADE_WIDTH = 0.2f;

	inline float ScreenSize(float radius, float distance, float projScale)
	{
		return radius * projScale / std::max(distance, 1e-4f);
	}

	// Discrete LOD for a screen size, limited to the levels that exist (lodCount >= 1)
	inline int Select(float screenSize, int lodCount)
	{
		int lod = 0;
		while (lod < 3 && screenSize < LOD_SCREEN_SIZE[lod]) lod++;
		return std::min(lod, std::max(lodCount, 1) - 1);
	}
}
