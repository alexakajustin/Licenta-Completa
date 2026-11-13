#include "ScriptEngine.h"
#include "Core/ScriptApi.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <array>
#include <glm/glm.hpp>
#include "Scene/GameObject.h"
#include "Scene/GameObjectRegistry.h"
#include "Scene/Component.h"
#include "Scene/RigidBody.h"
#include "Scene/BoxCollider.h"
#include "Scene/CapsuleCollider.h"
#include "Scene/MeshCollider.h"
#include "Scene/Player.h"
#include "Scene/LuaScriptComponent.h"
#include "Scene/SceneManager.h"
#include "Core/Transform.h"
#include "Core/ResourceHandle.h"
#include "Core/ServiceLocator.h"
#include "Rendering/Texture.h"
#include "Rendering/Material.h"
#include "Rendering/TextureLayer.h"
#include "Rendering/Model.h"
#include "Nodes/NodeGraph.h"
#include "Nodes/NodeAssets.h"
#include "Lighting/LightObject.h"
#include <GLFW/glfw3.h>

using ScriptApi::Kind;

namespace
{
    // A texture layer as seen from Lua. Layers live by value in a std::vector inside the
    // GameObject, so a raw pointer would dangle as soon as a layer is added or removed; the
    // reference keeps (object id, index) and resolves it on every access instead.
    struct LayerRef
    {
        uint64_t objectId = 0;
        int index = 0;

        TextureLayer* Get() const
        {
            GameObject* go = GameObjectRegistry::FindByID(objectId);
            if (!go) return nullptr;
            auto& layers = go->GetTextureLayers();
            return (index >= 0 && index < (int)layers.size()) ? &layers[index] : nullptr;
        }
    };

    std::string Lower(std::string s)
    {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }

    // "RigidBody" / "rigidbody" / "LuaScript" / "Script" / "LuaScriptComponent" all match
    bool ComponentNameMatches(Component* c, const std::string& query)
    {
        std::string want = Lower(query);
        std::string have = Lower(c->GetName());
        auto strip = [](std::string s) {
            const std::string suffix = "component";
            if (s.size() > suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
                s.resize(s.size() - suffix.size());
            return s;
        };
        if (have == want || strip(have) == strip(want)) return true;
        if (dynamic_cast<LuaScriptComponent*>(c) && (want == "script" || want == "luascript")) return true;
        return false;
    }

    // Pushes a component with its most derived bound type, so Lua sees its own members
    sol::object PushComponent(sol::state_view lua, Component* c)
    {
        if (!c) return sol::make_object(lua, sol::lua_nil);
        if (auto* p = dynamic_cast<RigidBody*>(c)) return sol::make_object(lua, p);
        if (auto* p = dynamic_cast<BoxCollider*>(c)) return sol::make_object(lua, p);
        if (auto* p = dynamic_cast<CapsuleCollider*>(c)) return sol::make_object(lua, p);
        if (auto* p = dynamic_cast<MeshCollider*>(c)) return sol::make_object(lua, p);
        if (auto* p = dynamic_cast<Player*>(c)) return sol::make_object(lua, p);
        if (auto* p = dynamic_cast<LuaScriptComponent*>(c)) return sol::make_object(lua, p);
        return sol::make_object(lua, c);
    }

    sol::object ScriptEnvOf(sol::state_view lua, GameObject& go, const std::string& nameOrPath)
    {
        for (const auto& comp : go.GetComponents())
        {
            auto* script = dynamic_cast<LuaScriptComponent*>(comp.get());
            if (script && script->MatchesScript(nameOrPath) && script->IsRunning())
                return sol::make_object(lua, script->GetEnvironment());
        }
        return sol::make_object(lua, sol::lua_nil);
    }

    template <typename T>
    void BindVecCommon(ScriptApi::Class<T>& c, const char* typeName)
    {
        c.Method("Length", [](const T& v) { return glm::length(v); }, "()", "number", "Length of the vector.")
         .Method("Normalized", [](const T& v) { float l = glm::length(v); return l > 0.0f ? v / l : v; }, "()", typeName, "Unit-length copy of the vector (zero stays zero).")
         .Method("Dot", [](const T& a, const T& b) { return glm::dot(a, b); }, "(other)", "number", "Dot product.")
         .Method("Distance", [](const T& a, const T& b) { return glm::distance(a, b); }, "(other)", "number", "Distance between two points.")
         .Method("Lerp", [](const T& a, const T& b, float t) { return a + (b - a) * t; }, "(other, t)", typeName, "Linear interpolation towards 'other' (t in 0..1).")
         .Method("Copy", [](const T& v) { return v; }, "()", typeName, "An independent copy (vectors read from objects are live references).");
    }
}

// =====================================================================================
// Math
// =====================================================================================

void ScriptEngine::RegisterMath()
{
    auto& reg = ScriptApi::Registry::Get();

    // vec2
    {
        // vec(...) and vec.new(...) both construct
        auto ut = lua.new_usertype<glm::vec2>("vec2",
            sol::call_constructor, sol::constructors<glm::vec2(), glm::vec2(float), glm::vec2(float, float)>(),
            sol::meta_function::construct, sol::constructors<glm::vec2(), glm::vec2(float), glm::vec2(float, float)>(),
            sol::meta_function::addition, [](const glm::vec2& a, const glm::vec2& b) { return a + b; },
            sol::meta_function::subtraction, [](const glm::vec2& a, const glm::vec2& b) { return a - b; },
            sol::meta_function::multiplication, sol::overload(
                [](const glm::vec2& a, float b) { return a * b; },
                [](float a, const glm::vec2& b) { return a * b; },
                [](const glm::vec2& a, const glm::vec2& b) { return a * b; }),
            sol::meta_function::division, [](const glm::vec2& a, float b) { return a / b; },
            sol::meta_function::unary_minus, [](const glm::vec2& a) { return -a; },
            sol::meta_function::equal_to, [](const glm::vec2& a, const glm::vec2& b) { return a == b; },
            sol::meta_function::to_string, [](const glm::vec2& v) {
                return "vec2(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")";
            });
        ScriptApi::Class<glm::vec2> c(ut, reg.AddType("vec2", "2D vector (tiling, offsets, UVs). Supports + - * / and ==."));
        c.Field("x", &glm::vec2::x, "number", "X component.")
         .Field("y", &glm::vec2::y, "number", "Y component.");
        BindVecCommon(c, "vec2");
        c.Doc({ "new", Kind::Function, "vec2", "(x, y)", "Creates a vector (same as vec2(...)).", true });
        reg.AddGlobal({ "vec2", Kind::Function, "vec2", "(x, y)", "Creates a 2D vector: vec2(), vec2(s), vec2(x, y).", true });
    }

