#pragma once

#include "Nodes/Operation.h"
#include "Nodes/OperationRegistry.h"
#include <cmath>
#include <cstdlib>
#include <thread>
#include <vector>
#include <random>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =====================================================================
//  Simple Perlin noise implementation for operations
//  (Self-contained — does not depend on PerlinNoiseGenerator)
// =====================================================================

namespace OpNoise
{
	static int perm[512];
	static bool permInitialized = false;

	inline void InitPerm(int seed)
	{
		srand(seed);
		int base[256];
		for (int i = 0; i < 256; i++) base[i] = i;
		for (int i = 255; i > 0; i--)
		{
			int j = rand() % (i + 1);
			int tmp = base[i]; base[i] = base[j]; base[j] = tmp;
		}
		for (int i = 0; i < 512; i++) perm[i] = base[i & 255];
		permInitialized = true;
	}

	inline float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
	inline float Lerp(float a, float b, float t) { return a + t * (b - a); }

	inline float Grad(int hash, float x, float y)
	{
		int h = hash & 7;
		float u = h < 4 ? x : y;
		float v = h < 4 ? y : x;
		return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
	}

	inline float Perlin2D(float x, float y)
	{
		int X = (int)floor(x) & 255;
		int Y = (int)floor(y) & 255;
		x -= floor(x); y -= floor(y);
		float u = Fade(x), v = Fade(y);
		int A = perm[X] + Y, B = perm[X + 1] + Y;
		return Lerp(
			Lerp(Grad(perm[A], x, y), Grad(perm[B], x - 1, y), u),
			Lerp(Grad(perm[A + 1], x, y - 1), Grad(perm[B + 1], x - 1, y - 1), u),
			v
		);
	}

	inline float FractalNoise(float x, float y, int octaves, float persistence)
	{
		float total = 0; float amplitude = 1; float frequency = 1; float maxVal = 0;
		for (int i = 0; i < octaves; i++)
		{
			total += Perlin2D(x * frequency, y * frequency) * amplitude;
			maxVal += amplitude;
			amplitude *= persistence;
			frequency *= 2.0f;
		}
		return total / maxVal;
	}
}


// =====================================================================
//  MeshOp_PerlinDisplace — Displace vertices via Perlin noise
//  OPTIMIZED: Multithreaded for large meshes
// =====================================================================

class MeshOp_PerlinDisplace : public Operation
{
public:
	std::string GetName() const override { return "Perlin Displace"; }
	std::string GetCategory() const override { return "Noise"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Float("Frequency", 1.0f, 0.01f, 20.0f),
			ParamDef::Float("Amplitude", 1.0f, 0.0f, 50.0f),
			ParamDef::Int("Octaves", 4, 1, 8),
			ParamDef::Float("Persistence", 0.5f, 0.0f, 1.0f),
			ParamDef::Int("Seed", 42, 0, 9999),
			ParamDef::Bool("Along Normal", true),
			ParamDef::Vec2("Offset", glm::vec2(0.0f))
		};
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty()) return;

		float freq    = GetFloat("Frequency");
		float amp     = GetFloat("Amplitude");
		int octaves   = GetInt("Octaves");
		float persist = GetFloat("Persistence");
		int seed      = GetInt("Seed");
		bool alongNormal = GetBool("Along Normal");
		glm::vec2 offset = GetVec2("Offset");

		OpNoise::InitPerm(seed);

		int count = ctx.mesh.GetVertexCount();

		// Check if vertex selection is active (any unselected vertex = selection is active)
		bool hasSelection = false;
		for (int i = 0; i < count && !hasSelection; i++)
			if (!ctx.IsVertexSelected(i)) hasSelection = true;

		// Multithread for large meshes without selection masks
		if (count > 1000 && !hasSelection)
		{
			unsigned int numThreads = std::thread::hardware_concurrency();
			if (numThreads == 0) numThreads = 4;
			if (numThreads > (unsigned int)count) numThreads = (unsigned int)count;

			std::vector<std::thread> threads;
			int perThread = count / numThreads;

			for (unsigned int t = 0; t < numThreads; t++)
			{
				int startV = t * perThread;
				int endV = (t == numThreads - 1) ? count : (t + 1) * perThread;

				threads.emplace_back([&ctx, freq, amp, octaves, persist, alongNormal, offset, startV, endV]() {
					for (int i = startV; i < endV; i++)
					{
						int base = i * 14;
						float px = ctx.mesh.vertices[base];
						float pz = ctx.mesh.vertices[base + 2];

						float noise = OpNoise::FractalNoise(
							(px + offset.x) * freq,
							(pz + offset.y) * freq,
							octaves, persist
						) * amp;

						if (alongNormal)
						{
							float nx = ctx.mesh.vertices[base + 5];
							float ny = ctx.mesh.vertices[base + 6];
							float nz = ctx.mesh.vertices[base + 7];
							ctx.mesh.vertices[base]     += nx * noise;
							ctx.mesh.vertices[base + 1] += ny * noise;
							ctx.mesh.vertices[base + 2] += nz * noise;
						}
						else
						{
							ctx.mesh.vertices[base + 1] += noise;
						}
					}
				});
			}
			for (auto& th : threads) th.join();
		}
		else
		{
			// Single-threaded path (small mesh or has vertex selection)
			for (int i = 0; i < count; i++)
			{
				if (!ctx.IsVertexSelected(i)) continue;

				int base = i * 14;
				float px = ctx.mesh.vertices[base];
				float pz = ctx.mesh.vertices[base + 2];

				float noise = OpNoise::FractalNoise(
					(px + offset.x) * freq,
					(pz + offset.y) * freq,
					octaves, persist
				) * amp;

				if (alongNormal)
				{
					float nx = ctx.mesh.vertices[base + 5];
					float ny = ctx.mesh.vertices[base + 6];
					float nz = ctx.mesh.vertices[base + 7];
					ctx.mesh.vertices[base]     += nx * noise;
					ctx.mesh.vertices[base + 1] += ny * noise;
					ctx.mesh.vertices[base + 2] += nz * noise;
				}
				else
				{
					ctx.mesh.vertices[base + 1] += noise;
				}
			}
		}
	}
};

