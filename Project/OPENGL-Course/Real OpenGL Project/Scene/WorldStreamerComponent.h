#pragma once
#include "Scene/Component.h"
#include <glm/glm.hpp>
#include <vector>
#include <set>
#include <string>

class SceneManager;

class WorldStreamerComponent : public Component
{
public:
	WorldStreamerComponent(GameObject* owner, SceneManager* scene, GameObject* referenceObject = nullptr, int radius = 2);
	virtual ~WorldStreamerComponent();

	virtual void Update(float deltaTime) override;
	virtual void DrawInspector() override;
	virtual std::string GetName() const override { return "WorldStreamerComponent"; }

	void SetReferenceObject(GameObject* ref) { referenceObject = ref; }
	void SetRadius(int r) { radius = r; }

private:
	void GenerateChunk(int x, int z);
	void UnloadChunk(int x, int z);

	SceneManager* sceneRef;
	GameObject* referenceObject = nullptr;
	std::string trackerName = "Camera";
	int radius = 2;

	std::set<std::pair<int, int>> activeChunks;
	int lastChunkX = -999999;
	int lastChunkZ = -999999;
};
