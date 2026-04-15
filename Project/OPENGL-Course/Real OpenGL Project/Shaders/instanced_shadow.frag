#version 460 core

uniform float materialAlpha;
layout(location = 0) out float outAlpha;

void main() {
    // Output alpha to the shadow color map (matches directional_shadow_map.frag)
    outAlpha = materialAlpha;
}
