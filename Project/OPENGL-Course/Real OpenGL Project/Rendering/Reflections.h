#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <functional>

#include "Rendering/Shader.h"

struct GraphicsSettings;

// Screen-space reflections (ssr.frag) over the reflection probes.
//
// In the main view, reflective objects (material reflectivity > 0) do not add their reflection
// themselves: shader.frag writes their normal, reflection weight, roughness and the probe's
// reflection into attachments 2-4 of the viewport (SceneManager enables them per draw). After the
// opaque scene this pass traces one stochastic ray per reflective pixel through the depth buffer,
// takes the colour at the hit from the previous frame, falls back to the probe where the ray finds
// nothing on screen, and adds the result into the viewport.
//
// The probes (one cubemap per reflective object, Renderer::UpdateReflectionProbes) are captured
// once and then refreshed a few faces per frame, with mips for rough reflections and a proxy box
// for parallax-corrected lookups.
class Reflections
{
public:
	void Init();
	void Shutdown();

	// Before the scene pass: attaches the reflection targets to the viewport and clears them.
	// enabled = false: reflective objects add their probe reflection themselves (forward).
	void BeginFrame(GLuint viewportFBO, int width, int height, bool enabled);

	// After the opaque scene (and AO), before the fog. prevColor: last frame's HDR scene (0: none
	// yet, probes only); prevViewProj: its camera. projection / view: this frame's, no jitter.
	void Render(GLuint viewportFBO, GLuint depthTexture, int width, int height,
		const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos,
		GLuint prevColor, const glm::mat4& prevViewProj, const GraphicsSettings& gs,
		const std::function<void()>& renderQuad);

	// The framebuffer whose attachments 2-4 take the deferred reflections this frame (0: none)
	static GLuint GetTarget() { return target; }

private:
	static GLuint target;
	Shader ssrShader;
	GLuint textures[3] = { 0, 0, 0 }; // Normal + depth tag, weight + roughness, probe reflection
	GLuint attachedFBO = 0;
	int texWidth = 0, texHeight = 0;
	bool frameActive = false;
};
