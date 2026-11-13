#include "Core/ResourceHandle.h"
#include "Scene/GameObjectRegistry.h"
#include "Scene/GameObject.h"
#include "Scene/SceneManager.h"
#include "Core/ServiceLocator.h"
#include "Core/AssetManager.h"
#include "Nodes/NodeAssets.h"
#include "Nodes/NodeGraph.h"
#include "Rendering/Texture.h"
#include "Rendering/Model.h"
#include "External Libs/nlohmann/json.hpp"

#include <iostream>
#include <fstream>
#include <algorithm>

using json = nlohmann::json;

// --- AudioClip Implementation ---
void AudioClip::Play()
{
    printf("[AudioClip] Playing: %s\n", path.c_str());
}

void AudioClip::Stop()
{
    printf("[AudioClip] Stopped: %s\n", path.c_str());
}

// --- ResourceHandle Implementation ---
std::unordered_map<std::string, std::shared_ptr<::AudioClip>> ResourceHandle::s_AudioCache;
std::unordered_map<std::string, std::shared_ptr<::NodeGraph>> ResourceHandle::s_GraphCache;
std::mutex ResourceHandle::s_ResourceMutex;

ResourceHandle::ResourceHandle()
    : m_Type(ResourceType::Invalid), m_PathOrIdentifier(""), m_TargetID(0)
{
}

ResourceHandle::ResourceHandle(ResourceType type, const std::string& pathOrIdentifier, uint64_t targetID)
    : m_Type(type), m_PathOrIdentifier(pathOrIdentifier), m_TargetID(targetID)
{
}

ResourceHandle ResourceHandle::Texture(const std::string& path)
{
    return ResourceHandle(ResourceType::Texture, path);
}

ResourceHandle ResourceHandle::Model(const std::string& path)
{
    return ResourceHandle(ResourceType::Model, path);
}

ResourceHandle ResourceHandle::Audio(const std::string& path)
{
    return ResourceHandle(ResourceType::Audio, path);
}

ResourceHandle ResourceHandle::NodeGraph(const std::string& identifier)
{
    return ResourceHandle(ResourceType::NodeGraph, identifier);
}

ResourceHandle ResourceHandle::GameObject(const std::string& name, uint64_t targetID)
{
    return ResourceHandle(ResourceType::GameObject, name, targetID);
}

ResourceHandle ResourceHandle::FromPath(const std::string& path)
{
    if (path.empty()) return ResourceHandle();

    std::filesystem::path fsPath(path);
    std::string ext = fsPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)::tolower(c); });

    ResourceType type = ResourceType::Invalid;
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr")
    {
        type = ResourceType::Texture;
    }
    else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae" || ext == ".blend" || ext == ".assbin")
    {
        type = ResourceType::Model;
    }
    else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac")
    {
        type = ResourceType::Audio;
    }
    else if (ext == ".json")
    {
        type = ResourceType::NodeGraph;
    }

    return ResourceHandle(type, path);
}

ResourceHandle ResourceHandle::FromScene(const std::string& name)
{
    ::GameObject* obj = GameObjectRegistry::FindByName(name);
    if (obj)
    {
        return ResourceHandle(ResourceType::GameObject, name, obj->GetID());
    }
    return ResourceHandle(ResourceType::GameObject, name, 0);
}

ResourceHandle ResourceHandle::FromID(uint64_t id)
{
    ::GameObject* obj = GameObjectRegistry::FindByID(id);
    if (obj)
    {
        return ResourceHandle(ResourceType::GameObject, obj->GetName(), id);
    }
    return ResourceHandle(ResourceType::GameObject, "", id);
}

ResourceHandle ResourceHandle::FromGameObject(::GameObject* obj)
{
    if (obj)
    {
        return ResourceHandle(ResourceType::GameObject, obj->GetName(), obj->GetID());
    }
    return ResourceHandle(ResourceType::GameObject, "", 0);
}

std::string ResourceHandle::GetTypeName() const
{
    switch (m_Type)
    {
    case ResourceType::Texture: return "Texture";
    case ResourceType::Model: return "Model";
    case ResourceType::Audio: return "Audio";
    case ResourceType::NodeGraph: return "NodeGraph";
    case ResourceType::GameObject: return "GameObject";
    default: return "Invalid";
    }
}

std::string ResourceHandle::GetIdentifier() const
{
    if (m_Type == ResourceType::GameObject)
    {
        if (m_TargetID != 0)
        {
            ::GameObject* obj = GameObjectRegistry::FindByID(m_TargetID);
            if (obj) return obj->GetName();
        }
        return m_PathOrIdentifier;
    }
    return m_PathOrIdentifier;
}

