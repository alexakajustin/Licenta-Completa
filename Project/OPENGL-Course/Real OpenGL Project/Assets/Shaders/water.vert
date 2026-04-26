#version 330

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex;
layout (location = 2) in vec3 norm;
layout (location = 3) in vec3 tangent;
layout (location = 4) in vec3 bitangent;
layout (location = 5) in mat4 instanceMatrix;

out vec4 vertex_color;
out vec2 TexCoord;
out vec3 Normal;
out vec3 FragPos;

// TBN matrix vectors for normal mapping
out vec3 TangentWorld;
out vec3 BitangentWorld;
out vec3 NormalWorld;

out vec3 LocalPos;
out vec3 WorldXBasis;
out vec3 WorldZBasis;
out float vIsSelected;
out float vFadeFactor;

uniform mat4 model;
uniform mat4 projection;
uniform mat4 view;
uniform int useInstancing;
uniform float time;

struct Material {
	 float specularIntensity;
	 float shininess;
	 float sssScale;
	 float sssDistortion;
	 vec4 baseColor;
	 vec2 tiling;
	 vec2 offset;
};

uniform int textureLayerCount;
uniform Material material;

// Custom Material Parameters (we set fallback defaults if they are 0)
uniform float material_waveSpeed;
uniform float material_waveStrength;
uniform float material_waveScale;

vec3 GerstnerWave(vec4 wave, vec3 p, inout vec3 tangentOut, inout vec3 binormalOut, float actualWaveSpeed) {
    float steepness = wave.z;
    float wavelength = wave.w;
    float k = 2.0 * 3.14159 / wavelength;
    float c = sqrt(9.8 / k);
    vec2 d = normalize(wave.xy);
    float f = k * (dot(d, p.xz) - c * time * actualWaveSpeed);
    float a = steepness / k;
    
    tangentOut += vec3(
        -d.x * d.x * (steepness * sin(f)),
        d.x * (steepness * cos(f)),
        -d.x * d.y * (steepness * sin(f))
    );
    
    binormalOut += vec3(
        -d.x * d.y * (steepness * sin(f)),
        d.y * (steepness * cos(f)),
        -d.y * d.y * (steepness * sin(f))
    );
    
    return vec3(
        d.x * (a * cos(f)),
        a * sin(f),
        d.y * (a * cos(f))
    );
}

void main()
{
	mat4 modelMatrix = model;
	if (useInstancing == 1) {
	    modelMatrix = instanceMatrix;
	}

	vIsSelected = 0.0;
	vFadeFactor = 0.0; 

    float actualWaveSpeed = material_waveSpeed == 0.0 ? 0.75 : material_waveSpeed;
    float actualWaveStrength = material_waveStrength == 0.0 ? 1.0 : material_waveStrength;
    float actualWaveScale = material_waveScale == 0.0 ? 1.0 : material_waveScale;

	vec3 gridPoint = pos;
	vec3 tangentG = vec3(1.0, 0.0, 0.0);
	vec3 binormalG = vec3(0.0, 0.0, 1.0);
	vec3 p = gridPoint;
	
    // Direction X, Direction Z, Steepness, Wavelength
	vec4 waveA = vec4(1.0, 1.0, 0.25 * actualWaveStrength, 15.0 / actualWaveScale);
	vec4 waveB = vec4(1.0, 0.6, 0.25 * actualWaveStrength, 7.0 / actualWaveScale);
	vec4 waveC = vec4(-1.0, 1.3, 0.25 * actualWaveStrength, 4.0 / actualWaveScale);
    vec4 waveD = vec4(-0.2, 0.7, 0.20 * actualWaveStrength, 2.0 / actualWaveScale);
    
    p += GerstnerWave(waveA, gridPoint, tangentG, binormalG, actualWaveSpeed);
	p += GerstnerWave(waveB, gridPoint, tangentG, binormalG, actualWaveSpeed);
	p += GerstnerWave(waveC, gridPoint, tangentG, binormalG, actualWaveSpeed);
    p += GerstnerWave(waveD, gridPoint, tangentG, binormalG, actualWaveSpeed);
    
	vec3 normalG = normalize(cross(binormalG, tangentG));

	gl_Position = projection * view * modelMatrix * vec4(p, 1.0);

	vertex_color = vec4(clamp(p, 0.0f, 1.0f), 1.0f);
	
	if (textureLayerCount > 0) {
		TexCoord = tex;
	} else {
		TexCoord = tex * material.tiling + material.offset;
	}
	
	mat3 normalMatrix = mat3(transpose(inverse(modelMatrix)));
    Normal = normalMatrix * normalG;
	
	FragPos = (modelMatrix * vec4(p, 1.0)).xyz; 
	LocalPos = p; 

	TangentWorld = normalize(normalMatrix * tangentG);
	BitangentWorld = normalize(normalMatrix * binormalG);
	NormalWorld = normalize(normalMatrix * normalG);
}
