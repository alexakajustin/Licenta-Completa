#pragma once

#include <glm/glm.hpp>
#include <vector>

struct Plane {
    glm::vec3 normal = { 0.f, 1.f, 0.f };
    float distance = 0.f;

    Plane() = default;
    Plane(const glm::vec3& p1, const glm::vec3& norm)
        : normal(glm::normalize(norm)),
          distance(glm::dot(normal, p1))
    {}

    float getSignedDistanceToPlane(const glm::vec3& point) const {
        return glm::dot(normal, point) - distance;
    }
};

struct Frustum {
    Plane topFace;
    Plane bottomFace;
     Plane rightFace;
    Plane leftFace;
    Plane farFace;
    Plane nearFace;

    static Frustum CreateFrustumFromMatrix(const glm::mat4& viewProj) {
        Frustum frustum;
        glm::mat4 m = glm::transpose(viewProj);

        // Near Plane
        frustum.nearFace.normal = glm::vec3(m[3] + m[2]);
        frustum.nearFace.distance = -(m[3][3] + m[2][3]);

        // Far Plane
        frustum.farFace.normal = glm::vec3(m[3] - m[2]);
        frustum.farFace.distance = -(m[3][3] - m[2][3]);

        // Left Plane
        frustum.leftFace.normal = glm::vec3(m[3] + m[0]);
        frustum.leftFace.distance = -(m[3][3] + m[0][3]);

        // Right Plane
        frustum.rightFace.normal = glm::vec3(m[3] - m[0]);
        frustum.rightFace.distance = -(m[3][3] - m[0][3]);

        // Bottom Plane
        frustum.bottomFace.normal = glm::vec3(m[3] + m[1]);
        frustum.bottomFace.distance = -(m[3][3] + m[1][3]);

        // Top Plane
        frustum.topFace.normal = glm::vec3(m[3] - m[1]);
        frustum.topFace.distance = -(m[3][3] - m[1][3]);

        // Normalize
        auto normalizePlane = [](Plane& p) {
            float length = glm::length(p.normal);
            p.normal /= length;
            p.distance /= length;
        };

        normalizePlane(frustum.nearFace);
        normalizePlane(frustum.farFace);
        normalizePlane(frustum.leftFace);
        normalizePlane(frustum.rightFace);
        normalizePlane(frustum.bottomFace);
        normalizePlane(frustum.topFace);

        return frustum;
    }

    bool IsBoxVisible(const glm::vec3& min, const glm::vec3& max) const {
        const Plane* planes[6] = { &nearFace, &farFace, &leftFace, &rightFace, &bottomFace, &topFace };
        
        for (int i = 0; i < 6; i++) {
            const Plane& p = *planes[i];
            
            // P-vertex of the box for this plane
            glm::vec3 pVertex = min;
            if (p.normal.x >= 0) pVertex.x = max.x;
            if (p.normal.y >= 0) pVertex.y = max.y;
            if (p.normal.z >= 0) pVertex.z = max.z;

            if (p.getSignedDistanceToPlane(pVertex) < 0) {
                return false; // Box is outside this plane
            }
        }
        return true;
    }
};
