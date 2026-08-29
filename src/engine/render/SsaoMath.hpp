#pragma once

#include "engine/Math.hpp"
#include "engine/render/RenderResolution.hpp"

#include <cstdint>

namespace sokoban {

// Half resolution rounds up so every full-resolution edge pixel maps to a
// valid AO texel, including odd-sized and one-pixel render targets.
[[nodiscard]] PixelExtent ssaoBufferExtent(PixelExtent renderExtent);

// CPU reference for the estimator's per-pixel kernel rotation. Both axes are
// deliberately mixed to prevent row-correlated noise from becoming bands.
[[nodiscard]] float ssaoRotationNoise(uint32_t pixelX, uint32_t pixelY);

// Vulkan depth is already NDC z in [0, 1]. UV y runs down the framebuffer,
// while NDC y runs up because the scene uses a negative-height viewport.
[[nodiscard]] Vec3 reconstructSsaoViewPosition(
    const Mat4& viewFromClip,
    Vec2 uv,
    float depth);
[[nodiscard]] Vec2 projectSsaoViewPosition(
    const Mat4& clipFromView,
    Vec3 viewPosition);

// Reference for the derivative normal reconstructed in ssao.frag.glsl.
// The result is always oriented toward the camera at view-space origin.
[[nodiscard]] Vec3 resolveSsaoViewNormal(
    Vec3 center,
    Vec3 right,
    Vec3 down);

// Reference for the shader's physical sample comparison. View space uses +Z
// away from the camera, so geometry with a smaller z than the proposed sample
// occludes it. The Euclidean range fade rejects unrelated silhouettes.
[[nodiscard]] float ssaoSampleOcclusion(
    Vec3 center,
    Vec3 proposedSample,
    Vec3 actualSample,
    float radius,
    float bias);

// Reference for the composite shader's bilateral upsample. Plane distance
// rejects depth discontinuities while the normal term rejects adjoining faces;
// spatialWeight is the ordinary bilinear contribution of the AO texel.
[[nodiscard]] float ssaoBilateralWeight(
    Vec3 centerPosition,
    Vec3 centerNormal,
    Vec3 samplePosition,
    Vec3 sampleNormal,
    float depthSigma,
    float normalThreshold,
    float spatialWeight);

} // namespace sokoban
