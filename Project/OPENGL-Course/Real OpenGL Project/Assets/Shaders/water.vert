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
out vec4 clipSpaceCoords;
out float vObjectScale;

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

// Custom Material Parameters
uniform float material_waveSpeed;
uniform float material_waveStrength;
uniform float material_waveScale;
uniform vec4 clipPlane;

// Gerstner wave function - displaces vertices for large-scale ocean movement
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

    // Extract object scale from model matrix (length of basis vectors)
    float scaleX = length(modelMatrix[0].xyz);
    float scaleZ = length(modelMatrix[2].xyz);
    float objectScale = max(scaleX, scaleZ); // Use largest horizontal axis
    
    // Auto-derive wave parameters from object scale
    // Reference: scale=100 -> baseline (waveScale=1, dudvTiling=6, etc.)
    float scaleFactor = objectScale / 100.0;
    
    // Allow manual overrides, but default to auto-scaled values
    float actualWaveSpeed = material_waveSpeed == 0.0 ? 0.75 : material_waveSpeed;
    float actualWaveStrength = material_waveStrength == 0.0 ? scaleFactor : material_waveStrength;
    float actualWaveScale = material_waveScale == 0.0 ? (1.0 / scaleFactor) : material_waveScale;

	vec3 gridPoint = pos;
	vec3 tangentG = vec3(1.0, 0.0, 0.0);
	vec3 binormalG = vec3(0.0, 0.0, 1.0);
	vec3 p = gridPoint;
	
    // 6 Gerstner waves at different scales for rich, varied motion
    // Direction X, Direction Z, Steepness, Wavelength
	vec4 waveA = vec4(1.0, 1.0, 0.25 * actualWaveStrength, 60.0 / actualWaveScale);
	vec4 waveB = vec4(1.0, 0.6, 0.20 * actualWaveStrength, 31.0 / actualWaveScale);
	vec4 waveC = vec4(1.0, 1.3, 0.15 * actualWaveStrength, 18.0 / actualWaveScale);
    vec4 waveD = vec4(0.7, 1.0, 0.15 * actualWaveStrength, 10.0 / actualWaveScale);
    vec4 waveE = vec4(0.2, 0.8, 0.10 * actualWaveStrength,  5.0 / actualWaveScale);
    vec4 waveF = vec4(-0.4, 0.6, 0.08 * actualWaveStrength,  3.0 / actualWaveScale);
    
    vec3 waveOffset = vec3(0.0);
    waveOffset += GerstnerWave(waveA, gridPoint, tangentG, binormalG, actualWaveSpeed);
	waveOffset += GerstnerWave(waveB, gridPoint, tangentG, binormalG, actualWaveSpeed);
	waveOffset += GerstnerWave(waveC, gridPoint, tangentG, binormalG, actualWaveSpeed);
    waveOffset += GerstnerWave(waveD, gridPoint, tangentG, binormalG, actualWaveSpeed);
    waveOffset += GerstnerWave(waveE, gridPoint, tangentG, binormalG, actualWaveSpeed);
    waveOffset += GerstnerWave(waveF, gridPoint, tangentG, binormalG, actualWaveSpeed);
    
    // Edge fade: smoothly kill vertex displacement near mesh borders
    // Plane local coords go from -1 to +1. Fade starts at 0.7 (outer ~15% of each edge).
    float edgeFadeX = smoothstep(0.0, 0.3, 1.0 - abs(gridPoint.x));
    float edgeFadeZ = smoothstep(0.0, 0.3, 1.0 - abs(gridPoint.z));
    float edgeFade = edgeFadeX * edgeFadeZ;

    // Only apply vertical (Y) displacement — keep the plane stationary in XZ
    // Multiply by edgeFade so corners/edges stay perfectly flat
    p.y += waveOffset.y * edgeFade;
    
	vec3 normalG = normalize(cross(binormalG, tangentG));

    vec4 worldPos = modelMatrix * vec4(p, 1.0);
	gl_ClipDistance[0] = dot(worldPos, clipPlane);
	clipSpaceCoords = projection * view * worldPos;
	gl_Position = clipSpaceCoords;

	vertex_color = vec4(clamp(p, 0.0f, 1.0f), 1.0f);
	
	if (textureLayerCount > 0) {
		TexCoord = tex;
	} else {
		TexCoord = tex * material.tiling + material.offset;
	}
	
	mat3 normalMatrix = mat3(transpose(inverse(modelMatrix)));
    Normal = normalMatrix * normalG;
	
	FragPos = worldPos.xyz; 
	LocalPos = p; 
	vObjectScale = objectScale;

	TangentWorld = normalize(normalMatrix * tangentG);
	BitangentWorld = normalize(normalMatrix * binormalG);
	NormalWorld = normalize(normalMatrix * normalG);
}
