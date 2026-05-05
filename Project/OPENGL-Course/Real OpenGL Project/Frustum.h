#pragma once

#include <glm/glm.hpp>
#include <cmath>

// =====================================================================
// Plane — standard form: dot(normal, point) + d = 0
//   Positive side = inside the frustum
//   Negative side = outside the frustum
// =====================================================================
struct Plane {
    glm::vec3 normal = { 0.f, 1.f, 0.f };
    float d = 0.f; // distance component in ax+by+cz+d=0

    Plane() = default;

    // Construct from a point on the plane and a normal
    Plane(const glm::vec3& point, const glm::vec3& norm)
        : normal(glm::normalize(norm)),
          d(-glm::dot(normal, point))
    {}

    // Signed distance from a point to this plane
    // Positive = on the normal side (inside frustum)
    // Negative = behind the plane (outside frustum)
    float SignedDistance(const glm::vec3& point) const {
        return glm::dot(normal, point) + d;
    }
};

// =====================================================================
// Frustum — multi-layer culling system
// 
//   Layer 1: Bounding Sphere vs Frustum  (cheapest — 6 dot products)
//   Layer 2: Contribution Culling        (skip sub-pixel objects)
//   Layer 3: AABB vs Frustum             (precise p-vertex test)
//
//   Use IsVisible() for the full pipeline, or call individual tests.
// =====================================================================
struct Frustum {
    Plane planes[6]; // near, far, left, right, bottom, top

    // Named accessors for clarity
    Plane& nearFace()   { return planes[0]; }
    Plane& farFace()    { return planes[1]; }
    Plane& leftFace()   { return planes[2]; }
    Plane& rightFace()  { return planes[3]; }
    Plane& bottomFace() { return planes[4]; }
    Plane& topFace()    { return planes[5]; }

    const Plane& nearFace()   const { return planes[0]; }
    const Plane& farFace()    const { return planes[1]; }
    const Plane& leftFace()   const { return planes[2]; }
    const Plane& rightFace()  const { return planes[3]; }
    const Plane& bottomFace() const { return planes[4]; }
    const Plane& topFace()    const { return planes[5]; }

    // =================================================================
    // Extract frustum planes from a View-Projection matrix
    // (Gribb/Hartmann method — standard, see OpenGL spec)
    // Normals point INWARD (positive half-space = inside frustum)
    // =================================================================
    static Frustum CreateFrustumFromMatrix(const glm::mat4& viewProj) {
        Frustum f;
        // Transpose so we can work with column vectors as rows
        glm::mat4 m = glm::transpose(viewProj);

        // Near:   row3 + row2
        f.planes[0].normal = glm::vec3(m[3] + m[2]);
        f.planes[0].d      = m[3][3] + m[2][3];

        // Far:    row3 - row2
        f.planes[1].normal = glm::vec3(m[3] - m[2]);
        f.planes[1].d      = m[3][3] - m[2][3];

        // Left:   row3 + row0
        f.planes[2].normal = glm::vec3(m[3] + m[0]);
        f.planes[2].d      = m[3][3] + m[0][3];

        // Right:  row3 - row0
        f.planes[3].normal = glm::vec3(m[3] - m[0]);
        f.planes[3].d      = m[3][3] - m[0][3];

        // Bottom: row3 + row1
        f.planes[4].normal = glm::vec3(m[3] + m[1]);
        f.planes[4].d      = m[3][3] + m[1][3];

        // Top:    row3 - row1
        f.planes[5].normal = glm::vec3(m[3] - m[1]);
        f.planes[5].d      = m[3][3] - m[1][3];

        // Normalize all planes
        for (int i = 0; i < 6; i++) {
            float len = glm::length(f.planes[i].normal);
            if (len > 0.0f) {
                f.planes[i].normal /= len;
                f.planes[i].d      /= len;
            }
        }

        return f;
    }

