#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <random>

// =====================================================================
// Randomness for the node graph
//
//  - TrueRandomSeed(): a seed nobody chose (OS entropy, the clock and a counter). The dice buttons,
//    new nodes and the Seed node's "new every run" mode use it: no more "42" everywhere, and no
//    std::rand() that was never seeded and replayed the same "random" seeds every session.
//  - Stateless, counter-based streams: element i of a seed is Hash(seed, i). Results never depend
//    on how many threads computed them or in which order (the same seed gives the same forest on
//    every machine).
// =====================================================================
namespace NodeRandom
{
	inline uint64_t SplitMix64(uint64_t x)
	{
		x += 0x9E3779B97F4A7C15ull;
		x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
		x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
		return x ^ (x >> 31);
	}

	// Combines two values into one well-mixed seed (e.g. a shared seed with a node's id)
	inline uint32_t Mix(uint64_t a, uint64_t b)
	{
		return (uint32_t)(SplitMix64(SplitMix64(a) ^ (b * 0xD1B54A32D192ED03ull)) >> 16);
	}

	// Seeds stay below a million: readable in the UI, plenty of variety
	constexpr int MAX_SEED = 1000000;

	inline int ToSeed(uint64_t v) { return (int)(v % (uint64_t)MAX_SEED); }

	inline int TrueRandomSeed()
	{
		static std::atomic<uint64_t> counter{ 0 };
		uint64_t entropy = 0;
		try {
			std::random_device device;
			entropy = ((uint64_t)device() << 32) ^ (uint64_t)device();
		}
		catch (...) {}
		uint64_t clock = (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
		uint64_t count = counter.fetch_add(1) + 1;
		return ToSeed(SplitMix64(entropy ^ SplitMix64(clock ^ SplitMix64(count))));
	}

	// Element `index` of stream `stream` of a seed: a uniform value in [0, 1)
	inline float Float01(uint32_t seed, uint64_t index, uint32_t stream = 0)
	{
		uint64_t h = SplitMix64(((uint64_t)seed << 32 | stream) ^ SplitMix64(index));
		return (float)(h >> 40) * (1.0f / 16777216.0f);
	}

	// A small generator for sequential use (one per element or per task, seeded with Mix(seed, i))
	struct Rng
	{
		uint64_t state;
		explicit Rng(uint64_t seed) : state(SplitMix64(seed ^ 0x6A09E667F3BCC909ull)) {}
		uint64_t Next() { state += 0x9E3779B97F4A7C15ull; return SplitMix64(state); }
		float Float01() { return (float)(Next() >> 40) * (1.0f / 16777216.0f); }
		float Range(float lo, float hi) { return lo + (hi - lo) * Float01(); }
		// Integer in [lo, hi]
		int Int(int lo, int hi) { return hi <= lo ? lo : lo + (int)(Next() % (uint64_t)(hi - lo + 1)); }
	};
}
