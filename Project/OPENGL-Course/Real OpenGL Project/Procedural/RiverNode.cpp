#include "Procedural/RiverNode.h"
#include "imgui.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "Rendering/Mesh.h"
#include "Rendering/Material.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>
#include <queue>
#include <thread>
#include <glm/gtc/matrix_transform.hpp>

RiverNode::RiverNode(NodeGraph& graph) : GraphNode(graph)
{
	AddInput(PinType::Mesh, "Mesh", "mesh");
	AddOutput(PinType::Mesh, "Mesh", "mesh");

	springCount = 8;
	maxSteps = 1500;
	baseDepth = 1.0f;
	baseWidth = 8.0f;
	riverWaterOffset = 0.005f;
	lakeWaterOffset = 0.005f;
	smoothPasses = 8;
	lakeDepth = 4.0f;

	AddInt("springCount", "Springs", &springCount, 0.2f, 1, 50).Tip("Rivers start at the heads of the highest valleys (where enough water gathers), this many of them");
	AddInt("maxSteps", "Max length", &maxSteps, 10.0f, 10, 4000).Tip("Longest river, in cells of a 512-cell terrain (a finer one: the same length)");
	AddFloat("baseDepth", "Base depth", &baseDepth, 0.1f, 0.0f, 50.0f, "%.2f");
	AddFloat("baseWidth", "Base width", &baseWidth, 0.1f, 0.1f, 100.0f, "%.1f");
	AddFloat("riverWaterOffset", "River offset", &riverWaterOffset, 0.01f, -10.0f, 10.0f).Legacy("waterOffset");
	AddFloat("lakeWaterOffset", "Lake offset", &lakeWaterOffset, 0.01f, -10.0f, 10.0f).Legacy("waterOffset");
	AddInt("smoothPasses", "Smooth passes", &smoothPasses, 0.1f, 0, 30).Tip("Smoothing of the carved ground, in passes on a 512-cell terrain (a finer one: as many as reach as far)");
	AddFloat("lakeDepth", "Lake depth", &lakeDepth, 0.1f, 0.0f, 50.0f, "%.1f m").Section("Lakes")
		.Tip("Depth of each lake away from its shore (the bed is dug out). The lake's extent follows the terrain up to its outlet.");
}

