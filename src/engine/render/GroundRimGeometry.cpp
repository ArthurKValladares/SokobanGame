#include "engine/render/GroundRimGeometry.hpp"
#include "engine/render/IsoScenePreparer.hpp"

#include <algorithm>
#include <array>
#include <bit>
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

using Quad = std::array<Vec3, 4>;
constexpr std::array<Vec2, 4> cornerOffsets {
    Vec2 { 0, 0 }, Vec2 { 1, 0 }, Vec2 { 1, 1 }, Vec2 { 0, 1 },
};
constexpr std::array<std::array<uint32_t, 2>, 4> sideCorners {{
    { 0, 1 }, { 1, 2 }, { 3, 2 }, { 0, 3 },
}};

uint32_t fractureHash(Vec2 point, uint32_t salt)
{
    // Integer operations give identical CPU/GLSL choices. Canonicalize -0.
    uint32_t hash = std::bit_cast<uint32_t>(point.x == 0 ? 0.0f : point.x) * 1664525U ^
        std::bit_cast<uint32_t>(point.y == 0 ? 0.0f : point.y) * 1013904223U ^ salt;
    hash ^= hash >> 16;
    hash *= 2246822519U;
    hash ^= hash >> 13;
    hash *= 3266489917U;
    return hash ^ (hash >> 16);
}

float fractureUnit(Vec2 point, uint32_t salt)
{
    return static_cast<float>(fractureHash(point, salt) & 65535U) / 65535.0f;
}

struct Corner {
    float width;
    float drop;
};

Corner cornerFor(uint32_t corner, const GroundRimProfile& profile)
{
    const Vec2 world = profile.origin + cornerOffsets[corner];
    return {
        std::min(0.40f, profile.width * (0.60f + 0.75f * fractureUnit(world, 17U))),
        profile.depth * (0.55f + 0.80f * fractureUnit(world, 29U)),
    };
}

Quad cornerQuad(uint32_t corner, const GroundRimProfile& profile)
{
    const Corner shape = cornerFor(corner, profile);
    const uint32_t outgoing = corner;
    const uint32_t incoming = (corner + 3) % 4;
    const bool afterExposed = (profile.exposedSides & (1U << outgoing)) != 0;
    const bool beforeExposed = (profile.exposedSides & (1U << incoming)) != 0;
    const bool active = afterExposed || beforeExposed ||
        (profile.concaveCorners & (1U << corner)) != 0;
    const Corner after = cornerFor((corner + 1) % 4, profile);
    const Corner before = cornerFor((corner + 3) % 4, profile);
    const float afterDrop = afterExposed
        ? shape.drop + shape.width * (after.drop - shape.drop) : 0.0f;
    const float beforeDrop = beforeExposed
        ? shape.drop + shape.width * (before.drop - shape.drop) : 0.0f;
    const Vec2 origin = cornerOffsets[corner];
    const Vec2 along = cornerOffsets[(corner + 1) % 4] - origin;
    const Vec2 across = cornerOffsets[(corner + 3) % 4] - origin;
    const auto vertex = [&](Vec2 xy, float drop) { return Vec3 { xy.x, xy.y, 1.0f - drop }; };
    return {
        vertex(origin, active ? shape.drop : 0.0f),
        vertex(origin + along * shape.width, afterDrop),
        vertex(origin + (along + across) * shape.width, 0.0f),
        vertex(origin + across * shape.width, beforeDrop),
    };
}

bool cornerDiagonal(uint32_t corner, const GroundRimProfile& profile)
{
    // The opposite diagonal keeps a concave notch local to its corner.
    return (profile.exposedSides & ((1U << corner) | (1U << ((corner + 3) % 4)))) != 0;
}

struct Side {
    std::array<float, 4> along;
    std::array<float, 4> width;
    std::array<float, 4> drop;
    uint32_t hash;
};

