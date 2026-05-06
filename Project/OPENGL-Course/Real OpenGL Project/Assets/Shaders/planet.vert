#version 410

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;
layout (location = 2) in vec3 norm;
layout (location = 3) in vec3 tangent;
layout (location = 4) in vec3 bitangent;

out vec3 WorldPos_CS_in;
out vec2 TexCoord_CS_in;
out vec3 Normal_CS_in;
out vec3 LocalPos_CS_in;

uniform mat4 model;

void main()
{
    LocalPos_CS_in = pos;
    WorldPos_CS_in = (model * vec4(pos, 1.0)).xyz;
    TexCoord_CS_in = tex;
    Normal_CS_in = mat3(transpose(inverse(model))) * norm;
}
