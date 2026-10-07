#include "TestAssetRoot.hpp"
#include "TestHarness.hpp"

#include "engine/render/GroundMeshGeometry.hpp"
#include "engine/render/GroundRimGeometry.hpp"
#include "engine/render/GroundRimSurface.hpp"
#include "engine/render/IsoScenePreparer.hpp"
#include "engine/render/VulkanRenderConstants.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace sokoban;

constexpr std::array<Vec2, 4> rimOrigins {
    Vec2 { 0, 0 }, Vec2 { 17, 23 }, Vec2 { -19, -7 }, Vec2 { 8, -11 },
};

constexpr std::array<std::array<std::size_t, 3>, 2> rimTriangles {{
    { 0, 1, 2 }, { 0, 2, 3 },
}};

void addTriangle(MeshData& mesh, uint32_t material, Vec3 a, Vec3 b, Vec3 c)
{
    for (Vec3 position : { a, b, c }) {
        mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size()));
        mesh.vertices.push_back({
            .position = position,
            .normal = { 0.2f, 0.3f, 0.4f },
            .tangent = { 0.7f, 0.8f, 0.9f, -1.0f },
            .uv = { 0.11f, 0.22f },
            .uv1 = { 0.33f, 0.44f },
            .materialIndex = material,
        });
    }
}

MeshData groundFixture()
{
    MeshData mesh;
    mesh.materials.resize(5);
    addTriangle(mesh, 0, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 });
    addTriangle(mesh, 0, { 0, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 });
    addTriangle(mesh, 1, { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 });
    addTriangle(mesh, 1, { 1, 0, 0 }, { 1, 1, 1 }, { 1, 0, 1 });
    addTriangle(mesh, 2, { 1, 1, 0 }, { 0, 1, 0 }, { 0, 1, 1 });
    addTriangle(mesh, 2, { 1, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 });
    addTriangle(mesh, 3, { 0, 1, 0 }, { 0, 0, 0 }, { 0, 0, 1 });
    addTriangle(mesh, 3, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 1, 1 });
    addTriangle(mesh, 4, { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 });
    addTriangle(mesh, 4, { 0, 0, 0 }, { 1, 1, 0 }, { 1, 0, 0 });
    return mesh;
}

template <typename Value>
std::vector<std::byte> snapshot(const std::vector<Value>& values)
{
    const auto bytes = std::as_bytes(std::span(values));
    return { bytes.begin(), bytes.end() };
}

template <typename Value>
void checkUnchanged(const std::vector<Value>& values, const std::vector<std::byte>& bytes)
{
    CHECK(values.size() * sizeof(Value) == bytes.size());
    CHECK(std::memcmp(values.data(), bytes.data(), bytes.size()) == 0);
}

void testAllMasks()
{
    MeshData mesh = groundFixture();
    const auto vertices = snapshot(mesh.vertices);
    const auto materials = snapshot(mesh.materials);
    const auto originalIndices = mesh.indices;
    const GroundMeshVariants variants = appendGroundMeshVariants(mesh);
    CHECK(variants.processed);
    checkUnchanged(mesh.vertices, vertices);
    checkUnchanged(mesh.materials, materials);
    CHECK(std::equal(originalIndices.begin(), originalIndices.end(), mesh.indices.begin()));
    CHECK((variants.ranges[15] == GroundMeshIndexRange { 0, 30 }));
    CHECK(mesh.indices.size() == 8 * originalIndices.size() + 48);
    for (uint8_t mask = 0; mask < 16; ++mask) {
        const GroundMeshIndexRange range = variants.ranges[mask];
        CHECK(range.indexCount % 3 == 0);
        CHECK(static_cast<uint64_t>(range.firstIndex) + range.indexCount <= mesh.indices.size());
        std::vector<uint32_t> expected;
        for (uint32_t index : originalIndices) {
            const uint32_t material = mesh.vertices[index].materialIndex;
            if (material == 4 || (mask & (1U << material)) != 0) {
                expected.push_back(index);
            }
        }
        CHECK(range.indexCount == expected.size());
        CHECK(std::equal(expected.begin(), expected.end(), mesh.indices.begin() + range.firstIndex));
    }
    CHECK(variants.ranges[0].indexCount == 6);
}

void checkFallback(MeshData mesh)
{
    const auto originalIndices = mesh.indices;
    const auto vertices = snapshot(mesh.vertices);
    const auto materials = snapshot(mesh.materials);
    const GroundMeshVariants variants = appendGroundMeshVariants(mesh);
    CHECK(!variants.processed);
    CHECK(mesh.indices == originalIndices);
    checkUnchanged(mesh.vertices, vertices);
    checkUnchanged(mesh.materials, materials);
    for (const GroundMeshIndexRange range : variants.ranges) {
        CHECK(range.firstIndex == 0);
        CHECK(range.indexCount == originalIndices.size());
    }
}

