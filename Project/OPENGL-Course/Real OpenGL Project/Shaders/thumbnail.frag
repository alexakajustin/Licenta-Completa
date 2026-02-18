#version 330

in vec2 TexCoord;
in vec3 Normal;

out vec4 color;

uniform sampler2D theTexture;
uniform bool hasTexture;

void main()
{
    vec3 mainLightDir = normalize(vec3(0.5, 0.8, 1.0)); // Top-down-front
    vec3 fillLightDir = normalize(vec3(-0.5, -0.2, 0.5)); // Fill from side/bottom
    
    float diff = max(dot(normalize(Normal), mainLightDir), 0.0); 
    float fill = max(dot(normalize(Normal), fillLightDir), 0.0) * 0.4;
    float ambient = 0.5; // High ambient to ensure visibility
    
    vec4 texColor = vec4(1.0, 1.0, 1.0, 1.0);
    if (hasTexture) {
        vec4 sampled = texture(theTexture, TexCoord);
        if (sampled.a < 0.1) discard;
        // Blend slightly with white to ensure visibility in dark icons
        texColor = mix(vec4(1.0), sampled, 0.8); // 0.8 sampled, 0.2 white
    }
    
    // Add stronger rim lighting
    float rim = 1.0 - max(dot(normalize(Normal), vec3(0,0,1)), 0.0);
    rim = pow(rim, 2.0) * 0.4;
    
    color = vec4(texColor.rgb * (diff + fill + ambient) + rim, 1.0);
}
