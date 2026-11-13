#ifdef RUN_UNIT_TESTS

#include "External Libs/doctest.h"
#include <GL/glew.h>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <sol/sol.hpp>

#include "Core/ScriptEngine.h"
#include "Core/ServiceLocator.h"
#include "Core/AssetManager.h"
#include "Core/Transform.h"
#include "Scene/GameObject.h"
#include "Scene/SceneManager.h"
#include "Scene/SceneSerializer.h"
#include "Scene/LuaScriptComponent.h"
#include "Editor/ScriptEditor.h"
#include "Rendering/Texture.h"
#include "Rendering/Material.h"
#include "Rendering/Model.h"

#if __has_include("Core/ResourceHandle.h")
#include "Core/ResourceHandle.h"
#define RESOURCE_HANDLE_AVAILABLE 1
#else
#define RESOURCE_HANDLE_AVAILABLE 0
#endif

#if __has_include("Scene/GameObjectRegistry.h")
#include "Scene/GameObjectRegistry.h"
#define GAME_OBJECT_REGISTRY_AVAILABLE 1
#else
#define GAME_OBJECT_REGISTRY_AVAILABLE 0
#endif

namespace {

// --- Test Fixture Helpers & Simulators ---

inline std::string NormalizeAssetPath(const std::string& path)
{
    std::string res = path;
    for (char& c : res)
    {
        if (c == '\\') c = '/';
    }
    return res;
}

inline std::string SanitizeLuaVarName(const std::string& name)
{
    std::string res;
    bool lastWasUnderscore = false;
    for (char c : name)
    {
        if (std::isalnum(static_cast<unsigned char>(c)))
        {
            res.push_back(c);
            lastWasUnderscore = false;
        }
        else
        {
            if (!lastWasUnderscore && !res.empty())
            {
                res.push_back('_');
                lastWasUnderscore = true;
            }
        }
    }
    while (!res.empty() && res.back() == '_')
    {
        res.pop_back();
    }
    if (res.empty() || std::isdigit(static_cast<unsigned char>(res[0])))
    {
        res = "res_" + res;
    }
    return res;
}

inline std::string FormatAssetDrop(const std::string& rawPath, const std::string& varName = "")
{
    std::string normPath = NormalizeAssetPath(rawPath);
    std::string v = varName;
    if (v.empty())
    {
        size_t lastSlash = normPath.find_last_of('/');
        std::string fname = (lastSlash != std::string::npos) ? normPath.substr(lastSlash + 1) : normPath;
        size_t dot = fname.find_last_of('.');
        std::string stem = (dot != std::string::npos) ? fname.substr(0, dot) : fname;
        v = SanitizeLuaVarName(stem);
    }
    return "local " + v + " = Resource.Get(\"" + normPath + "\")\n";
}

inline std::string FormatGameObjectDrop(const std::string& objName, const std::string& varName = "")
{
    std::string v = varName.empty() ? SanitizeLuaVarName(objName) : varName;
    return "local " + v + " = Resource.GameObject(\"" + objName + "\")\n";
}

inline std::string FormatTypedTextureDrop(const std::string& rawPath, const std::string& varName = "")
{
    std::string normPath = NormalizeAssetPath(rawPath);
    std::string v = varName.empty() ? "tex" : varName;
    return "local " + v + " = Resource.Texture(\"" + normPath + "\")\n";
}

inline std::string FormatTypedModelDrop(const std::string& rawPath, const std::string& varName = "")
{
    std::string normPath = NormalizeAssetPath(rawPath);
    std::string v = varName.empty() ? "mdl" : varName;
    return "local " + v + " = Resource.Model(\"" + normPath + "\")\n";
}

inline std::string FormatTypedAudioDrop(const std::string& rawPath, const std::string& varName = "")
{
    std::string normPath = NormalizeAssetPath(rawPath);
    std::string v = varName.empty() ? "snd" : varName;
    return "local " + v + " = Resource.Audio(\"" + normPath + "\")\n";
}

inline std::string FormatTypedNodeGraphDrop(const std::string& id, const std::string& varName = "")
{
    std::string v = varName.empty() ? "graph" : varName;
    return "local " + v + " = Resource.NodeGraph(\"" + id + "\")\n";
}

struct TestScriptTab
{
    std::string filepath;
    std::string title;
    std::string content;
    bool isModified = false;
    size_t cursorPos = 0;

    void InsertText(size_t pos, const std::string& text)
    {
        if (pos > content.size()) pos = content.size();
        content.insert(pos, text);
        cursorPos = pos + text.size();
        isModified = true;
    }

    void AppendText(const std::string& text)
    {
        content.append(text);
        cursorPos = content.size();
        isModified = true;
    }

    bool Save(const std::string& path)
    {
        std::ofstream out(path, std::ios::trunc);
        if (!out.is_open()) return false;
        out << content;
        out.close();
        filepath = path;
        isModified = false;
        return true;
    }

    bool Load(const std::string& path)
    {
        std::ifstream in(path);
        if (!in.is_open()) return false;
        std::stringstream ss;
        ss << in.rdbuf();
        content = ss.str();
        filepath = path;
        cursorPos = 0;
        isModified = false;
        return true;
    }
};

void EnsureScriptEngineReady()
{
    static bool initialized = false;
    if (!initialized)
    {
        ScriptEngine::GetInstance().Init();
        initialized = true;
    }
}

} // namespace

// =========================================================================
// TIER 1: FEATURE COVERAGE (>=5 tests per feature, >=50 total)
// =========================================================================

