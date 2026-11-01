#pragma once

#include "Nodes/NodeGraph.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>

// =====================================================================
// Filter Points: splits points by height (or any axis) and by slope
//
//  - Height: an absolute threshold, or a percentage of the points' own range (0% = the lowest
//    point, 100% = the highest).
//  - Slope: a component of the surface normal (normal Y = cos(slope angle)).
//  - Falloff: instead of a hard cut (every tree stopping on the same contour line), points within
//    +-falloff/2 of a threshold pass with a chance ramping from 0 to 1. The chance is hashed from
//    the point's position: the same points pass every run.
// Chain several to combine rules: Scatter -> Filter (height) -> Filter (slope) -> Spawn.
// =====================================================================
class FilterTransformListNode : public GraphNode
{
public:
	explicit FilterTransformListNode(NodeGraph& graph) : GraphNode(graph)
	{
		AddInput(PinType::Points, "Points", "points");
		AddOutput(PinType::Points, "Passed", "passed");
		AddOutput(PinType::Points, "Rejected", "rejected");

		AddEnum("axis", "Axis", &axis, { "X", "Y (height)", "Z" }).Section("Position");
		AddEnum("comparison", "Keep", &comparison, { "below", "above", "below or at", "above or at" });
		AddBool("usePercentage", "Percent of range", &usePercentage)
			.Tip("Threshold as 0-100% of the points' own range on this axis, instead of world units");
		AddFloat("threshold", "Threshold", &threshold, 0.5f, -100000.0f, 100000.0f, "%.1f");
		AddFloat("positionFalloff", "Falloff", &positionFalloff, 0.5f, 0.0f, 100000.0f, "%.1f")
			.Tip("Soft edge width around the threshold (same unit as the threshold)");

		AddBool("useNormalFilter", "Slope filter", &useNormalFilter).Section("Slope");
		AddEnum("normalAxis", "Normal axis", &normalAxis, { "X", "Y (up)", "Z" }).ShowIf([this] { return useNormalFilter; });
		AddEnum("normalComparison", "Keep normal", &normalComparison, { "below", "above", "below or at", "above or at" })
			.ShowIf([this] { return useNormalFilter; });
		AddFloat("normalThreshold", "Normal threshold", &normalThreshold, 0.01f, -1.0f, 1.0f, "%.2f")
			.ShowIf([this] { return useNormalFilter; })
			.Tip("Normal Y = cos(slope): 1 = flat ground, 0.7 = about 45 degrees, 0 = a cliff");
		AddFloat("normalFalloff", "Slope falloff", &normalFalloff, 0.005f, 0.0f, 1.0f, "%.3f").ShowIf([this] { return useNormalFilter; });
	}

	// Older saves: (Input, Threshold)
	std::vector<std::string> LegacyInputKeys() const override { return { "points", "threshold" }; }

	void RenderContent(SceneManager* scene) override
	{
		DrawParams(scene);
		if (usePercentage && lastMin != FLT_MAX)
			ImGui::TextDisabled("Range %.1f .. %.1f  -> %.1f", lastMin, lastMax, lastMin + threshold / 100.0f * (lastMax - lastMin));
		if (useNormalFilter && normalAxis == 1)
			ImGui::TextDisabled("Normal %.2f = %.1f deg slope", normalThreshold, glm::degrees(std::acos(std::clamp(normalThreshold, -1.0f, 1.0f))));
	}

	void Execute(NodeContext& ctx) override
	{
		const TransformList& points = In("points").Points();
		if (points.empty()) return;

		float actualThresh = threshold;
		float actualFalloff = positionFalloff;
		if (usePercentage) {
			float lo = FLT_MAX, hi = -FLT_MAX;
			for (const auto& t : points) {
				float v = t.position[axis];
				lo = std::min(lo, v);
				hi = std::max(hi, v);
			}
			lastMin = lo;
			lastMax = hi;
			float range = std::max(hi - lo, 0.001f);
			actualThresh = lo + threshold / 100.0f * range;
			actualFalloff = positionFalloff / 100.0f * range;
		}

		TransformList passed, rejected;
		passed.reserve(points.size());
		rejected.reserve(points.size() / 4);
		for (const auto& t : points) {
			bool pass = Compare(t.position[axis], comparison, actualThresh, actualFalloff, PositionHash(t, 0x68E31DA4u));
			if (pass && useNormalFilter)
				pass = Compare(t.normal[normalAxis], normalComparison, normalThreshold, normalFalloff, PositionHash(t, 0xB5297A4Du));
			(pass ? passed : rejected).push_back(t);
		}
		ctx.Info(std::to_string(passed.size()) + " passed, " + std::to_string(rejected.size()) + " rejected");
		Out(0, NodeValue::FromPoints(std::move(passed)));
		Out(1, NodeValue::FromPoints(std::move(rejected)));
	}

private:
	int axis = 1;
	int comparison = 0;
	float threshold = 50.0f;
	bool usePercentage = true;
	bool useNormalFilter = false;
	int normalAxis = 1;
	float normalThreshold = 0.7f;
	int normalComparison = 1;
	float positionFalloff = 0.0f;
	float normalFalloff = 0.0f;
	float lastMin = FLT_MAX, lastMax = -FLT_MAX;

	// Deterministic [0,1) value per point position
	static float PositionHash(const TransformData& t, uint32_t salt)
	{
		auto bits = [](float f) { uint32_t u; std::memcpy(&u, &f, sizeof(u)); return u; };
		uint32_t h = bits(t.position.x) * 0x9E3779B1u ^ bits(t.position.y) * 0x85EBCA77u ^ bits(t.position.z) * 0xC2B2AE3Du ^ salt;
		h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
		return (float)(h >> 8) / 16777216.0f;
	}

	// A hard comparison, or with a falloff a chance ramping across [thresh - f/2, thresh + f/2]
	static bool Compare(float val, int op, float thresh, float falloff, float random)
	{
		if (falloff <= 0.0f) {
			switch (op) {
			case 0: return val < thresh;
			case 1: return val > thresh;
			case 2: return val <= thresh;
			case 3: return val >= thresh;
			}
			return true;
		}
		float margin = (op == 1 || op == 3) ? (val - thresh) : (thresh - val);
		float chance = std::clamp(margin / falloff + 0.5f, 0.0f, 1.0f);
		chance = chance * chance * (3.0f - 2.0f * chance);
		return random < chance;
	}
};
