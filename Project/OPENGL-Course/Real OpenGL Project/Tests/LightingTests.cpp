#ifdef RUN_UNIT_TESTS

#include "External Libs/doctest.h"
#include "Lighting/DirectionalLight.h"
#include "Lighting/PointLight.h"
#include "Lighting/SpotLight.h"

TEST_CASE("DirectionalLight Functionality")
{
    SUBCASE("Initialization and Directions")
    {
        DirectionalLight dLight(1024, 1024, 1.0f, 1.0f, 1.0f, 0.2f, 0.8f, 0.0f, -1.0f, 0.0f);
        
        CHECK(dLight.GetColourPtr()->x == doctest::Approx(1.0f));
        CHECK(*dLight.GetAmbientIntensityPtr() == doctest::Approx(0.2f));
        CHECK(*dLight.GetDiffuseIntensityPtr() == doctest::Approx(0.8f));

        glm::vec3 dir = *dLight.GetDirectionPtr();
        CHECK(dir.y == doctest::Approx(-1.0f));
    }

    SUBCASE("Cascaded Light Matrices Generation")
    {
        DirectionalLight dLight(1024, 1024, 1.0f, 1.0f, 1.0f, 0.2f, 0.8f, 0.0f, -1.0f, 0.0f);
        dLight.SetDirection(glm::vec3(0.0f, -1.0f, 0.0f));

        // Create a dummy view and projection matrix
        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 16.0f/9.0f, 0.1f, 100.0f);

        dLight.CalculateCascadedLightMatrices(view, proj, 0.1f, 100.0f, 4);

        // Should have generated 4 matrices and distances
        auto matrices = dLight.GetCascadedLightMatrices();
        auto distances = dLight.GetCascadeSplitDistances();

        CHECK(matrices.size() == 4);
        CHECK(distances.size() == 4);
    }
}

TEST_CASE("PointLight Functionality")
{
    SUBCASE("Initialization and Range")
    {
        PointLight pLight(1024, 1024, 0.1f, 100.0f, 1.0f, 0.0f, 0.0f, 0.1f, 1.0f, 0.0f, 5.0f, 0.0f, 0.3f, 0.2f, 0.1f);
        
        CHECK(pLight.GetColourPtr()->x == doctest::Approx(1.0f));
        CHECK(pLight.GetPosition().y == doctest::Approx(5.0f));
    }
}

TEST_CASE("SpotLight Functionality")
{
    SUBCASE("Initialization and Edge calculation")
    {
        SpotLight sLight(1024, 1024, 0.1f, 100.0f, 0.0f, 1.0f, 0.0f, 0.2f, 1.0f, 
                         0.0f, 10.0f, 0.0f, 0.0f, -1.0f, 0.0f, 
                         0.3f, 0.2f, 0.1f, 20.0f);

        CHECK(sLight.GetColourPtr()->y == doctest::Approx(1.0f));
        
        // Edge angle in degrees
        CHECK(*sLight.GetEdgePtr() == doctest::Approx(20.0f));
    }
}

#endif // RUN_UNIT_TESTS
