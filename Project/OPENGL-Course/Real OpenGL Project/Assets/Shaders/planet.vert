#version 410 core

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;
layout (location = 2) in vec3 norm;
layout (location = 3) in vec3 tangent;
layout (location = 4) in vec3 bitangent;

out vec3 WorldPos;
out vec2 TexCoord;
out vec3 Normal;
out vec3 LocalPos;

uniform mat4 model;

void main()
{
    LocalPos = pos;
    WorldPos = (model * vec4(pos, 1.0)).xyz;
    TexCoord = tex;
    Normal = mat3(transpose(inverse(model))) * norm;
}