std::string ResourceHandle::GetResolvedPath() const
{
    if (m_PathOrIdentifier.empty()) return "";

    std::error_code ec;
    if (std::filesystem::exists(m_PathOrIdentifier, ec))
    {
        return m_PathOrIdentifier;
    }

    switch (m_Type)
    {
    case ResourceType::Texture:
    {
        std::string cand1 = "Assets/Textures/" + m_PathOrIdentifier;
        if (std::filesystem::exists(cand1, ec)) return cand1;
        std::string cand2 = "Assets/" + m_PathOrIdentifier;
        if (std::filesystem::exists(cand2, ec)) return cand2;
        break;
    }
    case ResourceType::Model:
    {
        std::string cand1 = "Assets/Models/" + m_PathOrIdentifier;
        if (std::filesystem::exists(cand1, ec)) return cand1;
        std::string cand2 = "Assets/" + m_PathOrIdentifier;
        if (std::filesystem::exists(cand2, ec)) return cand2;
        break;
    }
    case ResourceType::Audio:
    {
        std::string cand1 = "Assets/Audio/" + m_PathOrIdentifier;
        if (std::filesystem::exists(cand1, ec)) return cand1;
        std::string cand2 = "Assets/" + m_PathOrIdentifier;
        if (std::filesystem::exists(cand2, ec)) return cand2;
        break;
    }
    case ResourceType::NodeGraph:
    {
        std::string cand1 = "Assets/CustomNodes/" + m_PathOrIdentifier;
        if (std::filesystem::exists(cand1, ec)) return cand1;
        std::string cand2 = "Assets/Scenes/" + m_PathOrIdentifier;
        if (std::filesystem::exists(cand2, ec)) return cand2;
        std::string cand3 = "Assets/" + m_PathOrIdentifier;
        if (std::filesystem::exists(cand3, ec)) return cand3;
        break;
    }
    default:
        break;
    }

    return m_PathOrIdentifier;
}

bool ResourceHandle::IsValid() const
{
    if (m_Type == ResourceType::Invalid)
        return false;

    if (m_Type == ResourceType::GameObject)
    {
        if (m_TargetID != 0)
        {
            return GameObjectRegistry::FindByID(m_TargetID) != nullptr;
        }
        if (!m_PathOrIdentifier.empty())
        {
            return GameObjectRegistry::FindByName(m_PathOrIdentifier) != nullptr;
        }
        return false;
    }

    if (m_PathOrIdentifier.empty())
        return false;

    std::error_code ec;
    std::string resolved = GetResolvedPath();
    return std::filesystem::exists(resolved, ec) && !std::filesystem::is_directory(resolved, ec);
}

::Texture* ResourceHandle::ResolveTexture() const
{
    if (m_Type != ResourceType::Texture || !IsValid()) return nullptr;
    std::string resolved = GetResolvedPath();
    return NodeAssets::GetTexture(resolved);
}

::Model* ResourceHandle::ResolveModel() const
{
    if (m_Type != ResourceType::Model || !IsValid()) return nullptr;
    std::string resolved = GetResolvedPath();
    if (auto am = ServiceLocator::GetAssetManager())
    {
        return am->GetModel(resolved, true);
    }
    return nullptr;
}

std::shared_ptr<::AudioClip> ResourceHandle::ResolveAudioShared() const
{
    if (m_Type != ResourceType::Audio || !IsValid()) return nullptr;
    std::string resolved = GetResolvedPath();
    if (resolved.empty()) return nullptr;

    std::lock_guard<std::mutex> lock(s_ResourceMutex);
    auto it = s_AudioCache.find(resolved);
    if (it != s_AudioCache.end())
    {
        return it->second;
    }

    auto clip = std::make_shared<::AudioClip>();
    clip->path = m_PathOrIdentifier;
    std::filesystem::path p(m_PathOrIdentifier);
    clip->name = p.stem().string();
    clip->duration = 0.0f;

    s_AudioCache[resolved] = clip;
    return clip;
}

::AudioClip* ResourceHandle::ResolveAudio() const
{
    auto clip = ResolveAudioShared();
    return clip ? clip.get() : nullptr;
}