void RiverNode::Execute(NodeContext& ctx)
{
	const NodeValue& input = In("mesh");
	if (input.mesh.Empty()) return;
	auto progress = [&](float pct, const std::string& msg) { ctx.Progress(pct, msg); };

	MeshData data = input.mesh.Data();
	if (data.vertices.empty()) return;

	int totalVerts = (int)data.vertices.size() / 14;
	int gridRes = (int)std::sqrt(totalVerts);

	if (gridRes * gridRes != totalVerts)
	{
		ctx.Warn("Needs a square grid mesh (a subdivided plane): passed through unchanged");
		Out(0, input);
		return;
	}

	// The terrain's size: its scale is baked into the vertices (Scene Object)
	glm::vec3 terrainScale = input.mesh.scale;

	// World size of one grid cell (the terrain's scale is baked into the vertices)
	const float cellMetres = std::max(std::abs(data.vertices[14] - data.vertices[0]), 1e-4f);
	auto H = [&](int i) { return data.vertices[i * 14 + 1]; };

	// The node's grid-based constants (reaches in cells, smoothing passes) are tuned on a 512-cell
	// terrain. On a finer one they scale with it: the same rivers and lakes, drawn finer (in cells,
	// a river smoothed over half the metres ran straight down the grid's diagonals, and the pits
	// that trap a river into a lake shrank to half their size). Coarser grids keep them as they are.
	const float gridScale = std::max(1.0f, (float)(gridRes - 1) / 512.0f);
	auto scaledCells = [&](float cells) { return std::max(1, (int)std::lround(cells * gridScale)); };
	auto scaledPasses = [&](int passes) { return (int)std::lround((float)passes * gridScale * gridScale); }; // Smoothing reach ~ sqrt(passes)

	// 1. Drainage: where the water of the whole terrain flows and how much of it gathers in each
	// cell. Flow directions (D8: toward the steepest lower neighbour) over the terrain with its
	// hollows filled (priority flood, Barnes et al. 2014: every cell drains to the map's edge),
	// then the flow accumulation (cells of terrain draining through each cell).
	struct Drainage { std::vector<float> filled; std::vector<int> receiver; std::vector<float> accumulation; };
	auto computeDrainage = [](const std::vector<float>& h, int res) {
		const int count = res * res;
		Drainage d;
		d.filled.assign(count, 0.0f);
		d.receiver.assign(count, -1);
		d.accumulation.assign(count, 1.0f);
		std::vector<float>& filled = d.filled;
		std::vector<int>& receiver = d.receiver;
		std::vector<float>& accumulation = d.accumulation;
		std::vector<char> closed(count, 0);
		using Item = std::pair<float, int>;
		std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
		for (int i = 0; i < count; i++) {
			int x = i % res, z = i / res;
			if (x == 0 || z == 0 || x == res - 1 || z == res - 1) { filled[i] = h[i]; closed[i] = 1; open.push({ filled[i], i }); }
		}
		while (!open.empty()) {
			auto [level, c] = open.top(); open.pop();
			int cx = c % res, cz = c / res;
			for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
				if (dx == 0 && dz == 0) continue;
				int nx = cx + dx, nz = cz + dz;
				if (nx < 0 || nx >= res || nz < 0 || nz >= res) continue;
				int n = nz * res + nx;
				if (closed[n]) continue;
				closed[n] = 1;
				filled[n] = std::max(h[n], level + 1e-3f); // A slight slope across filled flats: they drain too
				open.push({ filled[n], n });
			}
		}
		for (int c = 0; c < count; c++) {
			int cx = c % res, cz = c / res;
			// The map's edge is where the water leaves the map: it drains nowhere further (draining
			// along the edge, the water of half a border gathered into a river running down it)
			if (cx == 0 || cz == 0 || cx == res - 1 || cz == res - 1) continue;
			float best = 0.0f;
			for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
				if (dx == 0 && dz == 0) continue;
				int nx = cx + dx, nz = cz + dz;
				if (nx < 0 || nx >= res || nz < 0 || nz >= res) continue;
				int n = nz * res + nx;
				float drop = (filled[c] - filled[n]) / ((dx != 0 && dz != 0) ? 1.41421356f : 1.0f);
				if (drop > best) { best = drop; receiver[c] = n; }
			}
		}
		std::vector<int> order(count);
		std::iota(order.begin(), order.end(), 0);
		std::sort(order.begin(), order.end(), [&](int a, int b) { return filled[a] > filled[b]; });
		for (int c : order) if (receiver[c] >= 0) accumulation[receiver[c]] += accumulation[c];
		return d;
	};
	std::vector<float> heights(totalVerts);
	for (int i = 0; i < totalVerts; i++) heights[i] = H(i);
	Drainage drainage = computeDrainage(heights, gridRes);
	std::vector<float>& filled = drainage.filled;
	std::vector<int>& receiver = drainage.receiver;
	std::vector<float>& accumulation = drainage.accumulation;

	// 2. Springs: the heads of the channel network - cells gathering at least HEAD cells of terrain
	// while no cell draining into them does - at the top of valleys, not on peaks (a river traced
	// from a peak runs down a ridge, with the ground falling away on both sides of its water).
	// Highest first, spread apart, away from the map's edge (where the water leaves the map) and
	// out of the hollows that hold water (lake basins).
	const float HEAD = std::max(48.0f, (float)totalVerts / 3000.0f);
	struct Spring { int x, z; float height; };
	auto chooseSprings = [&](const std::vector<float>& h, int res, const Drainage& d) {
		const int count = res * res;
		const float head = std::max(48.0f, (float)count / 3000.0f);
		std::vector<Spring> candidates;
		std::vector<char> bigDonor(count, 0);
		for (int c = 0; c < count; c++) if (d.receiver[c] >= 0 && d.accumulation[c] >= head) bigDonor[d.receiver[c]] = 1;
		int margin = std::max(2, res / 12);
		for (int c = 0; c < count; c++) {
			if (d.accumulation[c] < head || bigDonor[c]) continue;
			int x = c % res, z = c / res;
			if (x < margin || z < margin || x >= res - margin || z >= res - margin) continue;
			if (d.filled[c] > h[c] + 0.05f) continue;
			candidates.push_back({ x, z, h[c] });
		}
		if (candidates.empty()) // No valleys at all (a flat or tilted plane): the highest points
			for (int c = 0; c < count; c++) candidates.push_back({ c % res, c / res, h[c] });
		std::sort(candidates.begin(), candidates.end(), [](const Spring& a, const Spring& b) { return a.height > b.height; });

		std::vector<Spring> chosen;
		float minDist = (float)res / 10.0f;
		for (const auto& s : candidates)
		{
			bool farEnough = true;
			for (const auto& existing : chosen)
			{
				float dx = (float)(s.x - existing.x);
				float dz = (float)(s.z - existing.z);
				if (std::sqrt(dx * dx + dz * dz) < minDist) { farEnough = false; break; }
			}
			if (farEnough) {
				chosen.push_back(s);
				if ((int)chosen.size() >= springCount) break;
			}
		}
		return chosen;
	};
	std::vector<Spring> springs;
	if (gridScale > 1.0f) {
		// A finer terrain: the springs are chosen on it as seen at 512 cells, then set in its own
		// channel there. (The heads of the channels D8 drainage finds move with the grid: on a
		// finer one, other valleys' heads came out highest - other springs, another river network.)
		const int coarseRes = 513;
		const float step = (float)(gridRes - 1) / 512.0f;
		std::vector<float> coarse((size_t)coarseRes * coarseRes);
		for (int z = 0; z < coarseRes; z++) {
			for (int x = 0; x < coarseRes; x++) {
				float fx = (float)x * step, fz = (float)z * step;
				int x0 = std::min((int)fx, gridRes - 2), z0 = std::min((int)fz, gridRes - 2);
				float tx = fx - (float)x0, tz = fz - (float)z0;
				coarse[(size_t)z * coarseRes + x] = glm::mix(glm::mix(H(z0 * gridRes + x0), H(z0 * gridRes + x0 + 1), tx),
					glm::mix(H((z0 + 1) * gridRes + x0), H((z0 + 1) * gridRes + x0 + 1), tx), tz);
			}
		}
		Drainage coarseDrainage = computeDrainage(coarse, coarseRes);
		const int reach = (int)std::ceil(step);
		for (const Spring& c : chooseSprings(coarse, coarseRes, coarseDrainage)) {
			int fx = std::clamp((int)std::lround((float)c.x * step), 0, gridRes - 1);
			int fz = std::clamp((int)std::lround((float)c.z * step), 0, gridRes - 1);
			int best = fz * gridRes + fx;
			for (int dz = -reach; dz <= reach; dz++) for (int dx = -reach; dx <= reach; dx++) {
				int nx = fx + dx, nz = fz + dz;
				if (nx < 0 || nz < 0 || nx >= gridRes || nz >= gridRes) continue;
				if (accumulation[nz * gridRes + nx] > accumulation[best]) best = nz * gridRes + nx;
			}
			springs.push_back({ best % gridRes, best / gridRes, H(best) });
		}
	}
	else springs = chooseSprings(heights, gridRes, drainage);

	progress(10.0f, "Seeding Springs...");

	// 2. Trace Paths
	std::vector<int> flowVolume(totalVerts, 0);
	std::vector<bool> isSink(totalVerts, false);
	std::vector<int> pathID(totalVerts, -1);
	int currentPathID = 0;

	struct PathStep { int x, z; };
	std::vector<std::vector<PathStep>> riverPaths;

	for (const auto& spring : springs)
	{
		int currX = spring.x;
		int currZ = spring.z;
		std::vector<PathStep> path;

		bool trapped = false;
		bool merged = false;
		bool leftMap = false;

		const int steps = (int)std::lround((float)maxSteps * gridScale); // Max length: in cells of the 512 grid
		for (int step = 0; step < steps; step++)
		{
			int idx = currZ * gridRes + currX;
			flowVolume[idx]++;
			
			if (pathID[idx] == -1 || pathID[idx] == currentPathID) {
				pathID[idx] = currentPathID;
				if (!merged) path.push_back({ currX, currZ });
			} else {
				if (!merged) {
					path.push_back({ currX, currZ }); 
					merged = true;
				}
			}

			// At the map's edge the river flows off the map (running on down the edge, half of its
			// channel lay off the terrain)
			if (currX == 0 || currZ == 0 || currX == gridRes - 1 || currZ == gridRes - 1) { leftMap = true; break; }

			int bestX = currX, bestZ = currZ;
			float lowestH = data.vertices[(currZ * gridRes + currX) * 14 + 1];
			bool foundLower = false;

			for (int dz = -1; dz <= 1; dz++)
			{
				for (int dx = -1; dx <= 1; dx++)
				{
					if (dx == 0 && dz == 0) continue;
					int nx = currX + dx;
					int nz = currZ + dz;
					if (nx >= 0 && nx < gridRes && nz >= 0 && nz < gridRes)
					{
						float nh = data.vertices[(nz * gridRes + nx) * 14 + 1];
						if (nh < lowestH) { lowestH = nh; bestX = nx; bestZ = nz; foundLower = true; }
					}
				}
			}

			if (!foundLower) { 
				int jumpX = -1, jumpZ = -1;
				const int jumpCells = scaledCells(12.0f); // Pits narrower than this don't hold a lake
				for (int r = 2; r <= jumpCells && !foundLower; r++) {
					for (int dz = -r; dz <= r; dz++) {
						for (int dx = -r; dx <= r; dx++) {
							if (std::abs(dx) != r && std::abs(dz) != r) continue;
							int nx = currX + dx;
							int nz = currZ + dz;
							if (nx >= 0 && nx < gridRes && nz >= 0 && nz < gridRes) {
								float nh = data.vertices[(nz * gridRes + nx) * 14 + 1];
								if (nh < lowestH) { 
									lowestH = nh; 
									jumpX = nx; jumpZ = nz; 
									foundLower = true; 
								}
							}
						}
					}
				}
				if (foundLower) {
					if (!merged) {
						int stepCount = std::max(std::abs(jumpX - currX), std::abs(jumpZ - currZ));
						for (int s = 1; s < stepCount; s++) {
							path.push_back({ currX + (jumpX - currX) * s / stepCount, currZ + (jumpZ - currZ) * s / stepCount });
						}
					}
					bestX = jumpX; bestZ = jumpZ;
				}
			}

			if (!foundLower) { 
				isSink[currZ * gridRes + currX] = true; 
				trapped = true; 
				break; 
			}
			currX = bestX;
			currZ = bestZ;
		}
		
		if (!trapped && !leftMap) {
			isSink[currZ * gridRes + currX] = true;
		}
		
		riverPaths.push_back(path);
		currentPathID++;
	}

	struct Sink { int x, z; float volume; };
	std::vector<Sink> sinks;
	for (int i = 0; i < totalVerts; i++) {
		if (isSink[i]) {
			sinks.push_back({ i % gridRes, i / gridRes, (float)flowVolume[i] });
		}
	}

	progress(30.0f, "Calculating Flow...");

	std::vector<float> originalHeights(totalVerts);
	for (int i = 0; i < totalVerts; i++) {
		originalHeights[i] = data.vertices[i * 14 + 1];
	}

	std::vector<float> lakeWaterLevel(totalVerts, -1.0f);
	std::vector<bool> lakeMask(totalVerts, false);
	std::vector<bool> sinkHandled(totalVerts, false);

	std::sort(sinks.begin(), sinks.end(), [&](const Sink& a, const Sink& b) {
		return data.vertices[(a.z * gridRes + a.x) * 14 + 1] < data.vertices[(b.z * gridRes + b.x) * 14 + 1];
	});

	for (const auto& sink : sinks)
	{
		int sinkIdx = sink.z * gridRes + sink.x;
		if (sinkHandled[sinkIdx] || lakeMask[sinkIdx]) continue;
		sinkHandled[sinkIdx] = true;

		const float sinkHeight = originalHeights[sinkIdx];
		const float targetLevel = sinkHeight + lakeDepth;

		// --- 1. Highest level this sink can hold: the lowest pass toward the map border ---
		// Priority-flood (cells leave the queue lowest first; 'level' is the highest cell crossed
		// so far). Water may pour over small rims into neighbouring closed hollows, like a real lake
		// overflowing into the next basin, but the first map-border cell reached marks the level
		// at which everything would drain off the map: the lake can never rise above it.
		float level = sinkHeight;
		float maxLevel = targetLevel;
		{
			std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>, std::greater<std::pair<float, int>>> pq;
			std::vector<bool> visited(totalVerts, false);
			pq.push({ sinkHeight, sinkIdx });
			visited[sinkIdx] = true;
			while (!pq.empty())
			{
				auto [h, idx] = pq.top();
				pq.pop();
				level = std::max(level, h);
				if (level >= targetLevel) break;          // Requested depth reached first
				int cx = idx % gridRes, cz = idx / gridRes;
				if (cx == 0 || cz == 0 || cx == gridRes - 1 || cz == gridRes - 1) { maxLevel = level; break; }
				for (int dz = -1; dz <= 1; dz++) {
					for (int dx = -1; dx <= 1; dx++) {
						if (dx == 0 && dz == 0) continue;
						int nx = cx + dx, nz = cz + dz;
						if (nx < 0 || nx >= gridRes || nz < 0 || nz >= gridRes) continue;
						int nidx = nz * gridRes + nx;
						if (!visited[nidx]) { visited[nidx] = true; pq.push({ originalHeights[nidx], nidx }); }
					}
				}
			}
		}
		const float waterLevel = std::min(targetLevel, maxLevel) - 0.01f;
		if (waterLevel <= sinkHeight) continue;

		// --- 2. The lake: everything connected to the sink below the water level ---
		std::vector<int> lakePixels;
		{
			std::vector<bool> inLake(totalVerts, false);
			std::vector<int> stack = { sinkIdx };
			inLake[sinkIdx] = true;
			while (!stack.empty())
			{
				int idx = stack.back(); stack.pop_back();
				lakePixels.push_back(idx);
				if (isSink[idx]) sinkHandled[idx] = true; // Other river ends in the same lake
				int cx = idx % gridRes, cz = idx / gridRes;
				for (int dz = -1; dz <= 1; dz++) {
					for (int dx = -1; dx <= 1; dx++) {
						if (dx == 0 && dz == 0) continue;
						int nx = cx + dx, nz = cz + dz;
						if (nx < 0 || nx >= gridRes || nz < 0 || nz >= gridRes) continue;
						int nidx = nz * gridRes + nx;
						if (inLake[nidx] || lakeMask[nidx] || originalHeights[nidx] >= waterLevel) continue;
						inLake[nidx] = true;
						stack.push_back(nidx);
					}
				}
			}
		}

		// Minimum size for a realistic lake (50 cells of the 512 grid)
		if ((float)lakePixels.size() < 50.0f * gridScale * gridScale) continue;

		for (int idx : lakePixels) {
			lakeWaterLevel[idx] = waterLevel;
			lakeMask[idx] = true;
		}

		// --- 3. Lake bed: shelve down to lakeDepth ---
		// The water level can't rise above the basin's outlet, so on eroded (well drained) terrain
		// the natural lake is often only centimetres deep: a wet film, all shoreline foam. The
		// terrain defines the lake's SHAPE; lakeDepth defines how deep it is: the bed is dug from
		// 0 at the shore down to lakeDepth, over a shelf proportional to the lake's size.
		// (Never raised; a flat fixed-depth carve would leave a vertical step at the shore.)
		std::vector<int> shoreDist(totalVerts, -1);
		std::vector<int> frontier;
		for (int idx : lakePixels) {
			int cx = idx % gridRes, cz = idx / gridRes;
			bool shore = false;
			for (int k = 0; k < 4 && !shore; k++) {
				int nx = cx + (k == 0) - (k == 1), nz = cz + (k == 2) - (k == 3);
				if (nx < 0 || nx >= gridRes || nz < 0 || nz >= gridRes || lakeWaterLevel[nz * gridRes + nx] != waterLevel) shore = true;
			}
			if (shore) { shoreDist[idx] = 1; frontier.push_back(idx); }
		}
		for (size_t f = 0; f < frontier.size(); f++) {
			int idx = frontier[f];
			int cx = idx % gridRes, cz = idx / gridRes;
			for (int k = 0; k < 4; k++) {
				int nx = cx + (k == 0) - (k == 1), nz = cz + (k == 2) - (k == 3);
				if (nx < 0 || nx >= gridRes || nz < 0 || nz >= gridRes) continue;
				int nidx = nz * gridRes + nx;
				if (shoreDist[nidx] != -1 || lakeWaterLevel[nidx] != waterLevel) continue;
				shoreDist[nidx] = shoreDist[idx] + 1;
				frontier.push_back(nidx);
			}
		}
		const float shelfCells = std::clamp(std::sqrt((float)lakePixels.size()) * 0.25f, 2.0f * gridScale, 12.0f * gridScale);
		for (int idx : lakePixels) {
			float t = std::clamp((float)shoreDist[idx] / shelfCells, 0.0f, 1.0f);
			float shelf = t * t * (3.0f - 2.0f * t);
			float bed = waterLevel - std::clamp(lakeDepth, 0.0f, 50.0f) * shelf;
			float& terrainH = data.vertices[idx * 14 + 1];
			if (terrainH > bed) terrainH = bed;
		}
	}

	// height: the top of the channel's banks; the water stands half the channel's depth below it,
	// the bed a full depth. width: half the channel's width (m)
	struct SplinePoint { glm::vec2 pos; float volume; float lakeLevel; float height; float width = 0.0f; float depth = 0.0f; };
	// The flow accumulation (cells) at which a river has its base width and depth
	const float FULL_FLOW = HEAD * 60.0f;
	struct RiverData { std::vector<SplinePoint> path; bool isMerged; };
	std::vector<RiverData> fineRivers;

	auto clampGrid = [&](int v) { return std::max(0, std::min(v, gridRes - 1)); };
	auto catmullRom = [](glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, float t) -> glm::vec2 {
		float t2 = t * t, t3 = t2 * t;
		return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
	};

	for (auto& path : riverPaths) {
		if (path.size() < 2) continue;
		std::vector<glm::vec2> coarsePath;
		std::vector<float> coarseVolume;
		for (size_t pi = 0; pi < path.size(); pi++) {
			coarsePath.push_back(glm::vec2((float)path[pi].x, (float)path[pi].z));
			int idx = clampGrid(path[pi].z) * gridRes + clampGrid(path[pi].x);
			coarseVolume.push_back((float)std::max(1, flowVolume[idx]));
		}
		const int pathPasses = scaledPasses(10);
		for (int s = 0; s < pathPasses; s++) {
			for (size_t i = 1; i < coarsePath.size() - 1; i++) coarsePath[i] = (coarsePath[i - 1] + coarsePath[i] + coarsePath[i + 1]) / 3.0f;
		}

		int lakeHitIdx = -1; float lakeLevel = -1.0f;
		for (size_t i = 0; i < coarsePath.size(); i++) {
			int gx = clampGrid((int)coarsePath[i].x); int gz = clampGrid((int)coarsePath[i].y);
			if (lakeMask[gz * gridRes + gx]) { lakeHitIdx = (int)i; lakeLevel = lakeWaterLevel[gz * gridRes + gx]; break; }
		}

		if (coarsePath.size() < 2) continue;

		const int subdivisions = 12;
		std::vector<SplinePoint> finePath;
		for (size_t seg = 0; seg < coarsePath.size() - 1; seg++) {
			int i0 = (int)std::max((int)seg - 1, 0); int i1 = (int)seg;
			int i2 = (int)std::min(seg + 1, coarsePath.size() - 1); int i3 = (int)std::min(seg + 2, coarsePath.size() - 1);

			for (int sub = 0; sub < subdivisions; sub++) {
				float t = (float)sub / (float)subdivisions;
				glm::vec2 pt = catmullRom(coarsePath[i0], coarsePath[i1], coarsePath[i2], coarsePath[i3], t);
				float vol = glm::mix(coarseVolume[i1], coarseVolume[i2], t);
				int gx = clampGrid((int)pt.x); int gz = clampGrid((int)pt.y);
				float lvl = -1.0f;
				if (lakeHitIdx >= 0 && lakeMask[gz * gridRes + gx]) lvl = lakeLevel;
				finePath.push_back({ pt, vol, lvl, 0.0f });
			}
		}
		glm::vec2 pt = coarsePath.back();
		int gx = clampGrid((int)pt.x); int gz = clampGrid((int)pt.y);
		float lvl = -1.0f; if (lakeHitIdx >= 0 && lakeMask[gz * gridRes + gx]) lvl = lakeLevel;
		finePath.push_back({ pt, coarseVolume.back(), lvl, 0.0f });

		auto getOriginalH = [&](int x, int z) -> float { return data.vertices[(clampGrid(z) * gridRes + clampGrid(x)) * 14 + 1]; };
		for (auto& fp : finePath) {
			int x0 = clampGrid((int)std::floor(fp.pos.x)); int x1 = clampGrid(x0 + 1);
			int z0 = clampGrid((int)std::floor(fp.pos.y)); int z1 = clampGrid(z0 + 1);
			float tx = fp.pos.x - (float)x0; float tz = fp.pos.y - (float)z0;
			fp.height = glm::mix(glm::mix(getOriginalH(x0, z0), getOriginalH(x1, z0), tx), glm::mix(getOriginalH(x0, z1), getOriginalH(x1, z1), tx), tz);
		}

		// Size, from the water gathered upstream: a trickle at the spring, wider and deeper as the
		// valley's water joins (width ~ Q^0.5, depth ~ Q^0.4: hydraulic geometry, Leopold & Maddock
		// 1953), never narrower downstream, smoothed along the river
		{
			float grown = 0.0f;
			for (auto& fp : finePath) {
				int gx = clampGrid((int)std::round(fp.pos.x)), gz = clampGrid((int)std::round(fp.pos.y));
				float a = 0.0f;
				for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++)
					a = std::max(a, accumulation[clampGrid(gz + dz) * gridRes + clampGrid(gx + dx)]);
				grown = std::max(grown, a);
				float q = grown / FULL_FLOW;
				fp.width = baseWidth * glm::clamp(std::sqrt(q), 0.05f, 1.6f);
				fp.depth = baseDepth * glm::clamp(std::pow(q, 0.4f), 0.12f, 1.5f);
			}
			const int sizePasses = scaledPasses(6);
			for (int pass = 0; pass < sizePasses; pass++) {
				for (size_t i = 1; i + 1 < finePath.size(); i++) {
					finePath[i].width = (finePath[i - 1].width + 2.0f * finePath[i].width + finePath[i + 1].width) * 0.25f;
					finePath[i].depth = (finePath[i - 1].depth + 2.0f * finePath[i].depth + finePath[i + 1].depth) * 0.25f;
				}
			}
			// Never narrower than the terrain can hold: a channel under ~1.6 grid cells across has no
			// vertex inside it, so the ground can't dip under the water (it stood level with it, or
			// above: the brook hidden under its own banks and the grass)
			for (auto& fp : finePath) fp.width = std::max(fp.width, 0.8f * cellMetres);
			// The spring: the brook comes out of the ground at a point and opens over its first metres
			float along = 0.0f;
			for (size_t i = 0; i < finePath.size(); i++) {
				if (i > 0) along += glm::length(finePath[i].pos - finePath[i - 1].pos) * cellMetres;
				float t = glm::clamp(along / 15.0f, 0.0f, 1.0f);
				t = t * t * (3.0f - 2.0f * t);
				finePath[i].width *= t;
				finePath[i].depth *= glm::mix(0.3f, 1.0f, t);
			}
		}

		// The water's surface: the terrain with its hollows filled (the level water stands at in
		// them: it fills a dip up to its rim, then runs on), never rising downstream. Following the
		// lowest ground instead, the river dropped into every dip and then had to cut a gorge through
		// the rim after it - a valley tens of metres wide for a brook. Where the ground beside it or
		// under it is lower than the water, the carving raises it (what the river deposits: a bank,
		// a filled-in dip) instead of lowering everything around to the lowest point.
		{
			auto filledAt = [&](glm::vec2 g) {
				g = glm::clamp(g, glm::vec2(0.0f), glm::vec2((float)(gridRes - 1)));
				int x0 = clampGrid((int)std::floor(g.x)), z0 = clampGrid((int)std::floor(g.y));
				int x1 = clampGrid(x0 + 1), z1 = clampGrid(z0 + 1);
				float tx = g.x - (float)x0, tz = g.y - (float)z0;
				return glm::mix(glm::mix(filled[z0 * gridRes + x0], filled[z0 * gridRes + x1], tx),
					glm::mix(filled[z1 * gridRes + x0], filled[z1 * gridRes + x1], tx), tz);
			};
			// A hollow that holds a lake is not full: its water stands lower than the rim the filling
			// assumes. Running down into it, the river follows the ground down to the lake (filled
			// to the rim, it stood over the shore as a slab of water ending in the air)
			const size_t n = finePath.size();
			std::vector<float> level(n);
			for (size_t i = 0; i < n; i++) level[i] = std::min(filledAt(finePath[i].pos), finePath[i].height + 6.0f); // A deep hollow (no lake made of it) is not filled to the brim
			for (size_t i = 0; i < n; ) {
				if (level[i] <= finePath[i].height + 0.05f) { i++; continue; }
				size_t j = i;
				bool intoLake = false;
				while (j < n && level[j] > finePath[j].height + 0.05f) { intoLake |= finePath[j].lakeLevel >= 0.0f; j++; }
				if (j >= n || finePath[j].lakeLevel >= 0.0f) intoLake = true; // Ends in it (a lake, or off the map)
				if (intoLake) for (size_t k = i; k < j; k++) level[k] = finePath[k].height;
				i = j;
			}
			// The water stands a little under its banks (freeboard, more for a bigger river: a
			// brook's banks a hand over it, a river's most of a metre): brimful, the ground beside
			// it was barely over the water - on the terrain's grid often just under it, where the
			// surface could not end against a bank
			// And under the lower of its banks: where the ground just past the channel (a grid cell
			// beyond its edge, either side) is lower than that - a brook running along a low ridge,
			// across a shallow filled dip - the water sinks into its channel, by up to 40 cm (a river
			// cuts into a low ridge it follows). More than that, the carving raises that bank instead
			// (it never digs a gorge to stay under it).
			auto originalAt = [&](glm::vec2 g) {
				g = glm::clamp(g, glm::vec2(0.0f), glm::vec2((float)(gridRes - 1)));
				int x0 = clampGrid((int)std::floor(g.x)), z0 = clampGrid((int)std::floor(g.y));
				float tx = g.x - (float)x0, tz = g.y - (float)z0;
				return glm::mix(glm::mix(getOriginalH(x0, z0), getOriginalH(x0 + 1, z0), tx),
					glm::mix(getOriginalH(x0, z0 + 1), getOriginalH(x0 + 1, z0 + 1), tx), tz);
			};
			std::vector<float> surface(n), cap(n);
			for (size_t i = 0; i < n; i++) {
				float freeboard = 0.08f + 0.1f * finePath[i].depth;
				glm::vec2 d = finePath[std::min(i + 1, n - 1)].pos - finePath[i > 0 ? i - 1 : 0].pos;
				glm::vec2 across = glm::length(d) > 1e-5f ? glm::vec2(-d.y, d.x) / glm::length(d) : glm::vec2(1.0f, 0.0f);
				float reach = (finePath[i].width + cellMetres) / cellMetres; // Cells
				float lowBank = std::min(originalAt(finePath[i].pos + across * reach), originalAt(finePath[i].pos - across * reach));
				cap[i] = std::max(lowBank - 0.05f, level[i] - freeboard - 0.4f);
				surface[i] = std::min(level[i] - freeboard, cap[i]);
				if (finePath[i].lakeLevel >= 0.0f) surface[i] = std::min(surface[i], finePath[i].lakeLevel + lakeWaterOffset - 0.02f);
				if (i > 0) surface[i] = std::min(surface[i], surface[i - 1]);
			}
			// Smoothed both ways (cut at the top of a drop, filled at its foot, like a river grades
			// its bed): no steps, no bumps, no deep notch above every fall
			std::vector<float> tmp(surface.size());
			const int surfacePasses = scaledPasses(12);
			for (int pass = 0; pass < surfacePasses; pass++) {
				tmp = surface;
				for (size_t i = 1; i + 1 < surface.size(); i++)
					surface[i] = std::min((tmp[i - 1] + 2.0f * tmp[i] + tmp[i + 1]) * 0.25f, cap[i]);
				for (size_t i = 1; i < surface.size(); i++) surface[i] = std::min(surface[i], surface[i - 1]);
			}
			for (size_t i = 0; i < finePath.size(); i++) finePath[i].height = surface[i] + 0.5f * finePath[i].depth;
		}

		bool isMerged = false;
		int lastIdx = (int)path.size() - 1;
		int idx = clampGrid(path[lastIdx].z) * gridRes + clampGrid(path[lastIdx].x);
		if (pathID[idx] != currentPathID) isMerged = true;

		if (lakeHitIdx >= 0) {
			float targetVisualWater = lakeLevel + lakeWaterOffset;
			int entryIdx = -1;
			for (size_t i = 0; i < finePath.size(); i++) {
				if (finePath[i].lakeLevel > -1.0f) {
					entryIdx = (int)i;
					break;
				}
			}
			if (entryIdx >= 0 && entryIdx < (int)finePath.size() - 1) {
				float startH = finePath[entryIdx].height;
				if (startH > targetVisualWater) {
					for (size_t i = entryIdx; i < finePath.size(); i++) {
						float t = (float)(i - entryIdx) / (float)(finePath.size() - 1 - entryIdx);
						float slopedH = glm::mix(startH, targetVisualWater, t);
						if (finePath[i].height > slopedH) {
							finePath[i].height = slopedH;
						}
					}
				}
			}
		}

		fineRivers.push_back({finePath, isMerged});
		currentPathID++;
	}

	// Confluences: a tributary joins the river it runs into at that river's water level (a hair
	// below it, so the two surfaces never fight). Each surface computed on its own could put the
	// tributary higher: a crest of water standing up out of the junction. Only that excess is taken
	// off, fading out up the tributary over its last stretch (at least 20 m, at most a 5 % change of
	// its slope): the rest of it keeps its own profile (pulled onto a fixed grade, a steep brook was
	// dug a gorge a hundred metres deep).
	for (size_t r = 0; r < fineRivers.size(); r++) {
		auto& trib = fineRivers[r].path;
		if (!fineRivers[r].isMerged || trib.size() < 2) continue;
		glm::vec2 end = trib.back().pos;
		float best = 1e30f, target = 0.0f;
		for (size_t o = 0; o < fineRivers.size(); o++) {
			if (o == r) continue;
			for (const auto& q : fineRivers[o].path) {
				glm::vec2 d = q.pos - end;
				float d2 = glm::dot(d, d);
				if (d2 < best) { best = d2; target = q.height - 0.5f * q.depth; }
			}
		}
		const float joinCells = 8.0f * gridScale;
		if (best > joinCells * joinCells) continue; // No river within 8 cells (of the 512 grid): it ended in a lake or off the map
		target -= 0.02f;
		float excess = (trib.back().height - 0.5f * trib.back().depth) - target;
		if (excess <= 0.0f) continue;
		float stretch = std::max(20.0f, excess / 0.05f);
		float along = 0.0f;
		for (int i = (int)trib.size() - 1; i >= 0 && along < stretch; i--) {
			if (i < (int)trib.size() - 1) along += glm::length(trib[i + 1].pos - trib[i].pos) * cellMetres;
			float t = glm::clamp(along / stretch, 0.0f, 1.0f);
			trib[i].height -= excess * (1.0f - t * t * (3.0f - 2.0f * t));
		}
	}

	// Carving: the channel - its bed a full depth under its banks, rising to them at its edge -
	// then, where the river runs below the ground beside it (a notch in a slope, a gap through a
	// rim), the valley walls at their angle of repose (24-40 degrees, varying along the river)
	// instead of vertical cuts, their crest rounded into the untouched ground (smooth minimum).
	// Where the ground is lower than the water (a dip it filled, a low bank) it is raised instead:
	// the bed under the water, and a bank just above it falling away at the same angle - what a
	// river deposits. carved: the cells changed.
	std::vector<char> carved(totalVerts, 0);
	std::vector<float> channelFill(totalVerts, 1e30f); // Lowest bed any river wants here
	std::vector<float> bankFill(totalVerts, -1e30f);   // Highest bank any river wants here
	std::vector<char> inChannel(totalVerts, 0);       // Under some river's water
	std::vector<float> nearestDist(totalVerts, 1e9f); // Metres to the nearest river point
	std::vector<float> nearestWater(totalVerts, 0.0f); // That point's water level
	auto Hash1 = [](float x) { float f = std::sin(x * 127.1f) * 43758.5453f; return f - std::floor(f); };
	auto SmoothNoise1 = [&](float x) {
		float i0 = std::floor(x), f = x - i0;
		f = f * f * (3.0f - 2.0f * f);
		return glm::mix(Hash1(i0), Hash1(i0 + 1.0f), f);
	};
	int riverIndex = 0;
	for (const auto& riverData : fineRivers) {
		float along = 0.0f;
		for (size_t pi = 0; pi < riverData.path.size(); pi++) {
			const auto& pt = riverData.path[pi];
			if (pi > 0) along += glm::length(pt.pos - riverData.path[pi - 1].pos) * cellMetres;
			if (pt.width <= 0.01f) continue;
			float bank = pt.height, bed = pt.height - pt.depth;
			float slope = std::tan(glm::radians(glm::mix(24.0f, 40.0f, SmoothNoise1(along / 60.0f + riverIndex * 17.0f))));
			int cx = clampGrid((int)std::round(pt.pos.x)), cz = clampGrid((int)std::round(pt.pos.y));
			float water = pt.height - 0.5f * pt.depth;
			float cut = std::max(originalHeights[cz * gridRes + cx] - bank, 0.0f);
			float fill = std::max(water + 0.1f - originalHeights[cz * gridRes + cx], 0.0f);
			float crest = glm::clamp(cut * 0.3f, 0.15f, 3.0f); // A shallow river keeps sharp banks
			// A low bank is raised to a crest just over the water a grid cell wide (narrower, the
			// terrain's vertices could not hold it as one ridge, least of all along a diagonal), then
			// falls away at the same angle: the water surface runs into the bank there, it never
			// stands over lower ground behind it
			const float bankCrest = cellMetres;
			int gridRadius = (int)std::ceil((pt.width + bankCrest + std::max(cut, fill) / slope + crest * 2.0f) / cellMetres) + 1;
			gridRadius = std::min(gridRadius, gridRes / 4);

			for (int rz = -gridRadius; rz <= gridRadius; rz++) {
				for (int rx = -gridRadius; rx <= gridRadius; rx++) {
					int nx = cx + rx, nz = cz + rz;
					if (nx < 0 || nx >= gridRes || nz < 0 || nz >= gridRes) continue;
					int nidx = nz * gridRes + nx;
					if (lakeMask[nidx]) {
						float visualLakeWater = lakeWaterLevel[nidx] + lakeWaterOffset;
						float preRiverHeight = std::min(originalHeights[nidx], lakeWaterLevel[nidx] - baseDepth);
						if (preRiverHeight < visualLakeWater) continue; // Under the lake already
					}
					float dist = glm::length(glm::vec2((float)nx, (float)nz) - pt.pos) * cellMetres;
					if (dist <= pt.width) inChannel[nidx] = 1;
					if (dist < nearestDist[nidx]) { nearestDist[nidx] = dist; nearestWater[nidx] = pt.height - 0.5f * pt.depth; }
					float target;
					if (dist <= pt.width) {
						// A parabolic bed meeting the water at the water's edge (rising to the bank
						// level inside the river, it crossed the surface halfway out: half the water
						// was under ground)
						float t = dist / pt.width;
						target = glm::mix(bed, water + 0.02f, t * t);
						if (pt.lakeLevel < 0.0f) channelFill[nidx] = std::min(channelFill[nidx], glm::mix(bed, water - 0.05f, t));
					} else {
						target = std::max(water + 0.15f, bank - pt.depth * 0.35f) + (dist - pt.width) * slope; // Low banks: a few cm to tens of cm over the water
						if (pt.lakeLevel < 0.0f) bankFill[nidx] = std::max(bankFill[nidx], water + 0.1f - std::max(dist - pt.width - bankCrest, 0.0f) * slope);
					}
					float ground = originalHeights[nidx];
					float h = glm::clamp(0.5f + 0.5f * (ground - target) / crest, 0.0f, 1.0f);
					float smoothed = glm::mix(ground, target, h) - crest * h * (1.0f - h);
					float& terrainH = data.vertices[nidx * 14 + 1];
					if (smoothed < terrainH - 0.01f) { terrainH = smoothed; carved[nidx] = 1; }
				}
			}
		}
		riverIndex++;
	}
	// The deposits: raised where the ground is under them (a bank never fills another river's channel)
	std::vector<float> raisedTo(totalVerts, -1e30f);
	for (int i = 0; i < totalVerts; i++) {
		if (lakeMask[i]) continue; // The lake's shore and bed are the lake's
		float want = inChannel[i] ? (channelFill[i] < 1e29f ? channelFill[i] : -1e30f) : bankFill[i];
		float& terrainH = data.vertices[i * 14 + 1];
		if (want > terrainH + 0.01f) { terrainH = want; raisedTo[i] = want; carved[i] = 1; }
	}

	// Smoothing of what the rivers carved and a ring around it (no step or facet is left), and of
	// the lake beds; a lake's banks are left alone unless a river cut them (lowered under the water
	// they would let the lake spill over them)
	if (smoothPasses > 0) {
		std::vector<char> nearLake(totalVerts, 0), smoothMask(totalVerts, 0);
		const int lakeRing = scaledCells(4.0f), smoothRing = scaledCells(2.0f);
		for (int z = 0; z < gridRes; z++) {
			for (int x = 0; x < gridRes; x++) {
				if (!lakeMask[z * gridRes + x]) continue;
				for (int dz = -lakeRing; dz <= lakeRing; dz++) for (int dx = -lakeRing; dx <= lakeRing; dx++) {
					int nx = x + dx, nz = z + dz;
					if (nx >= 0 && nx < gridRes && nz >= 0 && nz < gridRes) nearLake[nz * gridRes + nx] = 1;
				}
			}
		}
		for (int z = 0; z < gridRes; z++) {
			for (int x = 0; x < gridRes; x++) {
				int idx = z * gridRes + x;
				if (!(carved[idx] || lakeMask[idx])) continue;
				for (int dz = -smoothRing; dz <= smoothRing; dz++) for (int dx = -smoothRing; dx <= smoothRing; dx++) {
					int nx = x + dx, nz = z + dz;
					if (nx < 0 || nx >= gridRes || nz < 0 || nz >= gridRes) continue;
					int nidx = nz * gridRes + nx;
					if (nearLake[nidx] && !lakeMask[nidx] && !carved[nidx]) continue;
					smoothMask[nidx] = 1;
				}
			}
		}
		std::vector<float> heightBuffer(totalVerts);
		const int terrainPasses = scaledPasses(smoothPasses); // Passes on the 512 grid
		for (int p = 0; p < terrainPasses; p++) {
			for (int i = 0; i < totalVerts; i++) heightBuffer[i] = data.vertices[i * 14 + 1];
			for (int z = 1; z < gridRes - 1; z++) {
				for (int x = 1; x < gridRes - 1; x++) {
					int idx = z * gridRes + x;
					if (!smoothMask[idx]) continue;
					// 3x3 average (weights 4 / 2 / 1), round with no direction preferred; a channel or
					// a lake bed is never raised (they keep their depth)
					float sum = heightBuffer[idx] * 4.0f
						+ (heightBuffer[idx - 1] + heightBuffer[idx + 1] + heightBuffer[idx - gridRes] + heightBuffer[idx + gridRes]) * 2.0f
						+ heightBuffer[idx - gridRes - 1] + heightBuffer[idx - gridRes + 1] + heightBuffer[idx + gridRes - 1] + heightBuffer[idx + gridRes + 1];
					float avg = sum / 16.0f;
					data.vertices[idx * 14 + 1] = (carved[idx] || lakeMask[idx]) ? std::min(avg, heightBuffer[idx]) : avg;
				}
			}
		}
	}

	// The banks stay above the water: ground beside a river (not under its water) that the
	// smoothing pulled toward the channel is held a few centimetres above the nearest water level
	// (never above where it stood), so the river never spills over a bank nor ends in a wall of water
	for (int i = 0; i < totalVerts; i++) {
		if (inChannel[i] || nearestDist[i] > 40.0f || lakeMask[i]) continue;
		float floorH = std::min(nearestWater[i] + 0.08f, std::max(originalHeights[i], raisedTo[i]));
		float& terrainH = data.vertices[i * 14 + 1];
		if (terrainH < floorH) terrainH = floorH;
	}

	// The water's edge: the ground across it a continuous slope through the water level at the
	// channel's edge - out of the water at the bank's own slope (from the water to the bank a
	// cell's diagonal further out: a flat floodplain, a gentle shore; a cut bank, a steep one),
	// under it the bed's curve (below) - out of the water over a cell's diagonal, eased into the
	// ground beyond. The shoreline, where the ground between two vertices
	// crosses the water, then follows the channel's edge, a curve along the river: exactly where
	// the bank is as steep as the bed, within a fraction of a cell where it is flatter (where the
	// shore is that flat its edge is soft: the water's depth fades over metres). Before, decided
	// vertex by vertex - in the channel and dug, or on the bank and held over the water - the
	// ground stepped from a metre and a half under the water to a few centimetres over it between
	// two vertices: the shoreline sat on the first bank vertex and followed the grid, a staircase
	// a cell wide wherever a river crosses the grid at an angle, at any resolution. (How terrain
	// tools shape a riverbank: a falloff profile of the distance to the river's spline, Unreal's
	// water brush; here over the reach of a grid cell, so its triangles hold it.)
	{
		const float zone = 1.42f * cellMetres;     // The farthest a vertex across the edge can be (a cell's diagonal)
		const float ease = cellMetres;             // Into the ground beyond
		const float S_MIN = 0.03f, S_MAX = 0.7f;    // Bank slopes: a floodplain (2 deg) .. a cut bank (35 deg)
		auto heightAt = [&](glm::vec2 g) {
			g = glm::clamp(g, glm::vec2(0.0f), glm::vec2((float)(gridRes - 1)));
			int x0 = std::min((int)g.x, gridRes - 2), z0 = std::min((int)g.y, gridRes - 2);
			float tx = g.x - (float)x0, tz = g.y - (float)z0;
			auto at = [&](int x, int z) { return data.vertices[(z * gridRes + x) * 14 + 1]; };
			return glm::mix(glm::mix(at(x0, z0), at(x0 + 1, z0), tx), glm::mix(at(x0, z0 + 1), at(x0 + 1, z0 + 1), tx), tz);
		};
		// Signed distance to the water's edge (< 0: in a river), over all the rivers (a confluence:
		// in either), with the water level, the bank's slope and the bed there
		std::vector<float> edgeDelta(totalVerts, 1e9f), edgeWater(totalVerts, 0.0f), edgeSlope(totalVerts, S_MIN), edgeBed(totalVerts, -1e9f), edgeWidth(totalVerts, 0.0f);
		for (const auto& riverData : fineRivers) {
			const auto& path = riverData.path;
			const size_t n = path.size();
			if (n < 2) continue;
			std::vector<glm::vec2> side(n);
			std::vector<float> along(n, 0.0f), slope[2] = { std::vector<float>(n, S_MIN), std::vector<float>(n, S_MIN) };
			for (size_t i = 0; i < n; i++) {
				glm::vec2 d = path[std::min(i + 1, n - 1)].pos - path[i > 0 ? i - 1 : 0].pos;
				float len = glm::length(d);
				side[i] = len > 1e-6f ? glm::vec2(-d.y, d.x) / len : glm::vec2(1.0f, 0.0f);
				if (i > 0) along[i] = along[i - 1] + glm::length(path[i].pos - path[i - 1].pos) * cellMetres;
				float water = path[i].height - 0.5f * path[i].depth;
				for (int k = 0; k < 2; k++) {
					float bank = heightAt(path[i].pos + side[i] * ((k == 0 ? 1.0f : -1.0f) * (path[i].width + zone) / cellMetres));
					slope[k][i] = glm::clamp((bank - water) / zone, S_MIN, S_MAX);
				}
			}
			// Along the river, averaged over 30 m: a bank's slope changes gradually
			for (int k = 0; k < 2; k++) {
				std::vector<double> sum(n + 1, 0.0);
				for (size_t i = 0; i < n; i++) sum[i + 1] = sum[i] + slope[k][i];
				std::vector<float> avg(n);
				size_t lo = 0, hi = 0;
				for (size_t i = 0; i < n; i++) {
					while (along[i] - along[lo] > 15.0f) lo++;
					while (hi + 1 < n && along[hi + 1] - along[i] <= 15.0f) hi++;
					avg[i] = (float)((sum[hi + 1] - sum[lo]) / (double)(hi + 1 - lo));
				}
				slope[k] = avg;
			}
			for (size_t i = 0; i < n; i++) {
				const auto& pt = path[i];
				if (pt.width <= 0.01f || pt.lakeLevel >= 0.0f) continue;
				float water = pt.height - 0.5f * pt.depth;
				int radius = (int)std::ceil((pt.width + 2.0f * zone) / cellMetres) + 1;
				int cx = clampGrid((int)std::round(pt.pos.x)), cz = clampGrid((int)std::round(pt.pos.y));
				for (int rz = -radius; rz <= radius; rz++) {
					for (int rx = -radius; rx <= radius; rx++) {
						int nx = cx + rx, nz = cz + rz;
						if (nx < 0 || nz < 0 || nx >= gridRes || nz >= gridRes) continue;
						int idx = nz * gridRes + nx;
						glm::vec2 off = glm::vec2((float)nx, (float)nz) - pt.pos;
						float delta = glm::length(off) * cellMetres - pt.width;
						if (delta >= edgeDelta[idx]) continue;
						edgeDelta[idx] = delta;
						edgeWater[idx] = water;
						edgeSlope[idx] = slope[glm::dot(off, side[i]) >= 0.0f ? 0 : 1][i];
						edgeBed[idx] = water - 0.5f * pt.depth;
						edgeWidth[idx] = pt.width;
					}
				}
			}
		}
		for (int i = 0; i < totalVerts; i++) {
			float delta = edgeDelta[i];
			if (delta > zone + ease || lakeMask[i]) continue;
			float& terrainH = data.vertices[i * 14 + 1];
			float h;
			if (delta >= 0.0f) {
				float ramp = edgeWater[i] + edgeSlope[i] * delta;
				h = delta <= zone ? ramp : glm::mix(ramp, terrainH, glm::smoothstep(zone, zone + ease, delta));
			} else {
				// The bed: a parabola across the channel, from the water at its edge down to the
				// centre, as deep as the river is or as the grid can hold. A curve drawn by vertices a
				// cell apart is only as smooth as it is gentle: the triangles between them cut its
				// bottom short wherever no vertex falls on the centreline, by about
				// 0.4 x depth x (cell / half-width)^2. So it is at most 0.3 x (half-width / cell)^2
				// deep - a pool and a sill under every vertex the river passed, otherwise, along it
				// (a 6 m brook on a 3.9 m grid: 20 cm; 16 m wide: 1.3 m; twice the resolution, four
				// times as deep). Smoothed along the river with its width, never under its bed.
				float w = std::max(edgeWidth[i], 1e-3f);
				float r = glm::clamp((w + delta) / w, 0.0f, 1.0f);
				float gridDepth = 0.3f * (w / cellMetres) * (w / cellMetres);
				float depthHere = std::min(edgeWater[i] - edgeBed[i], gridDepth);
				h = edgeWater[i] - depthHere * (1.0f - r * r);
			}
			if (std::abs(h - terrainH) > 0.005f) carved[i] = 1;
			terrainH = h;
		}
	}
	// How much ground the rivers moved (shown under the node): a brook should barely touch it
	std::string riverStats;
	{
		int cutCells = 0, raisedCells = 0;
		float maxCut = 0.0f, maxRaise = 0.0f;
		for (int i = 0; i < totalVerts; i++) {
			if (lakeMask[i]) continue;
			float d = originalHeights[i] - data.vertices[i * 14 + 1];
			if (d > 1.0f) cutCells++;
			if (d < -0.3f) raisedCells++;
			maxCut = std::max(maxCut, d);
			maxRaise = std::max(maxRaise, -d);
		}
		float cellArea = cellMetres * cellMetres;
		char msg[160];
		snprintf(msg, sizeof(msg), "%d rivers: %.0f m2 dug over 1 m (deepest %.1f m), %.0f m2 raised (highest %.1f m)",
			(int)fineRivers.size(), cutCells * cellArea, maxCut, raisedCells * cellArea, maxRaise);
		riverStats = msg;
		ctx.Info(riverStats);
	}
	RecomputeNormals(data, gridRes);

	progress(80.0f, "Generating Water Meshes...");

	MeshData riverMesh;
	MeshData lakeMesh;
	glm::vec3 up(0, 1, 0);
	float waterMeshWidthMultiplier = 1.15f;
	float rOffset = riverWaterOffset;
	// World size of one grid cell (river path positions are in grid units)
	const float cellWorldSize = std::abs(data.vertices[14] - data.vertices[0]);

	// Helper: bilinear sample carved terrain height at a world XZ position
	auto sampleCarvedTerrainAtWorld = [&](float wx, float wz) -> float {
		float gxf = ((wx / terrainScale.x) + 1.0f) * 0.5f * (float)(gridRes - 1);
		float gzf = ((wz / terrainScale.z) + 1.0f) * 0.5f * (float)(gridRes - 1);
		int gx0 = std::max(0, std::min((int)std::floor(gxf), gridRes - 2));
		int gz0 = std::max(0, std::min((int)std::floor(gzf), gridRes - 2));
		float txs = glm::clamp(gxf - (float)gx0, 0.0f, 1.0f);
		float tzs = glm::clamp(gzf - (float)gz0, 0.0f, 1.0f);
		float h00 = data.vertices[(gz0 * gridRes + gx0) * 14 + 1];
		float h10 = data.vertices[(gz0 * gridRes + gx0 + 1) * 14 + 1];
		float h01 = data.vertices[((gz0 + 1) * gridRes + gx0) * 14 + 1];
		float h11 = data.vertices[((gz0 + 1) * gridRes + gx0 + 1) * 14 + 1];
		return glm::mix(glm::mix(h00, h10, txs), glm::mix(h01, h11, txs), tzs);
	};

	int bankSides = 0, lowBankSides = 0; // Rows' sides (off the lake mouths), and those with no bank over the water within reach
	for (const auto& riverData : fineRivers)
	{
		int baseIdx = riverMesh.GetVertexCount();
		int emittedVerts = 0;

		// Pre-scan: find the first segment that enters a lake for smooth blending
		int lakeEntryIdx = -1;
		float lakeEntryLevel = -1.0f;
		for (size_t j = 0; j < riverData.path.size(); j++) {
			if (riverData.path[j].lakeLevel > -1.0f) {
				lakeEntryIdx = (int)j;
				lakeEntryLevel = riverData.path[j].lakeLevel;
				break;
			}
		}

		// River mouth: the last metres before the lake the river fades out over the lake surface,
		// which reaches up the channel and fades the other way (lake mesh below): they cross-fade
		// instead of meeting on a line. Distance along the path to the lake entry per point.
		const float MOUTH_FADE_METRES = 20.0f;  // River opacity 1 -> 0 over this
		const float MOUTH_RAISE_METRES = 45.0f; // Kept just above the lake surface over this (the lake is drawn first)
		std::vector<float> toLake(riverData.path.size(), -1.0f);
		if (lakeEntryIdx >= 0) {
			float along = 0.0f;
			for (int j = lakeEntryIdx; j >= 0; j--) {
				if (j < lakeEntryIdx) along += glm::length(riverData.path[j + 1].pos - riverData.path[j].pos) * cellWorldSize;
				toLake[j] = along;
			}
			for (size_t j = lakeEntryIdx; j < riverData.path.size(); j++) toLake[j] = 0.0f;
		}

		for (size_t i = 0; i < riverData.path.size(); i++)
		{
			float fx = riverData.path[i].pos.x;
			float fz = riverData.path[i].pos.y;
			fx = glm::clamp(fx, 0.0f, (float)(gridRes - 1));
			fz = glm::clamp(fz, 0.0f, (float)(gridRes - 1));

			float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
			int x0 = clampGrid((int)std::floor(fx)); int x1 = clampGrid(x0 + 1);
			int z0 = clampGrid((int)std::floor(fz)); int z1 = clampGrid(z0 + 1);
			float tx = fx - (float)x0; float tz = fz - (float)z0;
			tx = glm::clamp(tx, 0.0f, 1.0f); tz = glm::clamp(tz, 0.0f, 1.0f);

			posX = glm::mix(data.vertices[(z0 * gridRes + x0) * 14], data.vertices[(z0 * gridRes + x1) * 14], tx);
			posZ = glm::mix(data.vertices[(z0 * gridRes + x0) * 14 + 2], data.vertices[(z1 * gridRes + x0) * 14 + 2], tz);

			auto surfaceHeight = [&](size_t k) {
				float h = riverData.path[k].height - riverData.path[k].depth * 0.5f;
				// Once the river has entered the lake it dives under the lake surface, so the
				// (depth-writing) lake hides it instead of the river ribbon lying on top of it.
				if (lakeEntryIdx >= 0 && (int)k >= lakeEntryIdx)
					h = std::min(h, lakeEntryLevel + lakeWaterOffset - rOffset - 0.4f);
				// Before it, over the mouth, it stays 5 cm above the lake so it is drawn over the
				// lake's end while fading out (it would otherwise dive under it on one segment)
				else if (toLake[k] >= 0.0f && toLake[k] < MOUTH_RAISE_METRES)
					h = std::max(h, lakeEntryLevel + lakeWaterOffset - rOffset + 0.05f);
				return h;
			};
			posY = surfaceHeight(i);

			glm::vec3 dir;
			if (i == 0) {
				glm::vec2 d = riverData.path[i + 1].pos - riverData.path[i].pos;
				dir = glm::normalize(glm::vec3(d.x, 0, d.y));
			} else if (i == riverData.path.size() - 1) {
				glm::vec2 d = riverData.path[i].pos - riverData.path[i - 1].pos;
				dir = glm::normalize(glm::vec3(d.x, 0, d.y));
			} else {
				glm::vec2 d = riverData.path[i + 1].pos - riverData.path[i - 1].pos;
				float len = glm::length(d);
				if (len > 0.0001f) dir = glm::normalize(glm::vec3(d.x, 0, d.y));
				else dir = glm::vec3(1, 0, 0);
			}
			
			glm::vec3 right = glm::normalize(glm::cross(dir, up));
			glm::vec3 center(posX, posY + rOffset, posZ);

			// Real surface normal from the downstream slope (river.frag uses it for flow speed
			// and rapids foam; a constant 'up' made every river look like flat still water)
			glm::vec3 surfaceNormal = up;
			if (riverData.path.size() >= 2) {
				size_t i0 = (i == 0) ? 0 : i - 1;
				size_t i1 = std::min(i + 1, riverData.path.size() - 1);
				float run = glm::length(riverData.path[i1].pos - riverData.path[i0].pos) * cellWorldSize;
				if (run > 1e-4f) {
					float slope = (surfaceHeight(i1) - surfaceHeight(i0)) / run; // < 0 downstream
					glm::vec3 forward = glm::normalize(dir + up * slope);
					surfaceNormal = glm::normalize(glm::cross(right, forward));
					if (surfaceNormal.y < 0.0f) surfaceNormal = -surfaceNormal;
				}
			}
			
			// Its width, opening from a point at the spring (riverData.path: the channel's)
			float currentWidth = riverData.path[i].width;

			if (riverData.isMerged && i > riverData.path.size() - 6) {
				float t = (float)(riverData.path.size() - 1 - i) / 5.0f;
				currentWidth *= t;
			}
			
			// UV: u = across the channel (river.frag; 0 / 1 at its edges, < 0 / > 1 under the banks),
			// v = -1 - opacity at a lake mouth (-2 = the open river, -1 = faded out at the lake entry;
			// meshes without the marker keep v >= 0)
			float mouthFade = toLake[i] >= 0.0f ? glm::clamp(toLake[i] / MOUTH_FADE_METRES, 0.0f, 1.0f) : 1.0f;
			float vCoord = -1.0f - mouthFade;
			const glm::vec3& n = surfaceNormal;

			// A row across the river: ONE flat surface at the water's height - water finds its level,
			// it never drapes over its bed nor bends down at its edges - reaching past the channel
			// into the banks, which cut it along their contour: the shoreline is the terrain's own
			// line (softened by the water's depth, river.frag). The way river tools build them
			// (CryEngine, Arma, Unreal: the water wider than the river, the terrain decides what
			// shows). Each side runs out to where its bank stands above the water, then a little
			// under it, never on into a dip behind it (water there would float over lower ground),
			// and on the inside of a bend never past its centre (the rows would fold over).
			// Where no bank rises within reach (a low bank, a lake mouth) the edge dives a little
			// under the ground and the lake instead.
			float halfWidth = std::max(currentWidth * waterMeshWidthMultiplier, 0.05f);
			float innerRadius = 1e9f;
			int innerSide = 0;
			{
				const size_t w = (size_t)scaledCells(3.0f);
				size_t a = i >= w ? i - w : 0, c = std::min(i + w, riverData.path.size() - 1);
				glm::vec2 d1 = riverData.path[i].pos - riverData.path[a].pos, d2 = riverData.path[c].pos - riverData.path[i].pos;
				float l1 = glm::length(d1), l2 = glm::length(d2);
				if (l1 > 1e-5f && l2 > 1e-5f) {
					d1 /= l1; d2 /= l2;
					float angle = std::acos(glm::clamp(glm::dot(d1, d2), -1.0f, 1.0f));
					if (angle > 1e-4f) {
						innerRadius = 0.5f * (l1 + l2) * cellWorldSize / angle;
						innerSide = (d1.x * d2.y - d1.y * d2.x) > 0.0f ? 1 : -1; // The side 'right' points to
					}
				}
			}
			const float waterY = center.y;
			auto groundAt = [&](float side, float dist) {
				glm::vec3 q = center + right * (side * dist);
				return sampleCarvedTerrainAtWorld(q.x, q.z);
			};
			auto bankReach = [&](float side, bool& low) {
				const float STEP = 0.25f, TUCK = 0.75f;
				// ~ twice the river, at least 2 m and a grid cell and a half past it (the terrain's first
				// vertices beside the channel hold the bank only at their own points: between them it
				// dips toward the bed; the next ones stand clear of it)
				float maxReach = halfWidth + std::max(std::max(2.0f, halfWidth), 1.5f * cellWorldSize);
				if ((int)side == innerSide) maxReach = std::min(maxReach, std::max(innerRadius * 0.8f, halfWidth));
				low = true;
				float s = halfWidth * 0.5f, best = -1e30f, bestAt = halfWidth;
				for (; s <= maxReach; s += STEP) {
					float g = groundAt(side, s);
					if (g >= waterY + 0.01f) { low = false; break; }
					if (s >= halfWidth && g > best) { best = g; bestAt = s; }
				}
				if (low) {
					// A bank a few centimetres under the water (the terrain's grid between its vertices):
					// the surface ends flat on it, as good as transparent there (river.frag: depth fade)
					if (best >= waterY - 0.05f) { low = false; return std::max(bestAt, halfWidth); }
					// Really lower ground (a lake shore, the river it flows into): the edge goes under it
					// far out, so the strip toward it barely tilts
					return maxReach;
				}
				float reach = s;
				for (float e = s + STEP; e <= std::min(s + TUCK, maxReach); e += STEP) {
					if (groundAt(side, e) < waterY + 0.01f) break; // A dip behind the bank
					reach = e;
				}
				return std::max(reach, halfWidth);
			};
			bool lowLeft = false, lowRight = false;
			float reachLeft = bankReach(-1.0f, lowLeft), reachRight = bankReach(1.0f, lowRight);
			bool atMouth = toLake[i] >= 0.0f;
			if (!atMouth) {
				lowBankSides += (lowLeft ? 1 : 0) + (lowRight ? 1 : 0);
				bankSides += 2;
			}

			// Offsets across (metres, + = 'right'): under the left bank, the channel in 8 strips (the
			// waves need the vertices), under the right bank
			const int COLUMNS = 11;
			float offsets[COLUMNS];
			offsets[0] = -reachLeft;
			for (int c = 0; c <= 8; c++) offsets[c + 1] = halfWidth * ((float)c / 4.0f - 1.0f);
			offsets[COLUMNS - 1] = reachRight;
			for (int c = 0; c < COLUMNS; c++) {
				glm::vec3 p = center + right * offsets[c];
				float u = 0.5f + 0.5f * offsets[c] / halfWidth;
				bool lowEdge = (c == 0 && lowLeft) || (c == COLUMNS - 1 && lowRight);
				if (lowEdge) {
					float under = sampleCarvedTerrainAtWorld(p.x, p.z) - 0.1f;
					if (lakeEntryIdx >= 0) under = std::min(under, lakeEntryLevel + lakeWaterOffset - 0.1f);
					p.y = std::min(p.y, under);
				}
				riverMesh.AddVertex(p.x, p.y, p.z, u, vCoord, n.x, n.y, n.z, right.x, right.y, right.z, dir.x, dir.y, dir.z);
			}

			if (emittedVerts > 0)
			{
				int curr = baseIdx + emittedVerts * COLUMNS, prev = curr - COLUMNS;
				for (int c = 0; c + 1 < COLUMNS; c++) {
					riverMesh.AddTriangle(prev + c, curr + c + 1, curr + c);
					riverMesh.AddTriangle(prev + c, prev + c + 1, curr + c + 1);
				}
			}
			emittedVerts++;
		}
	}
	if (bankSides > 0) {
		char msg[96];
		snprintf(msg, sizeof(msg), "; water reaches a bank on %.1f %% of its sides", 100.0 * (bankSides - lowBankSides) / bankSides);
		ctx.Info(riverStats + msg);
	}

	// 6b. Lake Meshes (Static)
	std::vector<int> lakePixelList;
	for (int i = 0; i < totalVerts; i++) if (lakeWaterLevel[i] > -1.0f) lakePixelList.push_back(i);
	std::vector<int> expandedPixels = lakePixelList;
	std::vector<bool> isExpanded(totalVerts, false);
	for (int idx : lakePixelList) isExpanded[idx] = true;
	for(int pass=0; pass<5; pass++) {
		std::vector<int> currentPass = expandedPixels;
		for (int idx : currentPass) {
			for (int dz = -1; dz <= 1; dz++) {
				for (int dx = -1; dx <= 1; dx++) {
					int nx = (idx % gridRes) + dx; int nz = (idx / gridRes) + dz;
					if (nx >= 0 && nx < gridRes && nz >= 0 && nz < gridRes) {
						int nidx = nz * gridRes + nx;
						if (!isExpanded[nidx]) { 
							float waterLevel = lakeWaterLevel[idx];
							// Into the banks only: terrain at or above the water hides the surface under it, so the
							// shore follows the terrain's contour. Terrain BELOW the water that is not part of the lake
							// lies past a rim (the basin's flood fill stopped there): water over it would float in the
							// air, over a cliff or the next valley.
							if (originalHeights[nidx] >= waterLevel) {
								expandedPixels.push_back(nidx); isExpanded[nidx] = true; 
								if (lakeWaterLevel[nidx] < 0) lakeWaterLevel[nidx] = waterLevel;
							}
						}
					}
				}
			}
		}
	}
	// River mouths: the lake was grown over the terrain as it was before the rivers were carved,
	// so it stopped on a grid edge across each river channel - a straight line over deep water,
	// where the shoreline fade (water depth) cannot hide it. Grow it a little further up every
	// channel whose carved bed is still below the water, ring by ring, and fade it out over those
	// rings (vertex UV, water.vert): the river and the lake cross-fade instead of meeting on a line.
	const float MOUTH_FADE_METRES = 24.0f;
	const int mouthRings = std::max(2, (int)std::ceil(MOUTH_FADE_METRES / std::max(cellWorldSize, 0.01f)));
	std::vector<int> lakeRing(totalVerts, 0); // Rings grown up a channel (0 = the lake itself)
	for (int ring = 1; ring <= mouthRings; ring++) {
		std::vector<int> currentPass = expandedPixels;
		for (int idx : currentPass) {
			for (int dz = -1; dz <= 1; dz++) {
				for (int dx = -1; dx <= 1; dx++) {
					int nx = (idx % gridRes) + dx; int nz = (idx / gridRes) + dz;
					if (nx < 0 || nx >= gridRes || nz < 0 || nz >= gridRes) continue;
					int nidx = nz * gridRes + nx;
					if (isExpanded[nidx]) continue;
					if (data.vertices[nidx * 14 + 1] >= lakeWaterLevel[idx] + lakeWaterOffset - 0.05f) continue; // Bed above the water
					// A channel the river cut into a bank: the ground was at or above the water before it was
					// dug. Ground that was already lower is outside the basin (an outflow running downhill, a
					// slope past the rim): the lake must not follow it.
					if (originalHeights[nidx] < lakeWaterLevel[idx] - 0.05f) continue;
					expandedPixels.push_back(nidx); isExpanded[nidx] = true;
					lakeWaterLevel[nidx] = lakeWaterLevel[idx];
					lakeRing[nidx] = ring;
				}
			}
		}
	}

	// Shore skirt: two more rings into the banks, so the flat lake surface runs a little INTO them. The depth test then cuts it exactly along the terrain contour and the
	// shader fades it over the first centimetres of depth. (Skipping every vertex above the water
	// level instead ended the lake on whole grid cells short of the shore -> visible staircase.)
	for (int ring = 0; ring < 2; ring++) {
		std::vector<int> currentPass = expandedPixels;
		for (int idx : currentPass) {
			for (int dz = -1; dz <= 1; dz++) {
				for (int dx = -1; dx <= 1; dx++) {
					int nx = (idx % gridRes) + dx; int nz = (idx / gridRes) + dz;
					if (nx < 0 || nx >= gridRes || nz < 0 || nz >= gridRes) continue;
					int nidx = nz * gridRes + nx;
					if (isExpanded[nidx]) continue;
					// Into banks only (ground that stood at or above the water before the rivers were carved
					// and smoothed), never onto lower ground past the basin's rim
					if (originalHeights[nidx] < lakeWaterLevel[idx] - 0.05f) continue;
					expandedPixels.push_back(nidx); isExpanded[nidx] = true;
					if (lakeWaterLevel[nidx] < 0) lakeWaterLevel[nidx] = lakeWaterLevel[idx];
					if (lakeRing[idx] > 0) lakeRing[nidx] = lakeRing[idx] + 1; // Past a channel: keeps fading
				}
			}
		}
	}

	std::map<int, int> terrainToWaterIdx;
	for (int idx : expandedPixels) {
		float wLevel = lakeWaterLevel[idx];

		float x_pos = data.vertices[idx * 14]; float z_pos = data.vertices[idx * 14 + 2];
		float y_pos = wLevel + lakeWaterOffset;
		// The lake's outermost vertices sink 40 cm under the ground: wherever the carved and
		// smoothed banks ended up lower than the water, the surface still runs into them instead of
		// ending in the open on a grid line (a straight cut)
		bool border = false;
		for (int dz = -1; dz <= 1 && !border; dz++) {
			for (int dx = -1; dx <= 1 && !border; dx++) {
				int nx = (idx % gridRes) + dx, nz = (idx / gridRes) + dz;
				if (nx < 0 || nx >= gridRes || nz < 0 || nz >= gridRes || !isExpanded[nz * gridRes + nx]) border = true;
			}
		}
		if (border) y_pos = std::min(y_pos, data.vertices[idx * 14 + 1] - 0.4f);
		// UV: u = opacity at a river mouth (1 = the lake, 0 = its end up the channel), v = -1 marks
		// a lake from this node for water.vert (other water meshes keep their own UVs)
		float mouthFade = lakeRing[idx] > 0 ? glm::clamp(1.0f - (float)lakeRing[idx] / (float)mouthRings, 0.0f, 1.0f) : 1.0f;
		lakeMesh.AddVertex(x_pos, y_pos, z_pos, mouthFade, -1.0f, 0, 1, 0, 1, 0, 0, 0, 0, 1);
		terrainToWaterIdx[idx] = lakeMesh.GetVertexCount() - 1;
	}
	for (int idx : expandedPixels) {
		int cx = idx % gridRes; int cz = idx / gridRes;
		int r = cz * gridRes + (cx + 1); int b = (cz + 1) * gridRes + cx; int br = (cz + 1) * gridRes + (cx + 1);
		if (cx < gridRes - 1 && cz < gridRes - 1) {
			if (terrainToWaterIdx.count(idx) && terrainToWaterIdx.count(r) && terrainToWaterIdx.count(b) && terrainToWaterIdx.count(br)) {
				lakeMesh.AddTriangle(terrainToWaterIdx[idx], terrainToWaterIdx[b], terrainToWaterIdx[r]);
				lakeMesh.AddTriangle(terrainToWaterIdx[r], terrainToWaterIdx[b], terrainToWaterIdx[br]);
			}
		}
	}

	// 7. Sync Objects & Materials
	auto syncWaterObject = [&](const std::string& name, MeshData& meshData, const std::string& matPath) {
		// No water this run (no lake formed, no river long enough): the surface of an earlier run
		// (or of the saved scene) must go, not stay floating over the new terrain
		if (meshData.vertices.empty() || meshData.indices.empty()) {
			ctx.scene.RemoveObject(name);
			return;
		}
		// In the terrain's frame: at its position, unrotated and unscaled (the vertices have both)
		GameObject* obj = ctx.Object(name);
		obj->GetTransform().SetPosition(input.mesh.origin);
		obj->GetTransform().SetRotation(glm::vec3(0.0f));
		obj->GetTransform().SetScale(glm::vec3(1.0f));
		if (!obj->GetMaterial()) {
			Material* mat = Material::LoadFromFile(matPath);
			if (mat) obj->SetMaterial(mat);
		}
		obj->SetMesh(meshData.ToMesh());
		obj->SetCPUMeshData(meshData);
	};

	syncWaterObject("River_Water_" + std::to_string(id), riverMesh, "Assets/Materials/River.mat");
	syncWaterObject("Lake_Water_" + std::to_string(id), lakeMesh, "Assets/Materials/Water.mat");

	progress(100.0f, "Done!");
	MeshValue out = input.mesh;
	out.Set(std::move(data));
	Out(0, NodeValue::FromMesh(std::move(out)));
}

