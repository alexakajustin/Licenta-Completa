#version 330

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;
layout (location = 2) in vec3 norm;

out vec2 TexCoord;
out vec3 Normal;
out vec3 FragPos;

uniform mat4 model;
uniform mat4 projection;
uniform mat4 view;

uniform float windSpeed = 1.0;
uniform float windStrength = 0.1;
uniform float time;

void main()
{
	vec3 displacedPos = pos;
	if (pos.y > 0.0) {
		displacedPos.x += sin(time * windSpeed + pos.x) * windStrength;
		displacedPos.z += cos(time * windSpeed + pos.z) * windStrength;
	}
	
	gl_Position = projection * view * model * vec4(displacedPos, 1.0);
	TexCoord = tex;
	Normal = mat3(transpose(inverse(model))) * norm;
	FragPos = (model * vec4(pos, 1.0)).xyz;
}
