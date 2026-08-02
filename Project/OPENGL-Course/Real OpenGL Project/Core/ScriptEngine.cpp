#include "ScriptEngine.h"
#include <iostream>
#include <glm/glm.hpp>
#include "Scene/GameObject.h"
#include "Scene/Component.h"
#include "Core/Transform.h"

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
        "transform", sol::property([](GameObject& go) -> Transform& { return go.GetTransform(); })
    );
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