Side sideFor(uint32_t side, const GroundRimProfile& profile)
{
    const uint32_t start = sideCorners[side][0];
    const uint32_t end = sideCorners[side][1];
    const Corner a = cornerFor(start, profile);
    const Corner b = cornerFor(end, profile);
    const Vec2 world = profile.origin + cornerOffsets[start];
    const uint32_t salt = side % 2 == 0 ? 101U : 211U;
    Side result {
        .along = { a.width,
            a.width + (0.5f - a.width) * (0.40f + 0.35f * fractureUnit(world, salt)),
            0.5f + (0.5f - b.width) * (0.25f + 0.35f * fractureUnit(world, salt + 1U)),
            1.0f - b.width },
        .width = { a.width, 0, 0, b.width },
        .hash = fractureHash(world, salt + 4U),
    };
    for (uint32_t index = 1; index <= 2; ++index) {
        result.width[index] = std::min({ 0.40f,
            profile.width * (0.45f + 1.15f * fractureUnit(world, salt + index + 1U)),
            0.85f * std::min(result.along[index], 1.0f - result.along[index]) });
    }
    if ((profile.exposedSides & (1U << side)) != 0) {
        for (uint32_t index = 0; index < 4; ++index) {
            result.drop[index] = a.drop + result.along[index] * (b.drop - a.drop);
        }
    }
    return result;
}

Vec3 sideVertex(uint32_t side, float along, float distance, float drop)
{
    const Vec2 xy = side == 0 ? Vec2 { along, distance }
        : side == 1 ? Vec2 { 1.0f - distance, along }
        : side == 2 ? Vec2 { along, 1.0f - distance }
        : Vec2 { distance, along };
    return { xy.x, xy.y, 1.0f - drop };
}

Quad sideQuad(uint32_t side, uint32_t segment, const Side& shape)
{
    return {
        sideVertex(side, shape.along[segment], 0, shape.drop[segment]),
        sideVertex(side, shape.along[segment + 1], 0, shape.drop[segment + 1]),
        sideVertex(side, shape.along[segment + 1], shape.width[segment + 1], 0),
        sideVertex(side, shape.along[segment], shape.width[segment], 0),
    };
}

bool sampleTriangle(Vec2 point, Vec3 a, Vec3 b, Vec3 c, GroundRimSample& result)
{
    const Vec2 ab { b.x - a.x, b.y - a.y };
    const Vec2 ac { c.x - a.x, c.y - a.y };
    const Vec2 ap { point.x - a.x, point.y - a.y };
    const float determinant = ab.x * ac.y - ab.y * ac.x;
    const float u = (ap.x * ac.y - ap.y * ac.x) / determinant;
    const float v = (ab.x * ap.y - ab.y * ap.x) / determinant;
    if (u < -0.000001f || v < -0.000001f || u + v > 1.000001f) return false;
    const float dzB = b.z - a.z;
    const float dzC = c.z - a.z;
    result = {
        .drop = std::max(0.0f, 1.0f - (a.z + u * dzB + v * dzC)),
        .gradient = { -(dzB * ac.y - dzC * ab.y) / determinant,
            -(ab.x * dzC - ac.x * dzB) / determinant },
    };
    return true;
}

