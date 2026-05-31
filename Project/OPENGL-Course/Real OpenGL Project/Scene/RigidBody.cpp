#include "RigidBody.h"
#include "GameObject.h"
#include "BoxCollider.h"
#include "CapsuleCollider.h"
#include "Simulation/PhysicsSystem.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include "Scene/MeshCollider.h"
#include <imgui.h>
#include <glm/gtc/quaternion.hpp>

RigidBody::RigidBody(GameObject* owner) : Component(owner) {
}

RigidBody::~RigidBody() {
    DestroyBody();
}

void RigidBody::Start() {
    CreateBody();
}

void RigidBody::Update(float deltaTime) {
    if (!mIsInitialized || mType == BodyType::Static) return;

    auto& bodyInterface = PhysicsSystem::GetInstance().GetBodyInterface();
    JPH::Vec3 position = bodyInterface.GetPosition(mBodyID);
    JPH::Quat rotation = bodyInterface.GetRotation(mBodyID);

    glm::vec3 glmPos = ToGlm(position);
    
    glm::quat glmRot(rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ());
    glm::mat4 rotMat = glm::mat4_cast(glmRot);
    glm::mat4 modelMat = glm::translate(glm::mat4(1.0f), glmPos) * rotMat;
    modelMat = glm::scale(modelMat, gameObject->GetTransform().GetScale());
    
    gameObject->GetTransform().SetFromMatrix(modelMat);
}

