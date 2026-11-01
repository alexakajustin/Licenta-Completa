#include "Nodes/InfiniteRepeaterNode.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "Scene/WorldStreamerComponent.h"

InfiniteRepeaterNode::InfiniteRepeaterNode(NodeGraph& graph) : GraphNode(graph)
{
	paramPins = false;
	AddInt("radius", "Radius", &radius, 1.0f, 0, 50).Tip("Tiles streamed around the camera in each direction");
	AddObjects("targetName", "Target", &target, true).Tip("The object that streams (a manager object is created when none is set)");
	AddObjects("referenceName", "Tile size from", &reference, true).Tip("The object whose size sets the tile spacing");
}

void InfiniteRepeaterNode::Execute(NodeContext& ctx)
{
	GameObject* obj = target.empty() ? nullptr : ctx.scene.FindObject(target[0]);
	if (!obj) obj = ctx.Object("WorldStreamerManager");
	GameObject* ref = reference.empty() ? nullptr : ctx.scene.FindObject(reference[0]);

	if (auto* comp = obj->GetComponent<WorldStreamerComponent>()) {
		comp->SetReferenceObject(ref);
		comp->SetRadius(radius);
	} else {
		obj->AddComponent<WorldStreamerComponent>(&ctx.scene, ref, radius);
	}
}
