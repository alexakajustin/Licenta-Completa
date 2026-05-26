#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <numeric>
#include <algorithm>
#include <random>

/**
 * @class Noise3D
 * @brief Utility class that provides 3D Perlin noise, fractal Brownian motion (fBm), and 3D ridged noise lookup fields.
 */
class Noise3D {
public:
    /**
     * @brief Constructor that generates shuffled permutation vectors using an active seed.
     * @param seed Random seed for generating noise permutations.
     */
    Noise3D(unsigned int seed = 12345) {
        p.resize(256);
        std::iota(p.begin(), p.end(), 0);
        std::default_random_engine engine(seed);
        std::shuffle(p.begin(), p.end(), engine);
        p.insert(p.end(), p.begin(), p.end());
    }

    /**
     * @brief Computes raw single-octave 3D Perlin noise.
     * @param x coordinate.
     * @param y coordinate.
     * @param z coordinate.
     * @return Noise value normalized in the [-1.0, 1.0] range.
     */
    float Noise(float x, float y, float z) const {
        // Simple 3D Perlin noise implementation
        int X = (int)floor(x) & 255;
        int Y = (int)floor(y) & 255;
        int Z = (int)floor(z) & 255;

        x -= floor(x);
        y -= floor(y);
        z -= floor(z);

        float u = Fade(x);
        float v = Fade(y);
        float w = Fade(z);

        int A = p[X] + Y, AA = p[A] + Z, AB = p[A + 1] + Z;
        int B = p[X + 1] + Y, BA = p[B] + Z, BB = p[B + 1] + Z;

        return Lerp(w, Lerp(v, Lerp(u, Grad(p[AA], x, y, z),
                                     Grad(p[BA], x - 1, y, z)),
                             Lerp(u, Grad(p[AB], x, y - 1, z),
                                     Grad(p[BB], x - 1, y - 1, z))),
                      Lerp(v, Lerp(u, Grad(p[AA + 1], x, y, z - 1),
                                     Grad(p[BA + 1], x - 1, y, z - 1)),
                             Lerp(u, Grad(p[AB + 1], x, y - 1, z - 1),
                                     Grad(p[BB + 1], x - 1, y - 1, z - 1))));
    }

    /**
     * @brief Fractal Brownian Motion (fBm) noise summation.
     * @param pos 3D position vector.
     * @param octaves Number of octaves.
     * @param persistence Amplitude decay factor.
     * @param lacunarity Frequency multiplication factor.
     * @return Summed fractal noise value.
     */
    float fBm(glm::vec3 pos, int octaves, float persistence, float lacunarity) const {
        float total = 0;
        float frequency = 1;
        float amplitude = 1;
        float maxValue = 0;
        for (int i = 0; i < octaves; i++) {
            total += Noise(pos.x * frequency, pos.y * frequency, pos.z * frequency) * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= lacunarity;
        }
        return total / maxValue;
    }

    /**
     * @brief Computes 3D ridged multifractal noise (useful for mountain ranges or sharp details).
     * @param pos 3D position vector.
     * @param octaves Number of octaves.
     * @param persistence Amplitude decay factor.
     * @param lacunarity Frequency multiplication factor.
     * @return Summed ridged noise value.
     */
    float RidgedNoise(glm::vec3 pos, int octaves, float persistence, float lacunarity) const {
        float total = 0;
        float frequency = 1;
        float amplitude = 1;
        float maxValue = 0;
        for (int i = 0; i < octaves; i++) {
            float v = abs(Noise(pos.x * frequency, pos.y * frequency, pos.z * frequency));
            v = 1.0f - v;
            v *= v; // sharpening
            total += v * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= lacunarity;
        }
        return total / maxValue;
    }

private:
    std::vector<int> p; ///< Permutation table.

    static float Fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static float Lerp(float t, float a, float b) { return a + t * (b - a); }
    static float Grad(int hash, float x, float y, float z) {
        int h = hash & 15;
        float u = h < 8 ? x : y;
        float v = h < 4 ? y : h == 12 || h == 14 ? x : z;
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }
};
