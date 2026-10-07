#include "engine/render/GroundRimGeometry.hpp"
#include "engine/render/IsoScenePreparer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace sokoban {
namespace {

uint64_t saturatedAdd(uint64_t a, uint64_t b)
{
    return b > std::numeric_limits<uint64_t>::max() - a
        ? std::numeric_limits<uint64_t>::max() : a + b;
}

uint64_t saturatedMultiply(uint64_t count, uint64_t multiplier)
{
    return count > std::numeric_limits<uint64_t>::max() / multiplier
        ? std::numeric_limits<uint64_t>::max() : count * multiplier;
}

GroundRimSample edgeRamp(float distance, Vec2 distanceGradient, float width)
{
    const float raw = 1.0f - distance / width;
    return {
        .drop = std::clamp(raw, 0.0f, 1.0f),
        .gradient = raw >= 0.0f && raw <= 1.0f
            ? distanceGradient * (-1.0f / width) : Vec2 {},
    };
}

struct Deformation {
    Vec3 position {};
    // Last row of d(deformed position)/d(source position). The first two
    // rows are identity because horizontal coordinates remain untouched.
    Vec3 jacobian { 0.0f, 0.0f, 1.0f };
    bool active = false;
};

Deformation deformation(Vec3 position, const GroundRimProfile& profile)
{
    Deformation result { .position = position };
    const GroundRimSample sample = sampleGroundRim(
        { position.x, position.y }, profile);
    if (sample.drop <= 0.0f || !std::isfinite(position.z)) {
        return result;
    }
    const float rawBand = (position.z - (1.0f - profile.bodyBand)) /
        profile.bodyBand;
    const float band = std::clamp(rawBand, 0.0f, 1.0f);
    if (band <= 0.0f) {
        return result;
    }
    const float derivative = rawBand <= 1.0f ? 1.0f / profile.bodyBand : 0.0f;
    result.position.z -= band * sample.drop;
    result.jacobian = {
        -band * sample.gradient.x,
        -band * sample.gradient.y,
        1.0f - derivative * sample.drop,
    };
    result.active = true;
    return result;
}

} // namespace

uint64_t ordinarySceneDrawInstanceReserve(
    const RenderFrameData& frame,
    const PreparedRenderScene& scene) noexcept
{
    const uint64_t sceneDraws = saturatedAdd(
        saturatedAdd(scene.opaqueFaceIndices.size(), scene.translucentFaceIndices.size()),
        saturatedAdd(scene.opaqueModelIndices.size(), scene.translucentModelIndices.size()));
    uint64_t reserve = saturatedAdd(scene.shadowFaces.size(), saturatedMultiply(sceneDraws, 2));
    reserve = saturatedAdd(reserve, scene.particles.size());
    reserve = saturatedAdd(reserve, frame.waterSurfaces.size());
#if SOKOBAN_ENABLE_DEBUG_UI
    reserve = saturatedAdd(reserve, frame.debugItemOutlines.size());
    reserve = saturatedAdd(reserve, frame.debugItemLinks.size());
    reserve = saturatedAdd(reserve, saturatedMultiply(frame.debugItemLabels.size(),
        RenderFrameData::DebugItemLabel::textCapacity + 1ULL));
#endif
    if (frame.viewMode == RenderViewMode::TopDown2D) {
        reserve = saturatedAdd(reserve, frame.tiles.size());
        reserve = saturatedAdd(reserve,
            2ULL * (uint64_t { frame.levelWidth } + frame.levelHeight + 2ULL));
    }
    return reserve;
}

uint64_t ordinaryFrameDrawInstanceReserve(
    uint64_t mainSceneReserve,
    uint64_t previewSceneReserve,
    uint64_t uiInstanceCount) noexcept
{
    return saturatedAdd(saturatedAdd(
        saturatedAdd(mainSceneReserve, previewSceneReserve), uiInstanceCount), 64);
}