void RiverNode::RecomputeNormals(MeshData& data, int gridRes)
{
	const int stride = 14;
	for (int i = 0; i < gridRes * gridRes; i++) { data.vertices[i * stride + 5] = 0; data.vertices[i * stride + 6] = 0; data.vertices[i * stride + 7] = 0; }
	for (size_t i = 0; i + 2 < data.indices.size(); i += 3)
	{
		int i0 = data.indices[i], i1 = data.indices[i + 1], i2 = data.indices[i + 2];
		glm::vec3 v0(data.vertices[i0 * 14], data.vertices[i0 * 14 + 1], data.vertices[i0 * 14 + 2]);
		glm::vec3 v1(data.vertices[i1 * 14], data.vertices[i1 * 14 + 1], data.vertices[i1 * 14 + 2]);
		glm::vec3 v2(data.vertices[i2 * 14], data.vertices[i2 * 14 + 1], data.vertices[i2 * 14 + 2]);
		glm::vec3 n = glm::cross(v1 - v0, v2 - v0);
		for (int j = 0; j < 3; j++) { data.vertices[i0 * 14 + 5 + j] += n[j]; data.vertices[i1 * 14 + 5 + j] += n[j]; data.vertices[i2 * 14 + 5 + j] += n[j]; }
	}
	for (int i = 0; i < gridRes * gridRes; i++)
	{
		glm::vec3 n(data.vertices[i * 14 + 5], data.vertices[i * 14 + 6], data.vertices[i * 14 + 7]);
		if (glm::length(n) > 0.0001f) n = glm::normalize(n); else n = glm::vec3(0, 1, 0);
		data.vertices[i * 14 + 5] = n.x; data.vertices[i * 14 + 6] = n.y; data.vertices[i * 14 + 7] = n.z;
	}
}
