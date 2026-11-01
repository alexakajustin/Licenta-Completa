#include "Procedural/ScatterNode.h"
#include "Nodes/NodeRandom.h"
#include "imgui.h"
#include <algorithm>
#include <future>
#include <random>
#include <thread>

ScatterNode::ScatterNode(NodeGraph& graph) : GraphNode(graph), seed(NodeRandom::TrueRandomSeed())
{
	AddInput(PinType::Mesh, "Surface", "surface");
	AddOutput(PinType::Points, "Points", "points");
	AddOutput(PinType::Mesh, "Surface", "surface");

	AddInt("count", "Count", &count, 10.0f, 1, 10000000).Tip("How many points to scatter (before any filter)");
	AddFloat("minScale", "Min scale", &minScale, 0.01f, 0.01f, 100.0f, "%.2f");
	AddFloat("maxScale", "Max scale", &maxScale, 0.01f, 0.01f, 100.0f, "%.2f");
	AddBool("randomRotation", "Random rotation", &randomRotation).Tip("A random turn around the vertical axis for every point");
	AddSeed("seed", "Seed", &seed);
}

void ScatterNode::SaveState(json& j) const
{
	if (legacyRandom) j["legacyRandom"] = true;
}

void ScatterNode::LoadState(const json& j)
{
	legacyRandom = j.value("legacyRandom", false);

	// Older saves: Scatter itself spawned the instances
	if (!j.contains("in")) {
		legacyRandom = true;
		legacy.spawn = j.value("spawnAsObjects", false);
		legacy.alignToNormal = j.value("alignToNormal", true);
		if (j.contains("deletionVolumes"))
			for (const auto& v : j["deletionVolumes"])
				legacy.deletionVolumes.push_back({ glm::vec3(v.value("x", 0.0f), v.value("y", 0.0f), v.value("z", 0.0f)), v.value("radius", 1.0f) });
		if (j.contains("createdGroupNames"))
			for (const auto& g : j["createdGroupNames"]) legacy.groups.insert(g.get<std::string>());
	}
}

void ScatterNode::RenderContent(SceneManager* scene)
{
	DrawParams(scene);
	if (legacyRandom) {
		ImGui::TextDisabled("Legacy random layout (thread-count dependent)");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("This node keeps the exact layout it had before the node overhaul.\n"
				"Upgrade: points spread evenly by area, identical on every machine (the layout changes once).");
		ImGui::SameLine();
		if (ImGui::SmallButton("Upgrade")) legacyRandom = false;
	}
}

void ScatterNode::Execute(NodeContext& ctx)
{
	const NodeValue& in = In("surface");
	const MeshValue& surface = in.mesh;
	if (in.Valid()) Out(1, in);
	if (surface.Empty() || surface.Data().indices.size() < 3) return;

	int total = std::clamp(count, 0, 10000000);
	TransformList points;
	try {
		if (legacyRandom) ScatterLegacy(surface, points, total);
		else ScatterByArea(surface, points, total, ctx);
	}
	catch (const std::bad_alloc&) {
		throw std::runtime_error("Out of memory scattering " + std::to_string(total) + " points");
	}
	Out(0, NodeValue::FromPoints(std::move(points)));
}