REGISTER_OPERATION(MeshOp_PerlinDisplace, "Perlin Displace", "Noise")


// =====================================================================
//  MeshOp_WaveDeform — Sine/Cosine wave deformation
//  OPTIMIZED: Multithreaded for large meshes
// =====================================================================

class MeshOp_WaveDeform : public Operation
{
public:
	std::string GetName() const override { return "Wave Deform"; }
	std::string GetCategory() const override { return "Noise"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Enum("Wave Type", {"Sine", "Cosine"}, 0),
			ParamDef::Enum("Axis", {"X", "Y", "Z"}, 0),
			ParamDef::Float("Frequency", 1.0f, 0.01f, 20.0f),
			ParamDef::Float("Amplitude", 0.5f, 0.0f, 20.0f),
			ParamDef::Float("Phase", 0.0f, 0.0f, 6.28f)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty()) return;

		int waveType = GetEnum("Wave Type");
		int axis     = GetEnum("Axis");
		float freq   = GetFloat("Frequency");
		float amp    = GetFloat("Amplitude");
		float phase  = GetFloat("Phase");

		int count = ctx.mesh.GetVertexCount();

		// Check for active selection
		bool hasSelection = false;
		for (int i = 0; i < count && !hasSelection; i++)
			if (!ctx.IsVertexSelected(i)) hasSelection = true;

		if (count > 1000 && !hasSelection)
		{
			unsigned int numThreads = std::thread::hardware_concurrency();
			if (numThreads == 0) numThreads = 4;
			if (numThreads > (unsigned int)count) numThreads = (unsigned int)count;

			std::vector<std::thread> threads;
			int perThread = count / numThreads;

			for (unsigned int t = 0; t < numThreads; t++)
			{
				int startV = t * perThread;
				int endV = (t == numThreads - 1) ? count : (t + 1) * perThread;

				threads.emplace_back([&ctx, waveType, axis, freq, amp, phase, startV, endV]() {
					for (int i = startV; i < endV; i++)
					{
						int base = i * 14;
						float sampleVal;
						if (axis == 0) sampleVal = ctx.mesh.vertices[base];
						else if (axis == 1) sampleVal = ctx.mesh.vertices[base + 1];
						else sampleVal = ctx.mesh.vertices[base + 2];

						float displacement;
						if (waveType == 0) displacement = sinf(sampleVal * freq + phase) * amp;
						else               displacement = cosf(sampleVal * freq + phase) * amp;

						ctx.mesh.vertices[base + 1] += displacement;
					}
				});
			}
			for (auto& th : threads) th.join();
		}
		else
		{
			for (int i = 0; i < count; i++)
			{
				if (!ctx.IsVertexSelected(i)) continue;

				int base = i * 14;
				float sampleVal;
				if (axis == 0) sampleVal = ctx.mesh.vertices[base];
				else if (axis == 1) sampleVal = ctx.mesh.vertices[base + 1];
				else sampleVal = ctx.mesh.vertices[base + 2];

				float displacement;
				if (waveType == 0) displacement = sinf(sampleVal * freq + phase) * amp;
				else               displacement = cosf(sampleVal * freq + phase) * amp;

				ctx.mesh.vertices[base + 1] += displacement;
			}
		}
	}
};

