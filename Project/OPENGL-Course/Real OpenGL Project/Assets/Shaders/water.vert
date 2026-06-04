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

    // Extract object scale from model matrix
    float scaleX = length(modelMatrix[0].xyz);
    float scaleZ = length(modelMatrix[2].xyz);
    float objectScale = max(scaleX, scaleZ);
    
    // Wave parameters are now directly in WORLD units
    float actualWaveSpeed = material_waveSpeed == 0.0 ? 0.8 : material_waveSpeed;
    float actualWaveStrength = material_waveStrength == 0.0 ? 2.5 : material_waveStrength;
    float actualWaveScale = material_waveScale == 0.0 ? 1.0 : material_waveScale;

    // Convert local grid point to world space FIRST
    vec4 worldPos = modelMatrix * vec4(pos, 1.0);
	vec3 worldGridPoint = worldPos.xyz;
	
    // Base vectors in world space
	vec3 tangentW = vec3(1.0, 0.0, 0.0);
	vec3 binormalW = vec3(0.0, 0.0, 1.0);
	
    // 6 Gerstner waves spread across 360° for natural, non-repetitive ocean movement
    // Directions chosen at non-uniform angles to avoid symmetry artifacts
    // Format: vec4(dirX, dirZ, steepness, wavelength_meters)
	vec4 waveA = vec4( 0.95,  0.31, 0.22 * actualWaveStrength, 50.0 / actualWaveScale);  // ~18°
	vec4 waveB = vec4(-0.42,  0.91, 0.16 * actualWaveStrength, 28.0 / actualWaveScale);  // ~115°
	vec4 waveC = vec4(-0.87, -0.50, 0.13 * actualWaveStrength, 17.0 / actualWaveScale);  // ~210°
    vec4 waveD = vec4( 0.34, -0.94, 0.10 * actualWaveStrength,  9.0 / actualWaveScale);  // ~290°
    vec4 waveE = vec4( 0.71,  0.71, 0.07 * actualWaveStrength,  5.0 / actualWaveScale);  // ~45°
    vec4 waveF = vec4(-0.26,  0.97, 0.04 * actualWaveStrength,  2.5 / actualWaveScale);  // ~105°
    
    // Individual speed multipliers break the synchronized pulse
    vec3 waveOffset = vec3(0.0);
    waveOffset += GerstnerWave(waveA, worldGridPoint, tangentW, binormalW, actualWaveSpeed * 1.0);
	waveOffset += GerstnerWave(waveB, worldGridPoint, tangentW, binormalW, actualWaveSpeed * 0.8);
	waveOffset += GerstnerWave(waveC, worldGridPoint, tangentW, binormalW, actualWaveSpeed * 1.2);
    waveOffset += GerstnerWave(waveD, worldGridPoint, tangentW, binormalW, actualWaveSpeed * 0.65);
    waveOffset += GerstnerWave(waveE, worldGridPoint, tangentW, binormalW, actualWaveSpeed * 1.4);
    waveOffset += GerstnerWave(waveF, worldGridPoint, tangentW, binormalW, actualWaveSpeed * 0.9);
    
    // Edge fade based on LOCAL coordinates to smoothly flatten water near terrain edges
    float edgeFadeX = smoothstep(0.0, 0.05, 1.0 - abs(pos.x));
    float edgeFadeZ = smoothstep(0.0, 0.05, 1.0 - abs(pos.z));
    float edgeFade = edgeFadeX * edgeFadeZ;

    // Calculate steady clip space coords for reflection/refraction UVs BEFORE wave displacement
    // This prevents the "panning" effect where reflections crawl with the geometric waves.
    clipSpaceCoords = projection * view * vec4(worldGridPoint, 1.0);

    // Apply displacement to world position (X, Y, Z for true sharp Gerstner crests)
    worldPos.y += waveOffset.y * edgeFade;
    worldPos.x += waveOffset.x * edgeFade * 0.5; 
    worldPos.z += waveOffset.z * edgeFade * 0.5;
    
	vec3 normalW = normalize(cross(binormalW, tangentW));

	gl_ClipDistance[0] = dot(worldPos, clipPlane);
	gl_Position = projection * view * worldPos;

	vertex_color = vec4(clamp(pos, 0.0f, 1.0f), 1.0f);
	
	if (textureLayerCount > 0) {
		TexCoord = tex;
	} else {
		TexCoord = tex * material.tiling + material.offset;
	}
	
    // Provide normal data directly in world space
    Normal = normalW;
	FragPos = worldPos.xyz; 
	LocalPos = pos; 
	vObjectScale = objectScale;

	TangentWorld = normalize(tangentW);
	BitangentWorld = normalize(binormalW);
	NormalWorld = normalW;
}
