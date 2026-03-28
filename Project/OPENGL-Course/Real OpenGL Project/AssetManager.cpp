#include "AssetManager.h"
#include <iostream>

Model* AssetManager::GetModel(const std::string& path)
{
	std::lock_guard<std::mutex> lock(cacheMutex);

	auto it = modelCache.find(path);
	if (it != modelCache.end())
	{
		return it->second;
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