TEST_CASE("Tier 1 - Feature 1: Texture Resource Handle")
{
#if RESOURCE_HANDLE_AVAILABLE
    SUBCASE("1.1 Creation via typed helper Resource::Texture")
    {
        ResourceHandle tex = ResourceHandle::Texture("Assets/Textures/plain.png");
        CHECK(tex.GetType() == ResourceType::Texture);
        CHECK(tex.GetTypeName() == "Texture");
        CHECK(tex.GetPath() == "Assets/Textures/plain.png");
    }

    SUBCASE("1.2 Automatic classification via Resource::FromPath")
    {
        ResourceHandle tex = ResourceHandle::FromPath("Assets/Textures/grid_prototype.png");
        CHECK(tex.GetType() == ResourceType::Texture);
        CHECK(tex.GetTypeName() == "Texture");
        CHECK(tex.IsValid() == true);
    }

    SUBCASE("1.3 Extension classification across formats (.png, .jpg, .tga, .hdr)")
    {
        CHECK(ResourceHandle::FromPath("test.png").GetType() == ResourceType::Texture);
        CHECK(ResourceHandle::FromPath("test.jpg").GetType() == ResourceType::Texture);
        CHECK(ResourceHandle::FromPath("test.jpeg").GetType() == ResourceType::Texture);
        CHECK(ResourceHandle::FromPath("test.tga").GetType() == ResourceType::Texture);
        CHECK(ResourceHandle::FromPath("test.hdr").GetType() == ResourceType::Texture);
    }

    SUBCASE("1.4 Path and identifier querying")
    {
        ResourceHandle tex = ResourceHandle::Texture("Assets/Textures/plain.png");
        CHECK(tex.GetPath() == "Assets/Textures/plain.png");
        CHECK(tex.GetIdentifier() == "Assets/Textures/plain.png");
    }

    SUBCASE("1.5 C++ typed texture resolution")
    {
        ResourceHandle tex = ResourceHandle::Texture("Assets/Textures/plain.png");
        if (tex.IsValid())
        {
            Texture* resolved = tex.ResolveTexture();
            CHECK(resolved != nullptr);
        }
        else
        {
            CHECK(tex.ResolveTexture() == nullptr);
        }
    }
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 1 - Feature 2: Model Resource Handle")
{
#if RESOURCE_HANDLE_AVAILABLE
    SUBCASE("2.1 Creation via typed helper Resource::Model")
    {
        ResourceHandle mdl = ResourceHandle::Model("Assets/Models/grass.obj");
        CHECK(mdl.GetType() == ResourceType::Model);
        CHECK(mdl.GetTypeName() == "Model");
    }

    SUBCASE("2.2 Automatic classification via Resource::FromPath")
    {
        ResourceHandle mdl = ResourceHandle::FromPath("Assets/Models/Tree.obj");
        CHECK(mdl.GetType() == ResourceType::Model);
        CHECK(mdl.IsValid() == true);
    }

    SUBCASE("2.3 Model format classification (.obj, .fbx, .gltf, .glb, .dae, .assbin)")
    {
        CHECK(ResourceHandle::FromPath("model.obj").GetType() == ResourceType::Model);
        CHECK(ResourceHandle::FromPath("model.fbx").GetType() == ResourceType::Model);
        CHECK(ResourceHandle::FromPath("model.gltf").GetType() == ResourceType::Model);
        CHECK(ResourceHandle::FromPath("model.glb").GetType() == ResourceType::Model);
        CHECK(ResourceHandle::FromPath("model.dae").GetType() == ResourceType::Model);
        CHECK(ResourceHandle::FromPath("model.assbin").GetType() == ResourceType::Model);
    }

    SUBCASE("2.4 Path query preserves input model path")
    {
        ResourceHandle mdl = ResourceHandle::Model("Assets/Models/grass.obj");
        CHECK(mdl.GetPath() == "Assets/Models/grass.obj");
    }

    SUBCASE("2.5 Model typed resolution safety")
    {
        // Precondition requirement: AssetManager must be present in test context
        REQUIRE(ServiceLocator::GetAssetManager() != nullptr);

        // 1. Valid model resolution verification
        ResourceHandle mdl = ResourceHandle::Model("Assets/Models/grass.obj");
        CHECK(mdl.IsValid() == true);
        Model* resolved = mdl.ResolveModel();
        CHECK(resolved != nullptr);

        // 2. Invalid model resolution safety verification
        ResourceHandle invalidMdl = ResourceHandle::Model("Assets/Models/definitely_non_existent_404.obj");
        CHECK(invalidMdl.IsValid() == false);
        CHECK(invalidMdl.ResolveModel() == nullptr);
    }
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 1 - Feature 3: Audio Resource Handle")
{
#if RESOURCE_HANDLE_AVAILABLE
    SUBCASE("3.1 Creation via typed helper Resource::Audio")
    {
        ResourceHandle audio = ResourceHandle::Audio("Assets/Audio/ambient.wav");
        CHECK(audio.GetType() == ResourceType::Audio);
        CHECK(audio.GetTypeName() == "Audio");
        CHECK(audio.GetPath() == "Assets/Audio/ambient.wav");
    }

    SUBCASE("3.2 Audio format classification (.wav, .mp3, .ogg, .flac)")
    {
        CHECK(ResourceHandle::FromPath("sound.wav").GetType() == ResourceType::Audio);
        CHECK(ResourceHandle::FromPath("music.mp3").GetType() == ResourceType::Audio);
        CHECK(ResourceHandle::FromPath("sfx.ogg").GetType() == ResourceType::Audio);
        CHECK(ResourceHandle::FromPath("voice.flac").GetType() == ResourceType::Audio);
    }

    SUBCASE("3.3 Audio resolution returns valid AudioClip structure")
    {
        ResourceHandle audio = ResourceHandle::Audio("Assets/Audio/test.wav");
        AudioClip* clip = audio.ResolveAudio();
        if (audio.IsValid())
        {
            REQUIRE(clip != nullptr);
            CHECK(clip->GetPath() == "Assets/Audio/test.wav");
            CHECK(clip->GetName() == "test");
        }
        else
        {
            CHECK(clip == nullptr);
        }
    }

    SUBCASE("3.4 AudioClip Play and Stop interfaces exist and do not crash")
    {
        AudioClip clip;
        clip.path = "Assets/Audio/mock.wav";
        clip.name = "mock";
        clip.duration = 2.5f;
        CHECK(clip.GetDuration() == doctest::Approx(2.5f));
        clip.Play();
        clip.Stop();
        CHECK(clip.GetPath() == "Assets/Audio/mock.wav");
    }

    SUBCASE("3.5 Audio handle validity matches disk file presence")
    {
        ResourceHandle missing = ResourceHandle::Audio("Assets/Audio/ghost_404.wav");
        CHECK(missing.IsValid() == false);
        CHECK(missing.ResolveAudio() == nullptr);
    }
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 1 - Feature 4: NodeGraph Resource Handle")
{
#if RESOURCE_HANDLE_AVAILABLE
    SUBCASE("4.1 Creation via typed helper Resource::NodeGraph")
    {
        ResourceHandle graph = ResourceHandle::NodeGraph("Alien_Sphere");
        CHECK(graph.GetType() == ResourceType::NodeGraph);
        CHECK(graph.GetTypeName() == "NodeGraph");
    }

    SUBCASE("4.2 Classification of .json files as NodeGraph")
    {
        ResourceHandle graph = ResourceHandle::FromPath("Assets/Scenes/City/City_Grid.json");
        CHECK(graph.GetType() == ResourceType::NodeGraph);
        CHECK(graph.GetTypeName() == "NodeGraph");
        CHECK(graph.IsValid() == true);
    }

    SUBCASE("4.3 Identifier and path queries")
    {
        ResourceHandle graph = ResourceHandle::NodeGraph("Terrain_Gen");
        CHECK(graph.GetIdentifier() == "Terrain_Gen");
        CHECK(graph.GetPath() == "Terrain_Gen");
    }

    SUBCASE("4.4 NodeGraph resolution safety on existing JSON")
    {
        ResourceHandle graph = ResourceHandle::FromPath("Assets/Scenes/City/City_Grid.json");
        CHECK(graph.IsValid() == true);
        NodeGraph* g = graph.ResolveNodeGraph();
        CHECK(g != nullptr);
        CHECK(graph.ResolveNodeGraph() == g);
    }

    SUBCASE("4.5 NodeGraph resolution on non-existent JSON returns nullptr")
    {
        ResourceHandle badGraph = ResourceHandle::NodeGraph("definitely_non_existent_graph.json");
        CHECK(badGraph.IsValid() == false);
        CHECK(badGraph.ResolveNodeGraph() == nullptr);
    }
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 1 - Feature 5: GameObject Resource Handle")
{
#if RESOURCE_HANDLE_AVAILABLE && GAME_OBJECT_REGISTRY_AVAILABLE
    SUBCASE("5.1 Creation from active GameObject pointer")
    {
        GameObject obj("HeroPlayer");
        ResourceHandle handle = ResourceHandle::FromGameObject(&obj);
        CHECK(handle.GetType() == ResourceType::GameObject);
        CHECK(handle.GetTypeName() == "GameObject");
        CHECK(handle.IsValid() == true);
        CHECK(handle.GetIdentifier() == "HeroPlayer");
        CHECK(handle.GetID() == obj.GetID());
    }

    SUBCASE("5.2 Creation by name via Resource::GameObject")
    {
        GameObject obj("TargetEntity");
        ResourceHandle handle = ResourceHandle::GameObject("TargetEntity");
        CHECK(handle.GetType() == ResourceType::GameObject);
        CHECK(handle.GetTypeName() == "GameObject");
        CHECK(handle.IsValid() == true);
        CHECK(handle.ResolveGameObject() == &obj);
    }

    SUBCASE("5.3 Creation from 64-bit ID via Resource::FromID")
    {
        GameObject obj("UniqueTarget");
        uint64_t objID = obj.GetID();
        REQUIRE(objID > 0);

        ResourceHandle handle = ResourceHandle::FromID(objID);
        CHECK(handle.GetType() == ResourceType::GameObject);
        CHECK(handle.IsValid() == true);
        CHECK(handle.GetID() == objID);
        CHECK(handle.ResolveGameObject() == &obj);
    }

    SUBCASE("5.4 AsGameObject convenience resolver")
    {
        GameObject obj("ConvenienceObj");
        ResourceHandle handle = ResourceHandle::FromGameObject(&obj);
        CHECK(handle.AsGameObject() == &obj);
    }

    SUBCASE("5.5 Monotonic ID assigned on GameObject construction")
    {
        GameObject obj1("Entity1");
        GameObject obj2("Entity2");
        CHECK(obj1.GetID() > 0);
        CHECK(obj2.GetID() > obj1.GetID());
    }
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 1 - Feature 6: Dangling Pointer & Deletion Safety")
{
#if RESOURCE_HANDLE_AVAILABLE && GAME_OBJECT_REGISTRY_AVAILABLE
    SUBCASE("6.1 Active GameObject handle reports IsValid == true")
    {
        auto* tempObj = new GameObject("VolatileObject");
        ResourceHandle handle = ResourceHandle::FromGameObject(tempObj);
        CHECK(handle.IsValid() == true);
        CHECK(handle.ResolveGameObject() == tempObj);
        delete tempObj;
    }

    SUBCASE("6.2 Destructed GameObject automatically invalidates handle")
    {
        auto* tempObj = new GameObject("WillBeDeleted");
        uint64_t targetID = tempObj->GetID();
        ResourceHandle handle = ResourceHandle::FromID(targetID);
        CHECK(handle.IsValid() == true);

        delete tempObj; // Invokes GameObjectRegistry::Unregister(targetID)

        CHECK(handle.IsValid() == false);
        CHECK(handle.ResolveGameObject() == nullptr);
        CHECK(handle.Resolve() == nullptr);
    }

    SUBCASE("6.3 InvalidateTarget manual invalidation safety")
    {
        GameObject obj("ManualInvalidate");
        ResourceHandle handle = ResourceHandle::FromGameObject(&obj);
        CHECK(handle.IsValid() == true);

        handle.InvalidateTarget();
        CHECK(handle.IsValid() == false);
        CHECK(handle.GetType() == ResourceType::Invalid);
        CHECK(handle.ResolveGameObject() == nullptr);
    }

    SUBCASE("6.4 ABA Prevention: Monotonic ID prevents stale address alias")
    {
        uint64_t firstID = 0;
        {
            GameObject objA("FirstInstance");
            firstID = objA.GetID();
        }
        CHECK(GameObjectRegistry::FindByID(firstID) == nullptr);

        GameObject objB("SecondInstance");
        CHECK(objB.GetID() > firstID);
        CHECK(GameObjectRegistry::FindByID(firstID) == nullptr);
    }

    SUBCASE("6.5 Registry unregister of ID 0 or invalid ID is safe")
    {
        GameObjectRegistry::Unregister(0);
        GameObjectRegistry::Unregister(0xFFFFFFFFFFFFFFFFULL);
        CHECK(GameObjectRegistry::FindByID(0) == nullptr);
    }
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 1 - Feature 7: Polymorphic Lua Resolution & Instantiation")
{
    EnsureScriptEngineReady();
    sol::state& lua = ScriptEngine::GetInstance().GetState();

#if RESOURCE_HANDLE_AVAILABLE
    SUBCASE("7.1 Lua Resource.Get returns valid Resource userdata")
    {
        const char* script = R"(
            local h = Resource.Get("Assets/Textures/plain.png")
            assert(h ~= nil, "Resource handle should not be nil")
            assert(h:type_name() == "Texture", "Type name must be Texture")
            assert(h:is_valid() == true, "Texture must be valid")
        )";
        sol::protected_function_result res = lua.safe_script(script, sol::script_pass_on_error);
        CHECK(res.valid() == true);
    }

    SUBCASE("7.2 Lua :is_valid() and :type() properties and methods")
    {
        const char* script = R"(
            local h = Resource.Texture("Assets/Textures/plain.png")
            assert(h:is_valid() == true, "is_valid method must work")
            assert(h.valid == true, "valid property must work")
            assert(h.type == "Texture", "type property must work")
        )";
        sol::protected_function_result res = lua.safe_script(script, sol::script_pass_on_error);
        CHECK(res.valid() == true);
    }

    SUBCASE("7.3 Lua :resolve() produces underlying userdata")
    {
        const char* script = R"(
            local h = Resource.Get("Assets/Textures/plain.png")
            local tex = h:resolve()
            assert(tex ~= nil, "Resolved texture must not be nil")
            assert(tex:GetWidth() > 0, "Texture width must be greater than zero")
        )";
        sol::protected_function_result res = lua.safe_script(script, sol::script_pass_on_error);
        CHECK(res.valid() == true);
    }

    SUBCASE("7.4 Lua safe fallback on non-existent asset produces nil")
    {
        const char* script = R"(
            local bad = Resource.Get("Assets/Textures/ghost_file_404.png")
            assert(bad:is_valid() == false, "Ghost file must be invalid")
            local resolved = bad:resolve()
            assert(resolved == nil, "Resolving invalid handle must return nil")
        )";
        sol::protected_function_result res = lua.safe_script(script, sol::script_pass_on_error);
        CHECK(res.valid() == true);
    }

    SUBCASE("7.5 Lua :instantiate() on invalid handle returns nil without crash")
    {
        const char* script = R"(
            local bad = Resource.Get("NonExistent/Model.fbx")
            local inst = bad:instantiate()
            assert(inst == nil, "Instantiating invalid handle must produce nil")
        )";
        sol::protected_function_result res = lua.safe_script(script, sol::script_pass_on_error);
        CHECK(res.valid() == true);
    }
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 1 - Feature 8: Script Editor File I/O & Tab Buffers")
{
    SUBCASE("8.1 Script tab buffer creation and initial properties")
    {
        TestScriptTab tab;
        tab.title = "untitled.lua";
        tab.content = "-- new script\nfunction Start()\nend\n";
        tab.isModified = false;
        CHECK(tab.title == "untitled.lua");
        CHECK(tab.isModified == false);
        CHECK(tab.content.size() > 0);
    }

    SUBCASE("8.2 Text insertion at caret updates content and marks dirty")
    {
        TestScriptTab tab;
        tab.content = "local a = 10\n";
        tab.InsertText(6, "my_var = ");
        CHECK(tab.content == "local my_var = a = 10\n");
        CHECK(tab.isModified == true);
    }

    SUBCASE("8.3 Appending text preserves existing content")
    {
        TestScriptTab tab;
        tab.content = "function Start()\nend\n";
        tab.AppendText("function Update(dt)\nend\n");
        CHECK(tab.content.find("function Update(dt)") != std::string::npos);
        CHECK(tab.isModified == true);
    }

    SUBCASE("8.4 Save buffer to disk and reload")
    {
        std::string tempPath = "Assets/scripts/temp_unit_test_tab.lua";
        TestScriptTab tab;
        tab.content = "-- Unit test script buffer\nlocal x = 42\nlog('x is ' .. x)\n";
        bool saveOk = tab.Save(tempPath);
        CHECK(saveOk == true);
        CHECK(tab.isModified == false);

        TestScriptTab reloadTab;
        bool loadOk = reloadTab.Load(tempPath);
        CHECK(loadOk == true);
        CHECK(reloadTab.content == tab.content);

        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
    }

    SUBCASE("8.5 Multi-tab management simulation")
    {
        std::vector<TestScriptTab> tabs;
        tabs.push_back({ "script1.lua", "script1.lua", "print(1)", false, 0 });
        tabs.push_back({ "script2.lua", "script2.lua", "print(2)", false, 0 });
        tabs.push_back({ "script3.lua", "script3.lua", "print(3)", false, 0 });

        int activeTabIndex = 1;
        CHECK(tabs[activeTabIndex].title == "script2.lua");

        tabs.erase(tabs.begin() + 1);
        CHECK(tabs.size() == 2);
        CHECK(tabs[0].title == "script1.lua");
        CHECK(tabs[1].title == "script3.lua");
    }
}

TEST_CASE("Tier 1 - Feature 9: Drag-and-Drop Resource Insertion")
{
    SUBCASE("9.1 Generic asset drop format string generation")
    {
        std::string code = FormatAssetDrop("Assets/Textures/statue.png");
        CHECK(code == "local statue = Resource.Get(\"Assets/Textures/statue.png\")\n");
    }

    SUBCASE("9.2 GameObject drop format string generation")
    {
        std::string code = FormatGameObjectDrop("Enemy_Boss_1");
        CHECK(code == "local Enemy_Boss_1 = Resource.GameObject(\"Enemy_Boss_1\")\n");
    }

    SUBCASE("9.3 Typed texture, model, audio, and node graph format generators")
    {
        CHECK(FormatTypedTextureDrop("Assets/Textures/brick.png") == "local tex = Resource.Texture(\"Assets/Textures/brick.png\")\n");
        CHECK(FormatTypedModelDrop("Assets/Models/cube.obj") == "local mdl = Resource.Model(\"Assets/Models/cube.obj\")\n");
        CHECK(FormatTypedAudioDrop("Assets/Audio/ambient.wav") == "local snd = Resource.Audio(\"Assets/Audio/ambient.wav\")\n");
        CHECK(FormatTypedNodeGraphDrop("TerrainGen") == "local graph = Resource.NodeGraph(\"TerrainGen\")\n");
    }

    SUBCASE("9.4 Windows backslash normalization to forward slashes")
    {
        std::string winPath = "Assets\\Textures\\Subdir\\stone_wall.png";
        std::string code = FormatAssetDrop(winPath);
        CHECK(code.find("Assets/Textures/Subdir/stone_wall.png") != std::string::npos);
        CHECK(code.find('\\') == std::string::npos);
    }

    SUBCASE("9.5 Variable name sanitization for special characters and numbers")
    {
        CHECK(SanitizeLuaVarName("3D_Model-v2 (Copy)") == "res_3D_Model_v2_Copy");
        CHECK(SanitizeLuaVarName("normal_name") == "normal_name");
        CHECK(SanitizeLuaVarName("") == "res_");
    }
}

TEST_CASE("Tier 1 - Feature 10: Play Mode Lifecycle & Scene Reload")
{
    EnsureScriptEngineReady();
    sol::state& lua = ScriptEngine::GetInstance().GetState();

    SUBCASE("10.1 Script execution in isolated sol::environment")
    {
        sol::environment env(lua, sol::create, lua.globals());
        env["testVal"] = 100;
        sol::protected_function_result res = lua.safe_script("testVal = testVal + 23", env, sol::script_pass_on_error);
        CHECK(res.valid() == true);
        CHECK(env["testVal"].get<int>() == 123);
    }

    SUBCASE("10.2 Start and Update lifecycle calls simulation")
    {
        sol::environment env(lua, sol::create, lua.globals());
        const char* script = R"(
            local frameCounter = 0
            function Start()
                frameCounter = 10
            end
            function Update(dt)
                frameCounter = frameCounter + 1
            end
            function GetCount()
                return frameCounter
            end
        )";
        sol::protected_function_result res = lua.safe_script(script, env, sol::script_pass_on_error);
        REQUIRE(res.valid() == true);

        sol::protected_function startFn = env["Start"];
        sol::protected_function updateFn = env["Update"];
        sol::protected_function getCountFn = env["GetCount"];

        startFn();
        updateFn(0.016f);
        updateFn(0.016f);

        int count = getCountFn();
        CHECK(count == 12);
    }

    SUBCASE("10.3 Handle access within Play Mode script environment")
    {
#if RESOURCE_HANDLE_AVAILABLE
        sol::environment env(lua, sol::create, lua.globals());
        const char* script = R"(
            local handle = nil
            function Start()
                handle = Resource.Get("Assets/Textures/plain.png")
            end
            function IsReady()
                return handle ~= nil and handle:is_valid()
            end
        )";
        sol::protected_function_result res = lua.safe_script(script, env, sol::script_pass_on_error);
        REQUIRE(res.valid() == true);

        env["Start"]();
        bool ready = env["IsReady"]();
        CHECK(ready == true);
#else
        FAIL("Required component header is not available");
#endif
    }

    SUBCASE("10.4 Simulating scene reload: environment re-creation resets state cleanly")
    {
        sol::environment env1(lua, sol::create, lua.globals());
        lua.safe_script("persistentCounter = 42", env1, sol::script_pass_on_error);
        CHECK(env1["persistentCounter"].get<int>() == 42);

        sol::environment env2(lua, sol::create, lua.globals());
        CHECK(env2["persistentCounter"] == sol::nil);
    }

    SUBCASE("10.5 Deleted scene object during play loop handled cleanly")
    {
#if RESOURCE_HANDLE_AVAILABLE && GAME_OBJECT_REGISTRY_AVAILABLE
        auto* obj = new GameObject("DynamicBoss");
        uint64_t id = obj->GetID();

        sol::environment env(lua, sol::create, lua.globals());
        env["bossID"] = id;

        const char* script = R"(
            local handle = Resource.FromID(bossID)
            function CheckBoss()
                if handle:is_valid() then
                    local b = handle:resolve()
                    return b ~= nil
                else
                    return false
                end
            end
        )";
        lua.safe_script(script, env, sol::script_pass_on_error);

        CHECK(env["CheckBoss"]().get<bool>() == true);

        delete obj;

        CHECK(env["CheckBoss"]().get<bool>() == false);
#else
        FAIL("Required component header is not available");
#endif
    }
}

