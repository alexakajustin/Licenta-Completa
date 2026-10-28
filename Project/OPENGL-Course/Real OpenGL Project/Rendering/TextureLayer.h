#pragma once

#include <string>

class Texture;

// Blend modes for multi-texture layers
enum class LayerBlendMode {
	Normal,      // Simple alpha-blend (use mesh UVs)
	Height,      // Blend by world-space Y height
	Slope,       // Blend by surface slope (dot with up vector)
	HeightSlope  // Combined height + slope (great for terrain)
};

// A complete surface layer: diffuse + optional normal map + blend parameters
struct TextureLayer {
	Texture* texture = nullptr;         // Diffuse/albedo
	Texture* normalMap = nullptr;       // Normal map (optional)
	Texture* displacementMap = nullptr; // Height/displacement map (optional)
	LayerBlendMode blendMode = LayerBlendMode::Normal;
	float opacity = 1.0f;               // Overall layer influence [0..1]
	float tiling = 1.0f;                // Texture repeat scale
	float displacementScale = 0.05f;     // Physical depth intensity [0..1]

	// Height blending params (used when blendMode == Height or HeightSlope)
	float heightMin = 0.0f;           // Below this: layer invisible
	float heightMax = 100.0f;         // Above this: layer fully visible

	// Slope blending params (used when blendMode == Slope or HeightSlope)
	float slopeMin = 0.0f;            // Below this slope: layer invisible (0=flat)
	float slopeMax = 0.5f;            // Above this slope: layer fully visible (1=vertical)

	bool invert = false;              // Invert the computed blend mask

	// For serialization
	std::string texturePath;
	std::string normalMapPath;
	std::string displacementMapPath;
};

static const int MAX_TEXTURE_LAYERS = 5;

// Texture units for the layer samplers (shader.frag). Chosen to avoid every unit the
// renderer binds for lighting: 0/1 diffuse+normal, 2 skybox, 3 directional shadow,
// 4-9 omni shadows, 14-15 scene depth/reflection, 20 directional shadow colour,
// 21-23 point and 25-27 spot shadow colour. Binding a sampler2D to the same unit as one
// of those (sampler2DArray / samplerCube) makes the draw call invalid.
static const int LAYER_DIFFUSE_UNITS[MAX_TEXTURE_LAYERS]      = { 10, 12, 24, 29, 31 };
static const int LAYER_NORMAL_UNITS[MAX_TEXTURE_LAYERS]       = { 11, 13, 28, 30, 32 };
static const int LAYER_DISPLACEMENT_UNITS[MAX_TEXTURE_LAYERS] = { 16, 17, 18, 19, 33 };
