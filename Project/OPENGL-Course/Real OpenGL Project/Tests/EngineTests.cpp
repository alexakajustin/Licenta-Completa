#ifdef RUN_UNIT_TESTS

#include "External Libs/doctest.h"
#include "Scene/GameObject.h"
#include "Scene/SceneSerializer.h"
#include "Scene/SceneManager.h"
#include "Core/Transform.h"
#include "Rendering/Texture.h"
#include "Rendering/Material.h"
#include <iostream>

TEST_CASE("GameObject Hierarchy and Transforms")
{
    SUBCASE("Creating GameObject sets name and defaults")
    {
        GameObject obj("TestObject");
        CHECK(obj.GetName() == "TestObject");
        CHECK(obj.GetParent() == nullptr);
        CHECK(obj.GetChildren().empty());
        CHECK(obj.GetVisible() == true);
    }

    SUBCASE("Parent-Child relations and matrix propagation")
    {
        GameObject parent("Parent");
        GameObject child("Child");

        parent.AddChild(&child);
        CHECK(child.GetParent() == &parent);
        CHECK(parent.GetChildren().size() == 1);
        CHECK(parent.GetChildren()[0] == &child);

        // Move parent and check world matrix
        parent.GetTransform().SetPosition(glm::vec3(10.0f, 0.0f, 0.0f));
        child.GetTransform().SetPosition(glm::vec3(5.0f, 0.0f, 0.0f));

        glm::mat4 childWorld = child.GetWorldMatrix();
        glm::vec3 childWorldPos = glm::vec3(childWorld[3]);

        // Child world position should be parent + child local = 15.0 on X axis
        CHECK(childWorldPos.x == doctest::Approx(15.0f));

        // Properly decouple child using RemoveChild (Orphan only clears child-side pointers)
        parent.RemoveChild(&child);
        CHECK(child.GetParent() == nullptr);
        CHECK(parent.GetChildren().empty());

        childWorld = child.GetWorldMatrix();
        childWorldPos = glm::vec3(childWorld[3]);
        CHECK(childWorldPos.x == doctest::Approx(5.0f));
    }

    SUBCASE("Scale inheritance toggles")
    {
        GameObject parent("Parent");
        GameObject child("Child");

        parent.AddChild(&child);
        parent.GetTransform().SetScale(glm::vec3(2.0f, 2.0f, 2.0f));
        child.GetTransform().SetScale(glm::vec3(1.0f, 1.0f, 1.0f));

        // Default: inherit scale
        glm::mat4 childWorld = child.GetWorldMatrix();
        glm::vec3 scaleVec(glm::length(glm::vec3(childWorld[0])), glm::length(glm::vec3(childWorld[1])), glm::length(glm::vec3(childWorld[2])));
        CHECK(scaleVec.x == doctest::Approx(2.0f));

        // Turn off scale inheritance and invalidate cache
        child.SetInheritScale(false);
        child.SetDirty();
        childWorld = child.GetWorldMatrix();
        scaleVec = glm::vec3(glm::length(glm::vec3(childWorld[0])), glm::length(glm::vec3(childWorld[1])), glm::length(glm::vec3(childWorld[2])));
        CHECK(scaleVec.x == doctest::Approx(1.0f));
    }
}

TEST_CASE("Scene Serialization - Object Snapshot Roundtrip")
{
    // Test the in-memory snapshot/restore system used by Undo/Redo
    GameObject obj("SnapshotTest");
    obj.GetTransform().SetPosition(glm::vec3(1.0f, 2.0f, 3.0f));
    obj.GetTransform().SetRotation(glm::vec3(45.0f, 90.0f, 0.0f));
    obj.GetTransform().SetScale(glm::vec3(2.0f, 2.0f, 2.0f));

    // Snapshot the object to JSON
    std::string snapshot = SceneSerializer::SnapshotObject(&obj);
    REQUIRE(!snapshot.empty());

    // Modify the object
    obj.GetTransform().SetPosition(glm::vec3(99.0f, 99.0f, 99.0f));
    obj.GetTransform().SetScale(glm::vec3(5.0f, 5.0f, 5.0f));
    CHECK(obj.GetTransform().GetPosition().x == doctest::Approx(99.0f));

    // Restore from snapshot
    SceneSerializer::RestoreObject(&obj, snapshot);

    // Verify original values are restored
    CHECK(obj.GetTransform().GetPosition().x == doctest::Approx(1.0f));
    CHECK(obj.GetTransform().GetPosition().y == doctest::Approx(2.0f));
    CHECK(obj.GetTransform().GetPosition().z == doctest::Approx(3.0f));
    CHECK(obj.GetTransform().GetScale().x == doctest::Approx(2.0f));
}

TEST_CASE("SceneManager - Object Management")
{
    SceneManager scene;
    Texture dummyTex;
    Material dummyMat;
    scene.SetDefaultResources(&dummyTex, &dummyMat);

    // Add objects to the scene
    GameObject* obj1 = new GameObject("Object1");
    GameObject* obj2 = new GameObject("Object2");
    scene.AddObject(obj1);
    scene.AddObject(obj2);

    CHECK(scene.GetObjects().size() == 2);
    CHECK(scene.GetObjects()[0]->GetName() == "Object1");
    CHECK(scene.GetObjects()[1]->GetName() == "Object2");
}

#endif // RUN_UNIT_TESTS