// =========================================================================
// TIER 2: BOUNDARY & CORNER CASES (>=5 tests per feature, >=50 total)
// =========================================================================

TEST_CASE("Tier 2 - Boundary Cases: Path Queries & Malformed Inputs")
{
#if RESOURCE_HANDLE_AVAILABLE
    SUBCASE("2.1 Empty path string yields invalid handle")
    {
        ResourceHandle h = ResourceHandle::FromPath("");
        CHECK(h.IsValid() == false);
        CHECK(h.GetType() == ResourceType::Invalid);
        CHECK(h.Resolve() == nullptr);
    }

    SUBCASE("2.2 Whitespace path string yields invalid handle")
    {
        ResourceHandle h = ResourceHandle::FromPath("   \t\n   ");
        CHECK(h.IsValid() == false);
        CHECK(h.Resolve() == nullptr);
    }

    SUBCASE("2.3 Missing 404 file path reports invalid")
    {
        ResourceHandle h = ResourceHandle::FromPath("Assets/Textures/does_not_exist_404_omega.png");
        CHECK(h.IsValid() == false);
        CHECK(h.Resolve() == nullptr);
    }

    SUBCASE("2.4 Unsupported file extension resolves to Invalid")
    {
        ResourceHandle h = ResourceHandle::FromPath("Assets/Data/config.unknown_ext_xyz");
        CHECK(h.GetType() == ResourceType::Invalid);
        CHECK(h.IsValid() == false);
    }

    SUBCASE("2.5 Path with spaces and special symbols")
    {
        ResourceHandle h = ResourceHandle::Texture("Assets/Textures/My Texture (V2) #1.png");
        CHECK(h.GetPath() == "Assets/Textures/My Texture (V2) #1.png");
        CHECK(h.IsValid() == false);
    }
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 2 - Boundary Cases: GameObject Handles & Registry Edge Cases")
{
#if RESOURCE_HANDLE_AVAILABLE && GAME_OBJECT_REGISTRY_AVAILABLE
    SUBCASE("2.6 FromGameObject with nullptr returns safe invalid handle")
    {
        ResourceHandle h = ResourceHandle::FromGameObject(nullptr);
        CHECK(h.IsValid() == false);
        CHECK(h.ResolveGameObject() == nullptr);
        CHECK(h.GetID() == 0);
    }

    SUBCASE("2.7 Default unassigned ResourceHandle constructor")
    {
        ResourceHandle empty;
        CHECK(empty.IsValid() == false);
        CHECK(empty.GetType() == ResourceType::Invalid);
        CHECK(empty.GetPath().empty());
        CHECK(empty.GetID() == 0);
        CHECK(empty.Resolve() == nullptr);
    }

    SUBCASE("2.8 Non-existent GameObject lookup by name returns invalid")
    {
        ResourceHandle h = ResourceHandle::GameObject("Definitely_Non_Existent_Entity_#9999");
        CHECK(h.IsValid() == false);
        CHECK(h.ResolveGameObject() == nullptr);
    }

    SUBCASE("2.9 Unregistered 64-bit ID lookup returns nullptr")
    {
        uint64_t bogusID = 0xCAFEBABE12345678ULL;
        CHECK(GameObjectRegistry::FindByID(bogusID) == nullptr);
        ResourceHandle h = ResourceHandle::FromID(bogusID);
        CHECK(h.IsValid() == false);
        CHECK(h.ResolveGameObject() == nullptr);
    }

    SUBCASE("2.10 Double unregistration in GameObjectRegistry is safe")
    {
        uint64_t id = 99999;
        GameObjectRegistry::Unregister(id);
        GameObjectRegistry::Unregister(id);
        CHECK(GameObjectRegistry::FindByID(id) == nullptr);
    }

    SUBCASE("2.11 Rapid Monotonic ID Generation Integrity (1,000 entities)")
    {
        constexpr int count = 1000;
        std::vector<std::unique_ptr<GameObject>> objects;
        objects.reserve(count);

        uint64_t prevID = 0;
        for (int i = 0; i < count; i++)
        {
            objects.push_back(std::make_unique<GameObject>("BatchObj_" + std::to_string(i)));
            uint64_t curID = objects.back()->GetID();
            CHECK(curID > prevID);
            prevID = curID;
        }

        for (int i = 0; i < count; i++)
        {
            GameObject* found = GameObjectRegistry::FindByID(objects[i]->GetID());
            CHECK(found == objects[i].get());
        }
    }
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 2 - Boundary Cases: Lua Error Safety & Repeated Operations")
{
    EnsureScriptEngineReady();
    sol::state& lua = ScriptEngine::GetInstance().GetState();

    SUBCASE("2.12 Lua syntax error safely caught without crashing host")
    {
        const char* badScript = "function !!SyntaxError( { return }";
        try
        {
            sol::protected_function_result res = lua.safe_script(badScript, sol::script_pass_on_error);
            CHECK(res.valid() == false);
        }
        catch (const sol::error&)
        {
            CHECK(true);
        }
    }

#if RESOURCE_HANDLE_AVAILABLE
    SUBCASE("2.13 Calling :resolve() repeatedly on invalid handle is idempotent and crash-free")
    {
        ResourceHandle bad = ResourceHandle::Texture("Assets/Textures/ghost_404.png");
        for (int i = 0; i < 100; i++)
        {
            CHECK(bad.Resolve() == nullptr);
            CHECK(bad.ResolveTexture() == nullptr);
        }

        lua["badHandle"] = bad;
        const char* script = R"(
            for i = 1, 100 do
                local res = badHandle:resolve()
                assert(res == nil, "Must remain nil")
            end
        )";
        sol::protected_function_result res = lua.safe_script(script, sol::script_pass_on_error);
        CHECK(res.valid() == true);
    }

    SUBCASE("2.14 Calling :instantiate() repeatedly on invalid handle produces nil safely")
    {
        ResourceHandle bad = ResourceHandle::Model("Assets/Models/missing.obj");
        for (int i = 0; i < 50; i++)
        {
            CHECK(bad.Instantiate(glm::vec3(0.0f)) == nullptr);
        }

        lua["badMdl"] = bad;
        const char* script = R"(
            for i = 1, 50 do
                local inst = badMdl:instantiate()
                assert(inst == nil, "Must safely remain nil")
            end
        )";
        sol::protected_function_result res = lua.safe_script(script, sol::script_pass_on_error);
        CHECK(res.valid() == true);
    }
#endif

    SUBCASE("2.15 Empty ScriptBuffer save and load boundary")
    {
        TestScriptTab tab;
        tab.content = "";
        std::string emptyPath = "Assets/scripts/temp_empty_tab.lua";
        CHECK(tab.Save(emptyPath) == true);

        TestScriptTab loaded;
        CHECK(loaded.Load(emptyPath) == true);
        CHECK(loaded.content.empty());

        std::error_code ec;
        std::filesystem::remove(emptyPath, ec);
    }

    SUBCASE("2.16 Large ScriptBuffer capacity boundary (100,000 characters)")
    {
        TestScriptTab tab;
        std::string chunk = "-- 100 char comment line for testing dynamic buffer expansion in script editor...\n";
        for (int i = 0; i < 1000; i++)
        {
            tab.AppendText(chunk);
        }
        CHECK(tab.content.size() >= 80000);

        size_t mid = tab.content.size() / 2;
        tab.InsertText(mid, "\n-- INSERTED_IN_MIDDLE --\n");
        CHECK(tab.content.find("-- INSERTED_IN_MIDDLE --") != std::string::npos);
    }
}

