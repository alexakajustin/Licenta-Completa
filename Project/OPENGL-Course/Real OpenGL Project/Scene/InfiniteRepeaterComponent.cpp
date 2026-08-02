#include "Scene/InfiniteRepeaterComponent.h"
#include "Scene/GameObject.h"
#include "Scene/Player.h"
#include "Rendering/Mesh.h"
#include <string>
#include <cmath>

InfiniteRepeaterComponent::InfiniteRepeaterComponent(GameObject* owner, SceneManager* scene, const MeshData& baseMesh, float chunkSize, int repeatRadius)
	: Component(owner), sceneRef(scene), meshTemplate(baseMesh), chunkSize(chunkSize), radius(repeatRadius), 
	  currentGridX(-999999), currentGridZ(-999999), isInitialized(false), instancedGroup(nullptr)
{
	groupName = "InfiniteRepeater_" + owner->GetName() + "_" + std::to_string((unsigned long long)this);
}

InfiniteRepeaterComponent::~InfiniteRepeaterComponent()
{
	if (sceneRef && instancedGroup)
	{
		sceneRef->RemoveInstancedGroup(groupName);
	}
}

void InfiniteRepeaterComponent::Start()
{
	if (gameObject)
	{
		// Hide the original single GameObject so we only see the instanced grid
		gameObject->SetVisible(false);
	}
}

void InfiniteRepeaterComponent::SetSettings(const MeshData& baseMesh, float newChunkSize, int newRadius)
{
	meshTemplate = baseMesh;
	chunkSize = newChunkSize;
	radius = newRadius;
	
	currentGridX = -999999;
	currentGridZ = -999999;
	isInitialized = false;
}

void InfiniteRepeaterComponent::Update(float deltaTime)
{
	if (!sceneRef || !gameObject) return;
	
	glm::vec3 trackPos(0.0f);
	
	Player* player = sceneRef->FindPlayer();
	if (player && player->GetGameObject())
	{
		trackPos = player->GetGameObject()->GetTransform().GetPosition();
	}
	else
	{
		// Fallback to tracking camera or center if no player exists
		// In a real editor we might query the active camera from SceneManager or Application.
		// For simplicity, we just anchor at (0,0,0) in editor mode if no player is driving.
	}

	int gridX = (int)std::floor(trackPos.x / chunkSize);
	int gridZ = (int)std::floor(trackPos.z / chunkSize);

	if (!isInitialized || gridX != currentGridX || gridZ != currentGridZ)
	{
		UpdateInstances(gridX, gridZ);
		currentGridX = gridX;
		currentGridZ = gridZ;
		isInitialized = true;
	}
}

void InfiniteRepeaterComponent::UpdateInstances(int centerGridX, int centerGridZ)
{
	std::vector<InstancedGroup::PackedInstance> packedInstances;
	int count = (2 * radius + 1) * (2 * radius + 1);
	packedInstances.reserve(count);

	for (int z = -radius; z <= radius; z++)
	{
		for (int x = -radius; x <= radius; x++)
		{
			int cellX = centerGridX + x;
			int cellZ = centerGridZ + z;

			float worldX = (float)cellX * chunkSize;
			float worldZ = (float)cellZ * chunkSize;

			InstancedGroup::PackedInstance inst;
			inst.positionAndScale = glm::vec4(worldX, 0.0f, worldZ, 1.0f);
			inst.rotationAndFlags = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
			
			packedInstances.push_back(inst);
		}
	}

	if (!instancedGroup)
	{
		instancedGroup = new InstancedGroup(groupName);
		sceneRef->AddInstancedGroup(instancedGroup);
	}

	// We use the GameObject's Mesh since the Node Graph's OutputNode manages it
	if (gameObject->GetMesh())
	{
		instancedGroup->Setup(
			gameObject->GetMesh(), 
			packedInstances,
			gameObject->GetMaterial(),
			gameObject->GetTexture(),
			gameObject->GetNormalMap(),
			gameObject->GetTextureLayers()
		);
	}
}
