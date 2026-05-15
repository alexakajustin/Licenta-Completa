#pragma once

#include "UndoManager.h"
#include "GameObject.h"
#include "LightObject.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>

class SceneManager; // Forward declaration

// =====================================================================
// TransformSnapshot — Captures position/rotation/scale for one object
// =====================================================================
struct TransformSnapshot {
	GameObject* object;
	glm::vec3 position;
	glm::vec3 rotation;
	glm::vec3 scale;
};

// =====================================================================
// TransformAction — Undo/Redo for transform changes (gizmo or inspector)
// =====================================================================
class TransformAction : public UndoAction
{
public:
	TransformAction(const std::string& desc,
		const std::vector<TransformSnapshot>& before,
		const std::vector<TransformSnapshot>& after);

	void Undo() override;
	void Redo() override;
	std::string GetDescription() const override { return description; }

private:
	std::string description;
	std::vector<TransformSnapshot> beforeState;
	std::vector<TransformSnapshot> afterState;
};

// =====================================================================
// LightTransformSnapshot — Captures position for one light
// =====================================================================
struct LightTransformSnapshot {
	LightObject* light;
	glm::vec3 position;
};

// =====================================================================
// LightTransformAction — Undo/Redo for light position changes (gizmo)
// =====================================================================
class LightTransformAction : public UndoAction
{
public:
	LightTransformAction(const std::string& desc,
		const std::vector<LightTransformSnapshot>& before,
		const std::vector<LightTransformSnapshot>& after);

	void Undo() override;
	void Redo() override;
	std::string GetDescription() const override { return description; }

private:
	std::string description;
	std::vector<LightTransformSnapshot> beforeState;
	std::vector<LightTransformSnapshot> afterState;
};

// =====================================================================
// DeletedObjectEntry — Everything needed to restore a deleted object
// =====================================================================
struct DeletedObjectEntry {
	GameObject* object;       // The deleted object (ownership held by the action)
	int originalIndex;        // Index in the objects vector
	GameObject* parent;       // Parent pointer at time of deletion (nullable)
	std::string name;
};

// =====================================================================
// DeleteObjectsAction — Undo/Redo for deleting one or more objects
// =====================================================================
class DeleteObjectsAction : public UndoAction
{
public:
	// Takes ownership of the deleted objects
	DeleteObjectsAction(SceneManager* scene,
		const std::vector<DeletedObjectEntry>& entries,
		const std::vector<int>& previousSelection);
	~DeleteObjectsAction();

	void Undo() override;
	void Redo() override;
	std::string GetDescription() const override;

private:
	SceneManager* scene;
	std::vector<DeletedObjectEntry> deletedEntries;
	std::vector<int> previousSelection;
	bool ownsObjects; // True when objects are NOT in the scene (action owns memory)
};

// =====================================================================
// CreateObjectAction — Undo/Redo for creating a single object or group
// =====================================================================
class CreateObjectAction : public UndoAction
{
public:
	CreateObjectAction(SceneManager* scene,
		const std::vector<GameObject*>& createdObjects,
		const std::string& desc);
	~CreateObjectAction();

	void Undo() override;
	void Redo() override;
	std::string GetDescription() const override { return description; }

private:
	SceneManager* scene;
	std::string description;

	struct CreatedEntry {
		GameObject* object;
		int index; // Captured after creation
	};
	std::vector<CreatedEntry> entries;
	bool ownsObjects; // True when objects are NOT in the scene
};

// =====================================================================
// DeletedLightEntry — Everything needed to restore a deleted light
// =====================================================================
struct DeletedLightEntry {
	LightObject* light;
	int originalIndex;
	LightType type;
	std::string name;
	glm::vec3 color;
	float ambientIntensity;
	float diffuseIntensity;
	glm::vec3 position;
	glm::vec3 direction;
	float constant, linear, exponent;
	float edge;
};

// =====================================================================
// DeleteLightsAction — Undo/Redo for deleting lights
// =====================================================================
class DeleteLightsAction : public UndoAction
{
public:
	DeleteLightsAction(SceneManager* scene,
		const std::vector<DeletedLightEntry>& entries,
		const std::vector<int>& previousSelection);
	~DeleteLightsAction();

	void Undo() override;
	void Redo() override;
	std::string GetDescription() const override;

private:
	SceneManager* scene;
	std::vector<DeletedLightEntry> deletedEntries;
	std::vector<int> previousSelection;
	bool ownsLights;
};

// =====================================================================
// CreateLightAction — Undo/Redo for creating a light
// =====================================================================
class CreateLightAction : public UndoAction
{
public:
	CreateLightAction(SceneManager* scene, LightType type,
		const std::string& desc);
	~CreateLightAction() = default;

	void Undo() override;
	void Redo() override;
	std::string GetDescription() const override { return description; }

	// Set after creation so we know what to remove on undo
	void SetCreatedIndex(int idx) { createdIndex = idx; }

private:
	SceneManager* scene;
	std::string description;
	LightType lightType;
	int createdIndex = -1;

	// Snapshot for redo (captured on first undo)
	DeletedLightEntry snapshot;
	bool hasSnapshot = false;
};

// =====================================================================
// ReparentAction — Undo/Redo for hierarchy drag-drop reparenting
// =====================================================================
class ReparentAction : public UndoAction
{
public:
	ReparentAction(GameObject* object, GameObject* oldParent, GameObject* newParent);

	void Undo() override;
	void Redo() override;
	std::string GetDescription() const override;

private:
	GameObject* object;
	GameObject* oldParent;
	GameObject* newParent;
	// Store local transforms so SetParent's world-position preservation works correctly on undo
	glm::vec3 oldLocalPos, oldLocalRot, oldLocalScale;
	glm::vec3 newLocalPos, newLocalRot, newLocalScale;
};

// =====================================================================
// InspectorObjectAction — Generic Undo/Redo for ANY object property
//   Uses JSON snapshots via SceneSerializer::SnapshotObject/RestoreObject
// =====================================================================
class InspectorObjectAction : public UndoAction
{
public:
	InspectorObjectAction(SceneManager* scene, int objectIndex,
		const std::string& beforeJson, const std::string& afterJson,
		const std::string& desc = "Inspector Change");

	void Undo() override;
	void Redo() override;
	std::string GetDescription() const override { return description; }

private:
	SceneManager* scene;
	int objectIndex;
	std::string beforeJson;
	std::string afterJson;
	std::string description;
};

// =====================================================================
// InspectorLightAction — Generic Undo/Redo for ANY light property
//   Uses JSON snapshots via SceneSerializer::SnapshotLight/RestoreLight
// =====================================================================
class InspectorLightAction : public UndoAction
{
public:
	InspectorLightAction(SceneManager* scene, int lightIndex,
		const std::string& beforeJson, const std::string& afterJson,
		const std::string& desc = "Light Change");

	void Undo() override;
	void Redo() override;
	std::string GetDescription() const override { return description; }

private:
	SceneManager* scene;
	int lightIndex;
	std::string beforeJson;
	std::string afterJson;
	std::string description;
};

