#pragma once
#include "Component.h"
#include <glm/glm.hpp>

class BoxCollider : public Component {
public:
    glm::vec3 size = glm::vec3(1.0f);
    glm::vec3 offset = glm::vec3(0.0f);
    bool isTrigger = false;

    BoxCollider(GameObject* owner);
    virtual ~BoxCollider() = default;

    std::string GetName() const override { return "BoxCollider"; }

    void DrawInspector() override;
};
