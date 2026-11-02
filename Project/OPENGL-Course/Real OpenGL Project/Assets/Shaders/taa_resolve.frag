#version 460

/**
 * @file taa_resolve.frag
 * @brief Temporal anti-aliasing resolve (Rendering/TemporalAA.h).
 *
 * The frame was drawn with a sub-pixel jitter. Each pixel finds where its surface was last frame:
 * from its motion vector when a plant moving in the wind wrote one (and that plant is still the
 * visible surface: the vector carries the view depth it was written at), otherwise from the
 * depth buffer and the camera's motion. Both use the nearest depth of the 3x3 neighbourhood, so
 * edges move with the object in front. The history is read there with a 5-tap Catmull-Rom filter
 * (no blur from re-sampling), bounded by the current neighbourhood's colour distribution
 * (variance clipping in YCoCg, Salvi 2016: anything that changed is pulled back, no ghosting)
 * and ~10% of the new frame is blended in, luminance-weighted (Karis 2014) so bright specks do not
 * flicker. Off-screen history or a cut: the current frame alone. Everything stays in HDR.
 */

out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D currentColor; ///< This frame (jittered, HDR)
uniform sampler2D currentDepth; ///< Its depth
uniform sampler2D historyColor; ///< Accumulated result of the previous frames
uniform sampler2D velocityTex;  ///< xy: motion (uv) of wind-blown plants, z: view depth (< 0: none)
uniform int useVelocity;
uniform mat4 invViewProj;       ///< This frame, without the jitter
uniform mat4 viewProj;          ///< This frame, without the jitter
uniform mat4 prevViewProj;      ///< The history's frame, without the jitter
uniform int historyValid;

vec3 RGBToYCoCg(vec3 c)
{
    return vec3(dot(c, vec3(0.25, 0.5, 0.25)), dot(c, vec3(0.5, 0.0, -0.5)), dot(c, vec3(-0.25, 0.5, -0.25)));
}

vec3 YCoCgToRGB(vec3 c)
{
    return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// Catmull-Rom through 5 bilinear taps (the corners' weights are negligible)
vec3 SampleHistory(vec2 uv)
{
    vec2 size = vec2(textureSize(historyColor, 0));
    vec2 samplePos = uv * size;
    vec2 tc1 = floor(samplePos - 0.5) + 0.5;
    vec2 f = samplePos - tc1;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 tc0 = (tc1 - 1.0) / size;
    vec2 tc3 = (tc1 + 2.0) / size;
    vec2 tc12 = (tc1 + w2 / w12) / size;
    vec3 result = texture(historyColor, vec2(tc12.x, tc0.y)).rgb * (w12.x * w0.y)
                + texture(historyColor, vec2(tc0.x, tc12.y)).rgb * (w0.x * w12.y)
                + texture(historyColor, tc12).rgb * (w12.x * w12.y)
                + texture(historyColor, vec2(tc3.x, tc12.y)).rgb * (w3.x * w12.y)
                + texture(historyColor, vec2(tc12.x, tc3.y)).rgb * (w12.x * w3.y);
    float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(result / weight, vec3(0.0));
}

// Pulls a colour toward the box centre until it is inside the box (keeps its hue, unlike a clamp)
vec3 ClipToBox(vec3 color, vec3 center, vec3 extents)
{
    vec3 d = color - center;
    vec3 unit = abs(d / max(extents, vec3(1e-4)));
    float m = max(unit.x, max(unit.y, unit.z));
    return m > 1.0 ? center + d / m : color;
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 maxP = textureSize(currentColor, 0) - 1;
    vec2 size = vec2(maxP + 1);

    // Current neighbourhood: colour moments and range, and the nearest depth
    vec3 m1 = vec3(0.0), m2 = vec3(0.0), lo = vec3(1e9), hi = vec3(-1e9);
    vec3 center = vec3(0.0);
    float closest = 1.0;
    ivec2 closestP = p;
    for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++) {
        ivec2 q = clamp(p + ivec2(x, y), ivec2(0), maxP);
        vec3 c = RGBToYCoCg(max(texelFetch(currentColor, q, 0).rgb, vec3(0.0)));
        if (x == 0 && y == 0) center = c;
        m1 += c;
        m2 += c * c;
        lo = min(lo, c);
        hi = max(hi, c);
        float d = texelFetch(currentDepth, q, 0).r;
        if (d < closest) { closest = d; closestP = q; }
    }

    if (historyValid == 0) {
        FragColor = vec4(YCoCgToRGB(center), 1.0);
        return;
    }

    // Where this surface was in the history's frame
    vec2 uv = (vec2(p) + 0.5) / size;
    vec4 world = invViewProj * vec4(uv * 2.0 - 1.0, closest * 2.0 - 1.0, 1.0);
    world /= world.w;
    vec2 prevUV;
    bool onScreen = true;
    vec4 motion = useVelocity == 1 ? texelFetch(velocityTex, closestP, 0) : vec4(0.0, 0.0, -1.0, 0.0);
    float viewDepth = (viewProj * world).w;
    if (motion.z > 0.0 && abs(motion.z - viewDepth) < 0.01 * viewDepth + 0.05) {
        prevUV = uv - motion.xy; // A plant moving in the wind, still in front
    } else {
        vec4 prevClip = prevViewProj * world;
        onScreen = prevClip.w > 0.0;
        prevUV = prevClip.xy / max(prevClip.w, 1e-6) * 0.5 + 0.5;
    }
    if (!onScreen || any(lessThan(prevUV, vec2(0.0))) || any(greaterThan(prevUV, vec2(1.0)))) {
        FragColor = vec4(YCoCgToRGB(center), 1.0);
        return;
    }

    vec3 history = RGBToYCoCg(SampleHistory(prevUV));

    // Variance clipping, tightened to the neighbourhood's actual range
    vec3 mean = m1 / 9.0;
    vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
    vec3 boxLo = max(mean - 1.25 * sigma, lo);
    vec3 boxHi = min(mean + 1.25 * sigma, hi);
    history = ClipToBox(history, (boxLo + boxHi) * 0.5, (boxHi - boxLo) * 0.5);

    // ~10% of the new frame; a little more while things move fast (less smearing)
    float moved = length((uv - prevUV) * size);
    float alpha = mix(0.1, 0.25, clamp(moved / 24.0, 0.0, 1.0));

    // Luminance weights: a bright outlier sample counts less (no fireflies)
    float wCurrent = alpha / (1.0 + center.x);
    float wHistory = (1.0 - alpha) / (1.0 + history.x);
    vec3 result = (center * wCurrent + history * wHistory) / (wCurrent + wHistory);
    FragColor = vec4(max(YCoCgToRGB(result), vec3(0.0)), 1.0);
}
