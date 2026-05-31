#include "MeshCollider.h"
#include <imgui.h>

MeshCollider::MeshCollider(GameObject* owner) : Component(owner)
{
}

void MeshCollider::DrawInspector()
{
    if (ImGui::CollapsingHeader("Mesh Collider", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextWrapped("MeshCollider uses the cpuMeshData of the GameObject to perform accurate collision queries.");
    }
}
