#pragma once

#include <string>
#include <memory>
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <mutex>
#include <glm/glm.hpp>
#include <sol/sol.hpp>

class Texture;
class Model;
class NodeGraph;
class GameObject;

/**
 * @enum ResourceType
 * @brief Core enumeration distinguishing resource categories.
 */
enum class ResourceType
{
    Invalid = 0,
    Texture,
    Audio,
    NodeGraph,
    Model,
    GameObject,
    Unknown = Invalid
};

/**
 * @struct AudioClip
 * @brief Audio resource descriptor encapsulating file path, name, duration, and playback interface.
 */
struct AudioClip
{
    std::string path;
    std::string name;
    float duration = 0.0f;

    void Play();
    void Stop();
    const std::string& GetPath() const { return path; }
    const std::string& GetName() const { return name; }
    float GetDuration() const { return duration; }
};

/**
 * @class ResourceHandle
 * @brief Unified resource abstraction ("Everything is a Resource") representing engine assets
 * (Textures, Models, Audio, NodeGraphs) and scene GameObjects with lifetime safety.
 */
class ResourceHandle
{
public:
    ResourceHandle();
    ResourceHandle(ResourceType type, const std::string& pathOrIdentifier, uint64_t targetID = 0);

    // Static factory methods
    static ResourceHandle Texture(const std::string& path);
    static ResourceHandle Model(const std::string& path);
    static ResourceHandle Audio(const std::string& path);
    static ResourceHandle NodeGraph(const std::string& identifier);
    static ResourceHandle GameObject(const std::string& name, uint64_t targetID = 0);
    static ResourceHandle FromPath(const std::string& path);
    static ResourceHandle FromScene(const std::string& name);
    static ResourceHandle FromID(uint64_t id);
    static ResourceHandle FromGameObject(::GameObject* obj);

    // Uniform inspection API
    ResourceType GetType() const { return m_Type; }
    std::string GetTypeName() const;
    const std::string& GetPath() const { return m_PathOrIdentifier; }
    std::string GetIdentifier() const;
    uint64_t GetID() const { return m_TargetID; }
    bool IsValid() const;

    // C++ Typed resolvers
    ::Texture* ResolveTexture() const;
    ::Model* ResolveModel() const;
    ::AudioClip* ResolveAudio() const;
    ::NodeGraph* ResolveNodeGraph() const;
    ::GameObject* ResolveGameObject() const;

    // Shared pointer accessors (preventing dangling pointers when exposed to Lua / shared owners)
    std::shared_ptr<::AudioClip> ResolveAudioShared() const;
    std::shared_ptr<::NodeGraph> ResolveNodeGraphShared() const;

    // Cache management
    static void ClearCache();
    static void ClearAudioCache();
    static void ClearGraphCache();

    // Generic C++ resolver
    void* Resolve() const;
    ::GameObject* AsGameObject() const { return ResolveGameObject(); }
    void InvalidateTarget();

    // Uniform Polymorphic Lua resolver
    sol::object ResolveLua(sol::this_state s) const;

    // Instantiation API (Models & GameObjects)
    ::GameObject* Instantiate(const glm::vec3& spawnPos = glm::vec3(0.0f));

private:
    std::string GetResolvedPath() const;

    ResourceType m_Type = ResourceType::Invalid;
    std::string m_PathOrIdentifier;
    uint64_t m_TargetID = 0; // For GameObjects (0 = lookup by name)

    static std::unordered_map<std::string, std::shared_ptr<::AudioClip>> s_AudioCache;
    static std::unordered_map<std::string, std::shared_ptr<::NodeGraph>> s_GraphCache;
    static std::mutex s_ResourceMutex;
};

// Global aliases for convenience
using Resource = ResourceHandle;
using AssetHandle = ResourceHandle;