void testFallbacks()
{
    MeshData mesh = groundFixture();
    mesh.indices[0] = static_cast<uint32_t>(mesh.vertices.size());
    checkFallback(mesh);
    mesh = groundFixture();
    mesh.vertices[0].materialIndex = 10;
    checkFallback(mesh);
    mesh = groundFixture();
    mesh.vertices[0].position.z = std::numeric_limits<float>::quiet_NaN();
    checkFallback(mesh);
    mesh = groundFixture();
    mesh.materials[0].alphaMode = MaterialAlphaMode::Mask;
    checkFallback(mesh);
    mesh = groundFixture();
    mesh.materials[0].alphaMode = MaterialAlphaMode::Blend;
    checkFallback(mesh);
    mesh = groundFixture();
    mesh.materials[0].doubleSided = true;
    checkFallback(mesh);
    mesh = groundFixture();
    mesh.vertices[0].position.y = 0.5f; // A side primitive crosses into the tile.
    checkFallback(mesh);
    mesh = groundFixture();
    for (MeshVertex& vertex : mesh.vertices) {
        if (vertex.materialIndex == 1) {
            vertex.materialIndex = 0; // One material now spans multiple sides.
        }
    }
    checkFallback(mesh);
    mesh = groundFixture();
    addTriangle(mesh, 0, { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }); // Unexpected cap.
    checkFallback(mesh);
    mesh = groundFixture();
    mesh.indices.resize(mesh.indices.size() - 6); // No closing bottom.
    checkFallback(mesh);
    mesh = groundFixture();
    std::swap(mesh.indices[24], mesh.indices[25]); // Inverted bottom winding.
    checkFallback(mesh);
    mesh = groundFixture();
    mesh.indices.pop_back();
    checkFallback(mesh);
    mesh = groundFixture();
    mesh.vertices[0].materialIndex = 1; // Triangle with mixed materials.
    checkFallback(mesh);
    mesh = groundFixture();
    // Two half-square triangles with correct area and bounds can overlap.
    // They must share the square's diagonal, rather than its upper edge.
    mesh.vertices[27].position = { 0, 1, 0 };
    mesh.vertices[28].position = { 1, 1, 0 };
    mesh.vertices[29].position = { 1, 0, 0 };
    checkFallback(mesh);
    mesh = groundFixture();
    mesh.vertices[25].position = { 0, 0.9f, 0 }; // Bottom is not the unit square.
    checkFallback(mesh);
    mesh = groundFixture();
    // The remaining north triangle still spans its full material bounds,
    // but covers only half the side. Bounds alone cannot detect this hole.
    mesh.indices.erase(mesh.indices.begin(), mesh.indices.begin() + 3);
    checkFallback(mesh);
    mesh = groundFixture();
    std::swap(mesh.indices[0], mesh.indices[1]); // One inverted side triangle.
    checkFallback(mesh);
}

void testNeighborValidation()
{
    for (uint8_t candidate = 0; candidate < 16; ++candidate) {
        for (uint8_t readyMask = 0; readyMask < 16; ++readyMask) {
            std::array<bool, 4> ready {};
            for (std::size_t side = 0; side < ready.size(); ++side) {
                ready[side] = (readyMask & (1U << side)) != 0;
            }
            const uint8_t effective = effectiveGroundSideMask(candidate, ready);
            CHECK((effective & candidate) == candidate);
            for (uint32_t side = 0; side < 4; ++side) {
                const uint8_t bit = static_cast<uint8_t>(1U << side);
                CHECK(((effective & bit) == 0) ==
                    ((candidate & bit) == 0 && ready[side]));
            }
        }
    }
    CHECK(effectiveGroundSideMask(0, { false, false, false, false }) == groundAllSides);
    CHECK(effectiveGroundSideMask(0, { true, true, true, true }) == 0);
    CHECK(effectiveGroundSideMask(groundWestSide,
        { true, true, true, true }) == groundWestSide);
    CHECK(effectiveGroundSideMask(0, { true, false, true, true }) == groundEastSide);
    CHECK(effectiveGroundSideMask(0xff, { true, true, true, true }) == groundAllSides);
}

