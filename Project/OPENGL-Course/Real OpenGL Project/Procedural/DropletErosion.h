#pragma once

// Particle (droplet) based hydraulic erosion on a square heightmap.
//
// Algorithm from Hans Theobald Beyer, "Implementation of a method for hydraulic erosion"
// (TU Munich, 2015), as popularised by Sebastian Lague:
//  - height and gradient are sampled BILINEARLY at the droplet's exact position, so paths are
//    not locked to the grid (a per-cell gradient makes every channel zig-zag along cell edges);
//  - the direction keeps some inertia, the sediment capacity depends on slope, speed and water;
//  - material is removed with a radial brush (a few cells wide -> smooth gullies) and
//    deposited bilinearly; sediment is mass-conserving.
//
// Heights are normalized internally (range -> [0, (res-1)/255]) so the same parameters give
// the same look regardless of terrain amplitude or grid resolution.
//
// Header-only so it can be unit-tested / previewed outside the engine.

#include <vector>
#include <random>
#include <cmath>
#include <algorithm>
#include <functional>

struct DropletErosionParams
{
	int numDroplets = 2000000;        // ~8 per cell on a 513x513 grid
	int brushRadius = 3;              // Cells; larger = wider, smoother gullies
	float inertia = 0.05f;            // 0 = always follow the slope, 1 = never turn
	float sedimentCapacityFactor = 6.0f;
	float minSedimentCapacity = 0.01f;
	float erodeSpeed = 0.4f;
	float depositSpeed = 0.3f;
	float evaporateSpeed = 0.01f;
	float gravity = 4.0f;
	int maxLifetime = 30;
	float initialWater = 1.0f;
	float initialSpeed = 1.0f;
	float edgeFadeCells = 0.0f;       // Erosion fade width at the map border (0 = none)
	unsigned int seed = 42;
};

namespace DropletErosionDetail
{
	struct HeightAndGradient { float height, gradientX, gradientZ; };

	// Bilinear height + gradient at (posX, posZ); requires 0 <= pos < res - 1
	inline HeightAndGradient Sample(const std::vector<float>& map, int res, float posX, float posZ)
	{
		int x = (int)posX;
		int z = (int)posZ;
		float u = posX - x;
		float v = posZ - z;
		int i = z * res + x;
		float hNW = map[i];
		float hNE = map[i + 1];
		float hSW = map[i + res];
		float hSE = map[i + res + 1];

		HeightAndGradient r;
		r.gradientX = (hNE - hNW) * (1.0f - v) + (hSE - hSW) * v;
		r.gradientZ = (hSW - hNW) * (1.0f - u) + (hSE - hNE) * u;
		r.height = hNW * (1.0f - u) * (1.0f - v) + hNE * u * (1.0f - v) + hSW * (1.0f - u) * v + hSE * u * v;
		return r;
	}

	struct BrushTap { int dx, dz; float weight; };
}

