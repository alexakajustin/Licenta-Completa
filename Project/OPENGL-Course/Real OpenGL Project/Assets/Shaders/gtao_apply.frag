#version 460

/**
 * @file gtao_apply.frag
 * @brief Upsamples the half-resolution GTAO (gtao.frag) and darkens the scene with it
 * (Rendering/AmbientOcclusion.h). Drawn with a multiplicative blend into the viewport.
 *
 * Each full-resolution pixel averages the 3x3 half-resolution texels around it, weighted by how
 * close their depth is to its own (a cross-bilateral filter): the noise of the few directions
 * per pixel is smoothed without leaking across edges (a leaf over the sky stays crisp).
 */

out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D aoTex;       ///< r visibility, g view depth (half resolution)
uniform sampler2D depthTex;    ///< Full-resolution depth
uniform mat4 invProjection;
uniform float strength;        ///< 0 = no darkening, 1 = full
uniform float power;           ///< Contrast of the occlusion

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 maxP = textureSize(depthTex, 0) - 1;
    float d = texelFetch(depthTex, p, 0).r;
    if (d >= 1.0) { FragColor = vec4(1.0); return; }
    vec2 uv = (vec2(p) + 0.5) / vec2(maxP + 1);
    vec4 v = invProjection * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    float z = -v.z / v.w;

    ivec2 hp = p / 2;
    ivec2 hMax = textureSize(aoTex, 0) - 1;
    float tolerance = 0.03 * z + 0.05;
    float sum = 0.0, weightSum = 0.0;
    for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++) {
        vec2 s = texelFetch(aoTex, clamp(hp + ivec2(x, y), ivec2(0), hMax), 0).rg;
        float w = (x == 0 && y == 0 ? 1.0 : (x == 0 || y == 0 ? 0.6 : 0.35))
                * exp(-abs(s.y - z) / tolerance);
        sum += s.x * w;
        weightSum += w;
    }
    float ao = weightSum > 1e-3 ? sum / weightSum : 1.0;
    ao = mix(1.0, pow(ao, power), strength);
    FragColor = vec4(vec3(ao), 1.0);
}
