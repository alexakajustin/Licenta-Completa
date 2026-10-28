#version 460

/**
 * @file sky_upsample.frag
 * @brief Upscales the accumulated half-resolution sky into the viewport (Catmull-Rom via
 * 9 bilinear taps: sharp, and the 2x upscale shows no texel blocks). Alpha is forced to 1: the
 * viewport texture is shown as an opaque image.
 */

out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D skyTexture;

vec3 SampleCatmullRom(vec2 uv)
{
    vec2 size = vec2(textureSize(skyTexture, 0));
    vec2 st = uv * size - 0.5;
    vec2 i = floor(st);
    vec2 f = st - i;

    // Catmull-Rom weights, the two middle taps merged into one bilinear fetch (9 taps)
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 tc0 = (i - 0.5) / size;
    vec2 tc3 = (i + 2.5) / size;
    vec2 tc12 = (i + 0.5 + w2 / w12) / size;

    vec3 r = vec3(0.0);
    r += texture(skyTexture, vec2(tc0.x, tc0.y)).rgb * w0.x * w0.y;
    r += texture(skyTexture, vec2(tc12.x, tc0.y)).rgb * w12.x * w0.y;
    r += texture(skyTexture, vec2(tc3.x, tc0.y)).rgb * w3.x * w0.y;
    r += texture(skyTexture, vec2(tc0.x, tc12.y)).rgb * w0.x * w12.y;
    r += texture(skyTexture, vec2(tc12.x, tc12.y)).rgb * w12.x * w12.y;
    r += texture(skyTexture, vec2(tc3.x, tc12.y)).rgb * w3.x * w12.y;
    r += texture(skyTexture, vec2(tc0.x, tc3.y)).rgb * w0.x * w3.y;
    r += texture(skyTexture, vec2(tc12.x, tc3.y)).rgb * w12.x * w3.y;
    r += texture(skyTexture, vec2(tc3.x, tc3.y)).rgb * w3.x * w3.y;
    // Catmull-Rom overshoots slightly at hard edges; never let it go negative
    return max(r, vec3(0.0));
}

void main()
{
    FragColor = vec4(SampleCatmullRom(TexCoord), 1.0);
}
