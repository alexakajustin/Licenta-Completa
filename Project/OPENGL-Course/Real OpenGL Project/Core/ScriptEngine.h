#pragma once

#include <sol/sol.hpp>
#include <string>

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

private:
    ScriptEngine() = default;
    ~ScriptEngine() = default;

    void RegisterMath();
    void RegisterCoreTypes();
    void RegisterGameObject();
    void RegisterPhysics();

    // Delete copy and move semantics
    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    sol::state lua;
};
