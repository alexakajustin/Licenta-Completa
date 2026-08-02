#pragma once

#include "Scene/Component.h"
#include "Rendering/MeshData.h"
#include "Rendering/InstancedGroup.h"
#include "Scene/SceneManager.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>

/**
 * @class InfiniteRepeaterComponent
 * @brief Tracks the camera and repeats a base mesh infinitely in a grid around the player.
 */
class InfiniteRepeaterComponent : public Component
{
public:
	InfiniteRepeaterComponent(GameObject* owner, SceneManager* scene, const MeshData& baseMesh, float chunkSize, int repeatRadius);
	virtual ~InfiniteRepeaterComponent();

	virtual void Start() override;
	virtual void Update(float deltaTime) override;
	virtual std::string GetName() const override { return "InfiniteRepeater"; }

	void SetSettings(const MeshData& baseMesh, float newChunkSize, int newRadius);

private:
	SceneManager* sceneRef;
	MeshData meshTemplate;
	float chunkSize;
	int radius;
	
	int currentGridX;
	int currentGridZ;
	bool isInitialized;

	InstancedGroup* instancedGroup;
	std::string groupName;

	void UpdateInstances(int centerGridX, int centerGridZ);
};
