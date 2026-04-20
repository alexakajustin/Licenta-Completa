#version 330
out float FragColor;
in vec2 TexCoords;

uniform sampler2D ssaoInput;
uniform int blurSize = 4; // 4 = 4x4 kernel

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(ssaoInput, 0));
    float result = 0.0;
    int halfSize = blurSize / 2;
    int count = 0;
    for (int x = -halfSize; x < halfSize; ++x) {
        for (int y = -halfSize; y < halfSize; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result += texture(ssaoInput, TexCoords + offset).r;
            count++;
        }
    }
    FragColor = result / float(count);
}