bool sampleQuad(Vec2 point, const Quad& quad, bool diagonal, GroundRimSample& result)
{
    return diagonal
        ? sampleTriangle(point, quad[0], quad[1], quad[2], result) ||
            sampleTriangle(point, quad[0], quad[2], quad[3], result)
        : sampleTriangle(point, quad[0], quad[1], quad[3], result) ||
            sampleTriangle(point, quad[1], quad[2], quad[3], result);
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
    if (!std::isfinite(position.z) || position.z <= 1.0f - profile.bodyBand) {
        return result;
    }
    const GroundRimSample sample = sampleGroundRim(
        { position.x, position.y }, profile);
    if (sample.drop <= 0.0f) {
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
        profile.bodyBand > profile.depth * 1.35f && profile.bodyBand <= 1.0f &&
        std::isfinite(profile.origin.x) && std::isfinite(profile.origin.y);
}

GroundRimGeometry buildGroundRimGeometry(const GroundRimProfile& profile) noexcept
{
    GroundRimGeometry result;
    if (!groundRimProfileValid(profile) ||
        (profile.exposedSides == 0 && profile.concaveCorners == 0)) return result;
    const auto append = [&](Quad quad) {
        if (cross(quad[1] - quad[0], quad[2] - quad[0]).z < 0.0f) {
            if (quad[2] == quad[3]) {
                std::swap(quad[1], quad[2]);
                quad[3] = quad[2];
            } else {
                std::swap(quad[1], quad[3]);
            }
        }
        result.patches[result.count++] = quad;
    };
    const auto appendQuad = [&](const Quad& quad, bool diagonal) {
        // Merge only the flat centre/unused strips. Approximate planarity
        // tests scale poorly on narrow facets and could change their diagonal
        // relative to the GPU sampler, creating different cap/body fields.
        if (quad[0].z == 1 && quad[1].z == 1 && quad[2].z == 1 && quad[3].z == 1) {
            append(quad);
        } else if (diagonal) {
            append({ quad[0], quad[1], quad[2], quad[2] });
            append({ quad[0], quad[2], quad[3], quad[3] });
        } else {
            append({ quad[0], quad[1], quad[3], quad[3] });
            append({ quad[1], quad[2], quad[3], quad[3] });
        }
    };
    std::array<Side, 4> sides;
    for (uint32_t corner = 0; corner < 4; ++corner) {
        appendQuad(cornerQuad(corner, profile), cornerDiagonal(corner, profile));
    }
    for (uint32_t side = 0; side < 4; ++side) {
        sides[side] = sideFor(side, profile);
        for (uint32_t segment = 0; segment < 3; ++segment) {
            appendQuad(sideQuad(side, segment, sides[side]),
                ((sides[side].hash >> segment) & 1U) != 0);
        }
    }
    // Flat centre fan follows the same jagged outline. Pair consecutive
    // triangles into coplanar quads to retain the existing face batching.
    std::array<Vec3, 12> inner;
    for (uint32_t side = 0; side < 4; ++side) {
        for (uint32_t index = 0; index < 3; ++index) {
            const uint32_t knot = side < 2 ? index : 3 - index;
            inner[side * 3 + index] = sideVertex(side,
                sides[side].along[knot], sides[side].width[knot], 0);
        }
    }
    for (uint32_t index = 0; index < inner.size(); index += 2) {
        append({ Vec3 { 0.5f, 0.5f, 1 }, inner[index],
            inner[index + 1], inner[(index + 2) % inner.size()] });
    }
    return result;
}

GroundRimSample sampleGroundRim(
    Vec2 localPosition,
    const GroundRimProfile& profile) noexcept
{
    if (!groundRimProfileValid(profile) || !std::isfinite(localPosition.x) ||
        !std::isfinite(localPosition.y)) {
        return {};
    }
    const Vec2 point { std::clamp(localPosition.x, 0.0f, 1.0f),
        std::clamp(localPosition.y, 0.0f, 1.0f) };
    GroundRimSample result;
    const auto finish = [&] {
        if (point.x != localPosition.x) result.gradient.x = 0;
        if (point.y != localPosition.y) result.gradient.y = 0;
        return result;
    };
    for (uint32_t corner = 0; corner < 4; ++corner) {
        const float width = cornerFor(corner, profile).width;
        const Vec2 offset = cornerOffsets[corner];
        if (std::abs(point.x - offset.x) <= width &&
            std::abs(point.y - offset.y) <= width &&
            sampleQuad(point, cornerQuad(corner, profile),
                cornerDiagonal(corner, profile), result)) {
            return finish();
        }
    }
    for (uint32_t side = 0; side < 4; ++side) {
        if ((profile.exposedSides & (1U << side)) == 0) continue;
        const Side shape = sideFor(side, profile);
        const float along = side % 2 == 0 ? point.x : point.y;
        for (uint32_t segment = 0; segment < 3; ++segment) {
            if (along < shape.along[segment] || along > shape.along[segment + 1]) continue;
            if (sampleQuad(point, sideQuad(side, segment, shape),
                ((shape.hash >> segment) & 1U) != 0, result)) return finish();
        }
    }
    return {};
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