void testAssetGate()
{
    AssetManifest::Model model {
        .name = "GroundRock01",
        .path = "custom/pbr/models/GroundRock01.glb",
        .preserveSourceScale = true,
    };
    CHECK(isProcessableGroundRockModel(model));
    model.path = "custom/models/ground_rock_01.gltf";
    CHECK(isProcessableGroundRockModel(model));
    model.path = "custom/models/other.gltf";
    CHECK(!isProcessableGroundRockModel(model));
    model.path = "custom/models/ground_rock_01.gltf";
    model.name = "GroundRock11";
    CHECK(!isProcessableGroundRockModel(model));
    model.name = "GroundRock01";
    model.preserveSourceScale = false;
    CHECK(!isProcessableGroundRockModel(model));
    model.preserveSourceScale = true;
    model.rotateHalfTurn = true;
    CHECK(!isProcessableGroundRockModel(model));
    model.rotateHalfTurn = false;
    model.geometry = ModelGeometry::Skinned;
    CHECK(!isProcessableGroundRockModel(model));
    model.geometry = ModelGeometry::Static;
    model.attachments.push_back({ .path = "extra.gltf" });
    CHECK(!isProcessableGroundRockModel(model));
}

void checkAuthoredRimEdges(const MeshData& mesh, const char* label)
{
    std::array<uint32_t, 4> segmentCounts {};
    for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3) {
        for (std::size_t edge = 0; edge < 3; ++edge) {
            const Vec3 a = mesh.vertices[mesh.indices[triangle + edge]].position;
            const Vec3 b = mesh.vertices[mesh.indices[triangle + (edge + 1) % 3]].position;
            if (std::abs(a.z - 1.0f) > 1e-5f || std::abs(b.z - 1.0f) > 1e-5f ||
                length(b - a) < 1e-5f) continue;
            const int side = std::abs(a.y) < 1e-5f && std::abs(b.y) < 1e-5f ? 0
                : std::abs(a.x - 1) < 1e-5f && std::abs(b.x - 1) < 1e-5f ? 1
                : std::abs(a.y - 1) < 1e-5f && std::abs(b.y - 1) < 1e-5f ? 2
                : std::abs(a.x) < 1e-5f && std::abs(b.x) < 1e-5f ? 3 : -1;
            if (side < 0) continue;
            ++segmentCounts[static_cast<std::size_t>(side)];
            for (Vec2 origin : rimOrigins) {
                const GroundRimProfile profile { .origin = origin };
                const Vec3 mappedA = deformGroundRockPosition(a, profile);
                const Vec3 mappedB = deformGroundRockPosition(b, profile);
                for (float t : { 0.25f, 0.5f, 0.75f }) {
                    const Vec3 sourcePoint = a + (b - a) * t;
                    // The GPU connects authored wall vertices with straight
                    // segments. Its interpolated edge must seal the cap even
                    // where this segment spans several irregular cap facets.
                    const Vec3 renderedEdge = mappedA + (mappedB - mappedA) * t;
                    const float capHeight = 1.0f - sampleGroundRim(
                        { sourcePoint.x, sourcePoint.y }, profile).drop;
                    CHECK_MESSAGE(std::abs(renderedEdge.z - capHeight) < 1e-5f, label);
                    checkNear(deformGroundRockPosition(sourcePoint, profile).z,
                        capHeight, "authored top edge uses the same affine cap border");
                }
            }
        }
    }
    for (const auto count : segmentCounts) {
        CHECK_MESSAGE(count > 0, label);
    }
}

void testAuthoredModels()
{
#ifdef SOKOBAN_ASSET_DIR
    const std::filesystem::path assetRoot = SOKOBAN_ASSET_DIR;
#else
    const std::filesystem::path assetRoot = testAssetRoot();
#endif
    for (uint32_t number = 1; number <= 10; ++number) {
        const std::string suffix = number < 10 ? "0" + std::to_string(number) : std::to_string(number);
        for (const std::string& relative : {
                 "custom/models/ground_rock_" + suffix + ".gltf",
                 "custom/pbr/models/GroundRock" + suffix + ".glb",
             }) {
            MeshData mesh = loadGltfMesh(assetRoot / relative,
                { .preserveSourceScale = true });
            checkAuthoredRimEdges(mesh, relative.c_str());
            const auto originalIndices = mesh.indices;
            const auto vertices = snapshot(mesh.vertices);
            const auto materials = snapshot(mesh.materials);
            const GroundMeshVariants variants = appendGroundMeshVariants(mesh);
            CHECK_MESSAGE(variants.processed, relative.c_str());
            checkUnchanged(mesh.vertices, vertices);
            checkUnchanged(mesh.materials, materials);
            CHECK(std::equal(originalIndices.begin(), originalIndices.end(), mesh.indices.begin()));
            CHECK(variants.ranges[0].indexCount == 6);
            CHECK(variants.ranges[15].indexCount == originalIndices.size());
            CHECK(mesh.indices.size() == 8 * originalIndices.size() + 48);
            for (uint8_t mask = 0; mask < 16; ++mask) {
                const GroundMeshIndexRange range = variants.ranges[mask];
                CHECK(range.indexCount >= 6);
                CHECK(range.indexCount <= originalIndices.size());
                CHECK(static_cast<uint64_t>(range.firstIndex) + range.indexCount <= mesh.indices.size());
            }
        }
    }
}

