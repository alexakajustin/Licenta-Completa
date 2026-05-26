#include "Core/AssetManager.h"
#include "Rendering/Model.h"
#include <filesystem>

std::string AssetManager::GetShaderPath(const std::string& filename) const
{
    // Try the base path first
    std::string path = _baseShaderPath + filename;
    if (std::filesystem::exists(path)) {
        return path;
    }
    
    // Fallback path
    std::string fallback = _fallbackShaderPath + filename;
    if (std::filesystem::exists(fallback)) {
        return fallback;
    }

    // If neither exists, just return base path as default,
    // though this will likely fail to load later
    return path;
}

std::string AssetManager::GetTexturePath(const std::string& filename) const
{
    return _baseTexturePath + filename;
}

std::string AssetManager::GetModelPath(const std::string& filename) const
{
    return _baseModelPath + filename;
}

void AssetManager::RegisterShader(GLuint shaderID, const std::string& name)
{
    _shaderRegistry[shaderID] = name;
}

std::string AssetManager::GetShaderName(GLuint shaderID) const
{
    auto it = _shaderRegistry.find(shaderID);
    if (it != _shaderRegistry.end()) {
        return it->second;
    }
    return "Unknown Shader";
}

bool AssetManager::HasShader(GLuint shaderID) const
{
    return _shaderRegistry.find(shaderID) != _shaderRegistry.end();
}

const std::unordered_map<GLuint, std::string>& AssetManager::GetAllRegisteredShaders() const
{
    return _shaderRegistry;
}

// --- Async Model Loading ---

Model* AssetManager::GetModel(const std::string& path, bool loadIfMissing)
{
    std::lock_guard<std::mutex> lock(cacheMutex);

    auto it = modelCache.find(path);
    if (it != modelCache.end())
    {
        return it->second;
    }

    if (!loadIfMissing) {
        return nullptr;
    }

    // Create a new model and start loading it
    Model* newModel = new Model();
    modelCache[path] = newModel;

    // Start CPU loading on a background thread
    activeTasks.push_back({
        path,
        newModel,
        std::async(std::launch::async, [newModel, path]() {
            newModel->LoadModelCPU(path);
        })
    });

    printf("[AssetManager] Started loading model: %s\n", path.c_str());
    return newModel;
}

int AssetManager::GetActiveTasksCount()
{
    std::lock_guard<std::mutex> lock(cacheMutex);
    return (int)activeTasks.size();
}

void AssetManager::Update()
{
    std::lock_guard<std::mutex> lock(cacheMutex);

    for (auto it = activeTasks.begin(); it != activeTasks.end(); )
    {
        // Check if the future is ready
        if (it->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            // CPU loading is done, now perform GPU upload on the main thread
            it->model->LoadModelGPU();
            printf("[AssetManager] Finished loading model: %s\n", it->path.c_str());
            it = activeTasks.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void AssetManager::WaitForAll()
{
    std::lock_guard<std::mutex> lock(cacheMutex);

    for (auto& task : activeTasks)
    {
        if (task.future.valid())
            task.future.wait();

        task.model->LoadModelGPU();
        printf("[AssetManager] Finished loading model (Sync Load): %s\n", task.path.c_str());
    }
    activeTasks.clear();
}

void AssetManager::Clear()
{
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    // Wait for all tasks to finish before deleting
    for (auto& task : activeTasks)
    {
        if (task.future.valid())
            task.future.wait();
    }
    activeTasks.clear();

    for (auto& pair : modelCache)
    {
        pair.second->ClearModel();
        delete pair.second;
    }
    modelCache.clear();
}