    // vec3
    {
        // vec(...) and vec.new(...) both construct
        auto ut = lua.new_usertype<glm::vec3>("vec3",
            sol::call_constructor, sol::constructors<glm::vec3(), glm::vec3(float), glm::vec3(float, float, float)>(),
            sol::meta_function::construct, sol::constructors<glm::vec3(), glm::vec3(float), glm::vec3(float, float, float)>(),
            sol::meta_function::addition, [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
            sol::meta_function::subtraction, [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
            sol::meta_function::multiplication, sol::overload(
                [](const glm::vec3& a, float b) { return a * b; },
                [](float a, const glm::vec3& b) { return a * b; },
                [](const glm::vec3& a, const glm::vec3& b) { return a * b; }),
            sol::meta_function::division, [](const glm::vec3& a, float b) { return a / b; },
            sol::meta_function::unary_minus, [](const glm::vec3& a) { return -a; },
            sol::meta_function::equal_to, [](const glm::vec3& a, const glm::vec3& b) { return a == b; },
            sol::meta_function::to_string, [](const glm::vec3& v) {
                return "vec3(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
            });
        ScriptApi::Class<glm::vec3> c(ut, reg.AddType("vec3", "3D vector (positions, rotations in degrees, scales, colors). Supports + - * / and ==."));
        c.Field("x", &glm::vec3::x, "number", "X component.")
         .Field("y", &glm::vec3::y, "number", "Y component.")
         .Field("z", &glm::vec3::z, "number", "Z component.")
         .Property("r", [](const glm::vec3& v) { return v.x; }, [](glm::vec3& v, float f) { v.x = f; }, "number", "Red (same as x) - light colors are vec3.")
         .Property("g", [](const glm::vec3& v) { return v.y; }, [](glm::vec3& v, float f) { v.y = f; }, "number", "Green (same as y).")
         .Property("b", [](const glm::vec3& v) { return v.z; }, [](glm::vec3& v, float f) { v.z = f; }, "number", "Blue (same as z).")
         .Method("Cross", [](const glm::vec3& a, const glm::vec3& b) { return glm::cross(a, b); }, "(other)", "vec3", "Cross product.");
        BindVecCommon(c, "vec3");
        c.Doc({ "new", Kind::Function, "vec3", "(x, y, z)", "Creates a vector (same as vec3(...)).", true });
        reg.AddGlobal({ "vec3", Kind::Function, "vec3", "(x, y, z)", "Creates a 3D vector: vec3(), vec3(s), vec3(x, y, z).", true });
    }

    // vec4
    {
        // vec(...) and vec.new(...) both construct
        auto ut = lua.new_usertype<glm::vec4>("vec4",
            sol::call_constructor, sol::constructors<glm::vec4(), glm::vec4(float), glm::vec4(float, float, float, float)>(),
            sol::meta_function::construct, sol::constructors<glm::vec4(), glm::vec4(float), glm::vec4(float, float, float, float)>(),
            sol::meta_function::addition, [](const glm::vec4& a, const glm::vec4& b) { return a + b; },
            sol::meta_function::subtraction, [](const glm::vec4& a, const glm::vec4& b) { return a - b; },
            sol::meta_function::multiplication, sol::overload(
                [](const glm::vec4& a, float b) { return a * b; },
                [](float a, const glm::vec4& b) { return a * b; },
                [](const glm::vec4& a, const glm::vec4& b) { return a * b; }),
            sol::meta_function::equal_to, [](const glm::vec4& a, const glm::vec4& b) { return a == b; },
            sol::meta_function::to_string, [](const glm::vec4& v) {
                return "vec4(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ", " + std::to_string(v.w) + ")";
            });
        ScriptApi::Class<glm::vec4> c(ut, reg.AddType("vec4", "4D vector, mostly RGBA colors (r g b a are aliases of x y z w)."));
        c.Field("x", &glm::vec4::x, "number", "X / red component.")
         .Field("y", &glm::vec4::y, "number", "Y / green component.")
         .Field("z", &glm::vec4::z, "number", "Z / blue component.")
         .Field("w", &glm::vec4::w, "number", "W / alpha component.")
         .Property("r", [](const glm::vec4& v) { return v.x; }, [](glm::vec4& v, float f) { v.x = f; }, "number", "Red (same as x).")
         .Property("g", [](const glm::vec4& v) { return v.y; }, [](glm::vec4& v, float f) { v.y = f; }, "number", "Green (same as y).")
         .Property("b", [](const glm::vec4& v) { return v.z; }, [](glm::vec4& v, float f) { v.z = f; }, "number", "Blue (same as z).")
         .Property("a", [](const glm::vec4& v) { return v.w; }, [](glm::vec4& v, float f) { v.w = f; }, "number", "Alpha (same as w).");
        BindVecCommon(c, "vec4");
        c.Doc({ "new", Kind::Function, "vec4", "(x, y, z, w)", "Creates a vector (same as vec4(...)).", true });
        reg.AddGlobal({ "vec4", Kind::Function, "vec4", "(x, y, z, w)", "Creates a 4D vector / RGBA color: vec4(), vec4(s), vec4(r, g, b, a).", true });
    }
}

// =====================================================================================
// Transform / Component
// =====================================================================================

void ScriptEngine::RegisterCoreTypes()
{
    auto& reg = ScriptApi::Registry::Get();

    auto tut = lua.new_usertype<Transform>("Transform", sol::no_constructor);
    ScriptApi::Class<Transform>(tut, reg.AddType("Transform", "Position, rotation (Euler degrees) and scale of a GameObject, relative to its parent."))
        // Live references: transform.rotation.y = 45 (or += speed * dt) writes straight into the object
        .Property("position", [](Transform& t) { t.MarkDirty(); return t.GetPositionPtr(); }, &Transform::SetPosition, "vec3",
            "Local position (live: transform.position.x = 5 moves the object; :Copy() for a snapshot).")
        .Property("rotation", [](Transform& t) { t.MarkDirty(); return t.GetRotationPtr(); }, &Transform::SetRotation, "vec3",
            "Local rotation, Euler degrees (pitch, yaw, roll). Live: transform.rotation.y = transform.rotation.y + 90 * Time.deltaTime.")
        .Property("scale", [](Transform& t) { t.MarkDirty(); return t.GetScalePtr(); }, &Transform::SetScale, "vec3", "Local scale (live reference).")
        .Method("GetPosition", &Transform::GetPosition, "()", "vec3", "Returns the local position.")
        .Method("SetPosition", &Transform::SetPosition, "(position: vec3)", "", "Sets the local position.")
        .Method("GetRotation", &Transform::GetRotation, "()", "vec3", "Returns the rotation (Euler degrees).")
        .Method("SetRotation", &Transform::SetRotation, "(rotation: vec3)", "", "Sets the rotation (Euler degrees).")
        .Method("GetScale", &Transform::GetScale, "()", "vec3", "Returns the local scale.")
        .Method("SetScale", &Transform::SetScale, "(scale: vec3)", "", "Sets the local scale.")
        .Method("Translate", [](Transform& t, const glm::vec3& d) { t.SetPosition(t.GetPosition() + d); }, "(delta: vec3)", "", "Moves by 'delta'.")
        .Method("Rotate", [](Transform& t, const glm::vec3& d) { t.SetRotation(t.GetRotation() + d); }, "(deltaDegrees: vec3)", "", "Adds Euler angles (degrees) to the rotation.");

    auto cut = lua.new_usertype<Component>("Component", sol::no_constructor);
    ScriptApi::Class<Component>(cut, reg.AddType("Component", "Base of every component attached to a GameObject."))
        .ReadOnly("gameObject", &Component::GetGameObject, "GameObject", "The GameObject this component is attached to.")
        .ReadOnly("name", &Component::GetName, "string", "Component type name (\"RigidBody\", \"BoxCollider\"...).")
        .Method("GetGameObject", &Component::GetGameObject, "()", "GameObject", "The GameObject this component is attached to.")
        .Method("GetName", &Component::GetName, "()", "string", "Component type name.");
}

// =====================================================================================
// GameObject
// =====================================================================================

void ScriptEngine::RegisterGameObject()
{
    auto& reg = ScriptApi::Registry::Get();

    auto ut = lua.new_usertype<GameObject>("GameObject", sol::no_constructor);
    ScriptApi::Class<GameObject>(ut, reg.AddType("GameObject", "An object in the scene: transform, appearance (material, texture layers), hierarchy and components."))
        // Identity
        .Property("name", &GameObject::GetName, &GameObject::SetName, "string", "Object name (as shown in the Hierarchy).")
        .ReadOnly("id", &GameObject::GetID, "number", "Unique runtime id (stable for this session).")
        .Property("visible", &GameObject::GetVisible, &GameObject::SetVisible, "boolean", "Whether the object is rendered.")
        .Method("GetName", &GameObject::GetName, "()", "string", "Object name.")
        .Method("SetName", &GameObject::SetName, "(name: string)", "", "Renames the object.")
        .Method("GetID", &GameObject::GetID, "()", "number", "Unique runtime id.")
        .Method("SetVisible", &GameObject::SetVisible, "(visible: boolean)", "", "Shows / hides the object.")
        .Method("GetVisible", &GameObject::GetVisible, "()", "boolean", "Whether the object is rendered.")

        // Transform
        .ReadOnly("transform", [](GameObject& go) -> Transform& { return go.GetTransform(); }, "Transform", "Position / rotation / scale.")
        .Property("position", [](GameObject& go) { Transform& t = go.GetTransform(); t.MarkDirty(); return t.GetPositionPtr(); },
            [](GameObject& go, const glm::vec3& v) { go.GetTransform().SetPosition(v); }, "vec3", "Shortcut for transform.position (live reference).")
        .Property("rotation", [](GameObject& go) { Transform& t = go.GetTransform(); t.MarkDirty(); return t.GetRotationPtr(); },
            [](GameObject& go, const glm::vec3& v) { go.GetTransform().SetRotation(v); }, "vec3", "Shortcut for transform.rotation, Euler degrees (live reference).")
        .Property("scale", [](GameObject& go) { Transform& t = go.GetTransform(); t.MarkDirty(); return t.GetScalePtr(); },
            [](GameObject& go, const glm::vec3& v) { go.GetTransform().SetScale(v); }, "vec3", "Shortcut for transform.scale (live reference).")

        // Appearance
        .Property("material", &GameObject::GetMaterial, &GameObject::SetMaterial, "Material", "Material used to render the object (may be shared with other objects).")
        .Property("texture", &GameObject::GetTexture, &GameObject::SetTexture, "Texture", "Main diffuse texture.")
        .Property("normalMap", &GameObject::GetNormalMap, &GameObject::SetNormalMap, "Texture", "Main normal map.")
        .ReadOnly("model", &GameObject::GetModel, "Model", "Loaded model, if any.")
        .Method("SetModel", &GameObject::SetModel, "(model: Model)", "", "Replaces the model.")
        .Method("GetModel", &GameObject::GetModel, "()", "Model", "Loaded model, if any.")
        .Method("SetTexture", sol::overload(
                [](GameObject& go, Texture* t) { go.SetTexture(t); },
                [](GameObject& go, const std::string& path) { go.SetTexture(NodeAssets::GetTexture(path)); }),
            "(texture: Texture | path: string)", "", "Sets the main diffuse texture (object or file path).")
        .Method("GetTexture", &GameObject::GetTexture, "()", "Texture", "Main diffuse texture.")

        // Texture layers
        .ReadOnly("layers", [](GameObject& go, sol::this_state s) {
                sol::state_view lua(s);
                sol::table t = lua.create_table();
                for (int i = 0; i < (int)go.GetTextureLayers().size(); i++) t[i + 1] = LayerRef{ go.GetID(), i };
                return t;
            }, "TextureLayer[]", "All texture layers (Lua array, first is layers[1]).")
        .ReadOnly("layerCount", [](GameObject& go) { return (int)go.GetTextureLayers().size(); }, "number", "Number of texture layers.")
        .Method("GetLayer", [](GameObject& go, int index) { return LayerRef{ go.GetID(), index }; },
            "(index: number)", "TextureLayer", "Texture layer by index, 0-based like the Inspector (\"Layer 0\").")
        .Method("AddLayer", [](GameObject& go, sol::optional<std::string> texturePath) {
                TextureLayer layer;
                if (texturePath && !texturePath->empty()) { layer.texture = NodeAssets::GetTexture(*texturePath); layer.texturePath = *texturePath; }
                go.AddTextureLayer(layer);
                return LayerRef{ go.GetID(), (int)go.GetTextureLayers().size() - 1 };
            }, "(texturePath?: string)", "TextureLayer", "Adds a texture layer (max 5) and returns it.")
        .Method("RemoveLayer", [](GameObject& go, int index) { go.RemoveTextureLayer(index); },
            "(index: number)", "", "Removes the texture layer at 'index' (0-based).")

        // Hierarchy
        .ReadOnly("parent", &GameObject::GetParent, "GameObject", "Parent object, or nil.")
        .ReadOnly("children", [](GameObject& go, sol::this_state s) {
                sol::state_view lua(s);
                sol::table t = lua.create_table();
                int i = 1;
                for (GameObject* child : go.GetChildren()) t[i++] = child;
                return t;
            }, "GameObject[]", "Direct children (Lua array).")
        .Method("FindChild", [](GameObject& go, const std::string& name) -> GameObject* {
                for (GameObject* child : go.GetChildren()) if (child && child->GetName() == name) return child;
                return nullptr;
            }, "(name: string)", "GameObject", "Direct child with this name, or nil.")

        // Components
        .Method("GetComponent", [](GameObject& go, const std::string& name, sol::this_state s) -> sol::object {
                for (const auto& comp : go.GetComponents())
                    if (ComponentNameMatches(comp.get(), name)) return PushComponent(sol::state_view(s), comp.get());
                return sol::make_object(s, sol::lua_nil);
            }, "(type: string)", "Component", "First component of this type (\"RigidBody\", \"BoxCollider\", \"LuaScript\"...), or nil.")
        .Method("HasComponent", [](GameObject& go, const std::string& name) {
                for (const auto& comp : go.GetComponents()) if (ComponentNameMatches(comp.get(), name)) return true;
                return false;
            }, "(type: string)", "boolean", "Whether a component of this type is attached.")
        .ReadOnly("components", [](GameObject& go, sol::this_state s) {
                sol::state_view lua(s);
                sol::table t = lua.create_table();
                int i = 1;
                for (const auto& comp : go.GetComponents()) t[i++] = PushComponent(lua, comp.get());
                return t;
            }, "Component[]", "All components (Lua array).")
        .Method("GetScript", [](GameObject& go, const std::string& nameOrPath, sol::this_state s) {
                return ScriptEnvOf(sol::state_view(s), go, nameOrPath);
            }, "(name: string)", "script", "Another Lua script on this object, by name (\"enemy\") or path: its functions and variables. nil until Play mode runs it.")
        .Method("AddRigidBody", [](GameObject& go) -> RigidBody* { return go.AddComponent<RigidBody>(); }, "()", "RigidBody", "Adds a RigidBody component.")
        .Method("GetRigidBody", [](GameObject& go) -> RigidBody* { return go.GetComponent<RigidBody>(); }, "()", "RigidBody", "The RigidBody component, or nil.")
        .Method("AddBoxCollider", [](GameObject& go) -> BoxCollider* { return go.AddComponent<BoxCollider>(); }, "()", "BoxCollider", "Adds a BoxCollider component.")
        .Method("GetBoxCollider", [](GameObject& go) -> BoxCollider* { return go.GetComponent<BoxCollider>(); }, "()", "BoxCollider", "The BoxCollider component, or nil.")
        .Method("AddCapsuleCollider", [](GameObject& go) -> CapsuleCollider* { return go.AddComponent<CapsuleCollider>(); }, "()", "CapsuleCollider", "Adds a CapsuleCollider component.")
        .Method("GetCapsuleCollider", [](GameObject& go) -> CapsuleCollider* { return go.GetComponent<CapsuleCollider>(); }, "()", "CapsuleCollider", "The CapsuleCollider component, or nil.")
        .Method("AddMeshCollider", [](GameObject& go) -> MeshCollider* { return go.AddComponent<MeshCollider>(); }, "()", "MeshCollider", "Adds a MeshCollider component.")
        .Method("GetMeshCollider", [](GameObject& go) -> MeshCollider* { return go.GetComponent<MeshCollider>(); }, "()", "MeshCollider", "The MeshCollider component, or nil.")
        .Method("GetPlayer", [](GameObject& go) -> Player* { return go.GetComponent<Player>(); }, "()", "Player", "The Player component, or nil.");
}

// =====================================================================================
// Physics + other components
// =====================================================================================

void ScriptEngine::RegisterPhysics()
{
    auto& reg = ScriptApi::Registry::Get();

    lua.new_enum("BodyType",
        "Static", RigidBody::BodyType::Static,
        "Kinematic", RigidBody::BodyType::Kinematic,
        "Dynamic", RigidBody::BodyType::Dynamic);
    ScriptApi::DocumentEnum("BodyType", "How a RigidBody moves.", {
        { "Static", "Never moves." }, { "Kinematic", "Moved by code, pushes dynamic bodies." }, { "Dynamic", "Simulated by physics." } });

    auto rb = lua.new_usertype<RigidBody>("RigidBody", sol::no_constructor, sol::base_classes, sol::bases<Component>());
    ScriptApi::Class<RigidBody>(rb, reg.AddType("RigidBody", "Physics body (Jolt). Needs a collider on the same object.", "Component"))
        .Property("type", &RigidBody::GetType, &RigidBody::SetType, "BodyType", "Static / Kinematic / Dynamic.")
        .Property("mass", &RigidBody::GetMass, &RigidBody::SetMass, "number", "Mass in kg.")
        .Property("friction", &RigidBody::GetFriction, &RigidBody::SetFriction, "number", "Surface friction.")
        .Property("restitution", &RigidBody::GetRestitution, &RigidBody::SetRestitution, "number", "Bounciness (0..1).")
        .Property("lockRotation", &RigidBody::GetLockRotation, &RigidBody::SetLockRotation, "boolean", "Prevents the body from rotating.")
        .Method("SetType", &RigidBody::SetType, "(type: BodyType)", "", "Static / Kinematic / Dynamic.")
        .Method("GetType", &RigidBody::GetType, "()", "BodyType", "Current body type.")
        .Method("SetMass", &RigidBody::SetMass, "(mass: number)", "", "Sets the mass.")
        .Method("GetMass", &RigidBody::GetMass, "()", "number", "Mass in kg.")
        .Method("SetFriction", &RigidBody::SetFriction, "(friction: number)", "", "Sets the friction.")
        .Method("SetRestitution", &RigidBody::SetRestitution, "(restitution: number)", "", "Sets the bounciness.")
        .Method("SetLinearVelocity", &RigidBody::SetLinearVelocity, "(velocity: vec3)", "", "Sets the velocity (m/s).")
        .Method("SetLockRotation", &RigidBody::SetLockRotation, "(lock: boolean)", "", "Locks / unlocks rotation.")
        .Method("AddForce", &RigidBody::AddForce, "(force: vec3)", "", "Applies a continuous force (N) this step.")
        .Method("AddImpulse", &RigidBody::AddImpulse, "(impulse: vec3)", "", "Applies an instant impulse.");

    auto box = lua.new_usertype<BoxCollider>("BoxCollider", sol::no_constructor, sol::base_classes, sol::bases<Component>());
    ScriptApi::Class<BoxCollider>(box, reg.AddType("BoxCollider", "Box-shaped collision volume.", "Component"))
        .Field("size", &BoxCollider::size, "vec3", "Box size.")
        .Field("offset", &BoxCollider::offset, "vec3", "Offset from the object's origin.")
        .Field("isTrigger", &BoxCollider::isTrigger, "boolean", "Detects overlaps without blocking.");

    auto cap = lua.new_usertype<CapsuleCollider>("CapsuleCollider", sol::no_constructor, sol::base_classes, sol::bases<Component>());
    ScriptApi::Class<CapsuleCollider>(cap, reg.AddType("CapsuleCollider", "Capsule-shaped collision volume (characters).", "Component"))
        .Field("height", &CapsuleCollider::height, "number", "Total height.")
        .Field("radius", &CapsuleCollider::radius, "number", "Radius.")
        .Field("offset", &CapsuleCollider::offset, "vec3", "Offset from the object's origin.");

    auto mesh = lua.new_usertype<MeshCollider>("MeshCollider", sol::no_constructor, sol::base_classes, sol::bases<Component>());
    ScriptApi::Class<MeshCollider>(mesh, reg.AddType("MeshCollider", "Collision using the object's mesh.", "Component"));

    auto player = lua.new_usertype<Player>("Player", sol::no_constructor, sol::base_classes, sol::bases<Component>());
    ScriptApi::Class<Player>(player, reg.AddType("Player", "First-person player controller.", "Component"))
        .Property("moveSpeed", &Player::GetMoveSpeed, &Player::SetMoveSpeed, "number", "Walk speed (m/s).")
        .Property("turnSpeed", &Player::GetTurnSpeed, &Player::SetTurnSpeed, "number", "Mouse look sensitivity.")
        .Property("eyeHeight", &Player::GetEyeHeight, &Player::SetEyeHeight, "number", "Camera height above the feet.")
        .Property("jumpForce", &Player::GetJumpForce, &Player::SetJumpForce, "number", "Jump impulse.")
        .Property("gravity", &Player::GetGravity, &Player::SetGravity, "number", "Gravity acceleration.")
        .ReadOnly("grounded", &Player::IsGrounded, "boolean", "Whether the player stands on something.")
        .ReadOnly("yaw", &Player::GetYaw, "number", "Look yaw in degrees.")
        .ReadOnly("pitch", &Player::GetPitch, "number", "Look pitch in degrees.");

    auto script = lua.new_usertype<LuaScriptComponent>("LuaScript", sol::no_constructor, sol::base_classes, sol::bases<Component>());
    ScriptApi::Class<LuaScriptComponent>(script, reg.AddType("LuaScript", "A Lua script attached to a GameObject.", "Component"))
        .ReadOnly("path", &LuaScriptComponent::GetScriptPath, "string", "Script file path.")
        .ReadOnly("scriptName", &LuaScriptComponent::GetScriptName, "string", "File name without extension.")
        .ReadOnly("running", &LuaScriptComponent::IsRunning, "boolean", "Whether the script has been started (Play mode).")
        .ReadOnly("env", [](LuaScriptComponent& c, sol::this_state s) -> sol::object {
                return c.IsRunning() ? sol::make_object(s, c.GetEnvironment()) : sol::make_object(s, sol::lua_nil);
            }, "table", "The script's functions and variables (nil before Play mode).")
        .Method("Reload", &LuaScriptComponent::ReloadScript, "()", "", "Re-runs the script file and calls its Start().");
}

// =====================================================================================
// Rendering: Material, Texture, TextureLayer
// =====================================================================================

void ScriptEngine::RegisterRendering()
{
    auto& reg = ScriptApi::Registry::Get();

    lua.new_enum("LayerBlendMode",
        "Normal", LayerBlendMode::Normal,
        "Height", LayerBlendMode::Height,
        "Slope", LayerBlendMode::Slope,
        "HeightSlope", LayerBlendMode::HeightSlope);
    ScriptApi::DocumentEnum("LayerBlendMode", "How a texture layer is blended over the ones below.", {
        { "Normal", "Plain alpha blend using the mesh UVs." },
        { "Height", "Blend by world height (heightMin..heightMax)." },
        { "Slope", "Blend by surface slope (slopeMin..slopeMax)." },
        { "HeightSlope", "Height and slope combined (terrain)." } });

    auto tex = lua.new_usertype<Texture>("Texture", sol::no_constructor);
    ScriptApi::Class<Texture>(tex, reg.AddType("Texture", "A GPU texture loaded from an image file."))
        .ReadOnly("width", &Texture::GetWidth, "number", "Width in pixels.")
        .ReadOnly("height", &Texture::GetHeight, "number", "Height in pixels.")
        .ReadOnly("id", &Texture::GetTextureID, "number", "OpenGL texture id.")
        .ReadOnly("path", [](Texture& t) { const char* loc = t.GetFileLocation(); return loc ? std::string(loc) : std::string(); }, "string", "Source file.")
        .Method("GetWidth", &Texture::GetWidth, "()", "number", "Width in pixels.")
        .Method("GetHeight", &Texture::GetHeight, "()", "number", "Height in pixels.")
        .Method("GetTextureID", &Texture::GetTextureID, "()", "number", "OpenGL texture id.")
        .Method("GetFileLocation", [](Texture& t) { const char* loc = t.GetFileLocation(); return loc ? std::string(loc) : std::string(); }, "()", "string", "Source file.");

    auto mat = lua.new_usertype<Material>("Material", sol::no_constructor);
    ScriptApi::Class<Material>(mat, reg.AddType("Material", "Surface parameters used by a shader. Shared materials (loaded from a .mat file) change every object using them."))
        .Property("color", [](Material& m) { return m.GetVec4Ptr("material.baseColor"); }, [](Material& m, const glm::vec4& c) { m.SetColor(c); }, "vec4", "Base color RGBA 0..1 (live: material.color.r = 1).")
        .Property("alpha", &Material::GetAlpha, &Material::SetAlpha, "number", "Opacity (0..1).")
        .Property("specularIntensity", &Material::GetSpecularIntensity, &Material::SetSpecularIntensity, "number", "Strength of specular highlights.")
        .Property("shininess", &Material::GetShininess, &Material::SetShininess, "number", "Specular exponent (bigger = sharper highlights).")
        .Property("reflectivity", &Material::GetReflectivity, &Material::SetReflectivity, "number", "Environment reflection amount.")
        .Property("tiling", [](Material& m) { return m.GetVec2Ptr("material.tiling"); }, &Material::SetTiling, "vec2", "UV repeat (live reference).")
        .Property("offset", [](Material& m) { return m.GetVec2Ptr("material.offset"); }, &Material::SetOffset, "vec2", "UV offset (live: material.offset.x = material.offset.x + dt scrolls).")
        .ReadOnly("path", &Material::GetPath, "string", "The .mat file, if loaded from one.")
        .Method("SetColor", [](Material& m, const glm::vec4& c) { m.SetColor(c); }, "(color: vec4)", "", "Sets the base color.")
        .Method("GetFloat", &Material::GetFloat, "(name: string)", "number", "Reads a float shader parameter (\"material.shininess\"...).")
        .Method("SetFloat", &Material::SetFloat, "(name: string, value: number)", "", "Writes a float shader parameter.")
        .Method("GetInt", &Material::GetInt, "(name: string)", "number", "Reads an int shader parameter.")
        .Method("SetInt", &Material::SetInt, "(name: string, value: number)", "", "Writes an int shader parameter.")
        .Method("GetVec2", &Material::GetVec2, "(name: string)", "vec2", "Reads a vec2 shader parameter.")
        .Method("SetVec2", &Material::SetVec2, "(name: string, value: vec2)", "", "Writes a vec2 shader parameter.")
        .Method("GetVec3", &Material::GetVec3, "(name: string)", "vec3", "Reads a vec3 shader parameter.")
        .Method("SetVec3", &Material::SetVec3, "(name: string, value: vec3)", "", "Writes a vec3 shader parameter.")
        .Method("GetVec4", &Material::GetVec4, "(name: string)", "vec4", "Reads a vec4 shader parameter.")
        .Method("SetVec4", &Material::SetVec4, "(name: string, value: vec4)", "", "Writes a vec4 shader parameter.")
        .Method("SetTexture", [](Material& m, const std::string& name, const std::string& path) { m.SetTextureParam(name, path); },
            "(name: string, path: string)", "", "Binds a texture file to a sampler of the shader.");

    auto layer = lua.new_usertype<LayerRef>("TextureLayer", sol::no_constructor);
    ScriptApi::Class<LayerRef> lc(layer, reg.AddType("TextureLayer", "One surface layer of a GameObject (diffuse + normal + displacement, blended by height / slope)."));

#define LAYER_NUMBER(field, doc) \
    lc.Property(#field, [](const LayerRef& r) { auto* l = r.Get(); return l ? l->field : 0.0f; }, \
        [](LayerRef& r, float v) { if (auto* l = r.Get()) l->field = v; }, "number", doc)
    LAYER_NUMBER(opacity, "Overall influence (0..1).");
    LAYER_NUMBER(tiling, "Texture repeat scale.");
    LAYER_NUMBER(displacementScale, "Displacement depth.");
    LAYER_NUMBER(heightMin, "Height where the layer starts to appear (Height blend).");
    LAYER_NUMBER(heightMax, "Height where the layer is fully visible (Height blend).");
    LAYER_NUMBER(slopeMin, "Slope where the layer starts (0 = flat, Slope blend).");
    LAYER_NUMBER(slopeMax, "Slope where the layer is fully visible (1 = vertical).");
#undef LAYER_NUMBER

    lc.Property("invert", [](const LayerRef& r) { auto* l = r.Get(); return l ? l->invert : false; },
            [](LayerRef& r, bool v) { if (auto* l = r.Get()) l->invert = v; }, "boolean", "Inverts the blend mask.")
      .Property("blendMode", [](const LayerRef& r) { auto* l = r.Get(); return l ? l->blendMode : LayerBlendMode::Normal; },
            [](LayerRef& r, LayerBlendMode v) { if (auto* l = r.Get()) l->blendMode = v; }, "LayerBlendMode", "Normal / Height / Slope / HeightSlope.")
      .Property("texture", [](const LayerRef& r) { auto* l = r.Get(); return l ? l->texture : nullptr; },
            [](LayerRef& r, Texture* t) { if (auto* l = r.Get()) { l->texture = t; l->texturePath = (t && t->GetFileLocation()) ? t->GetFileLocation() : ""; } },
            "Texture", "Diffuse / albedo texture.")
      .Property("normalMap", [](const LayerRef& r) { auto* l = r.Get(); return l ? l->normalMap : nullptr; },
            [](LayerRef& r, Texture* t) { if (auto* l = r.Get()) { l->normalMap = t; l->normalMapPath = (t && t->GetFileLocation()) ? t->GetFileLocation() : ""; } },
            "Texture", "Normal map.")
      .Property("displacementMap", [](const LayerRef& r) { auto* l = r.Get(); return l ? l->displacementMap : nullptr; },
            [](LayerRef& r, Texture* t) { if (auto* l = r.Get()) { l->displacementMap = t; l->displacementMapPath = (t && t->GetFileLocation()) ? t->GetFileLocation() : ""; } },
            "Texture", "Height / displacement map.")
      .ReadOnly("index", [](const LayerRef& r) { return r.index; }, "number", "Index in the object's layer list (0-based).")
      .ReadOnly("valid", [](const LayerRef& r) { return r.Get() != nullptr; }, "boolean", "False once the layer or its object is gone.")
      .ReadOnly("owner", [](const LayerRef& r) { return GameObjectRegistry::FindByID(r.objectId); }, "GameObject", "The object this layer belongs to.")
      .Method("SetTexture", [](LayerRef& r, const std::string& path) { if (auto* l = r.Get()) { l->texture = NodeAssets::GetTexture(path); l->texturePath = path; } },
            "(path: string)", "", "Loads (cached) and sets the diffuse texture.")
      .Method("SetNormalMap", [](LayerRef& r, const std::string& path) { if (auto* l = r.Get()) { l->normalMap = NodeAssets::GetTexture(path); l->normalMapPath = path; } },
            "(path: string)", "", "Loads (cached) and sets the normal map.")
      .Method("SetDisplacementMap", [](LayerRef& r, const std::string& path) { if (auto* l = r.Get()) { l->displacementMap = NodeAssets::GetTexture(path); l->displacementMapPath = path; } },
            "(path: string)", "", "Loads (cached) and sets the displacement map.");
}

// =====================================================================================
// Scene / Assets / Script / Time / Math tables
// =====================================================================================

void ScriptEngine::RegisterSceneApi()
{
    ScriptApi::Table(lua, "Scene", "The current scene.")
        .Function("Find", [](const std::string& name) -> GameObject* {
                SceneManager* sm = ServiceLocator::GetSceneManager();
                return sm ? sm->FindObject(name) : GameObjectRegistry::FindByName(name);
            }, "(name: string)", "GameObject", "Object with this name (children included), or nil.")
        .Function("FindByID", [](uint64_t id) { return GameObjectRegistry::FindByID(id); },
            "(id: number)", "GameObject", "Object with this runtime id, or nil.")
        .Function("GetObjects", [](sol::this_state s) {
                sol::state_view lua(s);
                sol::table t = lua.create_table();
                if (SceneManager* sm = ServiceLocator::GetSceneManager())
                {
                    int i = 1;
                    for (GameObject* go : sm->GetObjects()) t[i++] = go;
                }
                return t;
            }, "()", "GameObject[]", "All root objects of the scene (Lua array).")
        .Function("FindLight", [](const std::string& name) -> LightObject* {
                if (SceneManager* sm = ServiceLocator::GetSceneManager())
                    for (LightObject* l : sm->GetLights()) if (l && l->GetName() == name) return l;
                return nullptr;
            }, "(name: string)", "Light", "Light with this name (\"Sun\"...), or nil.")
        .Function("GetLights", [](sol::this_state s) {
                sol::state_view lua(s);
                sol::table t = lua.create_table();
                if (SceneManager* sm = ServiceLocator::GetSceneManager())
                {
                    int i = 1;
                    for (LightObject* l : sm->GetLights()) t[i++] = l;
                }
                return t;
            }, "()", "Light[]", "All lights (Lua array).");

    ScriptApi::Table(lua, "Assets", "Loads project assets directly (cached), ready to assign.")
        .Function("Texture", [](const std::string& path) { return NodeAssets::GetTexture(path); },
            "(path: string)", "Texture", "Texture from an image file (cached).")
        .Function("Material", [this](const std::string& path) -> Material* {
                auto it = m_MaterialCache.find(path);
                if (it != m_MaterialCache.end()) return it->second;
                Material* m = Material::LoadFromFile(path);
                if (m) m->SetPath(path);
                m_MaterialCache[path] = m;
                return m;
            }, "(path: string)", "Material", "Material from a .mat file (cached, shared by everyone who loads it).")
        .Function("Model", [](const std::string& path) { return ResourceHandle::Model(path).ResolveModel(); },
            "(path: string)", "Model", "Model from a file (cached).")
        .Function("Audio", [](const std::string& path) { return ResourceHandle::Audio(path).ResolveAudioShared(); },
            "(path: string)", "AudioClip", "Audio clip from a file (cached).")
        .Function("Exists", [](const std::string& path) { std::error_code ec; return std::filesystem::exists(path, ec); },
            "(path: string)", "boolean", "Whether the file exists.");

    ScriptApi::Table(lua, "Script", "Use other scripts as modules.")
        .Function("Require", [this](const std::string& nameOrPath) { return RequireScript(nameOrPath, false); },
            "(name: string)", "script", "Runs a script once in its own environment and returns its functions / variables. \"util\" finds Assets/**/util.lua.")
        .Function("Reload", [this](const std::string& nameOrPath) { return RequireScript(nameOrPath, true); },
            "(name: string)", "script", "Like Require, but runs the file again.");

    ScriptApi::Table(lua, "Time", "Play-mode clock, updated by the engine every frame.")
        .Value("deltaTime", 0.0f, "number", "Seconds since the previous frame (same as Update's dt).")
        .Value("time", 0.0, "number", "Seconds since Play started.")
        .Value("frameCount", 0, "number", "Frames since Play started.")
        .Function("Now", []() { return glfwGetTime(); }, "()", "number", "Seconds since the engine started (wall clock).");

    ScriptApi::Table(lua, "Math", "Small math helpers (the standard 'math' library is available too).")
        .Function("Lerp", [](float a, float b, float t) { return a + (b - a) * t; }, "(a, b, t)", "number", "Linear interpolation.")
        .Function("Clamp", [](float x, float lo, float hi) { return std::max(lo, std::min(hi, x)); }, "(x, min, max)", "number", "Clamps x to [min, max].")
        .Function("Clamp01", [](float x) { return std::max(0.0f, std::min(1.0f, x)); }, "(x)", "number", "Clamps x to [0, 1].");

    ScriptApi::Registry::Get().AddGlobal({ "gameObject", Kind::Global, "GameObject", "",
        "The GameObject this script is attached to (scripts run by a Lua Script component).", true });
    ScriptApi::Registry::Get().AddGlobal({ "log", Kind::Function, "", "(message: string)", "Prints a message to the console.", true });
}

void ScriptEngine::RegisterLights()
{
    auto& reg = ScriptApi::Registry::Get();
    auto ut = lua.new_usertype<LightObject>("Light", sol::no_constructor);
    // Float members reached through the LightObject's pointer getters (nullptr: not this light type)
    auto floatProp = [](float* (LightObject::*getter)(), bool sunAngle) {
        return sol::property(
            [getter](LightObject& l) { float* p = (l.*getter)(); return p ? *p : 0.0f; },
            [getter, sunAngle](LightObject& l, float v) {
                float* p = (l.*getter)();
                if (!p) return;
                *p = v;
                if (sunAngle) if (auto* d = l.GetDirectionalLight()) d->UpdateDirectionFromEuler();
            });
    };
    ScriptApi::Class<LightObject> c(ut, reg.AddType("Light", "A light of the scene: directional (sun), point or spot."));
    c.Property("name", &LightObject::GetName, &LightObject::SetName, "string", "Light name (as in the Hierarchy).")
     .ReadOnly("type", [](LightObject& l) -> std::string {
            switch (l.GetLightType()) { case LightType::Directional: return "Directional"; case LightType::Point: return "Point"; default: return "Spot"; }
        }, "string", "Directional, Point or Spot.")
     .Property("color", [](LightObject& l) { return l.GetColorPtr(); }, [](LightObject& l, const glm::vec3& v) { if (auto* p = l.GetColorPtr()) *p = v; },
        "vec3", "Light color RGB 0..1 (live: light.color.r = 1).")
     .Property("position", [](LightObject& l) { return l.GetPositionPtr(); }, [](LightObject& l, const glm::vec3& v) { l.SetPosition(v); },
        "vec3", "Point / spot light position (live; nil for directional lights).")
     .Property("direction", [](LightObject& l) { return l.GetDirectionPtr(); }, [](LightObject& l, const glm::vec3& v) { if (auto* p = l.GetDirectionPtr()) *p = v; },
        "vec3", "Directional / spot direction (live; for the sun prefer pitch / yaw).");
    const struct { const char* name; float* (LightObject::*getter)(); bool angle; const char* doc; } floats[] = {
        { "ambientIntensity", &LightObject::GetAmbientIntensityPtr, false, "Ambient contribution." },
        { "diffuseIntensity", &LightObject::GetDiffuseIntensityPtr, false, "Direct light strength." },
        { "constant", &LightObject::GetConstantPtr, false, "Attenuation constant term (point / spot)." },
        { "linear", &LightObject::GetLinearPtr, false, "Attenuation linear term (point / spot)." },
        { "exponent", &LightObject::GetExponentPtr, false, "Attenuation quadratic term (point / spot)." },
        { "edge", &LightObject::GetSpotEdgePtr, false, "Spot cone edge (spot only)." },
        { "pitch", &LightObject::GetPitchPtr, true, "Sun elevation in degrees (directional only; updates the direction)." },
        { "yaw", &LightObject::GetYawPtr, true, "Sun heading in degrees (directional only; updates the direction)." },
    };
    for (const auto& f : floats)
    {
        ut.set(f.name, floatProp(f.getter, f.angle));
        c.Doc({ f.name, Kind::Property, "number", "", f.doc, false });
    }
}

void ScriptEngine::BeginFrame(float deltaTime)
{
    m_ScriptTime += deltaTime;
    m_FrameCount++;
    sol::table time = lua["Time"];
    if (!time.valid()) return;
    time["deltaTime"] = deltaTime;
    time["time"] = m_ScriptTime;
    time["frameCount"] = m_FrameCount;
}

void ScriptEngine::ResetClock()
{
    m_ScriptTime = 0.0;
    m_FrameCount = 0;
    sol::table time = lua["Time"];
    if (!time.valid()) return;
    time["deltaTime"] = 0.0f;
    time["time"] = 0.0;
    time["frameCount"] = 0;
}

void ScriptEngine::RegisterInput()
{
    ScriptApi::Table input(lua, "Input", "Keyboard and mouse state.");
    input.Function("IsKeyDown", [](int key) -> bool {
            GLFWwindow* window = glfwGetCurrentContext();
            return window && glfwGetKey(window, key) == GLFW_PRESS;
        }, "(key: number)", "boolean", "Whether a key is held (Input.KEY_W, Input.KEY_SPACE...).")
        .Function("IsMouseDown", [](int button) -> bool {
            GLFWwindow* window = glfwGetCurrentContext();
            return window && glfwGetMouseButton(window, button) == GLFW_PRESS;
        }, "(button: number)", "boolean", "Whether a mouse button is held (Input.MOUSE_LEFT...).");

    for (char c = 'A'; c <= 'Z'; c++)
        input.Constant((std::string("KEY_") + c).c_str(), GLFW_KEY_A + (c - 'A'), "number", "Letter key.");
    for (char c = '0'; c <= '9'; c++)
        input.Constant((std::string("KEY_") + c).c_str(), GLFW_KEY_0 + (c - '0'), "number", "Number key.");
    for (int f = 1; f <= 12; f++)
        input.Constant(("KEY_F" + std::to_string(f)).c_str(), GLFW_KEY_F1 + (f - 1), "number", "Function key.");
    const std::pair<const char*, int> named[] = {
        { "KEY_SPACE", GLFW_KEY_SPACE }, { "KEY_ENTER", GLFW_KEY_ENTER }, { "KEY_ESCAPE", GLFW_KEY_ESCAPE },
        { "KEY_TAB", GLFW_KEY_TAB }, { "KEY_BACKSPACE", GLFW_KEY_BACKSPACE },
        { "KEY_LEFT_SHIFT", GLFW_KEY_LEFT_SHIFT }, { "KEY_LEFT_CONTROL", GLFW_KEY_LEFT_CONTROL }, { "KEY_LEFT_ALT", GLFW_KEY_LEFT_ALT },
        { "KEY_UP", GLFW_KEY_UP }, { "KEY_DOWN", GLFW_KEY_DOWN }, { "KEY_LEFT", GLFW_KEY_LEFT }, { "KEY_RIGHT", GLFW_KEY_RIGHT } };
    for (const auto& k : named) input.Constant(k.first, k.second, "number", "Key code.");
    input.Constant("MOUSE_LEFT", GLFW_MOUSE_BUTTON_LEFT, "number", "Left mouse button.")
         .Constant("MOUSE_RIGHT", GLFW_MOUSE_BUTTON_RIGHT, "number", "Right mouse button.")
         .Constant("MOUSE_MIDDLE", GLFW_MOUSE_BUTTON_MIDDLE, "number", "Middle mouse button.");
}

// =====================================================================================
// Standard library documentation (editor help only; the libraries themselves come from Lua)
// =====================================================================================

void ScriptEngine::DocumentStandardLibrary()
{
    auto& reg = ScriptApi::Registry::Get();
    auto lib = [&](const char* name, const char* doc, std::vector<std::array<const char*, 4>> fns) {
        ScriptApi::Type& t = reg.AddType(name, doc, "", true);
        for (const auto& f : fns) t.Add({ f[0], Kind::Function, f[2], f[1], f[3], true });
        reg.AddGlobal({ name, Kind::Global, name, "", doc, true });
    };
    lib("math", "Lua standard math library.", {
        {"abs", "(x)", "number", "Absolute value."}, {"floor", "(x)", "number", "Rounds down."}, {"ceil", "(x)", "number", "Rounds up."},
        {"sqrt", "(x)", "number", "Square root."}, {"sin", "(radians)", "number", "Sine."}, {"cos", "(radians)", "number", "Cosine."},
        {"tan", "(radians)", "number", "Tangent."}, {"atan", "(y, x)", "number", "Arc tangent of y/x."}, {"exp", "(x)", "number", "e^x."},
        {"log", "(x, base?)", "number", "Logarithm."}, {"max", "(x, ...)", "number", "Largest argument."}, {"min", "(x, ...)", "number", "Smallest argument."},
        {"random", "(m?, n?)", "number", "Random number: [0,1), [1,m] or [m,n]."}, {"randomseed", "(x)", "", "Seeds the generator."},
        {"rad", "(degrees)", "number", "Degrees to radians."}, {"deg", "(radians)", "number", "Radians to degrees."},
        {"fmod", "(x, y)", "number", "Remainder of x/y."}, {"tointeger", "(x)", "number", "Converts to integer if exact."} });
    ScriptApi::Type& m = reg.AddType("math", "");
    m.Add({ "pi", Kind::Constant, "number", "", "3.14159...", true });
    m.Add({ "huge", Kind::Constant, "number", "", "Infinity.", true });
    lib("string", "Lua standard string library (also callable as s:upper()...).", {
        {"format", "(fmt, ...)", "string", "printf-style formatting: string.format(\"%.2f\", x)."}, {"len", "(s)", "number", "Length."},
        {"sub", "(s, i, j?)", "string", "Substring."}, {"upper", "(s)", "string", "Upper case."}, {"lower", "(s)", "string", "Lower case."},
        {"rep", "(s, n)", "string", "Repeats s n times."}, {"find", "(s, pattern, init?)", "number", "Finds a pattern."},
        {"match", "(s, pattern)", "string", "Captures of a pattern."}, {"gmatch", "(s, pattern)", "function", "Iterator over matches."},
        {"gsub", "(s, pattern, repl)", "string", "Replaces matches."}, {"byte", "(s, i?)", "number", "Character code."}, {"char", "(...)", "string", "Characters from codes."} });
    lib("table", "Lua standard table library.", {
        {"insert", "(t, [pos,] value)", "", "Inserts a value."}, {"remove", "(t, pos?)", "any", "Removes and returns a value."},
        {"concat", "(t, sep?)", "string", "Joins values into a string."}, {"sort", "(t, comp?)", "", "Sorts in place."},
        {"unpack", "(t)", "any", "Returns all values."}, {"pack", "(...)", "table", "Packs arguments into a table."} });
    lib("os", "Lua standard os library.", {
        {"time", "()", "number", "Current time (seconds)."}, {"clock", "()", "number", "CPU time used (seconds)."}, {"date", "(format?)", "string", "Formatted date."} });

    const std::array<const char*, 4> base[] = {
        {"print", "(...)", "", "Prints to the console."}, {"tostring", "(v)", "string", "Converts to string."}, {"tonumber", "(v)", "number", "Converts to number."},
        {"pairs", "(t)", "function", "Iterates all key/value pairs: for k, v in pairs(t) do."}, {"ipairs", "(t)", "function", "Iterates an array: for i, v in ipairs(t) do."},
        {"type", "(v)", "string", "Type name of a value."}, {"pcall", "(f, ...)", "boolean", "Calls f in protected mode."}, {"error", "(message)", "", "Raises an error."},
        {"assert", "(v, message?)", "any", "Errors if v is false/nil."}, {"select", "(n, ...)", "any", "Arguments after n / count with '#'."},
        {"next", "(t, k?)", "any", "Next key/value pair."}, {"setmetatable", "(t, mt)", "table", "Sets a metatable."}, {"getmetatable", "(t)", "table", "Gets a metatable."},
        {"rawget", "(t, k)", "any", "Get without metamethods."}, {"rawset", "(t, k, v)", "table", "Set without metamethods."}, {"require", "(name)", "any", "Lua modules (prefer Script.Require for project scripts)."} };
    for (const auto& f : base) reg.AddGlobal({ f[0], Kind::Function, f[2], f[1], f[3], true });
}

// =====================================================================================
// Init / execution
// =====================================================================================

void ScriptEngine::Init()
{
    if (m_Initialized) return;
    m_Initialized = true;

    lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::string, sol::lib::math, sol::lib::table, sol::lib::os);

