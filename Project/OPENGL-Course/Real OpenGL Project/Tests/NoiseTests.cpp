#ifdef RUN_UNIT_TESTS

#include "External Libs/doctest.h"
#include "Procedural/Noise3D.h"
#include <cmath>

TEST_CASE("Noise3D Generation")
{
    SUBCASE("Deterministic Seed")
    {
        Noise3D noise1(42);
        Noise3D noise2(42);

        float val1 = noise1.Noise(1.5f, 2.5f, 3.5f);
        float val2 = noise2.Noise(1.5f, 2.5f, 3.5f);

        // Same seed should yield identical results
        CHECK(val1 == doctest::Approx(val2));
    }

    SUBCASE("Different Seeds")
    {
        Noise3D noise1(42);
        Noise3D noise2(43);

        float val1 = noise1.Noise(1.5f, 2.5f, 3.5f);
        float val2 = noise2.Noise(1.5f, 2.5f, 3.5f);

        // Different seeds should yield different results (most of the time)
        CHECK(val1 != doctest::Approx(val2));
    }

    SUBCASE("fBm Bounds")
    {
        Noise3D noise(123);
        float val = noise.fBm(glm::vec3(10.0f, 20.0f, 30.0f), 4, 0.5f, 2.0f);
        
        // fBm returns normalized values, usually in [-1, 1] range
        CHECK(val >= -1.0f);
        CHECK(val <= 1.0f);
    }

    SUBCASE("RidgedNoise Bounds")
    {
        Noise3D noise(123);
        float val = noise.RidgedNoise(glm::vec3(10.0f, 20.0f, 30.0f), 4, 0.5f, 2.0f);
        
        // Ridged noise involves absolute values and 1-v, returning positive values usually in [0, 1] range
        CHECK(val >= 0.0f);
        CHECK(val <= 1.0f);
    }
}

#endif // RUN_UNIT_TESTS
