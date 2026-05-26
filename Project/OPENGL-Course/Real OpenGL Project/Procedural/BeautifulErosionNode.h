#pragma once

#include "Nodes/NodeGraph.h"
#include <glm/glm.hpp>

// Generates procedural erosion over the incoming MeshData stream using the Phacelle Noise analytical algorithm.
class BeautifulErosionNode : public GraphNode
{
public:
	BeautifulErosionNode(NodeGraph& graph);

	void RenderContent(SceneManager* scene) override;
	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override;

	json Serialize() const override;
	void Deserialize(const json& j) override;

private:
	// Mathematical helper structures
	struct ErosionResult {
		glm::vec3 heightAndSlopeDelta;
		float magnitude;
		float ridgeMap;
		float debug;
	};

	// Algorithm parameters
	float erosionScale = 0.15f;
	float erosionStrength = 0.22f;
	float erosionGullyWeight = 0.5f;
	float erosionDetail = 1.5f;
	
	glm::vec4 erosionRounding = glm::vec4(0.3f, 0.0f, 0.1f, 2.0f);
	glm::vec4 erosionOnset = glm::vec4(0.7f, 1.25f, 2.8f, 1.5f);
	glm::vec2 erosionAssumedSlope = glm::vec2(0.7f, 1.0f);
	
	float erosionCellScale = 0.7f;
	float erosionNormalization = 0.5f;
	
	int erosionOctaves = 5;
	float erosionLacunarity = 2.0f;
	float erosionGain = 0.5f;

	glm::vec2 terrainHeightOffset = glm::vec2(0.0f, 0.0f);
	int smoothPasses = 1;

	// Internal Algorithm methods
	glm::vec2 Hash(glm::vec2 x) const;
	glm::vec4 PhacelleNoise(glm::vec2 p, glm::vec2 normDir, float freq, float offset, float normalization) const;
	ErosionResult ErosionFilter(glm::vec2 p, glm::vec3 heightAndSlope, float fadeTarget, int gridRes) const;

	float PowInv(float t, float power) const;
	float EaseOut(float t) const;
	float SmoothStart(float t, float smoothing) const;
	glm::vec2 SafeNormalize(glm::vec2 n) const;
	float Clamp01(float x) const;

	// Mesh processing
	void RecomputeNormals(MeshData& data, int resX, int resZ);
};
