#version 330
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D ssaoText;
uniform sampler2D depthMap;

void main() {
    float depth = texture(depthMap, TexCoords).r;
    float ssao = texture(ssaoText, TexCoords).r;
    
    if (depth >= 0.9999) { 
        FragColor = vec4(1.0); 
    } else {
        FragColor = vec4(vec3(ssao), 1.0);
    }
}
