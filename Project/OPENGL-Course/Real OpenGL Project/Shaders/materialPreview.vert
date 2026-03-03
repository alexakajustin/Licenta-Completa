#version 330

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

void main()
{
    gl_Position = projection * view * model * vec4(pos, 1.0);
    FragPos = vec3(model * vec4(pos, 1.0));
    
    mat3 normalMatrix = mat3(transpose(inverse(model)));
    Normal = normalMatrix * norm;
    TexCoord = tex;
    
    // TBN matrix for normal mapping
    vec3 T = normalize(normalMatrix * tangent);
    vec3 N = normalize(Normal);
    T = normalize(T - dot(T, N) * N); // Re-orthogonalize
    vec3 B = cross(N, T);
    TBN = mat3(T, B, N);
}
