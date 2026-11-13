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
    // Leaves Play mode: drops the environment and callbacks (IsRunning() becomes false)
    void Stop();

    std::string GetName() const override { return "LuaScriptComponent"; }

    const std::string& GetFilepath() const { return filepath; }
    void SetFilepath(const std::string& path) { filepath = path; }
    const std::string& GetScriptPath() const { return filepath; }
    void SetScriptPath(const std::string& path) { filepath = path; }

    // File name without folder / extension ("Assets/scripts/enemy.lua" -> "enemy")
    std::string GetScriptName() const;
    // True if 'nameOrPath' names this script (by name, file name or path)
    bool MatchesScript(const std::string& nameOrPath) const;

    // The script's private environment: its globals and functions. Valid once the script ran
    // (Play mode); other scripts reach it through gameObject:GetScript("name").
    sol::environment& GetEnvironment() { return env; }
    bool IsRunning() const { return env.valid(); }

private:
    std::string filepath;
    sol::environment env;
    
    sol::protected_function startFunc;
    sol::protected_function updateFunc;
};
