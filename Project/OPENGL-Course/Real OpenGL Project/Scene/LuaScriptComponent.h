#pragma once

#include "Component.h"
#include <string>
#include <sol/sol.hpp>

/**
 * @class LuaScriptComponent
 * @brief Allows a GameObject to run Lua scripts, hooking into Start and Update.
 */
class LuaScriptComponent : public Component {
public:
    LuaScriptComponent(GameObject* owner, const std::string& scriptPath);
    ~LuaScriptComponent() override = default;

    void Start() override;
    void Update(float deltaTime) override;
    void DrawInspector() override;
    
    void ReloadScript();

    std::string GetName() const override { return "LuaScriptComponent"; }

private:
    std::string filepath;
    sol::environment env;
    
    sol::protected_function startFunc;
    sol::protected_function updateFunc;
};
