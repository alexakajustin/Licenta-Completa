#include "Rendering/Atmosphere.h"

#include "GraphicsSettings.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
	constexpr int TRANSMITTANCE_W = 256, TRANSMITTANCE_H = 64;
	constexpr int MULTISCATTER_SIZE = 32;
	constexpr int SKYVIEW_W = 192, SKYVIEW_H = 108;
	constexpr float BOTTOM = 6360000.0f, TOP = 6460000.0f;
	constexpr float RAYLEIGH_HEIGHT = 8000.0f, MIE_HEIGHT = 1200.0f;

	void MakeTarget(GLuint& tex, GLuint& fbo, int w, int h)
	{
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	}
}

void Atmosphere::Init()
{
	transmittanceShader.CreateFromFiles("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/atmosphere_transmittance.frag");
	multiScatterShader.CreateFromFiles("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/atmosphere_multiscatter.frag");
	skyViewShader.CreateFromFiles("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/atmosphere_skyview.frag");
	applyShader.CreateFromFiles("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/atmosphere_apply.frag");

	GLint oldFBO = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);
	MakeTarget(transmittanceTex, transmittanceFBO, TRANSMITTANCE_W, TRANSMITTANCE_H);
	MakeTarget(multiScatterTex, multiScatterFBO, MULTISCATTER_SIZE, MULTISCATTER_SIZE);
	MakeTarget(skyViewTex, skyViewFBO, SKYVIEW_W, SKYVIEW_H);
	// Azimuth wraps around the sun's side (0 and pi are both edges): clamp is right; the sky view
	// needs no mips
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);

	float quad[] = { -1, 1, 0, 0, 1, -1, -1, 0, 0, 0, 1, 1, 0, 1, 1, 1, -1, 0, 1, 0 };
	glGenVertexArrays(1, &quadVAO);
	glGenBuffers(1, &quadVBO);
	glBindVertexArray(quadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	glBindVertexArray(0);
}

Atmosphere::Params Atmosphere::MakeParams(const GraphicsSettings& gs) const
{
	Params p;
	p.rayleigh = glm::vec3(5.802e-6f, 13.558e-6f, 33.1e-6f);
	p.ozone = glm::vec3(0.650e-6f, 1.881e-6f, 0.085e-6f);
	// Visibility (Koschmieder): the distance at which the air leaves 2 % contrast, 3.912 / extinction.
	// What the molecules do not account for is haze; it scatters 90 % of what it takes
	float visibility = std::max(gs.atmosphereVisibility, 0.5f) * 1000.0f;
	p.mieExtinction = std::max(3.912f / visibility - p.rayleigh.g, 1e-7f);
	p.mieScattering = p.mieExtinction * 0.9f;
	return p;
}

void Atmosphere::SetCommon(GLuint program, const Params& p) const
{
	auto U = [&](const char* n) { return glGetUniformLocation(program, n); };
	glUniform3fv(U("rayleighScattering"), 1, glm::value_ptr(p.rayleigh));
	glUniform1f(U("rayleighScaleHeight"), RAYLEIGH_HEIGHT);
	glUniform1f(U("mieScattering"), p.mieScattering);
	glUniform1f(U("mieExtinction"), p.mieExtinction);
	glUniform1f(U("mieScaleHeight"), MIE_HEIGHT);
	glUniform1f(U("mieG"), 0.8f);
	glUniform3fv(U("ozoneAbsorption"), 1, glm::value_ptr(p.ozone));
	glUniform3f(U("groundAlbedo"), 0.3f, 0.3f, 0.3f);
	glUniform3fv(U("sunIlluminance"), 1, glm::value_ptr(sunIlluminance));
	glUniform1f(U("sunAngularRadius"), 0.0047f);
}

void Atmosphere::DrawQuad()
{
	glBindVertexArray(quadVAO);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
}

