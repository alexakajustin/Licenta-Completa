#pragma once

#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <mutex>
#include <future>
#include <GL/glew.h>

class Model;

/**
 * @class AssetManager
 * @brief Centralized manager for handling asset paths and metadata.
 * 
 * Replaces hardcoded string paths scattered throughout the codebase.
 * It also replaces the global `g_ShaderNames` registry.
 */
class AssetManager
{
public:
    AssetManager() = default;
    ~AssetManager() = default;

    // --- Path Management ---
    
    /**
     * @brief Gets the absolute or relative path to a shader file.
     * @param filename The name of the shader file (e.g., "shader.vert").
     * @return Full path to the shader.
     */
    std::string GetShaderPath(const std::string& filename) const;

    /**
     * @brief Gets the path to a texture file.
     * @param filename The name of the texture file.
     * @return Full path to the texture.
     */
    std::string GetTexturePath(const std::string& filename) const;
    
    /**
     * @brief Gets the path to a model file.
     */
    std::string GetModelPath(const std::string& filename) const;

    // --- Model Management & Async Loading ---
    
    Model* GetModel(const std::string& path, bool loadIfMissing = true);
    int GetActiveTasksCount();
    void Update();
    void WaitForAll();
    void Clear();

    // --- Shader Registry (replaces g_ShaderNames) ---
    
    void RegisterShader(GLuint shaderID, const std::string& name);
    std::string GetShaderName(GLuint shaderID) const;
    bool HasShader(GLuint shaderID) const;
    const std::unordered_map<GLuint, std::string>& GetAllRegisteredShaders() const;

private:
    std::string _baseShaderPath = "Assets/Shaders/";
    std::string _fallbackShaderPath = "Shaders/"; // Some shaders seem to be here based on logs
    std::string _baseTexturePath = "Assets/Textures/";
    std::string _baseModelPath = "Assets/Models/";

    std::unordered_map<GLuint, std::string> _shaderRegistry;

    // Async Loading Data
    std::map<std::string, Model*> modelCache;
    std::mutex cacheMutex;

    struct LoadingTask
    {
        std::string path;
        Model* model;
        std::future<void> future;
    };
    std::vector<LoadingTask> activeTasks;
};
