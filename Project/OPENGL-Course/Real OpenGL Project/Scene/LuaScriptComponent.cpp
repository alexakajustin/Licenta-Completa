#include "LuaScriptComponent.h"
#include "Core/ScriptEngine.h"
#include "GameObject.h"
#include <iostream>
#include "imgui.h"
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "Comdlg32.lib")

LuaScriptComponent::LuaScriptComponent(GameObject* owner, const std::string& scriptPath)
    : Component(owner), filepath(scriptPath)
{
}

void LuaScriptComponent::Start()
{
    // Do nothing on add. Scripts should only run during Play Mode.
}

void LuaScriptComponent::ReloadScript()
{
    if (filepath.empty()) return;

    sol::state& lua = ScriptEngine::GetInstance().GetState();
    
    // Create a new isolated environment for this component, with a fallback to the global state
    env = sol::environment(lua, sol::create, lua.globals());
    
    // Inject the GameObject reference into the environment
    env["gameObject"] = gameObject;
    
    try
    {
        // Load the script and run it within the environment
        sol::load_result script = lua.load_file(filepath);
        if (!script.valid())
        {
            sol::error err = script;
            std::cerr << "[LuaScriptComponent] Error loading '" << filepath << "': " << err.what() << std::endl;
            return;
        }
        
        // Execute script with environment
        sol::protected_function target = script;
        env.set_on(target);
        auto result = target();
        
        if (!result.valid())
        {
            sol::error err = result;
            std::cerr << "[LuaScriptComponent] Error executing '" << filepath << "': " << err.what() << std::endl;
        }
        
        // Clear old functions in case the new script doesn't define them
        startFunc = sol::nil;
        updateFunc = sol::nil;

        // Cache functions
        sol::optional<sol::protected_function> startOpt = env["Start"];
        if (startOpt) {
            startFunc = startOpt.value();
            env.set_on(startFunc);
        }
        
        sol::optional<sol::protected_function> updateOpt = env["Update"];
        if (updateOpt) {
            updateFunc = updateOpt.value();
            env.set_on(updateFunc);
        }
        
        // Call Start if it exists
        if (startFunc.valid())
        {
            auto res = startFunc();
            if (!res.valid()) {
                sol::error err = res;
                std::cerr << "[LuaScriptComponent] Error in Start(): " << err.what() << std::endl;
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[LuaScriptComponent] Exception starting script '" << filepath << "': " << e.what() << std::endl;
    }
}

void LuaScriptComponent::Update(float deltaTime)
{
    if (updateFunc.valid())
    {
        auto res = updateFunc(deltaTime);
        if (!res.valid()) {
            sol::error err = res;
            std::cerr << "[LuaScriptComponent] Error in Update(): " << err.what() << std::endl;
        }
    }
}

void LuaScriptComponent::DrawInspector()
{
    std::string scriptName = filepath.empty() ? "None" : filepath;
    size_t pos = scriptName.find_last_of("/\\");
    if (pos != std::string::npos) scriptName = scriptName.substr(pos + 1);

    // Make the header look like Unity's script component header
    if (ImGui::CollapsingHeader((scriptName + " (Script)").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        
        ImGui::Text("Script");
        ImGui::SameLine(80.0f); // align the button
        
        float availableWidth = ImGui::GetContentRegionAvail().x;
        float dotButtonWidth = 24.0f;
        float scriptButtonWidth = availableWidth - dotButtonWidth - ImGui::GetStyle().ItemSpacing.x;

        // Push a slight grey color for the button to look like Unity's disabled object field
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::Button(scriptName.c_str(), ImVec2(scriptButtonWidth, 0));
        ImGui::PopStyleColor();
        
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Double click to open script in external editor");
            if (ImGui::IsMouseDoubleClicked(0) && !filepath.empty())
            {
                // Open the script in the OS default editor/IDE
                ShellExecuteA(NULL, "open", filepath.c_str(), NULL, NULL, SW_SHOW);
            }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("O", ImVec2(dotButtonWidth, 0)))
        {
            OPENFILENAMEA ofn;
            CHAR szFile[260] = { 0 };
            ZeroMemory(&ofn, sizeof(OPENFILENAMEA));
            ofn.lStructSize = sizeof(OPENFILENAMEA);
            ofn.hwndOwner = NULL;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = sizeof(szFile);
            ofn.lpstrFilter = "Lua Scripts\0*.lua\0All Files\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrFileTitle = NULL;
            ofn.nMaxFileTitle = 0;
            ofn.lpstrInitialDir = NULL;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

            if (GetOpenFileNameA(&ofn) == TRUE)
            {
                filepath = ofn.lpstrFile;
                // Don't auto-reload script in edit mode to prevent executing top-level logic prematurely.
            }
        }
    }
}

