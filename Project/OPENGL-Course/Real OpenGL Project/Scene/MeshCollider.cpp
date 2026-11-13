#include "MeshCollider.h"
#include <imgui.h>
#include "Editor/ScriptReference.h"

MeshCollider::MeshCollider(GameObject* owner) : Component(owner)
{
}

void MeshCollider::DrawInspector()
{
    bool headerOpen = ImGui::CollapsingHeader("Mesh Collider", ImGuiTreeNodeFlags_DefaultOpen);
    ScriptReference::ComponentDragSource(this);
    if (headerOpen)
    {
        ImGui::TextWrapped("MeshCollider uses the cpuMeshData of the GameObject to perform accurate collision queries.");
    }
}
