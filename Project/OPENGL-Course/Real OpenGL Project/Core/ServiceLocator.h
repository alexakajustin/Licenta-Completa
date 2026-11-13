#pragma once

class AssetManager;
class SceneManager;

/**
 * @class ServiceLocator
 * @brief Provides global access to core engine services without relying on hardcoded Singletons or externs.
 * 
 * This follows the Dependency Inversion Principle, allowing us to swap implementations (e.g., for testing)
 * and keeps the codebase decoupled.
 */
class ServiceLocator
{
public:
    static void Provide(AssetManager* assetManager) { _assetManager = assetManager; }
    static AssetManager* GetAssetManager() { return _assetManager; }

    static void Provide(SceneManager* sceneManager) { _sceneManager = sceneManager; }
    static SceneManager* GetSceneManager() { return _sceneManager; }

private:
    static AssetManager* _assetManager;
    static SceneManager* _sceneManager;
};

/**
 * @class ScopedSceneManagerScope
 * @brief RAII helper restoring the previous SceneManager upon scope exit.
 */
class ScopedSceneManagerScope
{
public:
    explicit ScopedSceneManagerScope(SceneManager* sm)
        : m_Previous(ServiceLocator::GetSceneManager())
    {
        ServiceLocator::Provide(sm);
    }

    ~ScopedSceneManagerScope()
    {
        ServiceLocator::Provide(m_Previous);
    }

    ScopedSceneManagerScope(const ScopedSceneManagerScope&) = delete;
    ScopedSceneManagerScope& operator=(const ScopedSceneManagerScope&) = delete;

private:
    SceneManager* m_Previous;
};
