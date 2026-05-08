#version 410

out vec4 FragColor;

in vec2 TexCoord;
in vec3 WorldDir;

uniform float time;
uniform float starDensity;
uniform float starBrightness;
uniform float nebulaIntensity;
uniform float universeSpeed;
uniform vec3 nebulaColor1;
uniform vec3 nebulaColor2;

// Star Nest by Pablo Roman Andrioli
// Adapted for engine WorldDir and Uniforms

#define iterations 17
#define formuparam 0.53

#define volsteps 20
#define stepsize 0.1

#define tile   0.850

#define brightness 0.0015
#define darkmatter 0.300
#define distfading 0.730
#define saturation 0.850

void main()
{
	// Get direction from vertex shader
	vec3 dir = normalize(WorldDir);
	
	// Zoom factor (simulated via direction scaling)
	dir *= 0.8; 

	float s = 0.1, fade = 1.;
	vec3 v = vec3(0.);
	
	// Slow drift position
	vec3 from = vec3(1.0, 0.5, 0.5);
	from += vec3(time * universeSpeed * 2.0, time * universeSpeed, -2.0 * universeSpeed);
	
	// Volumetric Rendering
	for (int r = 0; r < volsteps; r++) {
		vec3 p = from + s * dir * 0.5;
		p = abs(vec3(tile) - mod(p, vec3(tile * 2.0))); // tiling fold
		
		float pa, a = pa = 0.0;
		for (int i = 0; i < iterations; i++) { 
			p = abs(p) / dot(p, p) - vec3(formuparam); // the magic formula
			a += abs(length(p) - pa); // absolute sum of average change
			pa = length(p);
		}
		
		float dm = max(0.0, darkmatter - a * a * 0.001); // dark matter
		a *= a * a; // add contrast
		
		if (r > 6) fade *= 1.0 - dm; // dark matter, don't render near
		
		// Map fractal iterations to a color gradient using user-defined nebula colors
		vec3 col = mix(nebulaColor1, nebulaColor2, sin(s * 3.0) * 0.5 + 0.5);
		
		// Original coloring logic tinted by user colors
		vec3 starCol = vec3(s, s * s, s * s * s * s);
		vec3 finalCol = mix(starCol, col, nebulaIntensity);
		
		v += vec3(fade);
		v += finalCol * a * brightness * starBrightness * fade; 
		
		fade *= distfading; // distance fading
		s += stepsize;
	}
	
	v = mix(vec3(length(v)), v, saturation); // color adjust
	
	vec3 finalColor = v * 0.01;
	
	// Add a subtle star density boost if requested
	finalColor *= (0.5 + starDensity * 0.5);

	FragColor = vec4(finalColor, 1.0);
}
