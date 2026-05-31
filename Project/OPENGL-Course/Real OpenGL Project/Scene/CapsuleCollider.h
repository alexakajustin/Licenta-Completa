#pragma once
#include "Component.h"
#include <glm/glm.hpp>

class CapsuleCollider : public Component {
public:
    float height = 1.7f;
    float radius = 0.5f;

    CapsuleCollider(GameObject* owner);
    virtual ~CapsuleCollider() = default;

    std::string GetName() const override { return "CapsuleCollider"; }
    void DrawInspector() override;
};
