#include "Scene/WorldStreamerComponent.h"
#include "Scene/GameObject.h"
#include "Scene/SceneManager.h"
#include "Core/ServiceLocator.h"
#include "Nodes/NodeGraph.h"
#include "imgui.h"

WorldStreamerComponent::WorldStreamerComponent(GameObject* owner, SceneManager* scene, GameObject* referenceObject, int radius)
	: Component(owner), sceneRef(scene), referenceObject(referenceObject), radius(radius)
{
}

WorldStreamerComponent::~WorldStreamerComponent()
{
}

void WorldStreamerComponent::Update(float deltaTime)
{
	if (!gameObject) return;
	SceneManager* scene = sceneRef;
	if (!scene) return;

	// Use the global camera position tracked by SceneManager
	glm::vec3 pos = scene->GetEditorCameraPos();
	
	float sizeX = 100.0f;
	float sizeZ = 100.0f;
	if (referenceObject)
	{
		sizeX = referenceObject->GetTransform().GetScale().x;
		sizeZ = referenceObject->GetTransform().GetScale().z;
		if (sizeX <= 0.0f) sizeX = 100.0f;
		if (sizeZ <= 0.0f) sizeZ = 100.0f;
	}

	int chunkX = (int)std::floor(pos.x / sizeX);
	int chunkZ = (int)std::floor(pos.z / sizeZ);

	if (chunkX != lastChunkX || chunkZ != lastChunkZ)
	{
		lastChunkX = chunkX;
		lastChunkZ = chunkZ;

		std::set<std::pair<int, int>> expectedChunks;
		for (int x = -radius; x <= radius; x++)
		{
			for (int z = -radius; z <= radius; z++)
			{
				expectedChunks.insert({ chunkX + x, chunkZ + z });
			}
		}

		// Unload chunks that are out of bounds
		std::vector<std::pair<int, int>> toUnload;
		for (auto& chunk : activeChunks)
		{
			if (expectedChunks.find(chunk) == expectedChunks.end())
			{
				toUnload.push_back(chunk);
			}
		}
		for (auto& chunk : toUnload)
		{
			UnloadChunk(chunk.first, chunk.second);
			activeChunks.erase(chunk);
		}

		// Generate new chunks
		for (auto& chunk : expectedChunks)
		{
			if (activeChunks.find(chunk) == activeChunks.end())
			{
				GenerateChunk(chunk.first, chunk.second);
				activeChunks.insert(chunk);
			}
		}
	}
}

void WorldStreamerComponent::GenerateChunk(int x, int z)
{
	SceneManager* scene = sceneRef;
	if (!scene) return;

	// Warning: If there are multiple node graphs, this gets the active one.
	// We assume the active node graph is the one defining the chunked pipeline.
	NodeGraph& graph = scene->GetNodeGraph();

	float sizeX = 100.0f;
	float sizeZ = 100.0f;
	if (referenceObject)
	{
		sizeX = referenceObject->GetTransform().GetScale().x;
		sizeZ = referenceObject->GetTransform().GetScale().z;
		if (sizeX <= 0.0f) sizeX = 100.0f;
		if (sizeZ <= 0.0f) sizeZ = 100.0f;
	}

	glm::vec2 offset = glm::vec2(x * sizeX, z * sizeZ);
	std::string suffix = "_Chunk_" + std::to_string(x) + "_" + std::to_string(z);

	printf("[WorldStreamer] Generating chunk %d, %d at Offset(%.1f, %.1f)\n", x, z, offset.x, offset.y);
	
	// Execute the entire pipeline asynchronously for this chunk
	graph.ExecuteAsync(sceneRef, offset, suffix);
}

void WorldStreamerComponent::UnloadChunk(int x, int z)
{
	SceneManager* scene = sceneRef;
	if (!scene) return;

	std::string suffix = "_Chunk_" + std::to_string(x) + "_" + std::to_string(z);
	
	printf("[WorldStreamer] Unloading chunk %d, %d (Suffix: %s)\n", x, z, suffix.c_str());

	// Remove all GameObjects that end with this suffix
	std::vector<GameObject*> allObjs;
	scene->GetAllObjects(allObjs);
	for (auto* obj : allObjs)
	{
		std::string name = obj->GetName();
		if (name.length() >= suffix.length() && 
			name.compare(name.length() - suffix.length(), suffix.length(), suffix) == 0)
		{
			scene->RemoveObject(name);
		}
	}

	// Remove all InstancedGroups that end with this suffix
	auto& groups = scene->GetInstancedGroups();
	for (int i = (int)groups.size() - 1; i >= 0; i--)
	{
		std::string name = groups[i]->GetName();
		if (name.length() >= suffix.length() && 
			name.compare(name.length() - suffix.length(), suffix.length(), suffix) == 0)
		{
			scene->RemoveInstancedGroup(name);
		}
	}
}

void WorldStreamerComponent::DrawInspector()
{
	ImGui::Text("World Streamer Component");
	ImGui::Separator();
	
	ImGui::Text("Tracker: Main Camera");

	if (referenceObject)
	{
		ImGui::Text("Reference: %s", referenceObject->GetName().c_str());
		ImGui::Text("Chunk Size: %.1f, %.1f", referenceObject->GetTransform().GetScale().x, referenceObject->GetTransform().GetScale().z);
	}
	else
	{
		ImGui::Text("No Reference Object. Using 100x100.");
	}

	if (ImGui::DragInt("Radius", &radius, 1, 0, 10))
	{
		// Force reload on resize
		lastChunkX = -999999;
	}
	
	if (ImGui::Button("Force Refresh Chunks"))
	{
		// Unload all
		for (auto& chunk : activeChunks) {
			UnloadChunk(chunk.first, chunk.second);
		}
		activeChunks.clear();
		lastChunkX = -999999;
	}

	ImGui::Text("Active Chunks: %d", (int)activeChunks.size());
	ImGui::Text("Current Chunk: %d, %d", lastChunkX, lastChunkZ);
}