void testRimProfileAndBody()
{
    const GroundRimProfile north { .exposedSides = groundNorthSide, .origin = { -3, 8 } };
    const float startDrop = sampleGroundRim({ 0, 0 }, north).drop;
    const float endDrop = sampleGroundRim({ 1, 0 }, north).drop;
    CHECK(startDrop > 0 && endDrop > 0);
    CHECK(std::abs(startDrop - endDrop) > 1e-4f);
    checkNear(sampleGroundRim({ 0.37f, 0 }, north).drop,
        startDrop + 0.37f * (endDrop - startDrop), "exposed border remains affine");
    checkNear(sampleGroundRim({ 0.5f, 0.5f }, north).drop, 0.0f, "interior remains flat");
    checkNear(sampleGroundRim({ 0.0f, 0.5f }, north).drop, 0.0f, "unexposed west edge remains flat");
    const GroundRimProfile convex {
        .exposedSides = groundNorthSide | groundWestSide,
        .origin = north.origin,
    };
    CHECK(sampleGroundRim({ 0.03f, 0.06f }, convex).drop > 0);
    const GroundRimProfile concave {
        .exposedSides = 0,
        .concaveCorners = groundNorthWestCorner,
        .origin = north.origin + Vec2 { 1, 0 },
    };
    CHECK(sampleGroundRim({ 0.03f, 0.06f }, concave).drop > 0);
    checkNear(sampleGroundRim({ 0.0f, 0.06f }, concave).drop,
        sampleGroundRim({ 1.0f, 0.06f }, north).drop,
        "concave corner matches neighboring edge ramp");

    // Select the interior of an actual non-flat facet. Fixed local points
    // can lie on intentional creases as world coordinates vary.
    const GroundRimGeometry geometry = buildGroundRimGeometry(north);
    Vec2 facetPoint {};
    bool foundFacet = false;
    for (std::size_t patch = 0; patch < geometry.count && !foundFacet; ++patch) {
        for (const auto& triangle : rimTriangles) {
            const Vec3 a = geometry.patches[patch][triangle[0]];
            const Vec3 b = geometry.patches[patch][triangle[1]];
            const Vec3 c = geometry.patches[patch][triangle[2]];
            if (cross(b - a, c - a).z <= 0 || a.z + b.z + c.z >= 2.999f) continue;
            const Vec3 centroid = (a + b + c) / 3.0f;
            facetPoint = { centroid.x, centroid.y };
            foundFacet = true;
            break;
        }
    }
    CHECK(foundFacet);
    const GroundRimSample middle = sampleGroundRim(facetPoint, north);
    MeshVertex vertex {
        .position = { facetPoint.x, facetPoint.y, 0.85f },
        .normal = normalize(Vec3 { 0.3f, 0.4f, 0.5f }),
        .tangent = { 0.8f, -0.6f, 0.0f, -1.0f },
        .uv = { 0.25f, 0.75f },
        .uv1 = { 0.125f, 0.875f },
        .materialIndex = 17,
    };
    const MeshVertex deformed = deformGroundRockVertex(vertex, north);
    checkNear(deformed.position.x, vertex.position.x, "body X remains authored");
    checkNear(deformed.position.y, vertex.position.y, "body Y remains authored");
    const float band = (vertex.position.z - (1.0f - north.bodyBand)) / north.bodyBand;
    checkNear(deformed.position.z, vertex.position.z - band * middle.drop,
        "only upper body band is compressed by the cap's actual drop");
    CHECK(deformed.uv == vertex.uv);
    CHECK(deformed.uv1 == vertex.uv1);
    CHECK(deformed.materialIndex == vertex.materialIndex);
    CHECK(deformed.tangent.w == vertex.tangent.w);
    checkNear(length(deformed.normal), 1.0f, "deformed normal remains normalized");
    const Vec3 tangent { deformed.tangent.x, deformed.tangent.y, deformed.tangent.z };
    checkNear(length(tangent), 1.0f, "deformed tangent remains normalized");
    checkNear(dot(deformed.normal, tangent), 0.0f, "Jacobian preserves normal/tangent orthogonality");
    // A finite-difference transformed tangent must agree with the analytic
    // Jacobian away from the profile's intentional planar seams.
    const Vec3 sourceTangent { vertex.tangent.x, vertex.tangent.y, vertex.tangent.z };
    const Vec3 mappedStep = deformGroundRockPosition(vertex.position + sourceTangent * 0.0001f, north) -
        deformGroundRockPosition(vertex.position, north);
    checkNear(dot(normalize(mappedStep), tangent), 1.0f, "tangent follows deformed geometry", 1e-4f);

    vertex.position.z = 0.6f;
    const MeshVertex lower = deformGroundRockVertex(vertex, north);
    CHECK(lower.position == vertex.position);
    CHECK(lower.normal == vertex.normal);
    CHECK(lower.tangent == vertex.tangent);
    GroundRimProfile disabled = north;
    disabled.width = 0.0f;
    vertex.position.z = 1.0f;
    CHECK(deformGroundRockPosition(vertex.position, disabled) == vertex.position);
    CHECK(deformGroundRockVertex(vertex, disabled).normal == vertex.normal);
    disabled = north;
    disabled.depth = disabled.bodyBand; // Would collapse the body's upper band.
    CHECK(deformGroundRockPosition(vertex.position, disabled) == vertex.position);

    for (uint8_t sides = 0; sides < 16; ++sides) {
        const GroundRimProfile profile { .exposedSides = sides, .origin = { -3, 8 } };
        for (Vec2 point : { Vec2 { 0.02f, 0.02f }, Vec2 { 0.5f, 0.06f },
                 Vec2 { 0.97f, 0.93f }, Vec2 { 0.5f, 0.5f } }) {
            float previous = -1.0f;
            for (uint32_t step = 0; step <= 100; ++step) {
                const float z = static_cast<float>(step) / 100.0f;
                const Vec3 mapped = deformGroundRockPosition({ point.x, point.y, z }, profile);
                CHECK(mapped.z > previous);
                previous = mapped.z;
                if (z <= 0.7f) {
                    CHECK(mapped.z == z);
                }
            }
        }
    }
}

