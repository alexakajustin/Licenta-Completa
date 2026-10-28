#version 460 core

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;
layout (location = 2) in vec3 norm;
layout (location = 3) in vec3 tangent;
layout (location = 4) in vec3 bitangent;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;
out mat3 TBN;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec2 tiling;
uniform vec2 offset;

void main()
{
    gl_Position = projection * view * model * vec4(pos, 1.0);
    FragPos = vec3(model * vec4(pos, 1.0));
    
    mat3 normalMatrix = mat3(transpose(inverse(model)));
    Normal = normalMatrix * norm;
    TexCoord = tex * tiling + offset;
    
    // TBN matrix for normal mapping
    vec3 T = normalize(normalMatrix * tangent);
    vec3 N = normalize(Normal);
    T = normalize(T - dot(T, N) * N); // Re-orthogonalize
    // Keep the mesh's bitangent handedness (mirrored UVs), same as shader.frag
    float handedness = dot(cross(N, T), normalMatrix * bitangent) < 0.0 ? -1.0 : 1.0;
    vec3 B = cross(N, T) * handedness;
    TBN = mat3(T, B, N);
}
