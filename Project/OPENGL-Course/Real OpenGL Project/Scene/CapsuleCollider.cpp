#include "CapsuleCollider.h"
#include <imgui.h>

CapsuleCollider::CapsuleCollider(GameObject* owner) : Component(owner)
{
}

void CapsuleCollider::DrawInspector()
{
    if (ImGui::CollapsingHeader("Capsule Collider", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::DragFloat("Height", &height, 0.1f, 0.1f, 100.0f);
        ImGui::DragFloat("Radius", &radius, 0.05f, 0.05f, 50.0f);
    }
}
