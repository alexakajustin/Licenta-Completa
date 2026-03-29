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
	Texture* texture = nullptr;       // Diffuse/albedo
	Texture* normalMap = nullptr;     // Normal map (optional)
	LayerBlendMode blendMode = LayerBlendMode::Normal;
	float opacity = 1.0f;             // Overall layer influence [0..1]
	float tiling = 1.0f;              // Texture repeat scale

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
};

static const int MAX_TEXTURE_LAYERS = 4;
