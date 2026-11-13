#include "Scene/GameObjectRegistry.h"
#include "Scene/GameObject.h"

std::unordered_map<uint64_t, GameObject*> GameObjectRegistry::s_Registry;
std::mutex GameObjectRegistry::s_Mutex;
std::atomic<uint64_t> GameObjectRegistry::s_NextGameObjectID{ 1 };

uint64_t GameObjectRegistry::Register(GameObject* obj)
{
    if (!obj) return 0;
    uint64_t id = s_NextGameObjectID.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(s_Mutex);
    s_Registry[id] = obj;
    return id;
}

void GameObjectRegistry::Unregister(uint64_t id)
{
    if (id == 0) return;
    std::lock_guard<std::mutex> lock(s_Mutex);
    s_Registry.erase(id);
}

GameObject* GameObjectRegistry::FindByID(uint64_t id)
{
    if (id == 0) return nullptr;
    std::lock_guard<std::mutex> lock(s_Mutex);
    auto it = s_Registry.find(id);
    return (it != s_Registry.end()) ? it->second : nullptr;
}

GameObject* GameObjectRegistry::FindByName(const std::string& name)
{
    if (name.empty()) return nullptr;
    std::lock_guard<std::mutex> lock(s_Mutex);
    for (const auto& pair : s_Registry)
    {
        if (pair.second && pair.second->GetName() == name)
        {
            return pair.second;
        }
    }
    return nullptr;
}

void GameObjectRegistry::Clear()
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    s_Registry.clear();
}
