#pragma once

#include "Simulation/JoltCommon.h"
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

#include <glm/glm.hpp>
#include <memory>

// Layer declarations
namespace Layers {
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING = 1;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}

namespace BroadPhaseLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr JPH::uint NUM_LAYERS(2);
}

// Convert GLM to Jolt
inline JPH::Vec3 ToJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
inline glm::vec3 ToGlm(const JPH::Vec3& v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }

class PhysicsSystem {
public:
    static PhysicsSystem& GetInstance() {
        static PhysicsSystem instance;
        return instance;
    }

    void Init();
    void Shutdown();
    void Update(float deltaTime);

    JPH::PhysicsSystem* GetJoltSystem() { return mPhysicsSystem.get(); }
    JPH::BodyInterface& GetBodyInterface() { return mPhysicsSystem->GetBodyInterface(); }

private:
    PhysicsSystem() = default;
    ~PhysicsSystem() = default;

    std::unique_ptr<JPH::PhysicsSystem> mPhysicsSystem;
    std::unique_ptr<JPH::TempAllocatorImpl> mTempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> mJobSystem;
    
    // We must hold pointers to the interfaces to keep them alive
    void* mBPLayerInterface = nullptr;
    void* mObjectVsBPLayerFilter = nullptr;
    void* mObjectLayerPairFilter = nullptr;

    bool mInitialized = false;
};
