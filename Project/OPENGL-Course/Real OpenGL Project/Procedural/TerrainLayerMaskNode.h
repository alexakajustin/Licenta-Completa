#pragma once
#include "Nodes/NodeGraph.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "Rendering/InstancedGroup.h"
#include "Rendering/TextureLayer.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// =====================================================================
// Terrain Layer Mask: where one texture layer of a surface (a terrain) shows, by the distance to
// other things - forest litter and moss under the trees, gravel around rocks, mud by the water,
// bare earth along a path, or a layer kept away from them. On top of the layer's own height and
// slope rules (it shows where both allow).
//
// "Near" what: scene objects (their position) and, for an object other graphs scattered as
// instances (a tree, a rock: Spawn Instances), every one of its instances, from any tab that ran
// before this one; plus any points linked into Near points.
//
// The mask is 1 within Inner, falling to 0 at Radius (Falloff shapes it); Breakup bends that
// border with noise so it frays like real ground cover instead of drawing circles. It is baked
// into a grid over the surface every run (GameObject::SetLayerMask), nothing to save.
// =====================================================================
class TerrainLayerMaskNode : public GraphNode
{
public:
	explicit TerrainLayerMaskNode(NodeGraph& graph) : GraphNode(graph)
	{
		AddInput(PinType::Points, "Near points", "near");
		AddObjects("target", "Surface", &target, true).Tip("The object whose texture layer this masks (a terrain)");
		AddInt("layer", "Layer", &layer, 0.1f, 1, MAX_TEXTURE_LAYERS).Tip("Which of its texture layers (1 = the first)");
		AddObjects("nearObjects", "Near objects", &nearObjects, false)
			.Tip("Objects the layer gathers around. An object scattered as instances by another graph (a tree, a rock) counts with every instance");
		AddEnum("mode", "Show", &mode, { "Near them", "Away from them" });
		AddFloat("inner", "Inner", &inner, 0.1f, 0.0f, 500.0f, "%.1f m").Tip("Full strength within this distance");
		AddFloat("radius", "Radius", &radius, 0.1f, 0.1f, 1000.0f, "%.1f m").Tip("Nothing beyond this distance");
		AddFloat("falloff", "Falloff", &falloff, 0.05f, 0.1f, 8.0f, "%.2f").Tip("1 linear, higher = held closer to the objects");
		AddFloat("breakup", "Breakup", &breakup, 0.01f, 0.0f, 1.0f, "%.2f").Tip("How much noise frays the border (0 = clean circles)");
		AddFloat("breakupScale", "Breakup size", &breakupScale, 0.1f, 0.5f, 200.0f, "%.1f m");
		AddInt("resolution", "Resolution", &resolution, 4.0f, 64, 2048).Tip("Texels along each side of the surface");
	}

	std::vector<std::string> LegacyInputKeys() const override { return { "near" }; }

