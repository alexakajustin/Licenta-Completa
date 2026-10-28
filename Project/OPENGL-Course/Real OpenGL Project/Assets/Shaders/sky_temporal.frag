#version 460

/**
 * @file sky_temporal.frag
 * @brief Temporal accumulation of the half-resolution volumetric sky.
 *
 * The cloud march is jittered per pixel and per frame, so a single frame is noisy. Each pixel's
 * view direction is reprojected into the previous frame (rotation only: the clouds are kilometres
 * away, so camera translation is negligible), the history is fetched there, clipped to the
 * colour range of the current 3x3 neighbourhood (variance clipping, Salvi 2016) so moving clouds
 * do not ghost, and blended with the new frame.
 */

out vec4 FragColor;

in vec2 TexCoord;
in vec3 WorldDir;

uniform sampler2D currentSky;  ///< This frame's noisy march (rgb = colour, a = cloud transmittance)
uniform sampler2D historySky;  ///< Accumulated result of the previous frame
uniform mat4 prevViewProjRot;  ///< Previous projection * rotation-only view
uniform float historyWeight;   ///< 0 = reset history (first frame / resize)

void main()
{
    vec2 texel = 1.0 / vec2(textureSize(currentSky, 0));
    vec4 current = texture(currentSky, TexCoord);

    vec4 m1 = vec4(0.0), m2 = vec4(0.0);
    for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++) {
        vec4 s = texture(currentSky, TexCoord + vec2(x, y) * texel);
        m1 += s;
        m2 += s * s;
    }
    m1 /= 9.0;
    m2 /= 9.0;
    vec4 sigma = sqrt(max(m2 - m1 * m1, vec4(0.0)));

    vec4 prevClip = prevViewProjRot * vec4(normalize(WorldDir), 1.0);
    float weight = historyWeight;
    vec4 result = current;
    if (prevClip.w > 1e-4 && weight > 0.0)
    {
        vec2 prevUV = prevClip.xy / prevClip.w * 0.5 + 0.5;
        if (all(greaterThanEqual(prevUV, vec2(0.0))) && all(lessThanEqual(prevUV, vec2(1.0))))
        {
            vec4 history = texture(historySky, prevUV);
            history = clamp(history, m1 - 1.5 * sigma, m1 + 1.5 * sigma);
            result = mix(current, history, weight);
        }
    }
    FragColor = result;
}
