#include "PhysicsSystem.h"
#include <iostream>
#include <cstdarg>

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

JPH_SUPPRESS_WARNINGS

static void TraceImpl(const char* inFMT, ...) {
    va_list list;
    va_start(list, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, list);
    va_end(list);
    std::cout << "[Jolt] " << buffer << std::endl;
}

#ifdef JPH_ENABLE_ASSERTS
static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, JPH::uint inLine) {
    std::cerr << "[Jolt Assert] " << inFile << ":" << inLine << " (" << inExpression << ") " << (inMessage ? inMessage : "") << std::endl;
    return true; // Break
}
#endif // JPH_ENABLE_ASSERTS

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
    }
    virtual JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
        return mObjectToBroadPhase[inLayer];
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        switch ((JPH::BroadPhaseLayer::Type)inLayer) {
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING: return "MOVING";
            default: JPH_ASSERT(false); return "INVALID";
        }
    }
#endif
private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
            case Layers::NON_MOVING: return inLayer2 == BroadPhaseLayers::MOVING;
            case Layers::MOVING: return true;
            default: JPH_ASSERT(false); return false;
        }
    }
};

class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
        switch (inObject1) {
            case Layers::NON_MOVING: return inObject2 == Layers::MOVING;
            case Layers::MOVING: return true;
            default: JPH_ASSERT(false); return false;
        }
    }
};

void PhysicsSystem::Init() {
    if (mInitialized) return;

    JPH::RegisterDefaultAllocator();

    JPH::Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)

    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    mTempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
    mJobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

    mBPLayerInterface = new BPLayerInterfaceImpl();
    mObjectVsBPLayerFilter = new ObjectVsBroadPhaseLayerFilterImpl();
    mObjectLayerPairFilter = new ObjectLayerPairFilterImpl();

    const JPH::uint cMaxBodies = 10240;
    const JPH::uint cNumBodyMutexes = 0;
    const JPH::uint cMaxBodyPairs = 10240;
    const JPH::uint cMaxContactConstraints = 10240;

    mPhysicsSystem = std::make_unique<JPH::PhysicsSystem>();
    mPhysicsSystem->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints,
        *static_cast<BPLayerInterfaceImpl*>(mBPLayerInterface),
        *static_cast<ObjectVsBroadPhaseLayerFilterImpl*>(mObjectVsBPLayerFilter),
        *static_cast<ObjectLayerPairFilterImpl*>(mObjectLayerPairFilter));

    mInitialized = true;
    std::cout << "Jolt Physics Initialized." << std::endl;
}

void PhysicsSystem::Shutdown() {
    if (!mInitialized) return;

    mPhysicsSystem.reset();
    delete static_cast<ObjectLayerPairFilterImpl*>(mObjectLayerPairFilter);
    delete static_cast<ObjectVsBroadPhaseLayerFilterImpl*>(mObjectVsBPLayerFilter);
    delete static_cast<BPLayerInterfaceImpl*>(mBPLayerInterface);
    
    mJobSystem.reset();
    mTempAllocator.reset();
    
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    mInitialized = false;
}

void PhysicsSystem::Update(float deltaTime) {
    if (!mInitialized || deltaTime <= 0.0f) return;

    // We simulate using 1 step. You might want to use a fixed time step logic here.
    const int cCollisionSteps = 1;
    mPhysicsSystem->Update(deltaTime, cCollisionSteps, mTempAllocator.get(), mJobSystem.get());
}
