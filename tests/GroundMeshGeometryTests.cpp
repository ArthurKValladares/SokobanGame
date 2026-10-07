#include "TestAssetRoot.hpp"
#include "TestHarness.hpp"

#include "engine/render/GroundMeshGeometry.hpp"
#include "engine/render/GroundRimGeometry.hpp"
#include "engine/render/IsoScenePreparer.hpp"
#include "engine/render/VulkanRenderConstants.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace sokoban;

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
    const GroundRimProfile north { .exposedSides = groundNorthSide };
    checkNear(sampleGroundRim({ 0.5f, 0.0f }, north).drop, 0.10f, "north rim endpoint");
    const GroundRimSample middle = sampleGroundRim({ 0.5f, 0.06f }, north);
    checkNear(middle.drop, 0.05f, "north rim midpoint");
    checkNear(middle.gradient.x, 0.0f, "north gradient X");
    checkNear(middle.gradient.y, -0.10f / 0.12f, "north gradient Y");
    checkNear(sampleGroundRim({ 0.5f, 0.5f }, north).drop, 0.0f, "interior remains flat");
    checkNear(sampleGroundRim({ 0.0f, 0.5f }, north).drop, 0.0f, "unexposed west edge remains flat");
    const GroundRimProfile convex {
        .exposedSides = groundNorthSide | groundWestSide,
    };
    checkNear(sampleGroundRim({ 0.03f, 0.06f }, convex).drop, 0.075f,
        "convex miter chooses nearest exposed side");
    const GroundRimProfile concave {
        .exposedSides = 0,
        .concaveCorners = groundNorthWestCorner,
    };
    checkNear(sampleGroundRim({ 0.03f, 0.06f }, concave).drop, 0.05f,
        "concave corner uses farther axis distance");
    checkNear(sampleGroundRim({ 0.0f, 0.06f }, concave).drop, middle.drop,
        "concave corner matches neighboring edge ramp");

    MeshVertex vertex {
        .position = { 0.5f, 0.06f, 0.85f },
        .normal = normalize(Vec3 { 0.3f, 0.4f, 0.5f }),
        .tangent = { 0.8f, -0.6f, 0.0f, -1.0f },
        .uv = { 0.25f, 0.75f },
        .uv1 = { 0.125f, 0.875f },
        .materialIndex = 17,
    };
    const MeshVertex deformed = deformGroundRockVertex(vertex, north);
    checkNear(deformed.position.x, vertex.position.x, "body X remains authored");
    checkNear(deformed.position.y, vertex.position.y, "body Y remains authored");
    checkNear(deformed.position.z, 0.825f, "only upper body band is compressed");
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
    const Vec3 mappedStep = deformGroundRockPosition(vertex.position + sourceTangent * 0.001f, north) -
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
        const GroundRimProfile profile { .exposedSides = sides };
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
            GroundRimProfile profile { .exposedSides = 0 };
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
    for (float depth : { -1.0f, 0.0f, 0.30f, 0.31f, infinity, nan }) {
        invalid(GroundRimProfile { .depth = depth });
    }
    for (float band : { -1.0f, 0.0f, 0.05f, 0.10f, 1.001f, infinity, nan }) {
        invalid(GroundRimProfile { .bodyBand = band });
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
    CHECK(scene.shadowFaces.size() == 13 * frame.tiles.size());
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
