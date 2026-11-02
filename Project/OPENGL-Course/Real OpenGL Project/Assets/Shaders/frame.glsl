// =====================================================================
// frame.glsl - per-frame values shared by every shader (Rendering/TemporalAA.h)
// =====================================================================

layout(std140) uniform FrameBlock {
    vec4 frameJitter;       ///< xy: sub-pixel jitter of the scene projection (pixels), z: dither offset [0, 1), w: 1 = TAA on
    mat4 frameViewProj;     ///< This frame's camera, without the jitter (motion vectors)
    mat4 framePrevViewProj; ///< Last frame's camera, without the jitter
};

/// Threshold of a dithered fade from its 4x4 Bayer value. While TAA is on, the threshold moves
/// every frame (the same for the outgoing and incoming LOD, so they stay complementary): the
/// accumulated frames average the pattern into a smooth blend. Without TAA it stays put.
float DitherThreshold(float bayer)
{
    return fract(bayer + frameJitter.z);
}
