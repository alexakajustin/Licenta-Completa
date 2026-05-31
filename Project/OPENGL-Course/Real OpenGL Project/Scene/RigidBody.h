#pragma once

#include "Scene/Component.h"
#include "Simulation/JoltCommon.h"
#include <Jolt/Physics/Body/BodyID.h>
#include <glm/glm.hpp>

class RigidBody : public Component {
public:
    enum class BodyType {
        Static,
        Kinematic,
        Dynamic
    };

    RigidBody(GameObject* owner);
    virtual ~RigidBody();

    virtual void Start() override;
    virtual void Update(float deltaTime) override;
    virtual void DrawInspector() override;
    std::string GetName() const override { return "RigidBody"; }

    // Getters for serialization
    BodyType GetType() const { return mType; }
    float GetMass() const { return mMass; }
    float GetFriction() const { return mFriction; }
    float GetRestitution() const { return mRestitution; }
    bool GetLockRotation() const { return mLockRotation; }

    void RecreateBody();

    // Properties
    void SetType(BodyType type);
    void SetMass(float mass);
    void SetFriction(float friction);
    void SetRestitution(float restitution);
    void SetLinearVelocity(const glm::vec3& velocity);
    void SetLockRotation(bool lock);
    void AddForce(const glm::vec3& force);
    void AddImpulse(const glm::vec3& impulse);

    JPH::BodyID GetBodyID() const { return mBodyID; }

private:
    void CreateBody();
    void DestroyBody();

    BodyType mType = BodyType::Dynamic;
    float mMass = 1.0f;
    float mFriction = 0.2f;
    float mRestitution = 0.0f;
    bool mLockRotation = false;

    JPH::BodyID mBodyID;
    bool mIsInitialized = false;
};
