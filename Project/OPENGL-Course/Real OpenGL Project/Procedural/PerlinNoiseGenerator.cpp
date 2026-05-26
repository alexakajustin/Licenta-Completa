#include "Procedural/PerlinNoiseGenerator.h"
#include "imgui.h"
#include <cstdlib>
#include <algorithm>
#include <cfloat>
#include <thread>
#include <vector>
#include <random>

PerlinNoiseGenerator::PerlinNoiseGenerator()
	: gridSize(128), scale(1.0f), amplitude(15.0f),
	  frequency(0.028f), octaves(10), persistence(1.0f), seed(42),
	  offsetX(0.0f), offsetZ(0.0f), useNormalDisplacement(false), useRidged(false)
{
	InitPermutation();
}

void PerlinNoiseGenerator::InitPermutation()
{
	// Standard Perlin permutation table seeded with our seed
	std::mt19937 rng(seed);
	for (int i = 0; i < 256; i++)
		permutation[i] = i;

	// Fisher-Yates shuffle
	for (int i = 255; i > 0; i--)
	{
		std::uniform_int_distribution<int> dist(0, i);
		int j = dist(rng);
		std::swap(permutation[i], permutation[j]);
	}
	// Duplicate for overflow
	for (int i = 0; i < 256; i++)
		permutation[256 + i] = permutation[i];
}