	void Execute(NodeContext& ctx) override
	{
		if (target.empty()) { ctx.Warn("Pick the surface"); return; }
		GameObject* surface = ctx.scene.FindObject(target[0]);
		if (!surface) { ctx.Warn("Object '" + target[0] + "' not found"); return; }
		int layerIndex = std::clamp(layer, 1, MAX_TEXTURE_LAYERS) - 1;

		std::vector<glm::vec2> sources;
		for (const auto& p : In("near").Points()) sources.push_back(glm::vec2(p.position.x, p.position.z));
		for (const auto& name : nearObjects) {
			size_t before = sources.size();
			for (InstancedGroup* g : ctx.scene.GetInstancedGroups()) {
				if (!g || !SpawnedFrom(g->GetName(), name)) continue;
				for (const auto& inst : g->cpuInstances) sources.push_back(glm::vec2(inst.positionAndScale.x, inst.positionAndScale.z));
			}
			if (sources.size() == before) {
				GameObject* obj = ctx.scene.FindObject(name);
				if (!obj) { ctx.Warn("Object '" + name + "' not found"); continue; }
				glm::vec3 p = glm::vec3(obj->GetWorldMatrix()[3]);
				sources.push_back(glm::vec2(p.x, p.z));
			}
		}

		// The grid: a square over the surface's world box
		glm::vec3 bmin, bmax;
		surface->GetWorldBounds(bmin, bmax);
		float size = std::max(bmax.x - bmin.x, bmax.z - bmin.z);
		if (size <= 0.0f) { ctx.Warn("The surface has no size"); return; }
		int res = std::clamp(resolution, 64, 2048);
		float texel = size / (float)(res - 1);

		const float reach = std::max(radius, inner) * (1.0f + breakup) + 1e-3f;
		std::unordered_map<int64_t, std::vector<int>> grid;
		auto Key = [](int x, int z) { return ((int64_t)x << 32) ^ (int64_t)(uint32_t)z; };
		for (int i = 0; i < (int)sources.size(); i++)
			grid[Key((int)std::floor(sources[i].x / reach), (int)std::floor(sources[i].y / reach))].push_back(i);

		ctx.Progress(10.0f, "Baking the mask...");
		std::vector<float> mask((size_t)res * res, mode == 1 ? 1.0f : 0.0f);
		for (int k = 0; k < res; k++) {
			for (int i = 0; i < res; i++) {
				glm::vec2 xz(bmin.x + i * texel, bmin.z + k * texel);
				int cx = (int)std::floor(xz.x / reach), cz = (int)std::floor(xz.y / reach);
				float best = 1e30f;
				for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
					auto it = grid.find(Key(cx + dx, cz + dz));
					if (it == grid.end()) continue;
					for (int s : it->second) {
						glm::vec2 d = sources[s] - xz;
						best = std::min(best, d.x * d.x + d.y * d.y);
					}
				}
				if (best >= 1e29f) continue; // Far from everything: the default above
				float dist = std::sqrt(best);
				// The border frays: the distance bent by noise, as much as Breakup of the radius
				dist += (Noise(xz / std::max(breakupScale, 0.1f)) * 2.0f - 1.0f) * breakup * radius;
				float t = radius > inner ? std::clamp((dist - inner) / (radius - inner), 0.0f, 1.0f) : (dist <= inner ? 0.0f : 1.0f);
				float nearness = std::pow(1.0f - t, std::max(falloff, 0.1f));
				mask[(size_t)k * res + i] = mode == 0 ? nearness : 1.0f - nearness;
			}
		}
		surface->SetLayerMask(layerIndex, mask, res, bmin, size);
		if ((int)surface->GetTextureLayers().size() <= layerIndex) ctx.Warn("'" + target[0] + "' has no layer " + std::to_string(layerIndex + 1) + " yet");
		ctx.Info("Layer " + std::to_string(layerIndex + 1) + " of " + target[0] + ": " + std::to_string(sources.size()) + " objects, " + std::to_string(res) + "^2 texels");
	}

private:
	static bool SpawnedFrom(const std::string& group, const std::string& object)
	{
		const std::string prefix = "Scatter_Instanced_";
		if (group.rfind(prefix, 0) != 0) return false;
		size_t underscore = group.find('_', prefix.size());
		if (underscore == std::string::npos) return false;
		std::string rest = group.substr(underscore + 1);
		return rest == object || rest.rfind(object + "_", 0) == 0;
	}

	static float Hash(int x, int z)
	{
		uint32_t h = (uint32_t)x * 0x8DA6B343u ^ (uint32_t)z * 0xD8163841u;
		h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
		return (float)(h & 0xFFFFFF) / 16777216.0f;
	}
	// Two octaves of value noise, 0..1
	static float Noise(glm::vec2 p)
	{
		float sum = 0.0f, amp = 0.65f;
		for (int o = 0; o < 2; o++) {
			glm::vec2 i = glm::floor(p), f = p - i;
			f = f * f * (3.0f - 2.0f * f);
			int x = (int)i.x, z = (int)i.y;
			float v = glm::mix(glm::mix(Hash(x, z), Hash(x + 1, z), f.x), glm::mix(Hash(x, z + 1), Hash(x + 1, z + 1), f.x), f.y);
			sum += v * amp;
			p = p * 2.3f + glm::vec2(17.1f, 5.3f);
			amp = 0.35f;
		}
		return sum;
	}

	std::vector<std::string> target;
	int layer = 2;
	std::vector<std::string> nearObjects;
	int mode = 0;
	float inner = 1.5f;
	float radius = 7.0f;
	float falloff = 1.2f;
	float breakup = 0.4f;
	float breakupScale = 4.0f;
	int resolution = 512;
};