void RigidBody::CreateBody() {
    if (mIsInitialized) return;

    auto physics = PhysicsSystem::GetInstance().GetJoltSystem();
    if (!physics) return;

    auto& bodyInterface = physics->GetBodyInterface();

    JPH::ShapeRefC shape;

    BoxCollider* box = gameObject->GetComponent<BoxCollider>();
    CapsuleCollider* capsule = gameObject->GetComponent<CapsuleCollider>();
    MeshCollider* mesh = gameObject->GetComponent<MeshCollider>();

    std::cout << "[Physics] CreateBody for: " << gameObject->GetName() << " (box: " << (box != nullptr) << ", capsule: " << (capsule != nullptr) << ", mesh: " << (mesh != nullptr) << ")" << std::endl;

    if (box) {
        glm::vec3 halfExtents = box->size * gameObject->GetTransform().GetScale() * 0.5f;
        shape = new JPH::BoxShape(ToJolt(halfExtents));
    } else if (capsule) {
        // Jolt capsule is along Y-axis, half height is the cylindrical part's half height
        float radius = capsule->radius * gameObject->GetTransform().GetScale().x;
        float height = capsule->height * gameObject->GetTransform().GetScale().y;
        float halfHeight = std::max(0.01f, (height * 0.5f) - radius);
        shape = new JPH::CapsuleShape(halfHeight, radius);
    } else if (mesh) {
        std::string prim = gameObject->GetPrimitiveType();
        glm::vec3 scale = gameObject->GetTransform().GetScale();

        if (prim == "Plane") {
            // Plane primitive goes from -1 to 1 in X and Z.
            shape = new JPH::BoxShape(ToJolt(glm::vec3(1.0f * scale.x, 0.05f, 1.0f * scale.z)));
            std::cout << "[Physics] Created Plane BoxShape for " << gameObject->GetName() << std::endl;
        } else if (prim == "Cube") {
            shape = new JPH::BoxShape(ToJolt(glm::vec3(0.5f * scale.x, 0.5f * scale.y, 0.5f * scale.z)));
            std::cout << "[Physics] Created Cube BoxShape for " << gameObject->GetName() << std::endl;
        } else if (prim == "Sphere") {
            shape = new JPH::SphereShape(1.0f * scale.x);
            std::cout << "[Physics] Created Sphere SphereShape for " << gameObject->GetName() << " with radius " << (1.0f * scale.x) << std::endl;
        } else {
            // Custom mesh or procedural object: check CPU mesh data
            const MeshData& md = gameObject->GetCPUMeshData();
            int triCount = md.GetTriangleCount();
            std::cout << "[Physics] MeshCollider on " << gameObject->GetName() << " has triCount: " << triCount << std::endl;
            if (triCount > 0) {
                if (mType == BodyType::Dynamic || mType == BodyType::Kinematic) {
                    // ConvexHullShape for dynamic meshes to support motion and inertia calculations in Jolt
                    std::vector<JPH::Vec3> points;
                    int vertexCount = md.GetVertexCount();
                    
                    // Jolt ConvexHullShape has a strict limit of 256 vertices on the final hull.
                    // If the input point cloud is too large, settings.Create() will fail.
                    // We stride downsample to keep the input points <= 128, guaranteeing success.
                    int stride = 1;
                    if (vertexCount > 128) {
                        stride = (vertexCount / 128) + 1;
                    }
                    
                    points.reserve(vertexCount / stride + 1);
                    for (int i = 0; i < vertexCount; i += stride) {
                        glm::vec3 v = md.GetPosition(i) * scale;
                        points.push_back(JPH::Vec3(v.x, v.y, v.z));
                    }
                    JPH::ConvexHullShapeSettings settings(points.data(), (int)points.size());
                    JPH::ShapeSettings::ShapeResult result = settings.Create();
                    if (result.IsValid()) {
                        shape = result.Get();
                        std::cout << "[Physics] Created JPH::ConvexHullShape for dynamic custom mesh " << gameObject->GetName() << std::endl;
                    } else {
                        shape = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
                        std::cout << "[Physics] Failed to create ConvexHullShape fallback to BoxShape for " << gameObject->GetName() << std::endl;
                    }
                } else {
                    // MeshShape for static concave geometry (e.g. terrains)
                    JPH::TriangleList triangles;
                    triangles.reserve(triCount);
                    for (int i = 0; i < triCount; ++i) {
                        glm::vec3 v0, v1, v2;
                        md.GetTriangle(i, v0, v1, v2);
                        v0 *= scale; v1 *= scale; v2 *= scale;
                        triangles.push_back(JPH::Triangle(JPH::Float3(v0.x, v0.y, v0.z), JPH::Float3(v1.x, v1.y, v1.z), JPH::Float3(v2.x, v2.y, v2.z)));
                    }
                    JPH::MeshShapeSettings meshSettings(triangles);
                    JPH::ShapeSettings::ShapeResult result = meshSettings.Create();
                    if (result.IsValid()) {
                        shape = result.Get();
                        std::cout << "[Physics] Created JPH::MeshShape for static custom mesh " << gameObject->GetName() << std::endl;
                    } else {
                        shape = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
                        std::cout << "[Physics] Failed to create MeshShape fallback to BoxShape for " << gameObject->GetName() << std::endl;
                    }
                }
            } else {
                shape = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
                std::cout << "[Physics] Empty MeshCollider fallback to BoxShape for " << gameObject->GetName() << std::endl;
            }
        }
    } else {
        // Fallback to a 1x1x1 box
        shape = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
        std::cout << "[Physics] No collider fallback to BoxShape for " << gameObject->GetName() << std::endl;
    }

    JPH::EMotionType motionType;
    JPH::ObjectLayer layer;

    switch (mType) {
        case BodyType::Static:
            motionType = JPH::EMotionType::Static;
            layer = Layers::NON_MOVING;
            break;
        case BodyType::Kinematic:
            motionType = JPH::EMotionType::Kinematic;
            layer = Layers::MOVING;
            break;
        case BodyType::Dynamic:
        default:
            motionType = JPH::EMotionType::Dynamic;
            layer = Layers::MOVING;
            break;
    }

    glm::vec3 pos = gameObject->GetTransform().GetPosition();
    // Assuming rotation is Euler angles in Transform
    glm::vec3 euler = gameObject->GetTransform().GetRotation();
    JPH::Quat rot = JPH::Quat::sEulerAngles(JPH::Vec3(glm::radians(euler.x), glm::radians(euler.y), glm::radians(euler.z)));

    JPH::BodyCreationSettings bodySettings(shape, ToJolt(pos), rot, motionType, layer);
    
    if (mType == BodyType::Dynamic) {
        bodySettings.mMassPropertiesOverride.mMass = mMass;
        bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        if (mLockRotation) {
            bodySettings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
        }
    }

    bodySettings.mFriction = mFriction;
    bodySettings.mRestitution = mRestitution;

    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    if (body) {
        mBodyID = body->GetID();
        bodyInterface.AddBody(mBodyID, mType == BodyType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        mIsInitialized = true;
        std::cout << "[Physics] Successfully created Jolt body with ID " << mBodyID.GetIndex() << " for: " << gameObject->GetName() << " (motionType: " << (int)motionType << ")" << std::endl;
    } else {
        std::cout << "[Physics] FAILED to create Jolt body for: " << gameObject->GetName() << std::endl;
    }
}

void RigidBody::DestroyBody() {
    if (!mIsInitialized) return;

    auto physics = PhysicsSystem::GetInstance().GetJoltSystem();
    if (physics) {
        auto& bodyInterface = physics->GetBodyInterface();
        bodyInterface.RemoveBody(mBodyID);
        bodyInterface.DestroyBody(mBodyID);
    }
    mIsInitialized = false;
}

void RigidBody::SetType(BodyType type) {
    if (mType == type) return;
    mType = type;
    if (mIsInitialized) {
        DestroyBody();
        CreateBody();
    }
}

void RigidBody::SetMass(float mass) {
    mMass = mass;
    if (mIsInitialized && mType == BodyType::Dynamic) {
        DestroyBody();
        CreateBody();
    }
}

void RigidBody::SetFriction(float friction) {
    mFriction = friction;
    if (mIsInitialized) {
        auto& bodyInterface = PhysicsSystem::GetInstance().GetBodyInterface();
        bodyInterface.SetFriction(mBodyID, friction);
    }
}

void RigidBody::SetRestitution(float restitution) {
    mRestitution = restitution;
    if (mIsInitialized) {
        auto& bodyInterface = PhysicsSystem::GetInstance().GetBodyInterface();
        bodyInterface.SetRestitution(mBodyID, restitution);
    }
}

void RigidBody::SetLinearVelocity(const glm::vec3& velocity) {
    if (mIsInitialized && mType != BodyType::Static) {
        auto& bodyInterface = PhysicsSystem::GetInstance().GetBodyInterface();
        bodyInterface.SetLinearVelocity(mBodyID, ToJolt(velocity));
    }
}

void RigidBody::SetLockRotation(bool lock) {
    mLockRotation = lock;
    if (mIsInitialized && mType == BodyType::Dynamic) {
        DestroyBody();
        CreateBody();
    }
}

void RigidBody::AddForce(const glm::vec3& force) {
    if (mIsInitialized && mType == BodyType::Dynamic) {
        auto& bodyInterface = PhysicsSystem::GetInstance().GetBodyInterface();
        bodyInterface.AddForce(mBodyID, ToJolt(force));
    }
}

void RigidBody::AddImpulse(const glm::vec3& impulse) {
    if (mIsInitialized && mType == BodyType::Dynamic) {
        auto& bodyInterface = PhysicsSystem::GetInstance().GetBodyInterface();
        bodyInterface.AddImpulse(mBodyID, ToJolt(impulse));
    }
}

void RigidBody::DrawInspector() {
    if (ImGui::CollapsingHeader("RigidBody", ImGuiTreeNodeFlags_DefaultOpen)) {
        int currentType = static_cast<int>(mType);
        const char* types[] = { "Static", "Kinematic", "Dynamic" };
        if (ImGui::Combo("Body Type", &currentType, types, IM_ARRAYSIZE(types))) {
            SetType(static_cast<BodyType>(currentType));
        }

        if (mType == BodyType::Dynamic) {
            float mass = mMass;
            if (ImGui::DragFloat("Mass", &mass, 0.1f, 0.01f, 1000.0f)) SetMass(mass);
            
            bool lockRot = mLockRotation;
            if (ImGui::Checkbox("Lock Rotation", &lockRot)) SetLockRotation(lockRot);
        }

        float friction = mFriction;
        if (ImGui::DragFloat("Friction", &friction, 0.05f, 0.0f, 1.0f)) SetFriction(friction);

        float restitution = mRestitution;
        if (ImGui::DragFloat("Restitution", &restitution, 0.05f, 0.0f, 1.0f)) SetRestitution(restitution);
    }
}

void RigidBody::RecreateBody() {
    DestroyBody();
    CreateBody();
}
