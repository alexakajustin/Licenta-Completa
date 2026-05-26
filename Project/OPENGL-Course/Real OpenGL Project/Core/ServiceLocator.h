#pragma once

class AssetManager;

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

private:
    static AssetManager* _assetManager;
};
