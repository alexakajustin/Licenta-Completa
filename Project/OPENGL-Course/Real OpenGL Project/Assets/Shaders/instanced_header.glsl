// =====================================================================
// GPU-Driven Instanced Header
//
// Shared logic for "Instancifying" any vertex shader.
// Provides the SSBO structure and a helper to build the model matrix.
// =====================================================================

struct PackedInstance {
    vec4 posAndScale;     // xyz = position, w = scale
    vec4 rotAndFlags;     // xyz = euler degrees, w = flags
};

layout(std430, binding = 1) readonly buffer VisibleInstances {
    PackedInstance instances[];
};

// Build rotation matrix from euler angles (degrees)
mat3 eulerToMat3(vec3 euler) {
    float cx = cos(radians(euler.x));
    float sx = sin(radians(euler.x));
    float cy = cos(radians(euler.y));
    float sy = sin(radians(euler.y));
    float cz = cos(radians(euler.z));
    float sz = sin(radians(euler.z));

    // Rotation order: X * Y * Z (matches GLM default)
    mat3 rx = mat3(1.0, 0.0, 0.0, 0.0, cx, -sx, 0.0, sx, cx);
    mat3 ry = mat3(cy, 0.0, sy, 0.0, 1.0, 0.0, -sy, 0.0, cy);
    mat3 rz = mat3(cz, -sz, 0.0, sz, cz, 0.0, 0.0, 0.0, 1.0);

    return rz * ry * rx;
}

// Build the model matrix for the current instance (gl_InstanceID)
mat4 ResolveInstancedModelMatrix() {
    PackedInstance inst = instances[gl_InstanceID];
    
    vec3 instancePos = inst.posAndScale.xyz;
    float instanceScale = inst.posAndScale.w;
    vec3 instanceRot = inst.rotAndFlags.xyz;
    
    mat3 rotMat = eulerToMat3(instanceRot);
    
    // Construct the matrix column by column (standard constructor)
    return mat4(
        vec4(rotMat[0] * instanceScale, 0.0),
        vec4(rotMat[1] * instanceScale, 0.0),
        vec4(rotMat[2] * instanceScale, 0.0),
        vec4(instancePos, 1.0)
    );
}
