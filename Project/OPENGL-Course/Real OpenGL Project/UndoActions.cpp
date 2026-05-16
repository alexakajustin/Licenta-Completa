#include "UndoActions.h"
#include "SceneManager.h"
#include <algorithm>
#include <cstdio>

// =====================================================================
// TransformAction
// =====================================================================

TransformAction::TransformAction(const std::string& desc,
	const std::vector<TransformSnapshot>& before,
	const std::vector<TransformSnapshot>& after)
	: description(desc), beforeState(before), afterState(after)
{
}

void TransformAction::Undo()
{
	for (const auto& snap : beforeState) {
		if (!snap.object) continue;
		snap.object->GetTransform().SetPosition(snap.position);
		snap.object->GetTransform().SetRotation(snap.rotation);
		snap.object->GetTransform().SetScale(snap.scale);
		snap.object->SetDirty();
	}
}

void TransformAction::Redo()
{
	for (const auto& snap : afterState) {
		if (!snap.object) continue;
		snap.object->GetTransform().SetPosition(snap.position);
		snap.object->GetTransform().SetRotation(snap.rotation);
		snap.object->GetTransform().SetScale(snap.scale);
		snap.object->SetDirty();
	}
}

// =====================================================================
// LightTransformAction
// =====================================================================

LightTransformAction::LightTransformAction(const std::string& desc,
	const std::vector<LightTransformSnapshot>& before,
	const std::vector<LightTransformSnapshot>& after)
	: description(desc), beforeState(before), afterState(after)
{
}

void LightTransformAction::Undo()
{
	for (const auto& snap : beforeState) {
		if (!snap.light) continue;
		snap.light->SetPosition(snap.position);
	}
}

void LightTransformAction::Redo()
{
	for (const auto& snap : afterState) {
		if (!snap.light) continue;
		snap.light->SetPosition(snap.position);
	}
}

// =====================================================================
// DeleteObjectsAction
// =====================================================================

DeleteObjectsAction::DeleteObjectsAction(SceneManager* scene,
	const std::vector<DeletedObjectEntry>& entries,
	const std::vector<int>& previousSelection)
	: scene(scene), deletedEntries(entries), previousSelection(previousSelection), ownsObjects(true)
{
	// Sort entries by original index in descending order for correct re-insertion
	std::sort(deletedEntries.begin(), deletedEntries.end(),
		[](const DeletedObjectEntry& a, const DeletedObjectEntry& b) {
			return a.originalIndex > b.originalIndex;
		});
}

DeleteObjectsAction::~DeleteObjectsAction()
{
	// If we still own the objects (they are not in the scene), delete them
	if (ownsObjects) {
		for (auto& entry : deletedEntries) {
			if (entry.object) {
				delete entry.object;
				entry.object = nullptr;
			}
		}
	}
}

void DeleteObjectsAction::Undo()
{
	// Re-insert objects at their original indices (ascending order)
	// We sorted descending in constructor, so iterate in reverse for insertion
	for (int i = (int)deletedEntries.size() - 1; i >= 0; i--) {
		auto& entry = deletedEntries[i];
		if (!entry.object) continue;

		scene->InsertObjectAt(entry.object, entry.originalIndex);
	}

	// Restore parent-child relationships
	for (auto& entry : deletedEntries) {
		if (!entry.object) continue;
		if (entry.parent) {
			entry.parent->AddChild(entry.object);
		}
	}

	// Restore selection
	scene->ClearSelection();
	for (int idx : previousSelection) {
		if (idx >= 0 && idx < (int)scene->GetObjects().size()) {
			scene->SetSelectedIndex(idx, true);
		}
	}

	ownsObjects = false;
	printf("[Undo] Restored %d deleted objects\n", (int)deletedEntries.size());
}

void DeleteObjectsAction::Redo()
{
	// Re-delete the objects (by removing from scene without freeing memory)
	// Process in descending index order so indices don't shift
	for (auto& entry : deletedEntries) {
		if (!entry.object) continue;

		// Find current index
		auto& objects = scene->GetObjects();
		auto it = std::find(objects.begin(), objects.end(), entry.object);
		if (it != objects.end()) {
			int idx = (int)(it - objects.begin());
			scene->RemoveObjectRaw(idx);
		}
	}

	scene->ClearSelection();
	ownsObjects = true;
	printf("[Redo] Re-deleted %d objects\n", (int)deletedEntries.size());
}

