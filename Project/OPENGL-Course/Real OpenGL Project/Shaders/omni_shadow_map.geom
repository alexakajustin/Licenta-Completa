#version 460 core

// whats expected to come in, from the vertex shader
layout (triangles) in;

// instead of 3 points per triangle and then 3 more for another, bind from 
// the other triangles
layout(triangle_strip, max_vertices=18) out;

// perspective projection and view from light to all sides of the cube LRUDFB
uniform mat4 lightMatrices[6];

layout(location = 0) in vec4 FragPosGS[];
layout(location = 0) out vec4 FragPosOut;

// Outputs are renamed (an in/out pair cannot share a name), so they match
// omni_shadow_map.frag by explicit location instead of by name.
// Passthrough for instanced omni shadow (texture coords and fade factor)
layout(location = 1) in vec2 TexCoordGS[];
layout(location = 1) out vec2 TexCoordOut;
layout(location = 2) in float vFadeFactorGS[];
layout(location = 2) out float vFadeFactorOut;

void main()
{
	// iterate each side
	for(int face = 0; face < 6; face++) 
	{
		// builtin opengl standard. defining which of these 6 textures i want to output to
		gl_Layer = face;
		
		// go to each vertex position in the IN triangle 
		for(int i = 0; i < 3; i++)
		{
			FragPosOut = FragPosGS[i];
			TexCoordOut = TexCoordGS[i];
			vFadeFactorOut = vFadeFactorGS[i];

			// define the position to 'emit' the vertex in the world
			gl_Position = lightMatrices[face] * FragPosGS[i]; // projection * view
			EmitVertex();
		}
		EndPrimitive();
		// finish drawing up 6 triangles, relative to each light matrix
	}
}
