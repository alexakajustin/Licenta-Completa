#version 330

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;
layout (location = 2) in vec3 norm;
layout (location = 5) in mat4 instanceMatrix;

out vec2 TexCoord;
out vec3 Normal;
out vec3 FragPos;
out vec3 PrevFragPos; // Last frame (wind): motion vectors (grass.frag)
out float vIsSelected;
out float vFadeFactor;

uniform mat4 model;
uniform mat4 projection;
uniform mat4 view;
uniform int useInstancing;

uniform float time;
uniform vec4 clipPlane;

// The scene's wind (wind.glsl): blades bend with the gusts rolling over the field
#include "wind.glsl"
uniform float windPlantHeight; // Blade height (mesh space, set per instanced group; 0 = 1 m)

void main()
{
	mat4 modelMatrix = model;
	if (useInstancing == 1) {
		modelMatrix = instanceMatrix;
	}

	vIsSelected = 0.0;
	vFadeFactor = 0.0;
	vec4 worldPos = modelMatrix * vec4(pos, 1.0);
	vec3 worldNormal = mat3(transpose(inverse(modelMatrix))) * norm;
	// Blades bend from the clump's origin; the normal tilts with them (waves of light)
	float bladeHeight = (windPlantHeight > 0.0 ? windPlantHeight : 1.0) * length(modelMatrix[1].xyz);
	vec3 rest = worldPos.xyz;
	vec3 prevNormal = worldNormal;
	worldPos.xyz = WindGrass(rest, modelMatrix[3].xyz, bladeHeight, worldNormal);
	PrevFragPos = WindGrassP(WindPrev(), rest, modelMatrix[3].xyz, bladeHeight, prevNormal);
	gl_ClipDistance[0] = dot(worldPos, clipPlane);
	gl_Position = projection * view * worldPos;
	TexCoord = tex;
	Normal = worldNormal;
	FragPos = worldPos.xyz;
}
