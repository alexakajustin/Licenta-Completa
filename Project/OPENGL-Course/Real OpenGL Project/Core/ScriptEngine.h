#pragma once

#ifndef SOL_DEFAULT_PASS_ON_ERROR
#define SOL_DEFAULT_PASS_ON_ERROR 1
#endif

#include <sol/sol.hpp>
#include <string>
#include <unordered_map>

class Material;

/**
 * @class ScriptEngine
 * @brief Manages the embedded Lua environment using sol2.
 */
class ScriptEngine
{
public:
    static ScriptEngine& GetInstance()
    {
        static ScriptEngine instance;
        return instance;
    }

    /**
     * @brief Initializes the Lua state, opens standard libraries, and binds engine API.
     */
    void Init();

    /**
     * @brief Executes a Lua script from a file.
     * @param filepath Path to the Lua script.
     * @return True if successful, false otherwise.
     */
    bool ExecuteFile(const std::string& filepath);

    /**
     * @brief Executes a string containing Lua code.
     * @param script String containing Lua code.
     * @return True if successful, false otherwise.
     */
    bool ExecuteString(const std::string& script);

    /**
     * @brief Gets the underlying sol::state.
     * @return Reference to the sol::state.
     */
    sol::state& GetState() { return lua; }

    /**
     * @brief Finds a script file from a name or path: "enemy", "enemy.lua",
     * "Assets/scripts/enemy.lua" or an absolute path. Searches Assets/ by file name.
     * @return Path of the existing file, or empty string.
     */
    static std::string ResolveScriptPath(const std::string& nameOrPath);

    /**
     * @brief Runs a script once in its own environment and returns that environment (its
     * functions and globals), like a module. Cached by resolved path; reload=true re-runs it.
     */
    sol::object RequireScript(const std::string& nameOrPath, bool reload = false);

    /**
     * @brief Play-mode clock seen by scripts: Time.deltaTime, Time.time, Time.frameCount.
     * BeginFrame runs once per simulated frame, before the components update.
     */
    void BeginFrame(float deltaTime);
    void ResetClock();

private:
    ScriptEngine() = default;
    ~ScriptEngine() = default;

    void RegisterMath();
    void RegisterCoreTypes();
    void RegisterGameObject();
    void RegisterPhysics();
    void RegisterResourceSystem();
    void RegisterRendering();   // Material, Texture layers
    void RegisterSceneApi();    // Scene, Assets, Script, Time, Math tables
    void RegisterInput();
    void RegisterLights();
    void DocumentStandardLibrary();

    // Delete copy and move semantics
    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    sol::state lua;
    bool m_Initialized = false;

    // Declared after 'lua': destroyed first, while the Lua state still exists
    std::unordered_map<std::string, sol::environment> m_Modules;
    std::unordered_map<std::string, Material*> m_MaterialCache;
    double m_ScriptTime = 0.0;
    long long m_FrameCount = 0;
};
