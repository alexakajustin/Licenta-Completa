#include "ScriptEngine.h"
#include <iostream>
#include <glm/glm.hpp>
#include "Scene/GameObject.h"
#include "Scene/Component.h"
#include "Scene/RigidBody.h"
#include "Scene/BoxCollider.h"
#include "Core/Transform.h"
#include <GLFW/glfw3.h>

void ScriptEngine::RegisterMath()
{
    // Bind glm::vec3
    lua.new_usertype<glm::vec3>("vec3",
        sol::constructors<glm::vec3(), glm::vec3(float), glm::vec3(float, float, float)>(),
        "x", &glm::vec3::x,
        "y", &glm::vec3::y,
        "z", &glm::vec3::z,
        // Operator overloads
        sol::meta_function::addition, [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
        sol::meta_function::subtraction, [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
        sol::meta_function::multiplication, [](const glm::vec3& a, float b) { return a * b; },
        sol::meta_function::to_string, [](const glm::vec3& v) { 
            return "vec3(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")"; 
        }
    );
}

void ScriptEngine::RegisterCoreTypes()
{
    // Bind Transform
    lua.new_usertype<Transform>("Transform",
        "GetPosition", &Transform::GetPosition,
        "SetPosition", &Transform::SetPosition,
        "GetRotation", &Transform::GetRotation,
        "SetRotation", &Transform::SetRotation,
        "GetScale", &Transform::GetScale,
        "SetScale", &Transform::SetScale
    );

    // Bind base Component
    lua.new_usertype<Component>("Component",
        "GetGameObject", &Component::GetGameObject,
        "GetName", &Component::GetName
    );
}

void ScriptEngine::RegisterGameObject()
{
    // Bind GameObject
    lua.new_usertype<GameObject>("GameObject",
        "GetName", &GameObject::GetName,
        "SetName", &GameObject::SetName,
        // Expose Transform as a property
        "transform", sol::property([](GameObject& go) -> Transform& { return go.GetTransform(); }),
        
        // Component adding/getting methods
        "AddRigidBody", [](GameObject& go) -> RigidBody* { return go.AddComponent<RigidBody>(); },
        "GetRigidBody", [](GameObject& go) -> RigidBody* { return go.GetComponent<RigidBody>(); },
        "AddBoxCollider", [](GameObject& go) -> BoxCollider* { return go.AddComponent<BoxCollider>(); },
        "GetBoxCollider", [](GameObject& go) -> BoxCollider* { return go.GetComponent<BoxCollider>(); }
    );
}

void ScriptEngine::RegisterPhysics()
{
    // Bind RigidBody::BodyType
    lua.new_enum("BodyType",
        "Static", RigidBody::BodyType::Static,
        "Kinematic", RigidBody::BodyType::Kinematic,
        "Dynamic", RigidBody::BodyType::Dynamic
    );

    // Bind RigidBody
    lua.new_usertype<RigidBody>("RigidBody",
        sol::base_classes, sol::bases<Component>(),
        "SetType", &RigidBody::SetType,
        "GetType", &RigidBody::GetType,
        "SetMass", &RigidBody::SetMass,
        "GetMass", &RigidBody::GetMass,
        "SetFriction", &RigidBody::SetFriction,
        "SetRestitution", &RigidBody::SetRestitution,
        "SetLinearVelocity", &RigidBody::SetLinearVelocity,
        "SetLockRotation", &RigidBody::SetLockRotation,
        "AddForce", &RigidBody::AddForce,
        "AddImpulse", &RigidBody::AddImpulse
    );

    // Bind BoxCollider
    lua.new_usertype<BoxCollider>("BoxCollider",
        sol::base_classes, sol::bases<Component>(),
        "size", &BoxCollider::size,
        "offset", &BoxCollider::offset,
        "isTrigger", &BoxCollider::isTrigger
    );
    
    // Simple global Input table for checking keys
    sol::table input = lua.create_named_table("Input");
    input.set_function("IsKeyDown", [](int key) -> bool {
        GLFWwindow* window = glfwGetCurrentContext();
        if (!window) return false;
        return glfwGetKey(window, key) == GLFW_PRESS;
    });
    
    // Bind a few common keys
    input["KEY_SPACE"] = GLFW_KEY_SPACE;
    input["KEY_W"] = GLFW_KEY_W;
    input["KEY_A"] = GLFW_KEY_A;
    input["KEY_S"] = GLFW_KEY_S;
    input["KEY_D"] = GLFW_KEY_D;
    input["KEY_UP"] = GLFW_KEY_UP;
    input["KEY_DOWN"] = GLFW_KEY_DOWN;
    input["KEY_LEFT"] = GLFW_KEY_LEFT;
    input["KEY_RIGHT"] = GLFW_KEY_RIGHT;
}

void ScriptEngine::Init()
{
    // Open basic Lua libraries
    lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::string, sol::lib::math, sol::lib::table, sol::lib::os);

    // Bind a simple log function to test it works
    lua.set_function("log", [](const std::string& message) {
        std::cout << "[Lua] " << message << std::endl;
    });

    RegisterMath();
    RegisterCoreTypes();
    RegisterGameObject();
    RegisterPhysics();

    std::cout << "ScriptEngine Initialized with Lua 5.4 and sol2." << std::endl;
}

bool ScriptEngine::ExecuteFile(const std::string& filepath)
{
    try
    {
        auto result = lua.script_file(filepath);
        if (!result.valid())
        {
            sol::error err = result;
            std::cerr << "[ScriptEngine] Error executing file '" << filepath << "':\n" << err.what() << std::endl;
            return false;
        }
        return true;
    }
    catch (const sol::error& e)
    {
        std::cerr << "[ScriptEngine] Exception executing file '" << filepath << "':\n" << e.what() << std::endl;
        return false;
    }
}

bool ScriptEngine::ExecuteString(const std::string& script)
{
    try
    {
        auto result = lua.script(script);
        if (!result.valid())
        {
            sol::error err = result;
            std::cerr << "[ScriptEngine] Error executing script string:\n" << err.what() << std::endl;
            return false;
        }
        return true;
    }
    catch (const sol::error& e)
    {
        std::cerr << "[ScriptEngine] Exception executing script string:\n" << e.what() << std::endl;
        return false;
    }
}
