#version 460

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;
out vec3 WorldDir;

uniform mat4 invProjection;
uniform mat4 invView;

void main()
{
    gl_Position = vec4(aPos, 1.0);
    TexCoord = aTexCoord;

    vec4 clipPos = vec4(aPos.xy, 1.0, 1.0);
    vec4 viewPos = invProjection * clipPos;
    viewPos /= viewPos.w;
    // Remove translation from invView for world direction
    WorldDir = mat3(invView) * normalize(viewPos.xyz);
}