bool groundRimDrawInstanceBudgetFits(
    uint64_t mainSceneReserve,
    uint64_t previewSceneReserve,
    uint64_t uiInstanceCount,
    uint64_t capacity) noexcept
{
    // Never let adversarial counts wrap into an apparently small reserve.
    if (mainSceneReserve > capacity) return false;
    capacity -= mainSceneReserve;
    if (previewSceneReserve > capacity) return false;
    capacity -= previewSceneReserve;
    if (uiInstanceCount > capacity) return false;
    return 64 <= capacity - uiInstanceCount;
}

bool groundRimProfileValid(const GroundRimProfile& profile) noexcept
{
    return std::isfinite(profile.width) && std::isfinite(profile.depth) &&
        std::isfinite(profile.bodyBand) && profile.width > 0.0f &&
        profile.width <= 0.5f && profile.depth > 0.0f &&
        profile.bodyBand > profile.depth && profile.bodyBand <= 1.0f;
}

GroundRimSample sampleGroundRim(
    Vec2 localPosition,
    const GroundRimProfile& profile) noexcept
{
    if (!groundRimProfileValid(profile) || !std::isfinite(localPosition.x) ||
        !std::isfinite(localPosition.y)) {
        return {};
    }
    const std::array<GroundRimSample, 4> edges {
        edgeRamp(localPosition.y, { 0.0f, 1.0f }, profile.width),
        edgeRamp(1.0f - localPosition.x, { -1.0f, 0.0f }, profile.width),
        edgeRamp(1.0f - localPosition.y, { 0.0f, -1.0f }, profile.width),
        edgeRamp(localPosition.x, { 1.0f, 0.0f }, profile.width),
    };
    GroundRimSample result;
    const auto accumulate = [&result](GroundRimSample candidate) {
        if (candidate.drop > result.drop) {
            result = candidate;
        }
    };
    for (uint32_t side = 0; side < edges.size(); ++side) {
        if ((profile.exposedSides & (1U << side)) != 0) {
            accumulate(edges[side]);
        }
    }
    constexpr std::array<std::array<uint32_t, 2>, 4> adjacentSides {
        std::array<uint32_t, 2> { 0, 3 },
        std::array<uint32_t, 2> { 0, 1 },
        std::array<uint32_t, 2> { 2, 1 },
        std::array<uint32_t, 2> { 2, 3 },
    };
    for (uint32_t corner = 0; corner < adjacentSides.size(); ++corner) {
        if ((profile.concaveCorners & (1U << corner)) == 0) {
            continue;
        }
        const GroundRimSample a = edges[adjacentSides[corner][0]];
        const GroundRimSample b = edges[adjacentSides[corner][1]];
        accumulate(a.drop <= b.drop ? a : b);
    }
    result.drop *= profile.depth;
    result.gradient = result.gradient * profile.depth;
    return result;
}

Vec3 deformGroundRockPosition(
    Vec3 localPosition,
    const GroundRimProfile& profile) noexcept
{
    return deformation(localPosition, profile).position;
}

MeshVertex deformGroundRockVertex(
    const MeshVertex& vertex,
    const GroundRimProfile& profile) noexcept
{
    const Deformation mapped = deformation(vertex.position, profile);
    if (!mapped.active) {
        return vertex;
    }
    MeshVertex result = vertex;
    result.position = mapped.position;
    const float normalZ = vertex.normal.z / mapped.jacobian.z;
    result.normal = normalize(Vec3 {
        vertex.normal.x - mapped.jacobian.x * normalZ,
        vertex.normal.y - mapped.jacobian.y * normalZ,
        normalZ,
    });
    const Vec3 sourceTangent {
        vertex.tangent.x, vertex.tangent.y, vertex.tangent.z,
    };
    const Vec3 tangent = normalize(Vec3 {
        sourceTangent.x,
        sourceTangent.y,
        dot(mapped.jacobian, sourceTangent),
    });
    result.tangent = { tangent.x, tangent.y, tangent.z, vertex.tangent.w };
    return result;
}

} // namespace sokoban