void testRimVariationAndPositiveGeometry()
{
    const GroundRimProfile profile {};
    const GroundRimGeometry geometry = buildGroundRimGeometry(profile);
    const GroundRimGeometry repeated = buildGroundRimGeometry(profile);
    CHECK(geometry.count == repeated.count);
    CHECK(geometry.patches == repeated.patches);
    const auto negativeZero = buildGroundRimGeometry(
        GroundRimProfile { .origin = { -0.0f, -0.0f } });
    CHECK(geometry.count == negativeZero.count);
    CHECK(geometry.patches == negativeZero.patches);
    const auto translated = buildGroundRimGeometry(
        GroundRimProfile { .origin = { 17, 23 } });
    CHECK(geometry.patches != translated.patches);

    float minDrop = std::numeric_limits<float>::max();
    float maxDrop = 0;
    float minInnerWidth = std::numeric_limits<float>::max();
    float maxInnerWidth = 0;
    for (Vec2 corner : { Vec2 { 0, 0 }, Vec2 { 1, 0 }, Vec2 { 1, 1 }, Vec2 { 0, 1 } }) {
        const float drop = sampleGroundRim(corner, profile).drop;
        minDrop = std::min(minDrop, drop);
        maxDrop = std::max(maxDrop, drop);
    }
    for (std::size_t patch = 0; patch < geometry.count; ++patch) {
        for (const Vec3 vertex : geometry.patches[patch]) {
            if (vertex.z == 1 && vertex.x > 0.15f && vertex.x < 0.85f &&
                vertex.y > 0 && vertex.y < 0.4f) {
                minInnerWidth = std::min(minInnerWidth, vertex.y);
                maxInnerWidth = std::max(maxInnerWidth, vertex.y);
            }
        }
    }
    CHECK(maxDrop - minDrop > 1e-4f);
    CHECK(maxInnerWidth - minInnerWidth > 1e-4f);

    constexpr std::array<Vec2, 8> origins {
        Vec2 { 0, 0 }, Vec2 { 17, 23 }, Vec2 { -19, -7 }, Vec2 { 8, -11 },
        Vec2 { -1, 0 }, Vec2 { 0, -1 }, Vec2 { 127, -129 }, Vec2 { -512, 256 },
    };
    for (const float width : { 0.005f, 0.02f, 0.06f, 0.12f, 0.25f, 0.4f, 0.5f }) {
        for (const float depth : { 0.10f, 0.22f }) {
            for (Vec2 origin : origins) {
                for (const uint8_t sides : std::array<uint8_t, 5> { 0, 1, 3, 5, 15 }) {
                    const GroundRimProfile candidate {
                        .exposedSides = sides,
                        .concaveCorners = sides == 0
                            ? uint8_t { groundNorthWestCorner | groundSouthEastCorner } : uint8_t { 0 },
                        .width = width, .depth = depth, .origin = origin,
                    };
                    CHECK(groundRimProfileValid(candidate));
                    const auto facets = buildGroundRimGeometry(candidate);
                    CHECK(facets.count > 0 && facets.count <= GroundRimGeometry::capacity);
                    float area = 0;
                    for (std::size_t patch = 0; patch < facets.count; ++patch) {
                        for (const auto& triangle : rimTriangles) {
                            const Vec3 a = facets.patches[patch][triangle[0]];
                            const Vec3 b = facets.patches[patch][triangle[1]];
                            const Vec3 c = facets.patches[patch][triangle[2]];
                            const Vec3 normal = cross(b - a, c - a);
                            if (a == c || b == c) continue; // Encoded triangle's second half.
                            CHECK(normal.z > 0);
                            area += normal.z * 0.5f;
                            const Vec3 centroid = (a + b + c) / 3.0f;
                            const GroundRimSample sampled = sampleGroundRim(
                                { centroid.x, centroid.y }, candidate);
                            CHECK(std::isfinite(sampled.drop));
                            CHECK(std::isfinite(sampled.gradient.x) &&
                                std::isfinite(sampled.gradient.y));
                            CHECK(sampled.drop >= 0 && sampled.drop <= depth * 1.35f + 1e-6f);
                            checkNear(1.0f - sampled.drop, centroid.z,
                                "sampler reproduces each facet's interior", 2e-5f);
                            const float jacobian = 1.0f - sampled.drop / candidate.bodyBand;
                            CHECK(jacobian > 0);
                            const Vec3 upper = deformGroundRockPosition(
                                { centroid.x, centroid.y, 0.852f }, candidate);
                            const Vec3 lower = deformGroundRockPosition(
                                { centroid.x, centroid.y, 0.850f }, candidate);
                            CHECK(upper.z > lower.z);
                            checkNear((upper.z - lower.z) / 0.002f, jacobian,
                                "upper-body Jacobian stays positive", 1e-4f);
                        }
                    }
                    checkNear(area, 1.0f, "facets including the centre fan cover one tile", 2e-5f);
                }
            }
        }
    }
}