// =========================================================================
// TIER 3: CROSS-FEATURE COMBINATIONS (Pairwise Coverage)
// =========================================================================

TEST_CASE("Tier 3 - Pairwise 1: Texture Handle + Lua Resolve + Texture Property Check")
{
    EnsureScriptEngineReady();
    sol::state& lua = ScriptEngine::GetInstance().GetState();

#if RESOURCE_HANDLE_AVAILABLE
    const char* script = R"(
        local handle = Resource.Texture("Assets/Textures/plain.png")
        assert(handle:is_valid() == true)
        local tex = handle:resolve()
        assert(tex ~= nil)
        assert(tex.width > 0)
        assert(tex.height > 0)
    )";
    sol::protected_function_result res = lua.safe_script(script, sol::script_pass_on_error);
    CHECK(res.valid() == true);
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 3 - Pairwise 2: Model Handle + Lua Resolve + GameObject Attachment")
{
    EnsureScriptEngineReady();
    sol::state& lua = ScriptEngine::GetInstance().GetState();

#if RESOURCE_HANDLE_AVAILABLE
    auto* go = new GameObject("ModelHolder");
    lua["testHolder"] = go;

    const char* script = R"(
        local mdlHandle = Resource.Model("Assets/Models/grass.obj")
        assert(mdlHandle:is_valid() == true, "Model handle for grass.obj must be valid")
        local mdl = mdlHandle:resolve()
        assert(mdl ~= nil, "Model handle must resolve to non-nil object")
        testHolder:SetModel(mdl)
        assert(testHolder:GetModel() ~= nil, "GameObject must have model attached via Lua")
    )";
    sol::protected_function_result res = lua.safe_script(script, sol::script_pass_on_error);
    CHECK(res.valid() == true);
    CHECK(go->GetModel() != nullptr);
    delete go;
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 3 - Pairwise 3: Audio Handle + Lua Query + AudioClip Properties")
{
    EnsureScriptEngineReady();
    sol::state& lua = ScriptEngine::GetInstance().GetState();

#if RESOURCE_HANDLE_AVAILABLE
    const char* script = R"(
        local audio = Resource.Audio("Assets/Audio/ambient.wav")
        assert(audio.type == "Audio")
        assert(audio:GetPath() == "Assets/Audio/ambient.wav")
    )";
    sol::protected_function_result res = lua.safe_script(script, sol::script_pass_on_error);
    CHECK(res.valid() == true);
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 3 - Pairwise 4: NodeGraph Handle + Lua Query + Graph Identifier")
{
    EnsureScriptEngineReady();
    sol::state& lua = ScriptEngine::GetInstance().GetState();

#if RESOURCE_HANDLE_AVAILABLE
    const char* script = R"(
        local graph = Resource.NodeGraph("Terrain_Generator")
        assert(graph.type == "NodeGraph")
        assert(graph:GetIdentifier() == "Terrain_Generator")
    )";
    sol::protected_function_result res = lua.safe_script(script, sol::script_pass_on_error);
    CHECK(res.valid() == true);
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 3 - Pairwise 5: GameObject Handle + Lua Resolve + Transform Manipulation")
{
    EnsureScriptEngineReady();
    sol::state& lua = ScriptEngine::GetInstance().GetState();

#if RESOURCE_HANDLE_AVAILABLE && GAME_OBJECT_REGISTRY_AVAILABLE
    auto* go = new GameObject("MovableEntity");
    go->GetTransform().SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));

    lua["targetID"] = go->GetID();
    const char* script = R"(
        local handle = Resource.FromID(targetID)
        assert(handle:is_valid() == true)
        local target = handle:resolve()
        assert(target ~= nil)
        target.transform:SetPosition(vec3.new(12.5, 34.0, -56.5))
    )";
    sol::protected_function_result res = lua.safe_script(script, sol::script_pass_on_error);
    CHECK(res.valid() == true);

    glm::vec3 pos = go->GetTransform().GetPosition();
    CHECK(pos.x == doctest::Approx(12.5f));
    CHECK(pos.y == doctest::Approx(34.0f));
    CHECK(pos.z == doctest::Approx(-56.5f));
    delete go;
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 3 - Pairwise 6: Deleted GameObject Handle + Safe Fallback in Lua Loop")
{
    EnsureScriptEngineReady();
    sol::state& lua = ScriptEngine::GetInstance().GetState();

#if RESOURCE_HANDLE_AVAILABLE && GAME_OBJECT_REGISTRY_AVAILABLE
    auto* go = new GameObject("TemporaryTarget");
    uint64_t targetID = go->GetID();

    lua["monitoredID"] = targetID;
    const char* setupScript = R"(
        handle = Resource.FromID(monitoredID)
        fallbackExecuted = false

        function Update(dt)
            if handle:is_valid() then
                local obj = handle:resolve()
                if obj then
                    -- Process object
                end
            else
                fallbackExecuted = true
            end
        end
    )";
    lua.safe_script(setupScript, sol::script_pass_on_error);

    lua["Update"](0.016f);
    CHECK(lua["fallbackExecuted"].get<bool>() == false);

    delete go;

    lua["Update"](0.016f);
    CHECK(lua["fallbackExecuted"].get<bool>() == true);
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 3 - Pairwise 7: Drag-and-Drop Generated Code Executed in Lua Environment")
{
    EnsureScriptEngineReady();
    sol::state& lua = ScriptEngine::GetInstance().GetState();

#if RESOURCE_HANDLE_AVAILABLE
    std::string generatedDrop = FormatAssetDrop("Assets\\Textures\\plain.png", "myTexHandle");
    generatedDrop += "assert(myTexHandle ~= nil and myTexHandle:is_valid())\n";

    sol::protected_function_result res = lua.safe_script(generatedDrop, sol::script_pass_on_error);
    CHECK(res.valid() == true);
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 3 - Pairwise 8: ScriptEditor Buffer Saved to Disk and Executed by ScriptEngine")
{
    EnsureScriptEngineReady();

    TestScriptTab tab;
    tab.content = R"(
        -- Generated from ScriptEditor tab buffer
        scriptEditorSuccess = 999
    )";
    std::string testPath = "Assets/scripts/temp_editor_exec.lua";
    REQUIRE(tab.Save(testPath) == true);

    bool execOk = ScriptEngine::GetInstance().ExecuteFile(testPath);
    CHECK(execOk == true);

    sol::state& lua = ScriptEngine::GetInstance().GetState();
    CHECK(lua["scriptEditorSuccess"].get<int>() == 999);

    std::error_code ec;
    std::filesystem::remove(testPath, ec);
}

