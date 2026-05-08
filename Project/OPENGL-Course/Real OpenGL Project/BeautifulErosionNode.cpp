#include "BeautifulErosionNode.h"
#include "imgui.h"
#include <cmath>
#include <thread>
#include <vector>

#define TAU 6.28318530717959f

BeautifulErosionNode::BeautifulErosionNode(NodeGraph& graph)
{
	id = graph.NextNodeId();
	title = "Beautiful Erosion";

	Pin meshIn(graph.NextPinId(), PinDataType::Mesh, "Mesh");
	inputs.push_back(meshIn);

	Pin meshOut(graph.NextPinId(), PinDataType::Mesh, "Mesh");
	outputs.push_back(meshOut);
}

void BeautifulErosionNode::RenderContent(SceneManager* scene)
{
	ImGui::PushItemWidth(120.0f);
	ImGui::Text("Erosion Parameters");
	ImGui::DragFloat("Scale", &erosionScale, 0.01f, 0.01f, 1.0f);
	ImGui::DragFloat("Strength", &erosionStrength, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Gully Weight", &erosionGullyWeight, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("Detail", &erosionDetail, 0.01f, 0.0f, 3.0f);
	ImGui::DragInt("Smooth Passes", &smoothPasses, 1, 0, 10);
	
	ImGui::Text("Advanced Rounding");
	ImGui::SliderFloat("Ridge Rounding", &erosionRounding.x, 0.0f, 1.0f);
	ImGui::SliderFloat("Crease Rounding", &erosionRounding.y, 0.0f, 1.0f);
	ImGui::SliderFloat("Base Rounding", &erosionRounding.z, 0.0f, 2.0f);
	ImGui::SliderFloat("Octave Rounding", &erosionRounding.w, 1.0f, 4.0f);

	ImGui::Separator();
	ImGui::Text("Noise Properties");
	ImGui::DragInt("Octaves", &erosionOctaves, 1, 1, 10);
	ImGui::DragFloat("Lacunarity", &erosionLacunarity, 0.05f, 1.0f, 5.0f);
	ImGui::DragFloat("Gain", &erosionGain, 0.05f, 0.0f, 2.0f);
	ImGui::DragFloat("Cell Scale", &erosionCellScale, 0.05f, 0.1f, 2.0f);
	ImGui::DragFloat("Normalization", &erosionNormalization, 0.05f, 0.0f, 1.0f);
	
	ImGui::PopItemWidth();
}

json BeautifulErosionNode::Serialize() const
{
	json j = GraphNode::Serialize();
	j["erosionScale"] = erosionScale;
	j["erosionStrength"] = erosionStrength;
	j["erosionGullyWeight"] = erosionGullyWeight;
	j["erosionDetail"] = erosionDetail;
	j["erosionOctaves"] = erosionOctaves;
	j["erosionLacunarity"] = erosionLacunarity;
	j["erosionGain"] = erosionGain;
	j["erosionCellScale"] = erosionCellScale;
	j["erosionNormalization"] = erosionNormalization;
	j["smoothPasses"] = smoothPasses;
	j["erosionRounding"] = { erosionRounding.x, erosionRounding.y, erosionRounding.z, erosionRounding.w };
	j["erosionOnset"] = { erosionOnset.x, erosionOnset.y, erosionOnset.z, erosionOnset.w };
	return j;
}

void BeautifulErosionNode::Deserialize(const json& j)
{
	GraphNode::Deserialize(j);
	erosionScale = j.value("erosionScale", 0.15f);
	erosionStrength = j.value("erosionStrength", 0.22f);
	erosionGullyWeight = j.value("erosionGullyWeight", 0.5f);
	erosionDetail = j.value("erosionDetail", 1.5f);
	erosionOctaves = j.value("erosionOctaves", 5);
	erosionLacunarity = j.value("erosionLacunarity", 2.0f);
	erosionGain = j.value("erosionGain", 0.5f);
	erosionCellScale = j.value("erosionCellScale", 0.7f);
	erosionNormalization = j.value("erosionNormalization", 0.5f);
	smoothPasses = j.value("smoothPasses", 1);
	
	if (j.contains("erosionRounding")) {
		auto r = j["erosionRounding"];
		erosionRounding = glm::vec4(r[0], r[1], r[2], r[3]);
	} else {
		erosionRounding = glm::vec4(0.3f, 0.0f, 0.1f, 2.0f);
	}
	if (j.contains("erosionOnset")) {
		auto o = j["erosionOnset"];
		erosionOnset = glm::vec4(o[0], o[1], o[2], o[3]);
	}
}

float BeautifulErosionNode::Clamp01(float x) const { return glm::clamp(x, 0.0f, 1.0f); }
float BeautifulErosionNode::PowInv(float t, float power) const { return 1.0f - std::pow(1.0f - Clamp01(t), power); }
float BeautifulErosionNode::EaseOut(float t) const { float v = 1.0f - Clamp01(t); return 1.0f - v * v; }
float BeautifulErosionNode::SmoothStart(float t, float smoothing) const {
	if (t >= smoothing) return t - 0.5f * smoothing;
	if (smoothing <= 0.0f) return t;
	return 0.5f * t * t / smoothing;
}
glm::vec2 BeautifulErosionNode::SafeNormalize(glm::vec2 n) const {
	float l = glm::length(n);
	return (std::abs(l) > 1e-10f) ? (n / l) : n;
}

glm::vec2 BeautifulErosionNode::Hash(glm::vec2 x) const {
	const glm::vec2 k(0.3183099f, 0.3678794f);
	x = x * k + glm::vec2(k.y, k.x);
	float val = x.x * x.y * (x.x + x.y);
	float fract_val = val - std::floor(val);
	glm::vec2 mult = 16.0f * k * fract_val;
	glm::vec2 fract_mult(mult.x - std::floor(mult.x), mult.y - std::floor(mult.y));
	return glm::vec2(-1.0f) + 2.0f * fract_mult;
}

glm::vec4 BeautifulErosionNode::PhacelleNoise(glm::vec2 p, glm::vec2 normDir, float freq, float offset, float normalization) const {
	glm::vec2 sideDir = glm::vec2(-normDir.y, normDir.x) * freq * TAU;
	offset *= TAU;

	glm::vec2 pInt = glm::floor(p);
	glm::vec2 pFrac = p - pInt;
	glm::vec2 phaseDir(0.0f);
	float weightSum = 0.0f;

	for (int i = -1; i <= 2; i++) {
		for (int j = -1; j <= 2; j++) {
			glm::vec2 gridOffset((float)i, (float)j);
			glm::vec2 gridPoint = pInt + gridOffset;
			glm::vec2 randomOffset = Hash(gridPoint) * 0.5f;

			glm::vec2 vectorFromCellPoint = pFrac - gridOffset - randomOffset;
			float sqrDist = glm::dot(vectorFromCellPoint, vectorFromCellPoint);
			float weight = std::exp(-sqrDist * 2.0f);
			weight = std::max(0.0f, weight - 0.01111f);

			weightSum += weight;

			float waveInput = glm::dot(vectorFromCellPoint, sideDir) + offset;
			phaseDir += glm::vec2(std::cos(waveInput), std::sin(waveInput)) * weight;
		}
	}

	glm::vec2 interpolated = phaseDir / weightSum;
	float magnitude = std::sqrt(glm::dot(interpolated, interpolated));
	magnitude = std::max(1.0f - normalization, magnitude);
	return glm::vec4(interpolated.x / magnitude, interpolated.y / magnitude, sideDir.x, sideDir.y);
}

BeautifulErosionNode::ErosionResult BeautifulErosionNode::ErosionFilter(glm::vec2 p, glm::vec3 heightAndSlope, float fadeTarget, int gridRes) const {
	float strength = erosionStrength * erosionScale;
	fadeTarget = glm::clamp(fadeTarget, -1.0f, 1.0f);

	glm::vec3 inputHeightAndSlope = heightAndSlope;
	float freq = 1.0f / (erosionScale * erosionCellScale);
	float slopeLength = std::max(glm::length(glm::vec2(heightAndSlope.y, heightAndSlope.z)), 1e-10f);
	float magnitude = 0.0f;
	float roundingMult = 1.0f;

	float roundingForInput = glm::mix(erosionRounding.y, erosionRounding.x, Clamp01(fadeTarget + 0.5f)) * erosionRounding.z;
	float combiMask = EaseOut(SmoothStart(slopeLength * erosionOnset.x, roundingForInput * erosionOnset.x));

	float ridgeMapCombiMask = EaseOut(slopeLength * erosionOnset.z);
	float ridgeMapFadeTarget = fadeTarget;

	glm::vec2 actualSlope(heightAndSlope.y, heightAndSlope.z);
	glm::vec2 assumedSlopeVec = actualSlope / slopeLength * erosionAssumedSlope.x;
	glm::vec2 gullySlope = glm::mix(actualSlope, assumedSlopeVec, erosionAssumedSlope.y);

	float currentFreq = freq;
	float currentStrength = strength;

	for (int i = 0; i < erosionOctaves; i++) {
		// Prevent Nyquist Aliasing on low-poly grids
		float maxFreq = (gridRes - 1) / 3.0f; // Max representable waves
		float freqRatio = currentFreq / maxFreq;
		float nyquistFade = 1.0f;
		if (freqRatio > 0.5f) {
			nyquistFade = glm::clamp(1.0f - (freqRatio - 0.5f) * 2.0f, 0.0f, 1.0f);
		}
		if (nyquistFade <= 0.0f) break; // Don't compute sub-pixel noise

		glm::vec4 phacelle = PhacelleNoise(p * currentFreq, SafeNormalize(gullySlope), erosionCellScale, 0.25f, erosionNormalization);
		phacelle.z *= -currentFreq;
		phacelle.w *= -currentFreq;

		float sloping = std::abs(phacelle.y);
		float signY = (phacelle.y > 0.0f) ? 1.0f : ((phacelle.y < 0.0f) ? -1.0f : 0.0f);

		gullySlope += signY * glm::vec2(phacelle.z, phacelle.w) * currentStrength * nyquistFade * erosionGullyWeight;

		glm::vec3 gullies(phacelle.x, phacelle.y * phacelle.z, phacelle.y * phacelle.w);
		glm::vec3 fadedGullies = glm::mix(glm::vec3(fadeTarget, 0.0f, 0.0f), gullies * erosionGullyWeight, combiMask);

		heightAndSlope += fadedGullies * currentStrength * nyquistFade;
		magnitude += currentStrength * nyquistFade;
		fadeTarget = fadedGullies.x;

		float roundingForOctave = glm::mix(erosionRounding.y, erosionRounding.x, Clamp01(phacelle.x + 0.5f)) * roundingMult;
		float newMask = EaseOut(SmoothStart(sloping * erosionOnset.y, roundingForOctave * erosionOnset.y));
		combiMask = PowInv(combiMask, erosionDetail) * newMask;

		ridgeMapFadeTarget = glm::mix(ridgeMapFadeTarget, gullies.x, ridgeMapCombiMask);
		float newRidgeMapMask = EaseOut(sloping * erosionOnset.w);
		ridgeMapCombiMask = ridgeMapCombiMask * newRidgeMapMask;

		currentStrength *= erosionGain;
		currentFreq *= erosionLacunarity;
		roundingMult *= erosionRounding.w;
	}

	ErosionResult res;
	res.ridgeMap = ridgeMapFadeTarget * (1.0f - ridgeMapCombiMask);
	res.debug = fadeTarget;
	res.heightAndSlopeDelta = heightAndSlope - inputHeightAndSlope;
	res.magnitude = magnitude;

	return res;
}

void BeautifulErosionNode::Execute(SceneManager& scene, NodeProgressCallback progress)
{
	outputs[0].data.Clear();
	PinData inputData = inputs[0].data;
	if (inputData.type != PinDataType::Mesh) return;

	MeshData data = inputData.meshData;
	if (data.vertices.empty()) return;

	int totalVerts = (int)data.vertices.size() / 14;
	int gridRes = (int)std::sqrt(totalVerts);

	if (gridRes * gridRes != totalVerts)
	{
		outputs[0].data = inputData;
		return;
	}

	if (progress) progress(10.0f, "Preparing Beautiful Erosion...");

	std::vector<float> originalHeights(gridRes * gridRes);
	float minH = 1e10f, maxH = -1e10f;
	for (int i = 0; i < gridRes * gridRes; i++) {
		float h = data.vertices[i * 14 + 1];
		originalHeights[i] = h;
		if (h < minH) minH = h;
		if (h > maxH) maxH = h;
	}
	float hRange = std::max(maxH - minH, 0.01f);
	float hCenter = (maxH + minH) * 0.5f;

	std::vector<float> ridgeMapBuffer(gridRes * gridRes);

	unsigned int numThreads = std::thread::hardware_concurrency();
	if (numThreads == 0) numThreads = 4;
	std::vector<std::thread> threads;
	int perThread = gridRes / numThreads;

	for (unsigned int t = 0; t < numThreads; t++)
	{
		int startZ = t * perThread;
		int endZ = (t == numThreads - 1) ? gridRes : (t + 1) * perThread;

		threads.emplace_back([this, &data, &originalHeights, &ridgeMapBuffer, gridRes, startZ, endZ, minH, hRange, hCenter]() {
			for (int z = startZ; z < endZ; z++) {
				for (int x = 0; x < gridRes; x++) {
					int idx = z * gridRes + x;
					float h = originalHeights[idx];

					float hL = (x > 0) ? originalHeights[z * gridRes + (x - 1)] : h;
					float hR = (x < gridRes - 1) ? originalHeights[z * gridRes + (x + 1)] : h;
					float hD = (z > 0) ? originalHeights[(z - 1) * gridRes + x] : h;
					float hU = (z < gridRes - 1) ? originalHeights[(z + 1) * gridRes + x] : h;

					// Map p to [-1, 1] space to match Shadertoy
					glm::vec2 p = glm::vec2((float)x / (gridRes - 1), (float)z / (gridRes - 1)) * 2.0f - 1.0f;
					float pStep = 2.0f / (gridRes - 1);

					// Normalize heights to [0, 1]
					float hNorm = (h - minH) / hRange;
					float hLNorm = (hL - minH) / hRange;
					float hRNorm = (hR - minH) / hRange;
					float hDNorm = (hD - minH) / hRange;
					float hUNorm = (hU - minH) / hRange;

					// Compute slope in normalized space
					float slopeX = (hRNorm - hLNorm) / (2.0f * pStep);
					float slopeZ = (hUNorm - hDNorm) / (2.0f * pStep);

					float fadeTarget = glm::clamp((h - hCenter) / (hRange * 0.5f), -1.0f, 1.0f);

					ErosionResult er = ErosionFilter(p, glm::vec3(hNorm, slopeX, slopeZ), fadeTarget, gridRes);

					float offset = glm::mix(terrainHeightOffset.x, -fadeTarget, terrainHeightOffset.y) * er.magnitude;
					
					// Scale the normalized delta back to world space
					float worldDelta = er.heightAndSlopeDelta.x * hRange;
					float worldOffset = offset * hRange;

					float erodedHeight = h + worldDelta + worldOffset;

					data.vertices[idx * 14 + 1] = erodedHeight;
					ridgeMapBuffer[idx] = er.ridgeMap;
				}
			}
		});
	}

	for (auto& th : threads) th.join();

	if (smoothPasses > 0)
	{
		if (progress) progress(85.0f, "Smoothing Terrain...");
		std::vector<float> heightBuffer(gridRes * gridRes);
		for (int pass = 0; pass < smoothPasses; pass++)
		{
			for (int i = 0; i < gridRes * gridRes; i++) heightBuffer[i] = data.vertices[i * 14 + 1];
			for (int z = 1; z < gridRes - 1; z++)
			{
				for (int x = 1; x < gridRes - 1; x++)
				{
					int idx = z * gridRes + x;
					float center = heightBuffer[idx] * 4.0f;
					float cross = heightBuffer[idx - 1] + heightBuffer[idx + 1] + heightBuffer[idx - gridRes] + heightBuffer[idx + gridRes];
					float diag = heightBuffer[idx - gridRes - 1] + heightBuffer[idx - gridRes + 1] + heightBuffer[idx + gridRes - 1] + heightBuffer[idx + gridRes + 1];
					data.vertices[idx * 14 + 1] = (center + cross * 2.0f + diag) / 16.0f;
				}
			}
		}
	}

	if (progress) progress(90.0f, "Recomputing Normals...");
	RecomputeNormals(data, gridRes - 1, gridRes - 1);

	// Encode RidgeMap into Tangent Length for optional shader usage (rivers)
	for (int i = 0; i < gridRes * gridRes; i++) {
		float r = ridgeMapBuffer[i]; 
		float scale = 2.0f + glm::clamp(r, -1.0f, 1.0f); // Length will be 1.0 to 3.0
		int base = i * 14;
		data.vertices[base + 8] *= scale;
		data.vertices[base + 9] *= scale;
		data.vertices[base + 10] *= scale;
	}

	if (progress) progress(100.0f, "Done!");
	outputs[0].data = inputData;
	outputs[0].data.meshData = data;
}

void BeautifulErosionNode::RecomputeNormals(MeshData& data, int resX, int resZ)
{
	const int stride = 14;
	int gridW = resX + 1;
	int gridH = resZ + 1;
	int totalVerts = gridW * gridH;

	for (int i = 0; i < totalVerts; i++) {
		int base = i * stride;
		data.vertices[base + 5] = 0.0f;
		data.vertices[base + 6] = 0.0f;
		data.vertices[base + 7] = 0.0f;
	}

	for (size_t i = 0; i + 2 < data.indices.size(); i += 3) {
		unsigned int i0 = data.indices[i], i1 = data.indices[i + 1], i2 = data.indices[i + 2];
		int b0 = i0 * stride, b1 = i1 * stride, b2 = i2 * stride;
		glm::vec3 v0(data.vertices[b0], data.vertices[b0 + 1], data.vertices[b0 + 2]);
		glm::vec3 v1(data.vertices[b1], data.vertices[b1 + 1], data.vertices[b1 + 2]);
		glm::vec3 v2(data.vertices[b2], data.vertices[b2 + 1], data.vertices[b2 + 2]);
		glm::vec3 faceNormal = glm::cross(v1 - v0, v2 - v0);
		for (int j = 0; j < 3; j++) {
			data.vertices[b0 + 5 + j] += faceNormal[j];
			data.vertices[b1 + 5 + j] += faceNormal[j];
			data.vertices[b2 + 5 + j] += faceNormal[j];
		}
	}

	for (int i = 0; i < totalVerts; i++) {
		int base = i * stride;
		glm::vec3 n(data.vertices[base + 5], data.vertices[base + 6], data.vertices[base + 7]);
		float len = glm::length(n);
		n = (len > 0.0001f) ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
		data.vertices[base + 5] = n.x; data.vertices[base + 6] = n.y; data.vertices[base + 7] = n.z;
		
		glm::vec3 ref = (std::abs(n.y) < 0.999f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
		glm::vec3 tangent = glm::normalize(ref - n * glm::dot(ref, n));
		glm::vec3 bitangent = glm::cross(n, tangent);
		
		data.vertices[base + 8] = tangent.x; data.vertices[base + 9] = tangent.y; data.vertices[base + 10] = tangent.z;
		data.vertices[base + 11] = bitangent.x; data.vertices[base + 12] = bitangent.y; data.vertices[base + 13] = bitangent.z;
	}
}