void testRimSeamsForEveryNeighborhood()
{
    constexpr std::array<GridPosition, 8> offsets {
        GridPosition { 0, -1 }, GridPosition { 1, 0 },
        GridPosition { 0, 1 }, GridPosition { -1, 0 },
        GridPosition { -1, -1 }, GridPosition { 1, -1 },
        GridPosition { 1, 1 }, GridPosition { -1, 1 },
    };
    constexpr std::array<std::array<uint32_t, 2>, 4> cornerSides {
        std::array<uint32_t, 2> { 0, 3 },
        std::array<uint32_t, 2> { 0, 1 },
        std::array<uint32_t, 2> { 2, 1 },
        std::array<uint32_t, 2> { 2, 3 },
    };
    for (Vec2 origin : rimOrigins) {
        for (uint32_t neighborhood = 0; neighborhood < 256; ++neighborhood) {
            const auto occupied = [&](GridPosition position) {
                if (position == GridPosition {}) {
                    return true;
                }
                for (uint32_t index = 0; index < offsets.size(); ++index) {
                    if (position == offsets[index]) {
                        return (neighborhood & (1U << index)) != 0;
                    }
                }
                return false;
            };
            const auto profileFor = [&](GridPosition cell) {
                GroundRimProfile profile {
                    .exposedSides = 0,
                    .origin = origin + Vec2 {
                        static_cast<float>(cell.x), static_cast<float>(cell.y),
                    },
                };
                std::array<bool, 4> cardinal {};
                for (uint32_t side = 0; side < 4; ++side) {
                    cardinal[side] = occupied({ cell.x + offsets[side].x,
                        cell.y + offsets[side].y });
                    if (!cardinal[side]) {
                        profile.exposedSides |= static_cast<uint8_t>(1U << side);
                    }
                }
                for (uint32_t corner = 0; corner < 4; ++corner) {
                    const GridPosition diagonal = offsets[4 + corner];
                    if (cardinal[cornerSides[corner][0]] && cardinal[cornerSides[corner][1]] &&
                        !occupied({ cell.x + diagonal.x, cell.y + diagonal.y })) {
                        profile.concaveCorners |= static_cast<uint8_t>(1U << corner);
                    }
                }
                return profile;
            };
            const GroundRimProfile centerProfile = profileFor({});
            constexpr std::array<float, 11> samples {
                0.0f, 0.01f, 0.03f, 0.06f, 0.12f, 0.5f,
                0.88f, 0.94f, 0.97f, 0.99f, 1.0f,
            };
            for (uint32_t side = 0; side < 4; ++side) {
                if ((neighborhood & (1U << side)) == 0) {
                    continue;
                }
                const GridPosition neighbor = offsets[side];
                const GroundRimProfile neighborProfile = profileFor(neighbor);
                for (float along : samples) {
                    const Vec2 centerPoint = side == 0 ? Vec2 { along, 0.0f }
                        : side == 1 ? Vec2 { 1.0f, along }
                        : side == 2 ? Vec2 { along, 1.0f }
                        : Vec2 { 0.0f, along };
                    const Vec2 neighborPoint {
                        centerPoint.x - static_cast<float>(neighbor.x),
                        centerPoint.y - static_cast<float>(neighbor.y),
                    };
                    const float centerDrop = sampleGroundRim(centerPoint, centerProfile).drop;
                    const float neighborDrop = sampleGroundRim(neighborPoint, neighborProfile).drop;
                    checkNear(centerDrop, neighborDrop, "all 256 neighborhood seams agree", 1e-5f);
                    const Vec3 bodyTop = deformGroundRockPosition(
                        { centerPoint.x, centerPoint.y, 1.0f }, centerProfile);
                    checkNear(bodyTop.z, 1.0f - centerDrop, "body border meets its cap exactly");
                }
            }
        }
    }
}