// =========================================================================
// TIER 4: REAL-WORLD APPLICATION SCENARIOS (5 Engine Lifecycle Scenarios)
// =========================================================================

TEST_CASE("Tier 4 - Scenario 1: Drag Asset -> Edit Script -> Save -> Reload & Execute")
{
    EnsureScriptEngineReady();
    sol::state& lua = ScriptEngine::GetInstance().GetState();

    std::string rawAssetPath = "Assets\\Textures\\plain.png";
    std::string dropCode = FormatAssetDrop(rawAssetPath, "ambientTex");

    TestScriptTab editorTab;
    editorTab.title = "AmbientRotator.lua";
    editorTab.content = "-- AmbientRotator Script\n";
    editorTab.AppendText(dropCode);
    editorTab.AppendText(R"(
        loadedTextureWidth = 0
        function Start()
            if ambientTex:is_valid() then
                local t = ambientTex:resolve()
                if t ~= nil then
                    loadedTextureWidth = t.width
                end
            end
        end
    )");

    std::string savePath = "Assets/scripts/temp_scenario1_tab.lua";
    REQUIRE(editorTab.Save(savePath) == true);
    CHECK(editorTab.isModified == false);

    TestScriptTab diskTab;
    REQUIRE(diskTab.Load(savePath) == true);
    CHECK(diskTab.content == editorTab.content);

#if RESOURCE_HANDLE_AVAILABLE
    sol::protected_function_result res = lua.safe_script(diskTab.content, sol::script_pass_on_error);
    CHECK(res.valid() == true);

    lua["Start"]();
    int width = lua["loadedTextureWidth"].get<int>();
    CHECK(width > 0);
#endif

    std::error_code ec;
    std::filesystem::remove(savePath, ec);
}