    // =================================================================
    // LAYER 1: Bounding Sphere vs Frustum
    // Cheapest test — one dot product per plane.
    // Returns false if the sphere is COMPLETELY outside any plane.
    // =================================================================
    bool IsSphereVisible(const glm::vec3& center, float radius) const {
        for (int i = 0; i < 6; i++) {
            // Add a small safety margin (2.0 units) to prevent popping at long distances
            if (planes[i].SignedDistance(center) < -radius - 2.0f) {
                return false; // Sphere is entirely behind this plane
            }
        }
        return true;
    }

    // =================================================================
    // LAYER 2: Contribution Culling (Screen-Space Size)
    // Estimates the object's screen-space diameter in pixels.
    // If it's smaller than minPixels, the object is too small to see.
    //
    // Formula: screenDiameter = (2 * radius * screenHeight) / (2 * dist * tan(fov/2))
    // Since projection[1][1] = 1/tan(fov/2), we use that directly.
    // =================================================================
    static bool IsLargeEnough(const glm::vec3& sphereCenter, float sphereRadius,
                              float minPixels,
                              const glm::mat4& projection,
                              float screenHeight,
                              const glm::vec3& cameraPos) {
        float dist = glm::length(sphereCenter - cameraPos);
        if (dist < 0.001f) return true; // Camera is inside/on the object

        // projection[1][1] = 1 / tan(fovY / 2)
        float projFactor = projection[1][1];
        float screenDiameter = (2.0f * sphereRadius * projFactor * screenHeight) / (2.0f * dist);

        return screenDiameter >= minPixels;
    }

    // =================================================================
    // LAYER 3: AABB vs Frustum (p-vertex method)
    // Most precise test — tests the AABB corner most in the direction
    // of each plane normal. If that corner is behind the plane, the
    // entire box is outside.
    // =================================================================
    bool IsBoxVisible(const glm::vec3& min, const glm::vec3& max) const {
        for (int i = 0; i < 6; i++) {
            const Plane& p = planes[i];

            // p-vertex: the corner of the AABB furthest along the plane normal
            glm::vec3 pVertex = min;
            if (p.normal.x >= 0) pVertex.x = max.x;
            if (p.normal.y >= 0) pVertex.y = max.y;
            if (p.normal.z >= 0) pVertex.z = max.z;

            // Add a small safety margin (2.0 units) to prevent popping at long distances
            if (p.SignedDistance(pVertex) < -2.0f) {
                return false; // Box is entirely behind this plane
            }
        }
        return true;
    }

    // =================================================================
    // FULL PIPELINE: IsVisible()
    // Chains all layers for maximum efficiency:
    //   1. Sphere reject (cheap)
    //   2. Contribution reject (trivial for visible objects)
    //   3. AABB reject (precise, only if sphere didn't fully decide)
    //
    // Set minPixels <= 0 to disable contribution culling.
    // Set screenHeight <= 0 to disable contribution culling.
    // =================================================================
    bool IsVisible(const glm::vec3& sphereCenter, float sphereRadius,
                   const glm::vec3& boxMin, const glm::vec3& boxMax,
                   float minPixels = 0.0f,
                   const glm::mat4& projection = glm::mat4(1.0f),
                   float screenHeight = 0.0f,
                   const glm::vec3& cameraPos = glm::vec3(0.0f)) const {

        // Layer 1: Sphere test — cheapest, rejects most objects
        if (!IsSphereVisible(sphereCenter, sphereRadius)) {
            return false;
        }

        // Layer 2: Contribution culling — skip sub-pixel objects
        if (minPixels > 0.0f && screenHeight > 0.0f) {
            if (!IsLargeEnough(sphereCenter, sphereRadius, minPixels, projection, screenHeight, cameraPos)) {
                return false;
            }
        }

        // Layer 3: Precise AABB test — only for objects that passed sphere test
        if (!IsBoxVisible(boxMin, boxMax)) {
            return false;
        }

        return true;
    }
};