std::string DeleteObjectsAction::GetDescription() const
{
	if (deletedEntries.size() == 1 && deletedEntries[0].object) {
		return "Delete " + deletedEntries[0].name;
	}
	return "Delete " + std::to_string(deletedEntries.size()) + " objects";
}

// =====================================================================
// CreateObjectAction
// =====================================================================

CreateObjectAction::CreateObjectAction(SceneManager* scene,
	const std::vector<GameObject*>& createdObjects,
	const std::string& desc)
	: scene(scene), description(desc), ownsObjects(false)
{
	for (auto* obj : createdObjects) {
		auto& objects = scene->GetObjects();
		auto it = std::find(objects.begin(), objects.end(), obj);
		int idx = (it != objects.end()) ? (int)(it - objects.begin()) : -1;
		entries.push_back({ obj, idx });
	}
}

CreateObjectAction::~CreateObjectAction()
{
	if (ownsObjects) {
		for (auto& entry : entries) {
			if (entry.object) {
				delete entry.object;
				entry.object = nullptr;
			}
		}
	}
}

void CreateObjectAction::Undo()
{
	// Remove created objects from scene (in reverse order to preserve indices)
	for (int i = (int)entries.size() - 1; i >= 0; i--) {
		auto& entry = entries[i];
		if (!entry.object) continue;

		auto& objects = scene->GetObjects();
		auto it = std::find(objects.begin(), objects.end(), entry.object);
		if (it != objects.end()) {
			int idx = (int)(it - objects.begin());
			scene->RemoveObjectRaw(idx);
		}
	}

	scene->ClearSelection();
	ownsObjects = true;
	printf("[Undo] Removed %d created objects\n", (int)entries.size());
}

void CreateObjectAction::Redo()
{
	// Re-insert objects at their original indices
	for (auto& entry : entries) {
		if (!entry.object) continue;
		scene->InsertObjectAt(entry.object, entry.index);
	}

	// Select the re-created objects
	scene->ClearSelection();
	if (!entries.empty()) {
		scene->SetSelectedIndex(entries.back().index);
	}

	ownsObjects = false;
	printf("[Redo] Re-created %d objects\n", (int)entries.size());
}

// =====================================================================
// DeleteLightsAction
// =====================================================================

DeleteLightsAction::DeleteLightsAction(SceneManager* scene,
	const std::vector<DeletedLightEntry>& entries,
	const std::vector<int>& previousSelection)
	: scene(scene), deletedEntries(entries), previousSelection(previousSelection), ownsLights(true)
{
}

DeleteLightsAction::~DeleteLightsAction()
{
	// Light memory is managed differently — the LightObject wrapper is owned by us
	// but the underlying PointLight/SpotLight lives in the global arrays.
	// We only delete the LightObject wrapper when we own it.
	if (ownsLights) {
		for (auto& entry : deletedEntries) {
			// Don't delete — the light data lives in global arrays managed by SceneManager
			// The LightObject was already deleted during the original delete operation
		}
	}
}

void DeleteLightsAction::Undo()
{
	// Re-create lights using SceneManager's CreateLight, then restore properties
	for (auto& entry : deletedEntries) {
		scene->CreateLight(entry.type, entry.position);
		LightObject* lo = scene->GetLights().back();
		lo->SetName(entry.name);
		*lo->GetColorPtr() = entry.color;
		*lo->GetAmbientIntensityPtr() = entry.ambientIntensity;
		*lo->GetDiffuseIntensityPtr() = entry.diffuseIntensity;
		if (lo->GetDirectionPtr()) *lo->GetDirectionPtr() = entry.direction;
		if (lo->GetConstantPtr()) *lo->GetConstantPtr() = entry.constant;
		if (lo->GetLinearPtr()) *lo->GetLinearPtr() = entry.linear;
		if (lo->GetExponentPtr()) *lo->GetExponentPtr() = entry.exponent;
		if (entry.type == LightType::Spot && lo->GetSpotEdgePtr()) 
			*lo->GetSpotEdgePtr() = entry.edge;
	}

	ownsLights = false;
	printf("[Undo] Restored %d deleted lights\n", (int)deletedEntries.size());
}