    lua.set_function("log", [](const std::string& message) {
        std::cout << "[Lua] " << message << std::endl;
    });

    RegisterMath();
    RegisterCoreTypes();
    RegisterGameObject();
    RegisterPhysics();
    RegisterRendering();
    RegisterResourceSystem();
    RegisterSceneApi();
    RegisterInput();
    RegisterLights();
    DocumentStandardLibrary();

    std::cout << "ScriptEngine Initialized with Lua 5.4 and sol2 (" << ScriptApi::Registry::Get().Types().size() << " documented types)." << std::endl;
}

void ScriptEngine::RegisterResourceSystem()
{
    auto& reg = ScriptApi::Registry::Get();

    lua.new_enum("ResourceType",
        "Invalid", ResourceType::Invalid,
        "Texture", ResourceType::Texture,
        "Audio", ResourceType::Audio,
        "NodeGraph", ResourceType::NodeGraph,
        "Model", ResourceType::Model,
        "GameObject", ResourceType::GameObject,
        "Unknown", ResourceType::Invalid);
    ScriptApi::DocumentEnum("ResourceType", "Kind of asset a Resource handle points to.", {
        {"Invalid", ""}, {"Texture", ""}, {"Audio", ""}, {"NodeGraph", ""}, {"Model", ""}, {"GameObject", ""}, {"Unknown", ""} });

    auto audio = lua.new_usertype<AudioClip>("AudioClip", sol::no_constructor);
    ScriptApi::Class<AudioClip>(audio, reg.AddType("AudioClip", "A sound file."))
        .Method("Play", &AudioClip::Play, "()", "", "Plays the clip.")
        .Method("play", &AudioClip::Play, "()", "", "Plays the clip.")
        .Method("Stop", &AudioClip::Stop, "()", "", "Stops the clip.")
        .Method("stop", &AudioClip::Stop, "()", "", "Stops the clip.")
        .Method("GetPath", &AudioClip::GetPath, "()", "string", "Source file.")
        .ReadOnly("path", &AudioClip::GetPath, "string", "Source file.")
        .Method("GetName", &AudioClip::GetName, "()", "string", "Clip name.")
        .ReadOnly("name", &AudioClip::GetName, "string", "Clip name.")
        .Method("GetDuration", &AudioClip::GetDuration, "()", "number", "Length in seconds.")
        .ReadOnly("duration", &AudioClip::GetDuration, "number", "Length in seconds.");

    auto model = lua.new_usertype<Model>("Model", sol::no_constructor);
    ScriptApi::Class<Model>(model, reg.AddType("Model", "A 3D model loaded from a file."))
        .Method("GetPath", &Model::GetPath, "()", "string", "Source file.")
        .ReadOnly("path", &Model::GetPath, "string", "Source file.")
        .Method("IsReady", &Model::IsReady, "()", "boolean", "Whether loading finished.")
        .ReadOnly("isReady", &Model::IsReady, "boolean", "Whether loading finished.")
        .Method("GetMeshCount", &Model::GetMeshCount, "()", "number", "Number of meshes.")
        .ReadOnly("meshCount", &Model::GetMeshCount, "number", "Number of meshes.");

    auto graph = lua.new_usertype<NodeGraph>("NodeGraph", sol::no_constructor);
    auto runGraph = [](NodeGraph& g) {
        SceneManager* sm = ServiceLocator::GetSceneManager();
        if (sm) g.Execute(*sm, nullptr, nullptr);
    };
    ScriptApi::Class<NodeGraph>(graph, reg.AddType("NodeGraph", "A procedural node graph."))
        .Method("Execute", runGraph, "()", "", "Runs the graph on the current scene.")
        .Method("execute", runGraph, "()", "", "Runs the graph on the current scene.");

    auto res = lua.new_usertype<ResourceHandle>("Resource",
        sol::constructors<
            ResourceHandle(),
            ResourceHandle(ResourceType, const std::string&),
            ResourceHandle(ResourceType, const std::string&, uint64_t)>(),
        sol::meta_function::to_string, [](const ResourceHandle& h) {
            return "Resource(" + h.GetTypeName() + ", '" + h.GetPath() + "')";
        });
    auto instantiate = sol::overload(
        [](ResourceHandle& h) { return h.Instantiate(glm::vec3(0.0f)); },
        [](ResourceHandle& h, const glm::vec3& pos) { return h.Instantiate(pos); });
    ScriptApi::Class<ResourceHandle>(res, reg.AddType("Resource", "A lazy handle to an asset or scene object; Resolve() returns the real thing. For direct objects prefer Assets.* and Scene.Find."))
        .Static("Texture", &ResourceHandle::Texture, "(path: string)", "Resource", "Handle to a texture file.")
        .Static("Model", &ResourceHandle::Model, "(path: string)", "Resource", "Handle to a model file.")
        .Static("Audio", &ResourceHandle::Audio, "(path: string)", "Resource", "Handle to an audio file.")
        .Static("NodeGraph", &ResourceHandle::NodeGraph, "(identifier: string)", "Resource", "Handle to a node graph.")
        .Static("GameObject", sol::overload(
                [](const std::string& name) { return ResourceHandle::GameObject(name); },
                [](const std::string& name, uint64_t id) { return ResourceHandle::GameObject(name, id); },
                [](::GameObject* obj) { return ResourceHandle::FromGameObject(obj); }),
            "(name: string)", "Resource", "Handle to a scene object by name.")
        .Static("FromPath", &ResourceHandle::FromPath, "(path: string)", "Resource", "Handle typed by file extension.")
        .Static("FromScene", &ResourceHandle::FromScene, "(name: string)", "Resource", "Handle to a scene object by name.")
        .Static("FromID", &ResourceHandle::FromID, "(id: number)", "Resource", "Handle to a scene object by id.")
        .Static("FromGameObject", &ResourceHandle::FromGameObject, "(object: GameObject)", "Resource", "Handle to an object.")
        .Static("Get", &ResourceHandle::FromPath, "(path: string)", "Resource", "Handle typed by file extension.")
        .Static("FindObject", &ResourceHandle::FromScene, "(name: string)", "Resource", "Handle to a scene object by name.")
        .Method("GetType", &ResourceHandle::GetType, "()", "ResourceType", "Kind of asset.")
        .Method("GetTypeName", &ResourceHandle::GetTypeName, "()", "string", "Kind of asset as text.")
        .Method("type_name", &ResourceHandle::GetTypeName, "()", "string", "Kind of asset as text.")
        .Method("GetPath", &ResourceHandle::GetPath, "()", "string", "Path or identifier.")
        .Method("GetIdentifier", &ResourceHandle::GetIdentifier, "()", "string", "Identifier.")
        .Method("GetID", &ResourceHandle::GetID, "()", "number", "Target object id (0 = by name).")
        .Method("IsValid", &ResourceHandle::IsValid, "()", "boolean", "Whether the target exists.")
        .Method("is_valid", &ResourceHandle::IsValid, "()", "boolean", "Whether the target exists.")
        .Method("Resolve", &ResourceHandle::ResolveLua, "()", "any", "The real object (Texture, Model, AudioClip, NodeGraph or GameObject).")
        .Method("resolve", &ResourceHandle::ResolveLua, "()", "any", "The real object.")
        .Method("Instantiate", instantiate, "(position?: vec3)", "GameObject", "Spawns the model / object into the scene.")
        .Method("instantiate", instantiate, "(position?: vec3)", "GameObject", "Spawns the model / object into the scene.")
        .ReadOnly("type", &ResourceHandle::GetTypeName, "string", "Kind of asset as text.")
        .ReadOnly("path", &ResourceHandle::GetPath, "string", "Path or identifier.")
        .ReadOnly("identifier", &ResourceHandle::GetIdentifier, "string", "Identifier.")
        .ReadOnly("isValid", &ResourceHandle::IsValid, "boolean", "Whether the target exists.")
        .ReadOnly("valid", &ResourceHandle::IsValid, "boolean", "Whether the target exists.");
    reg.AddGlobal({ "Resource", Kind::Global, "Resource", "", "Asset / object handles (Resource.Texture(path), Resource.GameObject(name)...).", true });

    // Global alias: AssetHandle == Resource
    lua["AssetHandle"] = lua["Resource"];
    reg.AddGlobal({ "AssetHandle", Kind::Global, "Resource", "", "Alias of Resource.", true });
}