void testInvalidRimProfiles()
{
    const MeshVertex vertex {
        .position = { 0.0f, 0.0f, 1.0f },
        .normal = { 0.0f, -1.0f, 0.0f },
        .tangent = { 1.0f, 0.0f, 0.0f, -1.0f },
        .uv = { 0.25f, 0.5f },
        .materialIndex = 7,
    };
    const auto invalid = [&](const GroundRimProfile& profile) {
        CHECK(!groundRimProfileValid(profile));
        const GroundRimSample sample = sampleGroundRim({}, profile);
        CHECK(sample.drop == 0.0f);
        CHECK(sample.gradient == Vec2 {});
        const MeshVertex mapped = deformGroundRockVertex(vertex, profile);
        CHECK(mapped.position == vertex.position);
        CHECK(mapped.normal == vertex.normal);
        CHECK(mapped.tangent == vertex.tangent);
        CHECK(mapped.uv == vertex.uv);
        CHECK(mapped.materialIndex == vertex.materialIndex);
    };
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (float width : { -1.0f, 0.0f, 0.5001f, infinity, nan }) {
        invalid(GroundRimProfile { .width = width });
    }
    for (float depth : { -1.0f, 0.0f, 0.225f, 0.30f, 0.31f, infinity, nan }) {
        invalid(GroundRimProfile { .depth = depth });
    }
    for (float band : { -1.0f, 0.0f, 0.05f, 0.10f, 1.001f, infinity, nan }) {
        invalid(GroundRimProfile { .bodyBand = band });
    }
    invalid(GroundRimProfile { .bodyBand = 0.10f * 1.35f });
    for (Vec2 origin : { Vec2 { nan, 0 }, Vec2 { 0, nan },
             Vec2 { infinity, 0 }, Vec2 { 0, -infinity } }) {
        invalid(GroundRimProfile { .origin = origin });
    }
    CHECK(groundRimProfileValid(GroundRimProfile {}));
    CHECK(groundRimProfileValid(GroundRimProfile { .width = 0.5f }));
    CHECK(groundRimProfileValid(GroundRimProfile { .bodyBand = 1.0f }));
    CHECK(sampleGroundRim({ nan, 0.0f }, GroundRimProfile {}).drop == 0.0f);
    CHECK(sampleGroundRim({ 0.0f, infinity }, GroundRimProfile {}).drop == 0.0f);
}