void DeleteLightsAction::Redo()
{
	// Re-delete: find lights by pointer (stable, unlike name-based lookup)
	for (int i = (int)deletedEntries.size() - 1; i >= 0; i--) {
		auto& entry = deletedEntries[i];
		auto& lights = scene->GetLights();
		for (int j = (int)lights.size() - 1; j >= 0; j--) {
			if (lights[j] == entry.light) {
				scene->DeleteLight(j);
				break;
			}
		}
	}

	scene->ClearSelection();
	ownsLights = true;
	printf("[Redo] Re-deleted %d lights\n", (int)deletedEntries.size());
}

std::string DeleteLightsAction::GetDescription() const
{
	if (deletedEntries.size() == 1) {
		return "Delete " + deletedEntries[0].name;
	}
	return "Delete " + std::to_string(deletedEntries.size()) + " lights";
}

// =====================================================================
// CreateLightAction
// =====================================================================

CreateLightAction::CreateLightAction(SceneManager* scene, LightType type, const std::string& desc)
	: scene(scene), description(desc), lightType(type)
{
}

void CreateLightAction::Undo()
{
	// Snapshot current state before removing (for redo)
	if (!hasSnapshot && createdIndex >= 0 && createdIndex < (int)scene->GetLights().size()) {
		LightObject* lo = scene->GetLights()[createdIndex];
		snapshot.type = lo->GetLightType();
		snapshot.name = lo->GetName();
		snapshot.color = *lo->GetColorPtr();
		snapshot.ambientIntensity = *lo->GetAmbientIntensityPtr();
		snapshot.diffuseIntensity = *lo->GetDiffuseIntensityPtr();
		if (lo->GetPositionPtr()) snapshot.position = *lo->GetPositionPtr();
		if (lo->GetDirectionPtr()) snapshot.direction = *lo->GetDirectionPtr();
		if (lo->GetConstantPtr()) snapshot.constant = *lo->GetConstantPtr();
		if (lo->GetLinearPtr()) snapshot.linear = *lo->GetLinearPtr();
		if (lo->GetExponentPtr()) snapshot.exponent = *lo->GetExponentPtr();
		if (snapshot.type == LightType::Spot && lo->GetSpotEdgePtr())
			snapshot.edge = *lo->GetSpotEdgePtr();
		hasSnapshot = true;
	}

	// Delete the created light
	if (createdIndex >= 0 && createdIndex < (int)scene->GetLights().size()) {
		scene->DeleteLight(createdIndex);
	}
	scene->ClearSelection();
	printf("[Undo] Removed created light\n");
}

void CreateLightAction::Redo()
{
	if (!hasSnapshot) return;

	scene->CreateLight(snapshot.type, snapshot.position);
	LightObject* lo = scene->GetLights().back();
	lo->SetName(snapshot.name);
	*lo->GetColorPtr() = snapshot.color;
	*lo->GetAmbientIntensityPtr() = snapshot.ambientIntensity;
	*lo->GetDiffuseIntensityPtr() = snapshot.diffuseIntensity;
	if (lo->GetDirectionPtr()) *lo->GetDirectionPtr() = snapshot.direction;
	if (lo->GetConstantPtr()) *lo->GetConstantPtr() = snapshot.constant;
	if (lo->GetLinearPtr()) *lo->GetLinearPtr() = snapshot.linear;
	if (lo->GetExponentPtr()) *lo->GetExponentPtr() = snapshot.exponent;
	if (snapshot.type == LightType::Spot && lo->GetSpotEdgePtr())
		*lo->GetSpotEdgePtr() = snapshot.edge;

	createdIndex = (int)scene->GetLights().size() - 1;
	printf("[Redo] Re-created light\n");
}

