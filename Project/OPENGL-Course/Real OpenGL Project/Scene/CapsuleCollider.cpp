#include "CapsuleCollider.h"
#include <imgui.h>
#include "Editor/ScriptReference.h"

CapsuleCollider::CapsuleCollider(GameObject* owner) : Component(owner)
{
}

void CapsuleCollider::DrawInspector()
{
    bool headerOpen = ImGui::CollapsingHeader("Capsule Collider", ImGuiTreeNodeFlags_DefaultOpen);
    ScriptReference::ComponentDragSource(this);
    if (headerOpen)
    {
        ImGui::DragFloat("Height", &height, 0.1f, 0.1f, 100.0f);
        ImGui::DragFloat("Radius", &radius, 0.05f, 0.05f, 50.0f);
    }
}
