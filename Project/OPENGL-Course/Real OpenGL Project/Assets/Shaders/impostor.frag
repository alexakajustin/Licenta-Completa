#version 430 core

/**
 * @file impostor.frag
 * @brief Lights an octahedral impostor card like the tree's own meshes (shader.frag): the leaves with
 *        the foliage model, the bark with the plain one (the atlas says which is which).
 *
 * The atlas holds albedo, normals and depth instead of a finished image, so the sun, the sunset
 * colour, the cascaded shadows and the crown occlusion are all computed live: the card moves its
 * fragment back onto the tree's surface with the baked depth before the shadow lookup, so the far
 * side of a crown is shadowed by its own leaves.
 *
 * impostor_common.glsl is pasted after the #version line by Impostor.cpp.
 */

out vec4 colour;

struct Light {
    vec3 colour;
    float ambientIntensity;
    float diffuseIntensity;
};

struct DirectionalLight {
    Light base;
    vec3 direction; ///< From the light toward the scene
};

uniform DirectionalLight directionalLight;
uniform sampler2DArray directionalShadowMap;
uniform sampler2DArray directionalShadowColorMap;
uniform mat4 directionalLightTransform[4];
uniform float cascadeSplits[4];
uniform int shadowCascadeCount;
uniform float shadowCascadeTexel[4];
uniform float shadowCascadeDepthRange[4];
uniform mat4 viewMatrix;
uniform vec3 eyePosition;

uniform float foliageRadius; ///< Bounding radius of the leaves mesh: crown occlusion scale
uniform bool debugLODColoring;
uniform vec3 lodDebugColor;

vec2 poissonDisk[16] = vec2[](
   vec2( -0.94201624, -0.39906216 ), vec2( 0.94558609, -0.76890725 ),
   vec2( -0.094184101, -0.92938870 ), vec2( 0.34495938, 0.29387760 ),
   vec2( -0.91588581, 0.45771432 ), vec2( -0.81544232, -0.87912464 ),
   vec2( -0.38277543, 0.27676845 ), vec2( 0.97484398, 0.75648379 ),
   vec2( 0.44323325, -0.97511554 ), vec2( 0.53742981, -0.47373420 ),
   vec2( -0.65476012, -0.051443273 ), vec2( -0.43701662, -0.81599235 ),
   vec2( 0.60288572, 0.44872051 ), vec2( -0.21151840, -0.36960612 ),
   vec2( -0.54245971, 0.48821213 ), vec2( -0.34613928, -0.64444749 )
);

float random(vec3 seed, int i) {
    vec4 seed4 = vec4(seed, i);
    float dot_product = dot(seed4, vec4(12.9898, 78.233, 45.164, 94.673));
    return fract(sin(dot_product) * 43758.5453);
}

/// Cascade lookup of shader.frag (normal offset + slope-scaled bias, rotated Poisson PCF) with half
/// the taps: impostors are always far away, the rotation noise is below a pixel there
float ShadowAtLayer(int layer, vec3 fragPos, vec3 normal, vec3 lightDir)
{
    float texelSizeWS = shadowCascadeTexel[layer];
    vec2 texSize = vec2(textureSize(directionalShadowMap, 0).xy);
    float cosAlpha = clamp(dot(normal, -lightDir), 0.0, 1.0);

    float offsetScale = max(texelSizeWS * 0.8, 0.015);
    vec3 offsetPos = fragPos + normal * (offsetScale * (0.2 + 0.8 * (1.0 - cosAlpha)));
    vec4 lightSpace = directionalLightTransform[layer] * vec4(offsetPos, 1.0);
    vec3 projCoords = lightSpace.xyz / lightSpace.w * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 0.0;

    float tanTheta = sqrt(1.0 - cosAlpha * cosAlpha) / max(cosAlpha, 0.0001);
    // The baked depth is coarse (8 bits over the tree): one texel more of bias than the meshes
    float biasWS = clamp(texelSizeWS * (1.4 + tanTheta * 2.0), 0.01, 4.0);
    float bias = biasWS / shadowCascadeDepthRange[layer];

    float angle = random(fragPos, 0) * 6.283185;
    float s = sin(angle), c = cos(angle);
    mat2 rot = mat2(c, s, -s, c);
    vec2 texelSize = 1.0 / texSize;

    float shadow = 0.0;
    for (int i = 0; i < 16; i += 2) {
        vec3 samplePos = vec3(projCoords.xy + rot * poissonDisk[i] * texelSize * 0.8, layer);
        float pcfDepth = texture(directionalShadowMap, samplePos).r;
        if (projCoords.z - bias > pcfDepth) shadow += texture(directionalShadowColorMap, samplePos).r;
    }
    shadow /= 8.0;

    if (layer == shadowCascadeCount - 1) {
        float edgeFade = min(min(smoothstep(0.0, 0.1, projCoords.x), smoothstep(0.0, 0.1, 1.0 - projCoords.x)),
                             min(smoothstep(0.0, 0.1, projCoords.y), smoothstep(0.0, 0.1, 1.0 - projCoords.y)));
        shadow *= edgeFade;
    }
    return shadow;
}

