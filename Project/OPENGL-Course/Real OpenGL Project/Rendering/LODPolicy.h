#pragma once

#include <algorithm>
#include "GraphicsSettings.h"

/**
 * @namespace LODPolicy
 * @brief Level of detail by distance to the camera, shared by the GPU culler (compute_cull.glsl)
 *        and the CPU path (GameObject::RenderSingle).
 *
 * Graphics Settings -> Level of Detail defines the stages an object goes through as it gets
 * further away: LOD0, LOD1 and LOD2, each ending at its own distance, then the impostor card
 * (trees, Impostor.h). Every stage can be switched off and is then skipped: the next stage in
 * use starts where the previous one ends (LOD2 off: LOD1, then the impostor). The last stage in
 * use reaches the render distance. Objects without an impostor keep their last LOD in use.
 */
namespace LODPolicy
{
	constexpr int IMPOSTOR = 3;              // Representation of the impostor card (mesh LODs: 0-2)
	constexpr int MAX_STAGES = 4;
	constexpr float NO_END = 1.0e30f;
	// Below this fraction of the screen height an instance is not drawn (about 2 px at 1080p)
	constexpr float CULL_SCREEN_SIZE = 0.002f;
	// Dithered transition over the last 15% of a stage's distance range
	constexpr float CROSSFADE_WIDTH = 0.15f;

	struct Stages
	{
		int count = 0;
		int rep[MAX_STAGES] = { 0, 0, 0, 0 };  // Mesh LOD 0-2 or IMPOSTOR
		float end[MAX_STAGES] = { NO_END, NO_END, NO_END, NO_END }; // Camera distance where it ends
	};

	// The stages of one object. lodCount: its LOD meshes (a missing level draws the last one);
	// hasImpostor: it has an impostor card in this pass; distanceScale: secondary views (water
	// reflection) switch sooner
	inline Stages Build(const GraphicsSettings* gs, int lodCount, bool hasImpostor, float distanceScale = 1.0f)
	{
		static const GraphicsSettings defaults;
		const GraphicsSettings& g = gs ? *gs : defaults;
		const int lastMesh = std::max(lodCount, 1) - 1;
		Stages s;
		float end = 0.0f;
		for (int i = 0; i < 3; i++) {
			if (!g.lodEnabled[i]) continue;
			end = std::max(end, g.lodDistance[i] * distanceScale); // Never before the previous stage ends
			s.rep[s.count] = std::min(i, lastMesh);
			s.end[s.count] = end;
			s.count++;
		}
		if (g.impostors && hasImpostor) s.rep[s.count++] = IMPOSTOR;
		if (s.count == 0) s.rep[s.count++] = 0; // Everything switched off: LOD0
		s.end[s.count - 1] = NO_END;            // The last stage reaches the render distance
		return s;
	}

	// Stage at a camera distance; minStage skips the first stages (far shadow cascades)
	inline int Select(const Stages& s, float distance, int minStage = 0)
	{
		int stage = 0;
		while (stage < s.count - 1 && distance >= s.end[stage]) stage++;
		return std::max(stage, std::min(minStage, s.count - 1));
	}
}