// Even density over the surface area: a triangle gets points in proportion to its area
void ScatterNode::ScatterByArea(const MeshValue& surface, TransformList& points, int total, NodeContext& ctx)
{
	const MeshData& mesh = surface.Data();
	const size_t triCount = mesh.indices.size() / 3;
	std::vector<double> cdf(triCount);
	double area = 0.0;
	for (size_t t = 0; t < triCount; t++) {
		glm::vec3 a = mesh.GetPosition(mesh.indices[t * 3]), b = mesh.GetPosition(mesh.indices[t * 3 + 1]), c = mesh.GetPosition(mesh.indices[t * 3 + 2]);
		area += 0.5 * (double)glm::length(glm::cross(b - a, c - a));
		cdf[t] = area;
	}
	if (area <= 0.0) return;

	points.resize((size_t)total);
	const uint32_t s = (uint32_t)seed;
	const float lo = std::min(minScale, maxScale), hi = std::max(minScale, maxScale);
	unsigned threadCount = std::max(1u, std::min(std::thread::hardware_concurrency(), (unsigned)(total / 4096 + 1)));
	std::vector<std::future<void>> jobs;
	size_t perThread = ((size_t)total + threadCount - 1) / threadCount;
	for (unsigned t = 0; t < threadCount; t++) {
		size_t start = t * perThread, end = std::min((size_t)total, start + perThread);
		if (start >= end) break;
		jobs.push_back(std::async(std::launch::async, [&, start, end]() {
			for (size_t i = start; i < end; i++) {
				NodeRandom::Rng rng(NodeRandom::Mix(s, i));
				double target = (double)rng.Float01() * area;
				size_t tri = std::min((size_t)(std::upper_bound(cdf.begin(), cdf.end(), target) - cdf.begin()), triCount - 1);
				unsigned i0 = mesh.indices[tri * 3], i1 = mesh.indices[tri * 3 + 1], i2 = mesh.indices[tri * 3 + 2];
				float r1 = rng.Float01(), r2 = rng.Float01();
				if (r1 + r2 > 1.0f) { r1 = 1.0f - r1; r2 = 1.0f - r2; }
				float r0 = 1.0f - r1 - r2;
				TransformData& p = points[i];
				p.position = surface.origin + mesh.GetPosition(i0) * r0 + mesh.GetPosition(i1) * r1 + mesh.GetPosition(i2) * r2;
				glm::vec3 n = mesh.GetNormal(i0) * r0 + mesh.GetNormal(i1) * r1 + mesh.GetNormal(i2) * r2;
				p.normal = glm::length(n) > 1e-6f ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
				p.scale = glm::vec3(rng.Range(lo, hi));
				p.rotation = glm::vec3(0.0f, randomRotation ? rng.Range(0.0f, 360.0f) : 0.0f, 0.0f);
			}
		}));
	}
	for (size_t t = 0; t < jobs.size(); t++) {
		jobs[t].get();
		ctx.Progress(10.0f + 80.0f * (t + 1) / jobs.size(), "Scattering " + std::to_string(total) + " points...");
	}
}

// The original algorithm, kept bit for bit for scenes saved before the overhaul: one mt19937 per
// thread (seed + thread index), a uniformly picked triangle per point
void ScatterNode::ScatterLegacy(const MeshValue& surface, TransformList& points, int total)
{
	const MeshData& mesh = surface.Data();
	points.resize((size_t)total);
	unsigned threads = std::thread::hardware_concurrency();
	if (threads == 0) threads = 4;
	if (threads > (unsigned)total) threads = (unsigned)total;
	if (threads == 0) return;
	int perThread = total / (int)threads;
	std::vector<std::future<void>> jobs;
	for (unsigned t = 0; t < threads; t++) {
		int start = (int)t * perThread;
		int end = (t == threads - 1) ? total : (int)(t + 1) * perThread;
		jobs.push_back(std::async(std::launch::async, [&, start, end, t]() {
			std::mt19937 gen(seed + t);
			std::uniform_int_distribution<> triDist(0, (int)mesh.indices.size() / 3 - 1);
			std::uniform_real_distribution<float> floatDist(0.0f, 1.0f);
			std::uniform_real_distribution<float> rotDist(0.0f, 360.0f);
			std::uniform_real_distribution<float> scaleDist(minScale, maxScale);
			for (int i = start; i < end; i++) {
				int tri = triDist(gen);
				unsigned i0 = mesh.indices[tri * 3], i1 = mesh.indices[tri * 3 + 1], i2 = mesh.indices[tri * 3 + 2];
				float r1 = floatDist(gen), r2 = floatDist(gen);
				if (r1 + r2 > 1.0f) { r1 = 1.0f - r1; r2 = 1.0f - r2; }
				float r0 = 1.0f - r1 - r2;
				TransformData& p = points[i];
				p.position = surface.origin + (mesh.GetPosition(i0) * r0 + mesh.GetPosition(i1) * r1 + mesh.GetPosition(i2) * r2);
				// Normalized twice, as the original did (local, then "world" through an identity matrix)
				p.normal = glm::normalize(glm::normalize(mesh.GetNormal(i0) * r0 + mesh.GetNormal(i1) * r1 + mesh.GetNormal(i2) * r2));
				p.scale = glm::vec3(scaleDist(gen));
				p.rotation = glm::vec3(0.0f);
				if (randomRotation) p.rotation.y = rotDist(gen);
			}
		}));
	}
	for (auto& j : jobs) j.get();
}
