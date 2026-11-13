#include "BoxCollider.h"
#include "GameObject.h"
#include <imgui.h>
#include "Editor/ScriptReference.h"

BoxCollider::BoxCollider(GameObject* owner) : Component(owner)
{
}

void BoxCollider::DrawInspector()
{
    ImGui::PushID(this);
    bool headerOpen = ImGui::CollapsingHeader("Box Collider", ImGuiTreeNodeFlags_DefaultOpen);
    ScriptReference::ComponentDragSource(this);
    if (headerOpen)
    {
        ImGui::DragFloat3("Size", &size[0], 0.1f);
        ImGui::DragFloat3("Offset", &offset[0], 0.1f);
        ImGui::Checkbox("Is Trigger", &isTrigger);
    }
    ImGui::PopID();
}