float PerlinNoiseGenerator::Fade(float t)
{
	// 6t^5 - 15t^4 + 10t^3
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float PerlinNoiseGenerator::Lerp(float a, float b, float t)
{
	return a + t * (b - a);
}

float PerlinNoiseGenerator::Grad(int hash, float x, float y)
{
	// Use lower 2 bits to pick gradient direction
	int h = hash & 3;
	float u = h < 2 ? x : y;
	float v = h < 2 ? y : x;
	return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

float PerlinNoiseGenerator::PerlinNoise2D(float x, float y)
{
	// Grid cell coordinates
	int xi = (int)floor(x) & 255;
	int yi = (int)floor(y) & 255;

	// Relative position within cell
	float xf = x - floor(x);
	float yf = y - floor(y);

	// Fade curves
	float u = Fade(xf);
	float v = Fade(yf);

	// Hash coordinates of the 4 corners
	int aa = permutation[permutation[xi] + yi];
	int ab = permutation[permutation[xi] + yi + 1];
	int ba = permutation[permutation[xi + 1] + yi];
	int bb = permutation[permutation[xi + 1] + yi + 1];

	// Blend
	float x1 = Lerp(Grad(aa, xf, yf), Grad(ba, xf - 1.0f, yf), u);
	float x2 = Lerp(Grad(ab, xf, yf - 1.0f), Grad(bb, xf - 1.0f, yf - 1.0f), u);

	return Lerp(x1, x2, v);
}

float PerlinNoiseGenerator::FractalNoise(float x, float y)
{
	float total = 0.0f;
	float amp = 1.0f;
	float freq = 1.0f;
	float maxVal = 0.0f;

	for (int i = 0; i < octaves; i++)
	{
		float n = PerlinNoise2D(x * freq, y * freq);
		if (useRidged)
		{
			n = 1.0f - std::abs(n);
			n = n * n; // Square for sharper peaks
		}

		total += n * amp;
		maxVal += amp;
		amp *= persistence;
		freq *= 2.0f;
	}

	return total / maxVal; // Normalize to roughly [-1, 1]
}



void PerlinNoiseGenerator::RenderUI()
{
	ImGui::PushID(this);

	bool seedChanged = false;

	ImGui::Text("Noise Settings");
	ImGui::Checkbox("Use Normal Displacement", &useNormalDisplacement);
	ImGui::Checkbox("Ridged Mode", &useRidged);
	ImGui::DragFloat("Amplitude", &amplitude, 0.1f, 0.0f, 1000.0f);
	ImGui::DragFloat("Frequency", &frequency, 0.01f, 0.0001f, 10.0f);
	
	ImGui::DragFloat2("Offset", &offsetX, 0.1f);

	ImGui::SliderInt("Octaves", &octaves, 1, 12);
	ImGui::DragFloat("Persistence", &persistence, 0.01f, 0.01f, 1.0f);

	ImGui::Separator();
	if (ImGui::InputInt("Seed", &seed))
		seedChanged = true;
	ImGui::SameLine();
	if (ImGui::Button("Randomize"))
	{
		seed = std::rand();
		seedChanged = true;
	}

	if (seedChanged)
		InitPermutation();

	ImGui::PopID();
}

MeshData PerlinNoiseGenerator::Generate(const MeshData* input)
{
	// Re-init permutation table (seed may have changed)
	InitPermutation();

	MeshData data;
	if (!input || input->vertices.empty())
	{
		// Fallback: Generate a simple subdivided plane if no input
		int verticesPerSide = 51; // 50x50 grid
		float fallbackScale = 0.2f;
		float halfSize = (50 * fallbackScale) / 2.0f;

		for (int z = 0; z < verticesPerSide; z++) {
			for (int x = 0; x < verticesPerSide; x++) {
				float worldX = (float)x * fallbackScale - halfSize;
				float worldZ = (float)z * fallbackScale - halfSize;
				float noise = FractalNoise((worldX + offsetX + 0.123f) * frequency, (worldZ + offsetZ + 0.123f) * frequency) * amplitude;
				data.AddVertex(worldX, noise, worldZ, (float)x/50.0f, (float)z/50.0f, 0,1,0, 1,0,0, 0,0,1);
			}
		}
		for (int z = 0; z < 50; z++) {
			for (int x = 0; x < 50; x++) {
				unsigned int topLeft = z * verticesPerSide + x;
				unsigned int topRight = topLeft + 1;
				unsigned int bottomLeft = (z + 1) * verticesPerSide + x;
				unsigned int bottomRight = bottomLeft + 1;
				data.AddTriangle(topLeft, bottomLeft, topRight);
				data.AddTriangle(topRight, bottomLeft, bottomRight);
			}
		}
	}
	else
	{
		// Displace existing input mesh
		data = *input;
		int vertCount = data.GetVertexCount();

		// === Compute mesh bounding box for coordinate normalization ===
		// Normalize vertex X/Z to [0,1] range so that frequency is always relative
		// to the mesh size (e.g. freq=0.5 = ~half a noise cycle across the terrain),
		// regardless of whether vertices are in [-1,1] or [-1000,1000] world-space.
		float minX = FLT_MAX, maxX = -FLT_MAX, minZ = FLT_MAX, maxZ = -FLT_MAX;
		for (int i = 0; i < vertCount; i++)
		{
			float vx = data.vertices[i * 14];
			float vz = data.vertices[i * 14 + 2];
			if (vx < minX) minX = vx;
			if (vx > maxX) maxX = vx;
			if (vz < minZ) minZ = vz;
			if (vz > maxZ) maxZ = vz;
		}
		float rangeX = (maxX - minX);
		float rangeZ = (maxZ - minZ);
		if (rangeX < 0.0001f) rangeX = 1.0f;
		if (rangeZ < 0.0001f) rangeZ = 1.0f;

		// === MULTITHREADED DISPLACEMENT ===
		unsigned int numThreads = std::thread::hardware_concurrency();
		if (numThreads == 0) numThreads = 4;
		if (vertCount < 1000 || numThreads == 1)
		{
			// Small mesh: single-threaded fast path
			for (int i = 0; i < vertCount; i++)
			{
				int base = i * 14;
				float vx = data.vertices[base];
				float vz = data.vertices[base + 2];
				// Normalize to [0,1] range so frequency is mesh-size-relative
				float nx = (vx - minX) / rangeX;
				float nz = (vz - minZ) / rangeZ;
				float sx = (nx + offsetX + 0.1234f);
				float sz = (nz + offsetZ + 0.1234f);
				float noise = FractalNoise(sx * frequency, sz * frequency);

				if (useNormalDisplacement)
				{
					float fnx = data.vertices[base + 5];
					float fny = data.vertices[base + 6];
					float fnz = data.vertices[base + 7];
					data.vertices[base] += fnx * noise * amplitude;
					data.vertices[base + 1] += fny * noise * amplitude;
					data.vertices[base + 2] += fnz * noise * amplitude;
				}
				else
				{
					data.vertices[base + 1] += noise * amplitude;
				}
			}
		}
		else
		{
			// Large mesh: parallel displacement
			if (numThreads > (unsigned int)vertCount) numThreads = (unsigned int)vertCount;
			std::vector<std::thread> threads;
			int perThread = vertCount / numThreads;

			for (unsigned int t = 0; t < numThreads; t++)
			{
				int startV = t * perThread;
				int endV = (t == numThreads - 1) ? vertCount : (t + 1) * perThread;

				threads.emplace_back([this, &data, startV, endV, minX, minZ, rangeX, rangeZ]() {
					for (int i = startV; i < endV; i++)
					{
						int base = i * 14;
						float vx = data.vertices[base];
						float vz = data.vertices[base + 2];
						// Normalize to [0,1] range so frequency is mesh-size-relative
						float nx = (vx - minX) / rangeX;
						float nz = (vz - minZ) / rangeZ;
						float sx = (nx + offsetX + 0.1234f);
						float sz = (nz + offsetZ + 0.1234f);
						float noise = FractalNoise(sx * frequency, sz * frequency);

						if (useNormalDisplacement)
						{
							float fnx = data.vertices[base + 5];
							float fny = data.vertices[base + 6];
							float fnz = data.vertices[base + 7];
							data.vertices[base] += fnx * noise * amplitude;
							data.vertices[base + 1] += fny * noise * amplitude;
							data.vertices[base + 2] += fnz * noise * amplitude;
						}
						else
						{
							data.vertices[base + 1] += noise * amplitude;
						}
					}
				});
			}
			for (auto& th : threads) th.join();
		}
	}

	// Recalculate normals
	int vertCount = data.GetVertexCount();
	if (vertCount == 0) return data;

	// Phase 1: Accumulate face normals (single-threaded due to shared writes per vertex)
	std::vector<float> fnx(vertCount, 0.0f), fny(vertCount, 0.0f), fnz(vertCount, 0.0f);
	for (int i = 0; i < (int)data.indices.size(); i += 3)
	{
		unsigned int i0 = data.indices[i];
		unsigned int i1 = data.indices[i + 1];
		unsigned int i2 = data.indices[i + 2];
		float* v0 = &data.vertices[i0 * 14];
		float* v1 = &data.vertices[i1 * 14];
		float* v2 = &data.vertices[i2 * 14];
		float e1x = v1[0] - v0[0], e1y = v1[1] - v0[1], e1z = v1[2] - v0[2];
		float e2x = v2[0] - v0[0], e2y = v2[1] - v0[1], e2z = v2[2] - v0[2];
		float cx = e1y * e2z - e1z * e2y, cy = e1z * e2x - e1x * e2z, cz = e1x * e2y - e1y * e2x;
		fnx[i0] += cx; fny[i0] += cy; fnz[i0] += cz;
		fnx[i1] += cx; fny[i1] += cy; fnz[i1] += cz;
		fnx[i2] += cx; fny[i2] += cy; fnz[i2] += cz;
	}

	// Phase 2: Normalize and write back (multithreaded — each vertex is independent)
	unsigned int numThreads2 = std::thread::hardware_concurrency();
	if (numThreads2 == 0) numThreads2 = 4;
	if (vertCount > 1000 && numThreads2 > 1)
	{
		if (numThreads2 > (unsigned int)vertCount) numThreads2 = (unsigned int)vertCount;
		std::vector<std::thread> threads;
		int perThread = vertCount / numThreads2;

		for (unsigned int t = 0; t < numThreads2; t++)
		{
			int startV = t * perThread;
			int endV = (t == numThreads2 - 1) ? vertCount : (t + 1) * perThread;

			threads.emplace_back([&data, &fnx, &fny, &fnz, startV, endV]() {
				for (int i = startV; i < endV; i++)
				{
					float len = sqrtf(fnx[i] * fnx[i] + fny[i] * fny[i] + fnz[i] * fnz[i]);
					if (len > 0.0f) { fnx[i] /= len; fny[i] /= len; fnz[i] /= len; }
					data.vertices[i * 14 + 5] = fnx[i];
					data.vertices[i * 14 + 6] = fny[i];
					data.vertices[i * 14 + 7] = fnz[i];
				}
			});
		}
		for (auto& th : threads) th.join();
	}
	else
	{
		for (int i = 0; i < vertCount; i++)
		{
			float len = sqrtf(fnx[i] * fnx[i] + fny[i] * fny[i] + fnz[i] * fnz[i]);
			if (len > 0.0f) { fnx[i] /= len; fny[i] /= len; fnz[i] /= len; }
			data.vertices[i * 14 + 5] = fnx[i];
			data.vertices[i * 14 + 6] = fny[i];
			data.vertices[i * 14 + 7] = fnz[i];
		}
	}

	return data;
}