TEST_CASE("Tier 4 - Scenario 2: Reference Scene GameObject -> Delete Object -> Safe Handle Resolution")
{
    EnsureScriptEngineReady();
    sol::state& lua = ScriptEngine::GetInstance().GetState();

#if RESOURCE_HANDLE_AVAILABLE && GAME_OBJECT_REGISTRY_AVAILABLE
    auto* boss = new GameObject("RaidBoss");
    boss->GetTransform().SetPosition(glm::vec3(100.0f, 50.0f, 0.0f));

    lua["bossID"] = boss->GetID();

    const char* script = R"(
        local bossHandle = Resource.FromID(bossID)
        local state = "COMBAT"

        function Update(dt)
            if bossHandle:is_valid() then
                local b = bossHandle:resolve()
                if b then
                    state = "ATTACKING_BOSS"
                end
            else
                state = "VICTORY_IDLE"
            end
        end

        function GetState()
            return state
        end
    )";
    lua.safe_script(script, sol::script_pass_on_error);

    lua["Update"](0.016f);
    CHECK(lua["GetState"]().get<std::string>() == "ATTACKING_BOSS");

    delete boss;

    lua["Update"](0.016f);
    CHECK(lua["GetState"]().get<std::string>() == "VICTORY_IDLE");
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 4 - Scenario 3: Dynamic Prefab Instantiation from Lua at Position")
{
    EnsureScriptEngineReady();
    sol::state& lua = ScriptEngine::GetInstance().GetState();

#if RESOURCE_HANDLE_AVAILABLE && GAME_OBJECT_REGISTRY_AVAILABLE
    SceneManager testScene;
    ServiceLocator::Provide(&testScene);

    auto* archetype = new GameObject("MonsterPrefab");
    archetype->GetTransform().SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
    testScene.AddObject(archetype);

    lua["prefabName"] = "MonsterPrefab";
    const char* script = R"(
        local prefab = Resource.FindObject(prefabName)
        assert(prefab ~= nil and prefab:is_valid())
        local spawnPos = vec3.new(25.0, 5.0, -15.0)
        spawnedMonster = prefab:instantiate(spawnPos)
        assert(spawnedMonster ~= nil)
    )";
    sol::protected_function_result res = lua.safe_script(script, sol::script_pass_on_error);
    CHECK(res.valid() == true);

    GameObject* spawned = lua["spawnedMonster"].get<GameObject*>();
    REQUIRE(spawned != nullptr);
    CHECK(spawned->GetName().find("MonsterPrefab") != std::string::npos);

    glm::vec3 p = spawned->GetTransform().GetPosition();
    CHECK(p.x == doctest::Approx(25.0f));
    CHECK(p.y == doctest::Approx(5.0f));
    CHECK(p.z == doctest::Approx(-15.0f));

    ServiceLocator::Provide(static_cast<SceneManager*>(nullptr));
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 4 - Scenario 4: Scene Reload Across Play Mode Transitions with Persisted Scripts")
{
    EnsureScriptEngineReady();
    sol::state& lua = ScriptEngine::GetInstance().GetState();

#if RESOURCE_HANDLE_AVAILABLE && GAME_OBJECT_REGISTRY_AVAILABLE
    auto* sceneObj = new GameObject("RotatorBox");
    sceneObj->GetTransform().SetPosition(glm::vec3(1.0f, 2.0f, 3.0f));
    uint64_t originalID = sceneObj->GetID();

    sol::environment playModeEnv(lua, sol::create, lua.globals());
    playModeEnv["targetID"] = originalID;

    const char* script = R"(
        local handle = Resource.FromID(targetID)
        function Tick()
            if handle:is_valid() then
                local o = handle:resolve()
                if o then
                    local p = o.transform:GetPosition()
                    p.y = p.y + 1.0
                    o.transform:SetPosition(p)
                end
            end
        end
    )";
    lua.safe_script(script, playModeEnv, sol::script_pass_on_error);

    playModeEnv["Tick"]();
    CHECK(sceneObj->GetTransform().GetPosition().y == doctest::Approx(3.0f));

    delete sceneObj;
    CHECK(GameObjectRegistry::FindByID(originalID) == nullptr);

    auto* restoredObj = new GameObject("RotatorBox");
    restoredObj->GetTransform().SetPosition(glm::vec3(1.0f, 2.0f, 3.0f));
    uint64_t restoredID = restoredObj->GetID();
    CHECK(restoredID != originalID);

    sol::environment newPlayModeEnv(lua, sol::create, lua.globals());
    newPlayModeEnv["targetID"] = restoredID;
    lua.safe_script(script, newPlayModeEnv, sol::script_pass_on_error);

    newPlayModeEnv["Tick"]();
    CHECK(restoredObj->GetTransform().GetPosition().y == doctest::Approx(3.0f));

    delete restoredObj;
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Tier 4 - Scenario 5: Full Engine Asset Browser / Hierarchy Drag Simulation & Script Compilation")
{
    EnsureScriptEngineReady();
    sol::state& lua = ScriptEngine::GetInstance().GetState();

#if RESOURCE_HANDLE_AVAILABLE && GAME_OBJECT_REGISTRY_AVAILABLE
    auto* playerObj = new GameObject("MainHero");
    playerObj->GetTransform().SetPosition(glm::vec3(0.0f, 1.0f, 0.0f));

    std::string s1 = FormatAssetDrop("Assets\\Textures\\plain.png", "diffuseTex");
    std::string s2 = FormatAssetDrop("Assets\\Models\\grass.obj", "worldProp");
    std::string s3 = FormatAssetDrop("Assets\\Audio\\ambient.wav", "ambientSound");
    std::string s4 = FormatGameObjectDrop("MainHero", "heroTarget");

    TestScriptTab compositeScript;
    compositeScript.title = "GameplayController.lua";
    compositeScript.AppendText("-- Generated by Editor Drag & Drop\n");
    compositeScript.AppendText(s1);
    compositeScript.AppendText(s2);
    compositeScript.AppendText(s3);
    compositeScript.AppendText(s4);
    compositeScript.AppendText(R"(
        verifiedCount = 0

        function Start()
            if diffuseTex:is_valid() then
                verifiedCount = verifiedCount + 1
            end
            if worldProp:is_valid() then
                verifiedCount = verifiedCount + 1
            end
            if ambientSound.type == "Audio" then
                verifiedCount = verifiedCount + 1
            end
            if heroTarget:is_valid() then
                local h = heroTarget:resolve()
                if h and h:GetName() == "MainHero" then
                    verifiedCount = verifiedCount + 1
                end
            end
        end
    )");

    sol::protected_function_result res = lua.safe_script(compositeScript.content, sol::script_pass_on_error);
    CHECK(res.valid() == true);

    lua["Start"]();
    int verifiedCount = lua["verifiedCount"].get<int>();
    CHECK(verifiedCount >= 3);
    delete playerObj;
#else
    FAIL("Required component header is not available");
#endif
}

