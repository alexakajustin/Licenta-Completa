#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>

class GameObject;

/**
 * @class GameObjectRegistry
 * @brief Thread-safe registry mapping monotonically increasing 64-bit IDs to active GameObjects.
 * Provides complete immunity against dangling pointers and ABA address reuse.
 */
class GameObjectRegistry
{
public:
    /**
     * @brief Registers an active GameObject with a new monotonic 64-bit ID.
     * @param obj Pointer to the GameObject being registered.
     * @return Assigned unique 64-bit ID.
     */
    static uint64_t Register(GameObject* obj);

    /**
     * @brief Unregisters a GameObject by its ID upon destruction.
     * @param id Unique 64-bit ID to unregister.
     */
    static void Unregister(uint64_t id);

    /**
     * @brief Finds an active GameObject by its unique 64-bit ID.
     * @param id The ID to query.
     * @return Valid pointer to GameObject, or nullptr if not found or deleted.
     */
    static GameObject* FindByID(uint64_t id);

    /**
     * @brief Finds an active GameObject by its unique 64-bit ID.
     * Forwarding alias fulfilling PROJECT.md § Interface Contracts (M1 ↔ M3).
     * @param id The ID to query.
     * @return Valid pointer to GameObject, or nullptr if not found or deleted.
     */
    static GameObject* Find(uint64_t id) { return FindByID(id); }

    /**
     * @brief Finds an active GameObject by its string name (first match).
     * @param name The name to query.
     * @return Valid pointer to GameObject, or nullptr if not found.
     */
    static GameObject* FindByName(const std::string& name);

    /**
     * @brief Clears all entries from the registry.
     */
    static void Clear();

private:
    static std::unordered_map<uint64_t, GameObject*> s_Registry;
    static std::mutex s_Mutex;
    static std::atomic<uint64_t> s_NextGameObjectID;
};
