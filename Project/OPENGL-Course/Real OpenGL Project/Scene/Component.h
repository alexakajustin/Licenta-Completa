#pragma once
#include <string>

class GameObject;

class Component {
protected:
    GameObject* gameObject;

public:
    Component(GameObject* owner) : gameObject(owner) {}
    virtual ~Component() = default;

    virtual void Start() {}
    virtual void Update(float deltaTime) {}
    virtual void DrawInspector() {}

    GameObject* GetGameObject() const { return gameObject; }
    void SetGameObject(GameObject* owner) { gameObject = owner; }
    virtual std::string GetName() const = 0;
};