TEST_CASE("Play Mode Verification Script: Assets/scripts/resource_verification.lua")
{
    EnsureScriptEngineReady();
    sol::state& lua = ScriptEngine::GetInstance().GetState();

    bool fileExists = std::filesystem::exists("Assets/scripts/resource_verification.lua");
    CHECK(fileExists == true);

    sol::environment env(lua, sol::create, lua.globals());
    sol::protected_function_result loadRes = lua.safe_script_file("Assets/scripts/resource_verification.lua", env, sol::script_pass_on_error);
    CHECK(loadRes.valid() == true);

    sol::protected_function startFn = env["Start"];
    REQUIRE(startFn.valid() == true);
    sol::protected_function_result startRes = startFn();
    CHECK(startRes.valid() == true);

    sol::protected_function updateFn = env["Update"];
    REQUIRE(updateFn.valid() == true);
    for (int frame = 1; frame <= 120; frame++)
    {
        sol::protected_function_result updateRes = updateFn(0.016f);
        CHECK(updateRes.valid() == true);
    }
}

TEST_CASE("Tier 4 - Scenario 6: Real ScriptEditor Class & SceneSerializer LuaScriptComponent Round-Trip")
{
    EnsureScriptEngineReady();
    sol::state& lua = ScriptEngine::GetInstance().GetState();

    ScriptEditor editor;
    int idx = editor.NewScript("-- Header comment\n");
    REQUIRE(idx == 0);
    REQUIRE(editor.GetActiveTab() != nullptr);

    std::string assetSnippet = ScriptEditor::FormatAssetSnippet("Assets\\Textures\\plain.png", "droppedTex");
    std::string objSnippet = ScriptEditor::FormatGameObjectSnippet("DroppedHero", "droppedHero");
    CHECK(assetSnippet == "local droppedTex = Resource.Get(\"Assets/Textures/plain.png\")\n");
    CHECK(objSnippet == "local droppedHero = Resource.GameObject(\"DroppedHero\")\n");

    editor.InsertAtCursor(assetSnippet);
    editor.InsertAtCursor(objSnippet);
    editor.GetActiveTab()->AppendText(
        "function VerifyDropped()\n"
        "    if droppedTex == nil or not droppedTex:is_valid() then return false end\n"
        "    if droppedHero == nil or not droppedHero:is_valid() then return false end\n"
        "    local resolved = droppedHero:resolve()\n"
        "    return resolved ~= nil and resolved:GetName() == \"DroppedHero\"\n"
        "end\n"
    );
    CHECK(editor.GetActiveTab()->IsDirty() == true);

    std::string tempScriptPath = "Assets/scripts/temp_real_script_editor.lua";
    REQUIRE(editor.GetActiveTab()->Save(tempScriptPath) == true);
    CHECK(editor.GetActiveTab()->IsDirty() == false);

    ScriptEditor reopenedEditor;
    int openIdx = reopenedEditor.OpenFile(tempScriptPath);
    REQUIRE(openIdx == 0);
    REQUIRE(reopenedEditor.GetActiveTab() != nullptr);
    CHECK(reopenedEditor.GetActiveTab()->GetBuffer().find("Resource.Get(\"Assets/Textures/plain.png\")") != std::string::npos);
    CHECK(reopenedEditor.GetActiveTab()->GetBuffer().find("Resource.GameObject(\"DroppedHero\")") != std::string::npos);

    // Verify SceneSerializer SnapshotObject / RestoreObject preserves LuaScriptComponent
    SceneManager testScene;
    ScopedSceneManagerScope sceneScope(&testScene);
    GameObject* hero = new GameObject("DroppedHero");
    hero->AddComponent<LuaScriptComponent>(tempScriptPath);
    testScene.AddObject(hero);

    std::string snap = SceneSerializer::SnapshotObject(hero);
    CHECK(snap.find("temp_real_script_editor.lua") != std::string::npos);

    GameObject* restored = new GameObject("RestoredHero");
    testScene.AddObject(restored);
    SceneSerializer::RestoreObject(restored, snap, &testScene);
    LuaScriptComponent* restoredComp = restored->GetComponent<LuaScriptComponent>();
    REQUIRE(restoredComp != nullptr);
    CHECK(restoredComp->GetFilepath() == tempScriptPath);

    // Execute the script saved by ScriptEditor and verify the dropped handles work in Lua
    sol::environment env(lua, sol::create, lua.globals());
    sol::protected_function_result execRes = lua.safe_script_file(tempScriptPath, env, sol::script_pass_on_error);
    CHECK(execRes.valid() == true);
    sol::protected_function verifyFn = env["VerifyDropped"];
    REQUIRE(verifyFn.valid() == true);
    sol::protected_function_result verifyRes = verifyFn();
    REQUIRE(verifyRes.valid() == true);
    CHECK(verifyRes.get<bool>() == true);

    std::error_code ec;
    std::filesystem::remove(tempScriptPath, ec);
}

#endif // RUN_UNIT_TESTS
