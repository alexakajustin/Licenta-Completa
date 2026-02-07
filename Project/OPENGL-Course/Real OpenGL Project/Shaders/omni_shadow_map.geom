#version 330

// whats expected to come in, from the vertex shader
layout (triangles) in;

// instead of 3 points per triangle and then 3 more for another, bind from 
// the other triangles
layout(triangle_strip, max_vertices=18) out;

// perspective projection and view from light to all sides of the cube LRUDFB
uniform mat4 lightMatrices[6];

out vec4 FragPos;

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
			FragPos = gl_in[i].gl_Position;

			// define the position to 'emit' the vertex in the world
			gl_Position = lightMatrices[face] * FragPos; // projection * view
			EmitVertex();
		}
		EndPrimitive();
		// finish drawing up 6 triangles, relative to each light matrix
	}
}