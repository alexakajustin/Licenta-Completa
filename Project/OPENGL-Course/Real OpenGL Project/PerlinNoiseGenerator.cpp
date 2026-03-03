#include "PerlinNoiseGenerator.h"
#include "imgui.h"
#include <cstdlib>
#include <algorithm>

PerlinNoiseGenerator::PerlinNoiseGenerator()
	: gridSize(100), scale(1.0f), amplitude(10.0f),
	  frequency(1.0f), octaves(4), persistence(0.5f), seed(42)
{
	InitPermutation();
}

void PerlinNoiseGenerator::InitPermutation()
{
	// Standard Perlin permutation table seeded with our seed
	std::srand(seed);
	for (int i = 0; i < 256; i++)
		permutation[i] = i;

	// Fisher-Yates shuffle
	for (int i = 255; i > 0; i--)
	{
		int j = std::rand() % (i + 1);
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
		total += PerlinNoise2D(x * freq, y * freq) * amp;
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
	ImGui::DragFloat("Amplitude", &amplitude, 0.05f, 0.0f, 100.0f);
	ImGui::DragFloat("Frequency", &frequency, 0.001f, 0.001f, 1.0f);
	
	static float offsetX = 0, offsetZ = 0;
	if (ImGui::DragFloat2("Offset", &offsetX, 0.1f)) {
		// We can reuse persistence or add new fields for offset if we want them saved
		// For now let's just use what's available or assume UI state is enough
	}

	ImGui::SliderInt("Octaves", &octaves, 1, 10);
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
		// (Reuse old logic but simplified)
		int verticesPerSide = 51; // 50x50 grid
		float fallbackScale = 0.2f;
		float halfSize = (50 * fallbackScale) / 2.0f;

		for (int z = 0; z < verticesPerSide; z++) {
			for (int x = 0; x < verticesPerSide; x++) {
				float worldX = (float)x * fallbackScale - halfSize;
				float worldZ = (float)z * fallbackScale - halfSize;
				float height = FractalNoise(worldX * frequency, worldZ * frequency) * amplitude;
				data.AddVertex(worldX, height, worldZ, (float)x/50.0f, (float)z/50.0f, 0,1,0, 1,0,0, 0,0,1);
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
		for (int i = 0; i < vertCount; i++)
		{
			int base = i * 14;
			float x = data.vertices[base];
			float z = data.vertices[base + 2];

			// Apply noise to Y (index base + 1)
			// We sample based on world X/Z
			float noise = FractalNoise(x * frequency, z * frequency);
			data.vertices[base + 1] += noise * amplitude;
		}
	}

	// Recalculate normals etc.
	int vertCount = data.GetVertexCount();
	if (vertCount == 0) return data;

	std::vector<float> nx(vertCount, 0.0f), ny(vertCount, 0.0f), nz(vertCount, 0.0f);
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
		nx[i0] += cx; ny[i0] += cy; nz[i0] += cz;
		nx[i1] += cx; ny[i1] += cy; nz[i1] += cz;
		nx[i2] += cx; ny[i2] += cy; nz[i2] += cz;
	}

	for (int i = 0; i < vertCount; i++)
	{
		float len = sqrtf(nx[i] * nx[i] + ny[i] * ny[i] + nz[i] * nz[i]);
		if (len > 0.0f) { nx[i] /= len; ny[i] /= len; nz[i] /= len; }
		data.vertices[i * 14 + 5] = nx[i];
		data.vertices[i * 14 + 6] = ny[i];
		data.vertices[i * 14 + 7] = nz[i];
	}

	return data;
}