void testRimDrawInstanceBudget()
{
    constexpr uint64_t capacity = drawInstanceDiscardSlot;
    CHECK(groundRimDrawInstanceBudgetFits(capacity - 64, 0, 0, capacity));
    CHECK(!groundRimDrawInstanceBudgetFits(capacity - 63, 0, 0, capacity));
    CHECK(groundRimDrawInstanceBudgetFits(capacity - 164, 100, 0, capacity));
    CHECK(!groundRimDrawInstanceBudgetFits(capacity - 164, 101, 0, capacity));
    CHECK(groundRimDrawInstanceBudgetFits(capacity - 164, 60, 40, capacity));
    CHECK(!groundRimDrawInstanceBudgetFits(capacity - 164, 60, 41, capacity));
    CHECK(!groundRimDrawInstanceBudgetFits(
        std::numeric_limits<uint64_t>::max(), 1, 0, capacity));
    CHECK(!groundRimDrawInstanceBudgetFits(
        std::numeric_limits<uint64_t>::max(), 0, 0,
        std::numeric_limits<uint64_t>::max()));
    CHECK(ordinaryFrameDrawInstanceReserve(100, 200, 300) == 664);
    CHECK(ordinaryFrameDrawInstanceReserve(std::numeric_limits<uint64_t>::max(), 1, 0) ==
        std::numeric_limits<uint64_t>::max());

    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = 100;
    frame.levelHeight = 200;
    frame.levelDepth = 1;
    // Five thousand isolated ground cells are a legal render list. Their
    // exact prepared caps exceed the shared buffer, while flat caps fit.
    for (int index = 0; index < 5000; ++index) {
        const int x = (index % 50) * 2;
        const int y = (index / 50) * 2;
        frame.tiles.push_back({
            .cell = { x, y, 0 },
            .position = { static_cast<float>(x), static_cast<float>(y) },
            .color = { 1, 1, 1, 1 },
            .height = 1.0f,
            .model = { 1 },
            .groundTop = true,
            .groundRimWidth = 0.12f,
            .groundRimDepth = 0.10f,
        });
    }
    IsoScenePreparer preparer;
    preparer.setFrustumCulling(false);
    PreparedRenderScene scene;
    preparer.prepare(frame, { 1280, 720 }, scene);
    std::size_t capCount = 0;
    for (const auto& tile : frame.tiles) capCount += buildGroundRimSurface(tile).count;
    CHECK(scene.shadowFaces.size() == capCount);
    const uint64_t rimReserve = ordinarySceneDrawInstanceReserve(frame, scene);
    CHECK(!groundRimDrawInstanceBudgetFits(rimReserve, 0, 128, capacity));
    CHECK(rimReserve == scene.shadowFaces.size() +
        2ULL * (scene.opaqueFaceIndices.size() + scene.translucentFaceIndices.size() +
            scene.opaqueModelIndices.size() + scene.translucentModelIndices.size()));
    // Point-light cube faces reuse one range. Their count cannot multiply
    // the mandatory reserve; the recorder borrows only spare headroom.
    frame.lighting.pointLightCount = RenderFrameData::pointLightCapacity;
    CHECK(ordinarySceneDrawInstanceReserve(frame, scene) == rimReserve);
    for (auto& tile : frame.tiles) {
        tile.groundRimWidth = 0.0f;
        tile.groundRimDepth = 0.0f;
    }
    preparer.prepare(frame, { 1280, 720 }, scene);
    CHECK(scene.shadowFaces.size() == frame.tiles.size());
    const uint64_t flatReserve = ordinarySceneDrawInstanceReserve(frame, scene);
    CHECK(groundRimDrawInstanceBudgetFits(flatReserve, 0, 128, capacity));
    CHECK(groundRimDrawInstanceBudgetFits(flatReserve, flatReserve, 128, capacity));
    CHECK(!groundRimDrawInstanceBudgetFits(flatReserve, rimReserve, 128, capacity));
    CHECK(pointShadowBatchFitsDrawInstances(100, capacity,
        ordinaryFrameDrawInstanceReserve(flatReserve, 0, 128)));
    CHECK(!pointShadowBatchFitsDrawInstances(100, capacity,
        ordinaryFrameDrawInstanceReserve(rimReserve, 0, 128)));
}

} // namespace

int main()
{
    testAllMasks();
    testFallbacks();
    testAssetGate();
    testNeighborValidation();
    testAuthoredModels();
    testRimProfileAndBody();
    testRimVariationAndPositiveGeometry();
    testRimSeamsForEveryNeighborhood();
    testInvalidRimProfiles();
    testRimDrawInstanceBudget();
    if (failures != 0) {
        std::cerr << "GroundMeshGeometryTests: " << failures << " checks failed\n";
        return 1;
    }
    std::cout << "GroundMeshGeometryTests passed (" << checks << " checks)\n";
    return 0;
}
