#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "Rendering/Shader.h"

struct GraphicsSettings;

// A physically based atmosphere (Hillaire, "A Scalable and Production Ready Sky and Atmosphere
// Rendering Technique", 2020 - the model behind Unreal's SkyAtmosphere): air (Rayleigh), haze
// (Mie) and ozone over a spherical planet, shared by everything that sees the air:
//  - the sky (volumetric_sky.frag): its colour in every direction, the sun's disc, the light on
//    the clouds at their own altitude (lit pink after the ground is in shadow),
//  - aerial perspective (atmosphere_apply.frag): the scene fades into the same air with its
//    distance - distant hills layered into the haze, toward the sky's own colour.
// Three look-up textures (atmosphere_common.glsl): transmittance and multiple scattering, rebuilt
// when the air changes, and the sky view around the camera, rebuilt every Update.
class Atmosphere
{
public:
	static constexpr int LUT_UNIT = 42; // Texture units 42..44 (the clouds' noise uses 40, 41)

	void Init();
	// toSun: unit direction toward the sun; cameraPos: world position (y = altitude, metres)
	void Update(const GraphicsSettings& gs, const glm::vec3& toSun, const glm::vec3& cameraPos);
	// The atmosphere's uniforms and LUTs on a program that includes atmosphere_common.glsl
	void Bind(GLuint program) const;
	// Aerial perspective over the scene in the bound framebuffer: depth of the scene, its camera
	void ApplyAerialPerspective(GLuint sceneDepth, const glm::mat4& projection, const glm::mat4& view,
		const glm::vec3& cameraPos, const GraphicsSettings& gs);
	void Shutdown();

	// Sunlight reaching an altitude through the air, relative to a sun at the zenith (CPU, for
	// the colour of the light on the scene)
	glm::vec3 SunTransmittance(float altitude, const glm::vec3& toSun) const;
	// The light the open sky sheds on ground facing up (cosine-weighted over the sky view LUT,
	// per unit sun illuminance, linear): its colour is the sky light's - blue by day, violet in
	// the blue hour. Read back when the sun or the air changed.
	glm::vec3 SkyIrradiance() const { return skyIrradiance; }

private:
	struct Params {
		glm::vec3 rayleigh;
		float mieScattering, mieExtinction;
		glm::vec3 ozone;
		bool operator!=(const Params& o) const {
			return rayleigh != o.rayleigh || mieScattering != o.mieScattering || mieExtinction != o.mieExtinction || ozone != o.ozone;
		}
	};
	Params MakeParams(const GraphicsSettings& gs) const;
	void SetCommon(GLuint program, const Params& p) const;
	void DrawQuad();

	Shader transmittanceShader, multiScatterShader, skyViewShader, applyShader;
	GLuint transmittanceTex = 0, multiScatterTex = 0, skyViewTex = 0;
	GLuint transmittanceFBO = 0, multiScatterFBO = 0, skyViewFBO = 0;
	GLuint quadVAO = 0, quadVBO = 0;
	Params current{};
	Params lastIrradianceParams{};
	bool built = false;
	glm::vec3 sunIlluminance = glm::vec3(1.0f);
	float aerialScale = 1.0f;
	glm::vec3 lastToSun = glm::vec3(0.0f, 1.0f, 0.0f);
	int frame = 0;
	glm::vec3 skyIrradiance = glm::vec3(0.0f);
	glm::vec3 irradianceSun = glm::vec3(0.0f);
	float irradianceAltitude = -1e9f;
	void ReadSkyIrradiance(float cameraAltitude);
};
