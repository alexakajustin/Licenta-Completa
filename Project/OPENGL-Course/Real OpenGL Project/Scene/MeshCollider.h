#pragma once
#include "Component.h"

class MeshCollider : public Component {
public:
    MeshCollider(GameObject* owner);
    virtual ~MeshCollider() = default;

    std::string GetName() const override { return "MeshCollider"; }
    void DrawInspector() override;
};