REGISTER_OPERATION(MeshOp_WaveDeform, "Wave Deform", "Noise")


// =====================================================================
//  MeshOp_Jitter — Random position offset per vertex
//  OPTIMIZED: Uses mt19937 per-thread for deterministic parallel jitter
// =====================================================================

class MeshOp_Jitter : public Operation
{
public:
	std::string GetName() const override { return "Jitter"; }
	std::string GetCategory() const override { return "Noise"; }

	std::vector<ParamDef> GetParamDefs() const override
	{
		return {
			ParamDef::Float("Strength", 0.1f, 0.0f, 5.0f),
			ParamDef::Int("Seed", 42, 0, 9999),
			ParamDef::Bool("Uniform", false)
		};
	}

	void Execute(OperationContext& ctx) override
	{
		if (ctx.mesh.vertices.empty()) return;

		float strength = GetFloat("Strength");
		int seed = GetInt("Seed");
		bool uniform = GetBool("Uniform");

		int count = ctx.mesh.GetVertexCount();

		// Check for active selection
		bool hasSelection = false;
		for (int i = 0; i < count && !hasSelection; i++)
			if (!ctx.IsVertexSelected(i)) hasSelection = true;

		if (count > 1000 && !hasSelection)
		{
			unsigned int numThreads = std::thread::hardware_concurrency();
			if (numThreads == 0) numThreads = 4;
			if (numThreads > (unsigned int)count) numThreads = (unsigned int)count;

			std::vector<std::thread> threads;
			int perThread = count / numThreads;

			for (unsigned int t = 0; t < numThreads; t++)
			{
				int startV = t * perThread;
				int endV = (t == numThreads - 1) ? count : (t + 1) * perThread;

				// Each thread gets its own RNG seeded deterministically
				threads.emplace_back([&ctx, strength, seed, uniform, startV, endV]() {
					std::mt19937 rng(seed + startV);
					std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

					for (int i = startV; i < endV; i++)
					{
						int base = i * 14;
						float rx = dist(rng) * strength;
						float ry = dist(rng) * strength;
						float rz = dist(rng) * strength;

						if (uniform)
						{
							float len = sqrtf(rx * rx + ry * ry + rz * rz);
							if (len > 0.0001f)
							{
								rx = rx / len * strength;
								ry = ry / len * strength;
								rz = rz / len * strength;
							}
						}

						ctx.mesh.vertices[base]     += rx;
						ctx.mesh.vertices[base + 1] += ry;
						ctx.mesh.vertices[base + 2] += rz;
					}
				});
			}
			for (auto& th : threads) th.join();
		}
		else
		{
			srand(seed);
			for (int i = 0; i < count; i++)
			{
				if (!ctx.IsVertexSelected(i)) continue;

				int base = i * 14;
				float rx = ((float)rand() / RAND_MAX - 0.5f) * 2.0f * strength;
				float ry = ((float)rand() / RAND_MAX - 0.5f) * 2.0f * strength;
				float rz = ((float)rand() / RAND_MAX - 0.5f) * 2.0f * strength;

				if (uniform)
				{
					float len = sqrtf(rx * rx + ry * ry + rz * rz);
					if (len > 0.0001f)
					{
						rx = rx / len * strength;
						ry = ry / len * strength;
						rz = rz / len * strength;
					}
				}

				ctx.mesh.vertices[base]     += rx;
				ctx.mesh.vertices[base + 1] += ry;
				ctx.mesh.vertices[base + 2] += rz;
			}
		}
	}
};

REGISTER_OPERATION(MeshOp_Jitter, "Jitter", "Noise")
