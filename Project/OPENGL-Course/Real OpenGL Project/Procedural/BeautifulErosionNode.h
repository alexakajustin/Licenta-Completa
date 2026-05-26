#pragma once

#include "Nodes/NodeGraph.h"
#include <glm/glm.hpp>

/**
 * @class BeautifulErosionNode
 * @brief Generates procedural erosion over the incoming MeshData stream using the Phacelle Noise analytical algorithm.
 */
class BeautifulErosionNode : public GraphNode
{
public:
	/**
	 * @brief Constructor.
	 * @param graph NodeGraph that owns this node.
	 */
	BeautifulErosionNode(NodeGraph& graph);

	/**
	 * @brief Renders the editor UI panel for configuring erosion variables.
	 * @param scene Pointer to active scene manager.
	 */
	void RenderContent(SceneManager* scene) override;

	/**
	 * @brief Evaluates the node, applying the erosion algorithm to the mesh data in-place.
	 * @param scene Active scene manager.
	 * @param progress Callback for updating execution progress.
	 */
	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override;

	/**
	 * @brief Serializes node properties into JSON.
	 * @return JSON object.
	 */
	json Serialize() const override;

	/**
	 * @brief Deserializes node properties from JSON.
	 * @param j JSON object.
	 */
	void Deserialize(const json& j) override;

private:
	/**
	 * @struct ErosionResult
	 * @brief Stores temporary results for intermediate height and slope calculations.
	 */
	struct ErosionResult {
		glm::vec3 heightAndSlopeDelta; ///< Delta vectors for height and slope.
		float magnitude;               ///< Magnitude of erosion.
		float ridgeMap;                ///< Ridge visibility map.
		float debug;                   ///< Debug helper field.
	};

	// Algorithm parameters
	float erosionScale = 0.15f; ///< General frequency scaling.
	float erosionStrength = 0.22f; ///< Amplitude of the height adjustment.
	float erosionGullyWeight = 0.5f; ///< Gully-like carving factor.
	float erosionDetail = 1.5f; ///< Detail frequency multiplier.
	
	glm::vec4 erosionRounding = glm::vec4(0.3f, 0.0f, 0.1f, 2.0f); ///< Parameter bounds for erosion rounding.
	glm::vec4 erosionOnset = glm::vec4(0.7f, 1.25f, 2.8f, 1.5f); ///< Threshold parameters for erosion onset.
	glm::vec2 erosionAssumedSlope = glm::vec2(0.7f, 1.0f); ///< Assumed bounds for scaling based on slope steepness.
	
	float erosionCellScale = 0.7f; ///< Scale factor for Voronoi-like cells.
	float erosionNormalization = 0.5f; ///< Normalization multiplier.
	
	int erosionOctaves = 5; ///< Number of fractal octaves.
	float erosionLacunarity = 2.0f; ///< Lacunarity for octave frequency scaling.
	float erosionGain = 0.5f; ///< Gain for octave amplitude attenuation.

	glm::vec2 terrainHeightOffset = glm::vec2(0.0f, 0.0f); ///< Constant coordinate translation offset.
	int smoothPasses = 1; ///< Number of post-processing height smoothing filter runs.

	// Internal Algorithm methods
	
	/**
	 * @brief Basic 2D hashing helper.
	 */
	glm::vec2 Hash(glm::vec2 x) const;

	/**
	 * @brief Computes Phacelle noise values at a coordinate.
	 */
	glm::vec4 PhacelleNoise(glm::vec2 p, glm::vec2 normDir, float freq, float offset, float normalization) const;

	/**
	 * @brief Computes erosion amounts based on height and slope inputs.
	 */
	ErosionResult ErosionFilter(glm::vec2 p, glm::vec3 heightAndSlope, float fadeTarget, int gridRes) const;

	float PowInv(float t, float power) const;
	float EaseOut(float t) const;
	float SmoothStart(float t, float smoothing) const;
	glm::vec2 SafeNormalize(glm::vec2 n) const;
	float Clamp01(float x) const;

	/**
	 * @brief Helper to rebuild vertex normals of the terrain mesh.
	 */
	void RecomputeNormals(MeshData& data, int resX, int resZ);
};
