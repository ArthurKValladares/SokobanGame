#include "engine/render/GroundMeshGeometry.hpp"

#include "engine/Geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <vector>

namespace sokoban {
namespace {

constexpr float epsilon = 0.0001f;
constexpr float outwardLimit = 0.031f;
constexpr float inwardLimit = 0.081f;
constexpr uint8_t bottomSection = 0;
constexpr uint8_t invalidSection = 0xff;

bool finite(Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

uint8_t bottomCorner(Vec3 position)
{
    if (std::abs(position.z) > epsilon ||
        (std::abs(position.x) > epsilon && std::abs(position.x - 1.0f) > epsilon) ||
        (std::abs(position.y) > epsilon && std::abs(position.y - 1.0f) > epsilon)) {
        return 0;
    }
    const uint32_t corner = (position.x > 0.5f ? 1U : 0U) |
        (position.y > 0.5f ? 2U : 0U);
    return static_cast<uint8_t>(1U << corner);
}

uint8_t classifyMaterial(const Aabb& bounds)
{
    if (!bounds.valid()) {
        return invalidSection;
    }
    const Vec3 minimum = bounds.minimum;
    const Vec3 maximum = bounds.maximum;
    if (minimum.x < -outwardLimit || maximum.x > 1.0f + outwardLimit ||
        minimum.y < -outwardLimit || maximum.y > 1.0f + outwardLimit ||
        minimum.z < -epsilon || maximum.z > 1.0f + epsilon) {
        return invalidSection;
    }
    if (std::abs(minimum.z) <= epsilon &&
        std::abs(maximum.z) <= epsilon &&
        std::abs(minimum.x) <= epsilon &&
        std::abs(minimum.y) <= epsilon &&
        std::abs(maximum.x - 1.0f) <= epsilon &&
        std::abs(maximum.y - 1.0f) <= epsilon) {
        return bottomSection;
    }
    // A cap or tiny corner fragment must not be mistaken for a whole side
    // material. The authored plates span a meaningful portion of its height.
    if (maximum.z - minimum.z <= inwardLimit) {
        return invalidSection;
    }
    uint8_t section = invalidSection;
    uint32_t candidates = 0;
    const auto candidate = [&](bool condition, uint8_t bit) {
        if (condition) {
            section = bit;
            ++candidates;
        }
    };
    candidate(maximum.y <= inwardLimit, 1);
    candidate(minimum.x >= 1.0f - inwardLimit, 2);
    candidate(minimum.y >= 1.0f - inwardLimit, 4);
    candidate(maximum.x <= inwardLimit, 8);
    return candidates == 1 ? section : invalidSection;
}

} // namespace

bool isProcessableGroundRockModel(const AssetManifest::Model& model) noexcept
{
    if (model.geometry != ModelGeometry::Static ||
        !model.preserveSourceScale || model.rotateHalfTurn ||
        !model.attachments.empty() || model.hasScrollingMaterial()) {
        return false;
    }
    const std::string_view name = model.name;
    if (name.size() != 12 || !name.starts_with("GroundRock")) {
        return false;
    }
    const std::string_view suffix = name.substr(10);
    if (!((suffix[0] == '0' && suffix[1] >= '1' && suffix[1] <= '9') ||
            suffix == "10")) {
        return false;
    }
    const std::string_view path = model.path;
    const auto matchingPath = [&](std::string_view prefix, std::string_view extension) {
        return path.size() == prefix.size() + suffix.size() + extension.size() &&
            path.starts_with(prefix) && path.ends_with(extension) &&
            path.substr(prefix.size(), suffix.size()) == suffix;
    };
    return matchingPath("custom/pbr/models/GroundRock", ".glb") ||
        matchingPath("custom/models/ground_rock_", ".gltf");
}

GroundMeshVariants appendGroundMeshVariants(MeshData& mesh)
{
    GroundMeshVariants result;
    if (mesh.indices.size() > std::numeric_limits<uint32_t>::max()) {
        return result;
    }
    const uint32_t originalCount = static_cast<uint32_t>(mesh.indices.size());
    result.ranges.fill({ 0, originalCount });
    if (originalCount == 0 || originalCount % 3 != 0 ||
        mesh.vertices.empty() || mesh.materials.empty()) {
        return result;
    }
    for (const MeshMaterial& material : mesh.materials) {
        if (material.alphaMode != MaterialAlphaMode::Opaque ||
            material.doubleSided || material.flags != PrimitiveMaterialNone) {
            return result;
        }
    }

    std::vector<Aabb> bounds(mesh.materials.size());
    for (uint32_t index : mesh.indices) {
        if (index >= mesh.vertices.size()) {
            return result;
        }
        const MeshVertex& vertex = mesh.vertices[index];
        if (vertex.materialIndex >= bounds.size() || !finite(vertex.position)) {
            return result;
        }
        bounds[vertex.materialIndex] = expand(
            bounds[vertex.materialIndex], vertex.position);
    }
    std::vector<uint8_t> materialSections(bounds.size(), invalidSection);
    for (std::size_t material = 0; material < bounds.size(); ++material) {
        if (bounds[material].valid()) {
            materialSections[material] = classifyMaterial(bounds[material]);
            if (materialSections[material] == invalidSection) {
                return result;
            }
        }
    }
    std::array<uint32_t, 16> sectionIndexCounts {};
    std::vector<uint8_t> triangleSections(originalCount / 3);
    uint8_t presentSides = 0;
    std::array<double, 16> projectedSideAreas {};
    std::array<Aabb, 16> sideBounds {};
    double bottomArea = 0.0;
    std::array<uint8_t, 2> bottomCornerMasks {};
    uint32_t bottomTriangleCount = 0;
    for (uint32_t first = 0; first < originalCount; first += 3) {
        const MeshVertex& a = mesh.vertices[mesh.indices[first]];
        const MeshVertex& b = mesh.vertices[mesh.indices[first + 1]];
        const MeshVertex& c = mesh.vertices[mesh.indices[first + 2]];
        if (a.materialIndex != b.materialIndex ||
            a.materialIndex != c.materialIndex) {
            return result;
        }
        const uint8_t section = materialSections[a.materialIndex];
        if (section == invalidSection) {
            return result;
        }
        if (section == bottomSection) {
            if (bottomTriangleCount >= bottomCornerMasks.size()) {
                return result;
            }
            const uint8_t cornerA = bottomCorner(a.position);
            const uint8_t cornerB = bottomCorner(b.position);
            const uint8_t cornerC = bottomCorner(c.position);
            if (cornerA == 0 || cornerB == 0 || cornerC == 0 ||
                cornerA == cornerB || cornerB == cornerC || cornerA == cornerC) {
                return result;
            }
            bottomCornerMasks[bottomTriangleCount++] =
                static_cast<uint8_t>(cornerA | cornerB | cornerC);
            const Vec3 normal = cross(b.position - a.position, c.position - a.position);
            if (normal.z >= -epsilon) {
                return result;
            }
            bottomArea += -static_cast<double>(normal.z) * 0.5;
        } else {
            presentSides |= section;
            const Vec3 normal = cross(b.position - a.position, c.position - a.position);
            const float outwardProjection = section == 1 ? -normal.y
                : section == 2 ? normal.x
                : section == 4 ? normal.y
                : -normal.x;
            projectedSideAreas[section] +=
                static_cast<double>(outwardProjection) * 0.5;
            sideBounds[section] = expand(sideBounds[section], a.position);
            sideBounds[section] = expand(sideBounds[section], b.position);
            sideBounds[section] = expand(sideBounds[section], c.position);
        }
        triangleSections[first / 3] = section;
        sectionIndexCounts[section] += 3;
    }
    const uint8_t sharedCorners =
        static_cast<uint8_t>(bottomCornerMasks[0] & bottomCornerMasks[1]);
    if (presentSides != 0x0f || sectionIndexCounts[bottomSection] != 6 ||
        (bottomCornerMasks[0] | bottomCornerMasks[1]) != 15 ||
        (sharedCorners != 9 && sharedCorners != 6) ||
        std::abs(bottomArea - 1.0) > epsilon) {
        return result;
    }
    // The canonical authored patches seal the full square perimeter. Signed
    // projection counts steep bevels correctly while detecting missing or
    // inverted triangles that leave holes in an otherwise plausible slab.
    for (uint8_t side : { uint8_t { 1 }, uint8_t { 2 }, uint8_t { 4 }, uint8_t { 8 } }) {
        const Aabb& sideExtent = sideBounds[side];
        const float tangentMinimum = (side == 1 || side == 4)
            ? sideExtent.minimum.x : sideExtent.minimum.y;
        const float tangentMaximum = (side == 1 || side == 4)
            ? sideExtent.maximum.x : sideExtent.maximum.y;
        if (std::abs(projectedSideAreas[side] - 1.0) > epsilon ||
            std::abs(tangentMinimum) > epsilon ||
            std::abs(tangentMaximum - 1.0f) > epsilon ||
            std::abs(sideExtent.minimum.z) > epsilon ||
            std::abs(sideExtent.maximum.z - 1.0f) > epsilon) {
            return result;
        }
    }

    uint64_t totalCount = originalCount;
    auto ranges = result.ranges;
    for (uint8_t mask = 0; mask < 15; ++mask) {
        uint32_t count = sectionIndexCounts[bottomSection];
        for (uint8_t side : { uint8_t { 1 }, uint8_t { 2 }, uint8_t { 4 }, uint8_t { 8 } }) {
            if ((mask & side) != 0) {
                count += sectionIndexCounts[side];
            }
        }
        if (totalCount + count > std::numeric_limits<uint32_t>::max()) {
            return result;
        }
        ranges[mask] = { static_cast<uint32_t>(totalCount), count };
        totalCount += count;
    }
    mesh.indices.reserve(static_cast<std::size_t>(totalCount));
    for (uint8_t mask = 0; mask < 15; ++mask) {
        for (uint32_t first = 0; first < originalCount; first += 3) {
            const uint8_t section = triangleSections[first / 3];
            if (section == bottomSection || (mask & section) != 0) {
                mesh.indices.push_back(mesh.indices[first]);
                mesh.indices.push_back(mesh.indices[first + 1]);
                mesh.indices.push_back(mesh.indices[first + 2]);
            }
        }
    }
    result.ranges = ranges;
    result.processed = true;
    return result;
}

} // namespace sokoban