void Atmosphere::Update(const GraphicsSettings& gs, const glm::vec3& toSun, const glm::vec3& cameraPos)
{
	if (!quadVAO) return;
	sunIlluminance = glm::vec3(std::max(gs.skyIntensity, 0.0f));
	aerialScale = std::max(gs.aerialPerspectiveScale, 0.0f);
	lastToSun = toSun;
	Params p = MakeParams(gs);

	GLint oldFBO = 0, oldViewport[4];
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);
	glGetIntegerv(GL_VIEWPORT, oldViewport);
	GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	if (!built || p != current) {
		current = p;
		built = true;
		glBindFramebuffer(GL_FRAMEBUFFER, transmittanceFBO);
		glViewport(0, 0, TRANSMITTANCE_W, TRANSMITTANCE_H);
		transmittanceShader.UseShader();
		SetCommon(transmittanceShader.GetShaderID(), p);
		DrawQuad();

		glBindFramebuffer(GL_FRAMEBUFFER, multiScatterFBO);
		glViewport(0, 0, MULTISCATTER_SIZE, MULTISCATTER_SIZE);
		multiScatterShader.UseShader();
		SetCommon(multiScatterShader.GetShaderID(), p);
		Bind(multiScatterShader.GetShaderID());
		DrawQuad();
	}

	glBindFramebuffer(GL_FRAMEBUFFER, skyViewFBO);
	glViewport(0, 0, SKYVIEW_W, SKYVIEW_H);
	skyViewShader.UseShader();
	GLuint sv = skyViewShader.GetShaderID();
	Bind(sv);
	glUniform3fv(glGetUniformLocation(sv, "cameraPos"), 1, glm::value_ptr(cameraPos));
	glUniform3fv(glGetUniformLocation(sv, "toSunDir"), 1, glm::value_ptr(toSun));
	DrawQuad();

	// The sky's light on the ground, when the sun moved or the air changed (a small read back)
	bool rebuilt = frame == 0 || p != lastIrradianceParams;
	if (rebuilt || glm::dot(toSun, irradianceSun) < 0.99998f || std::abs(cameraPos.y - irradianceAltitude) > 50.0f) {
		lastIrradianceParams = p;
		irradianceSun = toSun;
		ReadSkyIrradiance(cameraPos.y);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
	glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
	if (depthTest) glEnable(GL_DEPTH_TEST);
	if (blend) glEnable(GL_BLEND);
	frame++;
}

void Atmosphere::Bind(GLuint program) const
{
	SetCommon(program, current);
	const GLuint tex[3] = { transmittanceTex, multiScatterTex, skyViewTex };
	const char* names[3] = { "transmittanceLUT", "multiScatterLUT", "skyViewLUT" };
	for (int i = 0; i < 3; i++) {
		glActiveTexture(GL_TEXTURE0 + LUT_UNIT + i);
		glBindTexture(GL_TEXTURE_2D, tex[i]);
		glUniform1i(glGetUniformLocation(program, names[i]), LUT_UNIT + i);
	}
	glActiveTexture(GL_TEXTURE0);
}

void Atmosphere::ApplyAerialPerspective(GLuint sceneDepth, const glm::mat4& projection, const glm::mat4& view,
	const glm::vec3& cameraPos, const GraphicsSettings& gs)
{
	if (!quadVAO || aerialScale <= 0.0f) return;
	applyShader.UseShader();
	GLuint id = applyShader.GetShaderID();
	Bind(id);
	glUniformMatrix4fv(glGetUniformLocation(id, "invProjection"), 1, GL_FALSE, glm::value_ptr(glm::inverse(projection)));
	glUniformMatrix4fv(glGetUniformLocation(id, "invView"), 1, GL_FALSE, glm::value_ptr(glm::inverse(view)));
	glUniformMatrix4fv(glGetUniformLocation(id, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	glUniform3fv(glGetUniformLocation(id, "cameraPos"), 1, glm::value_ptr(cameraPos));
	glUniform3fv(glGetUniformLocation(id, "toSunDir"), 1, glm::value_ptr(lastToSun));
	glUniform1f(glGetUniformLocation(id, "distanceScale"), aerialScale);
	glUniform1i(glGetUniformLocation(id, "frame"), frame);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, sceneDepth);
	glUniform1i(glGetUniformLocation(id, "sceneDepth"), 0);

	GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
	GLboolean depthMask; glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_SRC1_COLOR); // scene * transmittance (per colour) + in-scattered light
	DrawQuad();
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	if (!blend) glDisable(GL_BLEND);
	if (depthTest) glEnable(GL_DEPTH_TEST);
	glDepthMask(depthMask);
}

