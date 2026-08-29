#include "engine/render/SsaoMath.hpp"

#include <algorithm>
#include <cmath>

namespace sokoban {
namespace {

float smoothstep(float edge0, float edge1, float value)
{
    if (edge0 == edge1) {
        return value < edge0 ? 0.0f : 1.0f;
    }
    const float t = std::clamp(
        (value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

PixelExtent ssaoBufferExtent(PixelExtent renderExtent)
{
    const auto halfUp = [](uint32_t value) {
        return value / 2U + value % 2U;
    };
    return {
        .width = halfUp(renderExtent.width),
        .height = halfUp(renderExtent.height),
    };
}

float ssaoRotationNoise(uint32_t pixelX, uint32_t pixelY)
{
    uint32_t state = pixelX * 0x9e3779b9U ^ pixelY * 0x85ebca6bU;
    state ^= state >> 16U;
    state *= 0x7feb352dU;
    state ^= state >> 15U;
    state *= 0x846ca68bU;
    state ^= state >> 16U;
    return static_cast<float>(state >> 8U) * (1.0f / 16777216.0f);
}

Vec3 reconstructSsaoViewPosition(
    const Mat4& viewFromClip,
    Vec2 uv,
    float depth)
{
    const Vec4 homogeneous = transform(
        viewFromClip,
        {
            uv.x * 2.0f - 1.0f,
            1.0f - uv.y * 2.0f,
            depth,
            1.0f,
        });
    const float inverseW = std::abs(homogeneous.w) > 0.000001f
        ? 1.0f / homogeneous.w
        : 0.0f;
    return {
        homogeneous.x * inverseW,
        homogeneous.y * inverseW,
        homogeneous.z * inverseW,
    };
}

Vec2 projectSsaoViewPosition(
    const Mat4& clipFromView,
    Vec3 viewPosition)
{
    const Vec4 clip = transform(
        clipFromView,
        { viewPosition.x, viewPosition.y, viewPosition.z, 1.0f });
    const float inverseW = std::abs(clip.w) > 0.000001f
        ? 1.0f / clip.w
        : 0.0f;
    return {
        clip.x * inverseW * 0.5f + 0.5f,
        0.5f - clip.y * inverseW * 0.5f,
    };
}

Vec3 resolveSsaoViewNormal(Vec3 center, Vec3 right, Vec3 down)
{
    Vec3 normal = normalize(cross(
        subtract(right, center), subtract(down, center)));
    if (length(normal) <= 0.000001f) {
        return { 0.0f, 0.0f, -1.0f };
    }
    if (dot(normal, -center) < 0.0f) {
        normal = -normal;
    }
    return normal;
}

float ssaoSampleOcclusion(
    Vec3 center,
    Vec3 proposedSample,
    Vec3 actualSample,
    float radius,
    float bias)
{
    const float safeRadius = std::max(radius, 0.000001f);
    const float distance = length(subtract(actualSample, center));
    const float rangeWeight = 1.0f - smoothstep(
        safeRadius, safeRadius * 2.0f, distance);
    const float occluded = actualSample.z <=
            proposedSample.z - std::max(bias, 0.0f)
        ? 1.0f
        : 0.0f;
    return occluded * rangeWeight;
}

float ssaoBilateralWeight(
    Vec3 centerPosition,
    Vec3 centerNormal,
    Vec3 samplePosition,
    Vec3 sampleNormal,
    float depthSigma,
    float normalThreshold,
    float spatialWeight)
{
    const float safeSigma = std::max(depthSigma, 0.000001f);
    const float planeDistance = std::abs(dot(
        subtract(samplePosition, centerPosition), centerNormal));
    const float normalizedDistance = planeDistance / safeSigma;
    const float depthWeight = std::exp(
        -0.5f * normalizedDistance * normalizedDistance);
    const float threshold = std::clamp(normalThreshold, -1.0f, 0.999999f);
    const float normalWeight = smoothstep(
        threshold,
        1.0f,
        std::clamp(dot(centerNormal, sampleNormal), -1.0f, 1.0f));
    return std::max(spatialWeight, 0.0f) * depthWeight * normalWeight;
}

} // namespace sokoban
