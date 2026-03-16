#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <future>
#include <filesystem>

#include "Model.h"

class AssetManager
{
public:
	static AssetManager& Get()
	{
		static AssetManager instance;
		return instance;
	}

	// Returns a pointer to a model. If it's not loaded, it starts loading it.
	// Note: The model might not be ready for rendering immediately.
	Model* GetModel(const std::string& path);

	// Called every frame to handle deferred GPU uploads on the main thread
	void Update();

	void Clear();

private:
	AssetManager() = default;
	~AssetManager() { Clear(); }

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