glm::vec3 Atmosphere::SunTransmittance(float altitude, const glm::vec3& toSun) const
{
	auto depthAlong = [&](float mu) {
		float r = BOTTOM + std::max(altitude, 0.0f);
		float d = r * r * (mu * mu - 1.0f) + TOP * TOP;
		float len = -r * mu + std::sqrt(std::max(d, 0.0f));
		const int STEPS = 40;
		glm::vec3 depth(0.0f);
		float dt = len / STEPS;
		for (int i = 0; i < STEPS; i++) {
			float t = (i + 0.5f) * dt;
			float h = std::sqrt(r * r + t * t + 2.0f * r * mu * t) - BOTTOM;
			h = std::max(h, 0.0f);
			float ray = std::exp(-h / RAYLEIGH_HEIGHT), mie = std::exp(-h / MIE_HEIGHT);
			float oz = std::max(0.0f, 1.0f - std::abs(h - 25000.0f) / 15000.0f);
			depth += (current.rayleigh * ray + glm::vec3(current.mieExtinction * mie) + current.ozone * oz) * dt;
		}
		return depth;
	};
	glm::vec3 t = glm::exp(-depthAlong(toSun.y)) / glm::exp(-depthAlong(1.0f));
	// Behind the horizon over the sun's disc
	float r = BOTTOM + std::max(altitude, 0.0f);
	float horizonMu = -std::sqrt(std::max(1.0f - (BOTTOM / r) * (BOTTOM / r), 0.0f));
	float x = glm::clamp((toSun.y - horizonMu + 0.0047f) / 0.0094f, 0.0f, 1.0f);
	return t * (x * x * (3.0f - 2.0f * x));
}

void Atmosphere::ReadSkyIrradiance(float cameraAltitude)
{
	irradianceAltitude = cameraAltitude;
	static std::vector<float> px;
	px.resize((size_t)SKYVIEW_W * SKYVIEW_H * 4);
	glBindTexture(GL_TEXTURE_2D, skyViewTex);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, px.data());
	glBindTexture(GL_TEXTURE_2D, 0);
	// The LUT's rows (atmosphere_skyview.frag): v < 0.5 the sky, zenith angle packed toward the
	// horizon; its columns the azimuth from the sun over half the circle (the sky is symmetric)
	float r = BOTTOM + std::max(cameraAltitude, 1.0f);
	float horizon = std::acos(glm::clamp(std::sqrt(std::max(r * r - BOTTOM * BOTTOM, 0.0f)) / r, -1.0f, 1.0f));
	float horizonAngle = 0.5f * 3.14159265f + horizon;
	glm::dvec3 sum(0.0);
	for (int y = 0; y < SKYVIEW_H / 2; y++) {
		float v0 = (float)y / SKYVIEW_H, v1 = (float)(y + 1) / SKYVIEW_H;
		auto Zenith = [&](float v) { float x = 1.0f - 2.0f * v; return horizonAngle * (1.0f - x * x); };
		float z0 = std::min(Zenith(v0), 0.5f * 3.14159265f), z1 = std::min(Zenith(v1), 0.5f * 3.14159265f);
		// Cosine-weighted solid angle of the ring between the two zenith angles: pi (sin^2 z1 - sin^2 z0) / 2 per half
		float w = 0.5f * (std::sin(z1) * std::sin(z1) - std::sin(z0) * std::sin(z0));
		if (w <= 0.0f) continue;
		glm::dvec3 row(0.0); double rowW = 0.0;
		for (int x = 0; x < SKYVIEW_W; x++) {
			// Columns span azimuth u^2 * pi: each column's share of the half circle
			float u0 = (float)x / SKYVIEW_W, u1 = (float)(x + 1) / SKYVIEW_W;
			double a = (double)(u1 * u1 - u0 * u0);
			size_t i = ((size_t)y * SKYVIEW_W + x) * 4;
			row += glm::dvec3(px[i], px[i + 1], px[i + 2]) * a;
			rowW += a;
		}
		sum += row / std::max(rowW, 1e-9) * (double)w;
	}
	// integral L cos dw = 2 pi * integral avgL(z) cos z sin z dz = 2 pi * sum
	skyIrradiance = glm::vec3(sum) * 2.0f * 3.14159265f;
}

void Atmosphere::Shutdown()
{
	GLuint tex[3] = { transmittanceTex, multiScatterTex, skyViewTex };
	GLuint fbo[3] = { transmittanceFBO, multiScatterFBO, skyViewFBO };
	glDeleteTextures(3, tex);
	glDeleteFramebuffers(3, fbo);
	if (quadVAO) glDeleteVertexArrays(1, &quadVAO);
	if (quadVBO) glDeleteBuffers(1, &quadVBO);
	quadVAO = quadVBO = 0;
	built = false;
}