std::string ScriptEngine::ResolveScriptPath(const std::string& nameOrPath)
{
    namespace fs = std::filesystem;
    if (nameOrPath.empty()) return "";
    std::error_code ec;

    std::string candidate = nameOrPath;
    for (char& c : candidate) if (c == '\\') c = '/';
    if (fs::is_regular_file(candidate, ec)) return candidate;

    fs::path p(candidate);
    if (!p.has_extension())
    {
        if (fs::is_regular_file(candidate + ".lua", ec)) return candidate + ".lua";
        if (fs::is_regular_file("Assets/scripts/" + candidate + ".lua", ec)) return "Assets/scripts/" + candidate + ".lua";
        p += ".lua";
    }

    // Search Assets/ by file name (case-insensitive)
    std::string wanted = Lower(p.filename().string());
    if (fs::is_directory("Assets", ec))
    {
        for (auto it = fs::recursive_directory_iterator("Assets", fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec) break;
            if (it->is_regular_file(ec) && Lower(it->path().filename().string()) == wanted)
            {
                std::string found = it->path().generic_string();
                return found;
            }
        }
    }
    return "";
}

sol::object ScriptEngine::RequireScript(const std::string& nameOrPath, bool reload)
{
    std::string path = ResolveScriptPath(nameOrPath);
    if (path.empty())
    {
        std::cerr << "[ScriptEngine] Script.Require: no script named '" << nameOrPath << "'" << std::endl;
        return sol::make_object(lua, sol::lua_nil);
    }

    std::string key = Lower(path);
    if (!reload)
    {
        auto it = m_Modules.find(key);
        if (it != m_Modules.end()) return sol::make_object(lua, it->second);
    }

    sol::environment env(lua, sol::create, lua.globals());
    sol::load_result chunk = lua.load_file(path);
    if (!chunk.valid())
    {
        sol::error err = chunk;
        std::cerr << "[ScriptEngine] Script.Require: error loading '" << path << "': " << err.what() << std::endl;
        return sol::make_object(lua, sol::lua_nil);
    }
    sol::protected_function fn = chunk;
    env.set_on(fn);
    auto result = fn();
    if (!result.valid())
    {
        sol::error err = result;
        std::cerr << "[ScriptEngine] Script.Require: error running '" << path << "': " << err.what() << std::endl;
        return sol::make_object(lua, sol::lua_nil);
    }
    m_Modules[key] = env;
    return sol::make_object(lua, env);
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
