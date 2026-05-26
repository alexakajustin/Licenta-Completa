#ifdef RUN_UNIT_TESTS

#include "External Libs/doctest.h"
#include "Core/Transform.h"
#include "Core/Camera.h"
#include <glm/gtx/string_cast.hpp>

TEST_CASE("Transform Data and Matrices")
{
    SUBCASE("Default Initialization")
    {
        Transform t;
        CHECK(t.GetPosition().x == doctest::Approx(0.0f));
        CHECK(t.GetPosition().y == doctest::Approx(0.0f));
        CHECK(t.GetPosition().z == doctest::Approx(0.0f));
        
        CHECK(t.GetRotation().x == doctest::Approx(0.0f));
        
        CHECK(t.GetScale().x == doctest::Approx(1.0f));
        CHECK(t.GetScale().y == doctest::Approx(1.0f));
        CHECK(t.GetScale().z == doctest::Approx(1.0f));
    }

    SUBCASE("Setting Position and Scale")
    {
        Transform t;
        t.SetPosition(glm::vec3(10.0f, -5.0f, 3.5f));
        t.SetScale(glm::vec3(2.0f, 0.5f, 1.0f));

        glm::mat4 model = t.GetModelMatrix();
        
        // Check translation column
        CHECK(model[3][0] == doctest::Approx(10.0f));
        CHECK(model[3][1] == doctest::Approx(-5.0f));
        CHECK(model[3][2] == doctest::Approx(3.5f));
        
        // Check scale by measuring the length of axis vectors
        CHECK(glm::length(glm::vec3(model[0])) == doctest::Approx(2.0f));
        CHECK(glm::length(glm::vec3(model[1])) == doctest::Approx(0.5f));
        CHECK(glm::length(glm::vec3(model[2])) == doctest::Approx(1.0f));
    }
}

TEST_CASE("Camera Operations")
{
    SUBCASE("Initialization and defaults")
    {
        Camera cam(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f, 5.0f, 0.5f);
        
        CHECK(cam.getCameraPosition().y == doctest::Approx(1.0f));
        CHECK(cam.getYaw() == doctest::Approx(-90.0f));
        CHECK(cam.getPitch() == doctest::Approx(0.0f));
        CHECK(cam.getMoveSpeed() == doctest::Approx(5.0f));
    }

    SUBCASE("LookAt logic")
    {
        Camera cam;
        cam.SetPositionAndLookAt(glm::vec3(10.0f, 0.0f, 0.0f), 5.0f);
        
        // If looking at (10,0,0) from a distance of 5, position should be somewhere on a sphere of radius 5.
        // It typically places the camera at (10, 0, 5) depending on the default logic, let's just check the distance
        float dist = glm::distance(cam.getCameraPosition(), glm::vec3(10.0f, 0.0f, 0.0f));
        CHECK(dist == doctest::Approx(5.0f));
        
        // Check view matrix calculates correctly without crashing
        glm::mat4 view = cam.calculateViewMatrix();
        CHECK(view[3][3] == doctest::Approx(1.0f));
    }
}

#endif // RUN_UNIT_TESTS