std::shared_ptr<::NodeGraph> ResourceHandle::ResolveNodeGraphShared() const
{
    if (m_Type != ResourceType::NodeGraph || !IsValid()) return nullptr;
    std::string resolved = GetResolvedPath();
    if (resolved.empty()) return nullptr;

    std::lock_guard<std::mutex> lock(s_ResourceMutex);
    auto it = s_GraphCache.find(resolved);
    if (it != s_GraphCache.end())
    {
        return it->second;
    }

    try
    {
        std::ifstream file(resolved);
        if (!file.is_open()) return nullptr;
        json j = json::parse(file, nullptr, false);
        if (j.is_discarded()) return nullptr;

        auto graph = std::make_shared<::NodeGraph>();
        SceneManager* sm = ServiceLocator::GetSceneManager();
        if (sm)
        {
            graph->Deserialize(j, *sm);
        }
        s_GraphCache[resolved] = graph;
        return graph;
    }
    catch (...)
    {
        return nullptr;
    }
}

::NodeGraph* ResourceHandle::ResolveNodeGraph() const
{
    auto graph = ResolveNodeGraphShared();
    return graph ? graph.get() : nullptr;
}

void ResourceHandle::ClearAudioCache()
{
    std::lock_guard<std::mutex> lock(s_ResourceMutex);
    s_AudioCache.clear();
}

void ResourceHandle::ClearGraphCache()
{
    std::lock_guard<std::mutex> lock(s_ResourceMutex);
    s_GraphCache.clear();
}

void ResourceHandle::ClearCache()
{
    std::lock_guard<std::mutex> lock(s_ResourceMutex);
    s_AudioCache.clear();
    s_GraphCache.clear();
}

::GameObject* ResourceHandle::ResolveGameObject() const
{
    if (m_Type != ResourceType::GameObject) return nullptr;
    if (m_TargetID != 0)
    {
        return GameObjectRegistry::FindByID(m_TargetID);
    }
    if (!m_PathOrIdentifier.empty())
    {
        return GameObjectRegistry::FindByName(m_PathOrIdentifier);
    }
    return nullptr;
}

void* ResourceHandle::Resolve() const
{
    if (!IsValid()) return nullptr;

    switch (m_Type)
    {
    case ResourceType::Texture: return ResolveTexture();
    case ResourceType::Model: return ResolveModel();
    case ResourceType::Audio: return ResolveAudio();
    case ResourceType::NodeGraph: return ResolveNodeGraph();
    case ResourceType::GameObject: return ResolveGameObject();
    default: return nullptr;
    }
}

void ResourceHandle::InvalidateTarget()
{
    m_TargetID = 0;
    m_PathOrIdentifier.clear();
    m_Type = ResourceType::Invalid;
}

sol::object ResourceHandle::ResolveLua(sol::this_state s) const
{
    sol::state_view lua(s);
    if (!IsValid())
    {
        return sol::nil;
    }

    switch (m_Type)
    {
    case ResourceType::Texture:
    {
        ::Texture* tex = ResolveTexture();
        if (tex) return sol::make_object(lua, tex);
        break;
    }
    case ResourceType::Model:
    {
        ::Model* mdl = ResolveModel();
        if (mdl) return sol::make_object(lua, mdl);
        break;
    }
    case ResourceType::Audio:
    {
        auto audio = ResolveAudioShared();
        if (audio) return sol::make_object(lua, audio);
        break;
    }
    case ResourceType::NodeGraph:
    {
        auto graph = ResolveNodeGraphShared();
        if (graph) return sol::make_object(lua, graph);
        break;
    }
    case ResourceType::GameObject:
    {
        ::GameObject* go = ResolveGameObject();
        if (go) return sol::make_object(lua, go);
        break;
    }
    default:
        break;
    }

    return sol::nil;
}

::GameObject* ResourceHandle::Instantiate(const glm::vec3& spawnPos)
{
    if (!IsValid()) return nullptr;

    if (m_Type == ResourceType::GameObject)
    {
        ::GameObject* target = ResolveGameObject();
        if (!target) return nullptr;

        ::GameObject* clone = target->Clone(target->GetName() + " (Instance)");
        if (!clone) return nullptr;

        clone->GetTransform().SetPosition(spawnPos);
        SceneManager* sm = ServiceLocator::GetSceneManager();
        if (sm)
        {
            sm->AddObject(clone);
        }
        return clone;
    }
    else if (m_Type == ResourceType::Model)
    {
        ::Model* mdl = ResolveModel();
        if (!mdl) return nullptr;

        std::filesystem::path p(m_PathOrIdentifier);
        std::string baseName = p.stem().string();
        if (baseName.empty()) baseName = "ModelInstance";

        ::GameObject* obj = new ::GameObject(baseName);
        obj->SetModel(mdl);
        obj->SetModelSourcePath(m_PathOrIdentifier);
        obj->GetTransform().SetPosition(spawnPos);

        SceneManager* sm = ServiceLocator::GetSceneManager();
        if (sm)
        {
            sm->AddObject(obj);
        }
        return obj;
    }

    return nullptr;
}
