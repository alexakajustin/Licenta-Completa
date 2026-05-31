#include "BoxCollider.h"
#include "GameObject.h"
#include <imgui.h>

BoxCollider::BoxCollider(GameObject* owner) : Component(owner)
{
}

void BoxCollider::DrawInspector()
{
    ImGui::PushID(this);
    if (ImGui::CollapsingHeader("Box Collider", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::DragFloat3("Size", &size[0], 0.1f);
        ImGui::DragFloat3("Offset", &offset[0], 0.1f);
        ImGui::Checkbox("Is Trigger", &isTrigger);
    }
    ImGui::PopID();
}