// heights: res*res row-major (index = z * res + x), modified in place.
// progress (optional) receives 0..1.
inline void RunDropletErosion(std::vector<float>& heights, int res, const DropletErosionParams& p,
	const std::function<void(float)>& progress = nullptr)
{
	using namespace DropletErosionDetail;
	if (res < 4 || (int)heights.size() != res * res || p.numDroplets <= 0) return;

	// --- Normalize heights so behaviour is independent of amplitude and resolution ---
	auto mm = std::minmax_element(heights.begin(), heights.end());
	float hMin = *mm.first;
	float range = *mm.second - hMin;
	if (range <= 1e-6f) return;
	const float toSim = ((float)(res - 1) / 255.0f) / range;
	std::vector<float> map(heights.size());
	for (size_t i = 0; i < heights.size(); i++) map[i] = (heights[i] - hMin) * toSim;

	// --- Precomputed radial brush (weight falls off linearly with distance) ---
	const int radius = std::max(1, p.brushRadius);
	std::vector<BrushTap> brush;
	float brushSum = 0.0f;
	for (int dz = -radius; dz <= radius; dz++) {
		for (int dx = -radius; dx <= radius; dx++) {
			float d = std::sqrt((float)(dx * dx + dz * dz));
			if (d < (float)radius) {
				float w = 1.0f - d / (float)radius;
				brush.push_back({ dx, dz, w });
				brushSum += w;
			}
		}
	}
	for (auto& t : brush) t.weight /= brushSum;

	std::mt19937 rng(p.seed);
	std::uniform_real_distribution<float> randPos(0.0f, (float)(res - 1) - 1e-3f);
	const float inertia = std::clamp(p.inertia, 0.0f, 1.0f);
	const float erodeSpeed = std::clamp(p.erodeSpeed, 0.0f, 1.0f);
	const float depositSpeed = std::clamp(p.depositSpeed, 0.0f, 1.0f);
	const float evaporate = std::clamp(p.evaporateSpeed, 0.0f, 1.0f);
	const int progressEvery = std::max(1, p.numDroplets / 100);
	// Erosion fades out smoothly toward the map border; a short linear fade left a visible
	// rim between the eroded interior and the untouched edge cells.
	const float edgeFadeCells = p.edgeFadeCells;

	for (int drop = 0; drop < p.numDroplets; drop++)
	{
		if (progress && drop % progressEvery == 0) progress((float)drop / (float)p.numDroplets);

		float posX = randPos(rng);
		float posZ = randPos(rng);
		float dirX = 0.0f, dirZ = 0.0f;
		float speed = p.initialSpeed;
		float water = p.initialWater;
		float sediment = 0.0f;

		for (int life = 0; life < p.maxLifetime; life++)
		{
			int nodeX = (int)posX;
			int nodeZ = (int)posZ;
			float cellU = posX - nodeX;
			float cellV = posZ - nodeZ;
			int nodeIndex = nodeZ * res + nodeX;

			// Height/gradient re-sampled every step (the map changes under the droplet)
			HeightAndGradient hg = Sample(map, res, posX, posZ);

			dirX = dirX * inertia - hg.gradientX * (1.0f - inertia);
			dirZ = dirZ * inertia - hg.gradientZ * (1.0f - inertia);
			float len = std::sqrt(dirX * dirX + dirZ * dirZ);
			if (len < 1e-8f) {
				// Flat pit: nowhere to flow. Drop the load here (fills pits).
				map[nodeIndex]           += sediment * (1.0f - cellU) * (1.0f - cellV);
				map[nodeIndex + 1]       += sediment * cellU * (1.0f - cellV);
				map[nodeIndex + res]     += sediment * (1.0f - cellU) * cellV;
				map[nodeIndex + res + 1] += sediment * cellU * cellV;
				break;
			}
			dirX /= len;
			dirZ /= len;
			posX += dirX;
			posZ += dirZ;

			if (posX < 0.0f || posX >= (float)(res - 1) || posZ < 0.0f || posZ >= (float)(res - 1)) break;

			float newHeight = Sample(map, res, posX, posZ).height;
			float deltaHeight = newHeight - hg.height; // < 0 going downhill

			// Fade erosion/deposition near the map border so the edges don't get walls or trenches
			float edgeDist = std::min(std::min((float)nodeX, (float)(res - 2 - nodeX)), std::min((float)nodeZ, (float)(res - 2 - nodeZ)));
			float edgeT = edgeFadeCells > 0.0f ? std::clamp(edgeDist / edgeFadeCells, 0.0f, 1.0f) : 1.0f;
			float edgeFactor = edgeT * edgeT * (3.0f - 2.0f * edgeT);

			float capacity = std::max(-deltaHeight * speed * water * p.sedimentCapacityFactor, p.minSedimentCapacity);

			if (sediment > capacity || deltaHeight > 0.0f)
			{
				// Uphill: fill the pit we're leaving (at most up to the new height). Otherwise drop the excess.
				float amount = (deltaHeight > 0.0f) ? std::min(deltaHeight, sediment) : (sediment - capacity) * depositSpeed;
				amount *= (deltaHeight > 0.0f) ? 1.0f : edgeFactor;
				sediment -= amount;

				map[nodeIndex]           += amount * (1.0f - cellU) * (1.0f - cellV);
				map[nodeIndex + 1]       += amount * cellU * (1.0f - cellV);
				map[nodeIndex + res]     += amount * (1.0f - cellU) * cellV;
				map[nodeIndex + res + 1] += amount * cellU * cellV;
			}
			else
			{
				// Never erode more than the height difference: digging a hole behind the droplet
				// would create spikes and pits.
				float amount = std::min((capacity - sediment) * erodeSpeed, -deltaHeight) * edgeFactor;

				bool nearEdge = nodeX < radius || nodeZ < radius || nodeX >= res - radius || nodeZ >= res - radius;
				float norm = 1.0f;
				if (nearEdge) {
					float sum = 0.0f;
					for (const auto& t : brush) {
						int bx = nodeX + t.dx, bz = nodeZ + t.dz;
						if (bx >= 0 && bx < res && bz >= 0 && bz < res) sum += t.weight;
					}
					norm = sum > 0.0f ? 1.0f / sum : 0.0f;
				}

				for (const auto& t : brush) {
					int bx = nodeX + t.dx, bz = nodeZ + t.dz;
					if (bx < 0 || bx >= res || bz < 0 || bz >= res) continue;
					float& h = map[bz * res + bx];
					float take = std::min(h, amount * t.weight * norm);
					h -= take;
					sediment += take;
				}
			}

			// Gains speed going downhill (-deltaHeight > 0), loses it climbing
			speed = std::sqrt(std::max(0.0f, speed * speed - deltaHeight * p.gravity));
			water *= (1.0f - evaporate);
		}

		// Sediment still carried when the lifetime ends is discarded on purpose (as in Beyer /
		// Lague): it stands for material rivers transport away. Dropping it where droplets die
		// fills every gully back up with speckled deposits.
	}

	if (progress) progress(1.0f);

	// --- Border: no drainage notches, no rim ---
	// With erosion faded out at the border, the interior is lowered (sediment leaves the system)
	// while the edge isn't, which would leave a raised rim. Lower the fade band smoothly by the
	// interior's mean lowering instead, so the border follows the eroded terrain.
	if (edgeFadeCells > 0.0f)
	{
		double lowered = 0.0;
		size_t count = 0;
		int inner = (int)std::ceil(edgeFadeCells) + 1;
		for (int z = inner; z < res - inner; z++)
			for (int x = inner; x < res - inner; x++) {
				int i = z * res + x;
				lowered += (double)((heights[i] - hMin) * toSim - map[i]);
				count++;
			}
		float meanLowering = count ? (float)(lowered / (double)count) : 0.0f;
		for (int z = 0; z < res; z++)
			for (int x = 0; x < res; x++) {
				float edgeDist = (float)std::min(std::min(x, res - 1 - x), std::min(z, res - 1 - z));
				float t = std::clamp(edgeDist / edgeFadeCells, 0.0f, 1.0f);
				float fade = t * t * (3.0f - 2.0f * t);
				map[z * res + x] -= meanLowering * (1.0f - fade);
			}
	}

	// --- Back to world heights ---
	const float toWorld = 1.0f / toSim;
	for (size_t i = 0; i < heights.size(); i++) heights[i] = map[i] * toWorld + hMin;
}
