#include "TestAssetRoot.hpp"
#include "TestHarness.hpp"

#include "engine/render/GroundMeshGeometry.hpp"

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

} // namespace

int main()
{
    testAllMasks();
    testFallbacks();
    testAssetGate();
    testNeighborValidation();
    testAuthoredModels();
    if (failures != 0) {
        std::cerr << "GroundMeshGeometryTests: " << failures << " checks failed\n";
        return 1;
    }
    std::cout << "GroundMeshGeometryTests passed (" << checks << " checks)\n";
    return 0;
}
