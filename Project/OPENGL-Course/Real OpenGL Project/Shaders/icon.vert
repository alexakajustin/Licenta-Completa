#version 330

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;
layout (location = 2) in vec3 norm; // Unused but keeps VAO state happy

out vec2 TexCoord;

uniform mat4 projection;
uniform mat4 view;
uniform vec3 worldPos;
uniform float iconSize;

void main()
{
    // Billboard logic: 
    // Get the camera right and up vectors from the view matrix
    vec3 CameraRight_worldspace = vec3(view[0][0], view[1][0], view[2][0]);
    vec3 CameraUp_worldspace = vec3(view[0][1], view[1][1], view[2][1]);

    vec3 vertexPosition_worldspace = 
        worldPos
        + CameraRight_worldspace * pos.x * iconSize
        + CameraUp_worldspace * pos.y * iconSize;

    gl_Position = projection * view * vec4(vertexPosition_worldspace, 1.0f);
    TexCoord = tex;
}