float DirectionalShadow(vec3 fragPos, vec3 normal)
{
    if (shadowCascadeCount <= 0) return 0.0;
    float depth = abs((viewMatrix * vec4(fragPos, 1.0)).z);
    vec3 lightDir = normalize(directionalLight.direction);

    int layer = shadowCascadeCount - 1;
    for (int i = 0; i < shadowCascadeCount; i++) {
        if (depth < cascadeSplits[i]) { layer = i; break; }
    }
    float shadow = ShadowAtLayer(layer, fragPos, normal, lightDir);

    // Blend into the next cascade over the last 10% of this one (as shader.frag)
    if (layer < shadowCascadeCount - 1) {
        float splitDist = cascadeSplits[layer];
        float prevSplit = layer > 0 ? cascadeSplits[layer - 1] : 0.0;
        float blendStart = splitDist - (splitDist - prevSplit) * 0.1;
        if (depth > blendStart)
            shadow = mix(shadow, ShadowAtLayer(layer + 1, fragPos, normal, lightDir),
                         (depth - blendStart) / max(splitDist - blendStart, 1e-4));
    }
    return shadow;
}

void main()
{
    if (DitherDiscard(vFadeFactor)) discard;

    ImpostorAlbedo albedo = SampleImpostorAlbedo();
    if (CoverageDiscard(albedo.coverage)) discard;
    ImpostorTexel t = SampleImpostorSurface(albedo, true);

    vec3 N = normalize(t.normal);
    vec3 fragPos = t.worldPos;
    vec3 L = normalize(-directionalLight.direction);
    vec3 V = normalize(eyePosition - fragPos);
    vec3 baseColor = t.albedo;

    // Crown self-occlusion and canopy variation of shader.frag, from the reconstructed mesh-space position
    vec3 localPos = transpose(InstanceRotation) * (fragPos - InstancePos) / InstanceScale;
    float occlusion = 1.0;
    if (foliageRadius > 0.0) {
        float radial = length(localPos.xz) / (foliageRadius * 0.45);
        float height = localPos.y / (foliageRadius * 1.6);
        float exposure = max(smoothstep(0.1, 0.75, radial), smoothstep(0.7, 0.95, height));
        occlusion = mix(0.35, 1.0, exposure) * mix(0.7, 1.0, smoothstep(0.0, 0.6, height));
    }
    vec3 q = fragPos * 0.35;
    occlusion *= 0.85 + 0.15 * sin(q.x + 1.7 * sin(q.z)) * sin(q.z * 1.3 + q.y * 0.7);

    // Foliage lighting of shader.frag: wrapped diffuse, sky fill, light through the leaves near the sun
    float shadowFactor = DirectionalShadow(fragPos, N);
    vec3 ambientColour = directionalLight.base.colour * directionalLight.base.ambientIntensity;
    float wrapped = max((dot(N, L) + 0.5) / 1.5, 0.0);
    float backLit = pow(max(dot(V, -L), 0.0), 10.0);
    float transmission = (0.06 * max(-dot(N, L), 0.0) + 0.5 * backLit) * occlusion;
    vec3 transmitColor = baseColor * vec3(0.8, 1.6, 0.6);
    vec3 lightIn = directionalLight.base.colour * directionalLight.base.diffuseIntensity * (1.0 - shadowFactor * 0.8);
    vec3 skyAmbient = ambientColour * vec3(0.85, 1.0, 1.15);
    vec3 foliageColor = baseColor * (skyAmbient * mix(0.6, 1.0, occlusion) + lightIn * wrapped * occlusion)
                      + transmitColor * lightIn * transmission;

    // Bark: the plain lighting of shader.frag (no crown occlusion, no translucency)
    vec3 sunLight = directionalLight.base.colour * directionalLight.base.diffuseIntensity * max(dot(N, L), 0.0);
    vec3 barkColor = baseColor * (ambientColour + (1.0 - shadowFactor) * sunLight);
    vec3 finalColor = mix(barkColor, foliageColor, t.foliage);

    if (vIsSelected > 0.5) finalColor += vec3(0.35, 0.25, 0.0);
    if (debugLODColoring) finalColor = mix(finalColor, lodDebugColor, 0.4);

    colour = vec4(finalColor, 1.0);
}