// =====================================================================
// ReparentAction
// =====================================================================

ReparentAction::ReparentAction(GameObject* object, GameObject* oldParent, GameObject* newParent)
	: object(object), oldParent(oldParent), newParent(newParent)
{
	// Capture current local transform (this is the "old" local transform before reparenting)
	oldLocalPos = object->GetTransform().GetPosition();
	oldLocalRot = object->GetTransform().GetRotation();
	oldLocalScale = object->GetTransform().GetScale();
}

void ReparentAction::Undo()
{
	if (!object) return;

	// Capture the new local transform before undoing (for redo)
	newLocalPos = object->GetTransform().GetPosition();
	newLocalRot = object->GetTransform().GetRotation();
	newLocalScale = object->GetTransform().GetScale();

	// Remove from current parent
	if (newParent) newParent->RemoveChild(object);

	// Re-attach to old parent
	if (oldParent) {
		oldParent->AddChild(object);
	}

	// Restore old local transform
	object->GetTransform().SetPosition(oldLocalPos);
	object->GetTransform().SetRotation(oldLocalRot);
	object->GetTransform().SetScale(oldLocalScale);
	object->SetDirty();

	printf("[Undo] Reparented '%s' back to '%s'\n",
		object->GetName().c_str(),
		oldParent ? oldParent->GetName().c_str() : "Root");
}

void ReparentAction::Redo()
{
	if (!object) return;

	// Remove from current parent
	if (oldParent) oldParent->RemoveChild(object);

	// Re-attach to new parent
	if (newParent) {
		newParent->AddChild(object);
	}

	// Restore the new local transform
	object->GetTransform().SetPosition(newLocalPos);
	object->GetTransform().SetRotation(newLocalRot);
	object->GetTransform().SetScale(newLocalScale);
	object->SetDirty();

	printf("[Redo] Reparented '%s' to '%s'\n",
		object->GetName().c_str(),
		newParent ? newParent->GetName().c_str() : "Root");
}

std::string ReparentAction::GetDescription() const
{
	return "Reparent " + (object ? object->GetName() : "?");
}

// =====================================================================
// InspectorObjectAction
// =====================================================================

#include "SceneSerializer.h"

InspectorObjectAction::InspectorObjectAction(SceneManager* scene, GameObject* object,
	const std::string& beforeJson, const std::string& afterJson,
	const std::string& desc)
	: scene(scene), object(object), beforeJson(beforeJson), afterJson(afterJson), description(desc)
{
}

void InspectorObjectAction::Undo()
{
	if (!object) return;
	// Verify the object is still in the scene
	auto& objects = scene->GetObjects();
	if (std::find(objects.begin(), objects.end(), object) == objects.end()) return;

	SceneSerializer::RestoreObject(object, beforeJson, scene);
	printf("[Undo] %s\n", description.c_str());
}

void InspectorObjectAction::Redo()
{
	if (!object) return;
	auto& objects = scene->GetObjects();
	if (std::find(objects.begin(), objects.end(), object) == objects.end()) return;

	SceneSerializer::RestoreObject(object, afterJson, scene);
	printf("[Redo] %s\n", description.c_str());
}

// =====================================================================
// InspectorLightAction
// =====================================================================

InspectorLightAction::InspectorLightAction(SceneManager* scene, LightObject* light,
	const std::string& beforeJson, const std::string& afterJson,
	const std::string& desc)
	: scene(scene), light(light), beforeJson(beforeJson), afterJson(afterJson), description(desc)
{
}

void InspectorLightAction::Undo()
{
	if (!light) return;
	auto& lights = scene->GetLights();
	if (std::find(lights.begin(), lights.end(), light) == lights.end()) return;

	SceneSerializer::RestoreLight(light, beforeJson);
	printf("[Undo] %s\n", description.c_str());
}

void InspectorLightAction::Redo()
{
	if (!light) return;
	auto& lights = scene->GetLights();
	if (std::find(lights.begin(), lights.end(), light) == lights.end()) return;

	SceneSerializer::RestoreLight(light, afterJson);
	printf("[Redo] %s\n", description.c_str());
}

