#include "TestAssetRoot.hpp"
#include "TestHarness.hpp"

#include "engine/render/GltfMesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using sokoban::MeshData;
using sokoban::MeshVertex;
using sokoban::Vec3;

struct Bounds {
    Vec3 minimum {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    Vec3 maximum {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };
};

Bounds boundsOf(const MeshData& mesh)
{
    Bounds bounds;
    for (const MeshVertex& vertex : mesh.vertices) {
        bounds.minimum.x = std::min(bounds.minimum.x, vertex.position.x);
        bounds.minimum.y = std::min(bounds.minimum.y, vertex.position.y);
        bounds.minimum.z = std::min(bounds.minimum.z, vertex.position.z);
        bounds.maximum.x = std::max(bounds.maximum.x, vertex.position.x);
        bounds.maximum.y = std::max(bounds.maximum.y, vertex.position.y);
        bounds.maximum.z = std::max(bounds.maximum.z, vertex.position.z);
    }
    return bounds;
}

bool near(float value, float expected, float tolerance = 0.0001f)
{
    return std::abs(value - expected) <= tolerance;
}

bool hasVertexNear(
    const MeshData& mesh,
    uint32_t material,
    float x,
    float y,
    float z,
    float tolerance)
{
    return std::ranges::any_of(mesh.vertices, [&](const MeshVertex& vertex) {
        return vertex.materialIndex == material &&
            std::abs(vertex.position.x - x) <= tolerance &&
            std::abs(vertex.position.y - y) <= tolerance &&
            std::abs(vertex.position.z - z) <= tolerance;
    });
}

MeshData loadMinecartModel(std::string_view file)
{
    return sokoban::loadGltfMesh(
        testAssetRoot() / "custom/models" / file,
        { .preserveSourceScale = true });
}

void checkCommonMeshInvariants(const MeshData& mesh)
{
    CHECK(!mesh.vertices.empty());
    CHECK(!mesh.indices.empty());
    CHECK(mesh.indices.size() % 3 == 0);
    CHECK(mesh.materials.size() == 4);
    for (const MeshVertex& vertex : mesh.vertices) {
        CHECK(std::isfinite(vertex.position.x));
        CHECK(std::isfinite(vertex.position.y));
        CHECK(std::isfinite(vertex.position.z));
        CHECK(std::isfinite(vertex.normal.x));
        CHECK(std::isfinite(vertex.normal.y));
        CHECK(std::isfinite(vertex.normal.z));
        CHECK(vertex.materialIndex < mesh.materials.size());
    }
    for (uint32_t index : mesh.indices) {
        CHECK(index < mesh.vertices.size());
    }
}

void testStraightRail()
{
    TEST("straight rail");
    const MeshData mesh = loadMinecartModel("minecart_rail_straight.glb");
    checkCommonMeshInvariants(mesh);
    const Bounds bounds = boundsOf(mesh);
    CHECK(bounds.minimum.x >= 0.0f && bounds.maximum.x <= 1.0f);
    CHECK(near(bounds.minimum.y, 0.0f));
    CHECK(near(bounds.maximum.y, 1.0f));
    CHECK(near(bounds.minimum.z, 0.0f));
    CHECK(near(bounds.maximum.z, 0.165f));

    // Gunmetal is material slot 1. Both rails expose matching end faces at
    // the two tile boundaries, centred on the shared 0.32/0.68 gauge.
    for (float endpoint : { 0.0f, 1.0f }) {
        CHECK(hasVertexNear(mesh, 1, 0.32f, endpoint, 0.12f, 0.05f));
        CHECK(hasVertexNear(mesh, 1, 0.68f, endpoint, 0.12f, 0.05f));
    }
}

void testCornerRail()
{
    TEST("corner rail");
    const MeshData mesh = loadMinecartModel("minecart_rail_corner.glb");
    checkCommonMeshInvariants(mesh);
    const Bounds bounds = boundsOf(mesh);
    CHECK(bounds.minimum.x >= -0.0001f && bounds.maximum.x <= 1.0f);
    CHECK(bounds.minimum.y >= -0.0001f && bounds.maximum.y <= 1.0f);
    CHECK(near(bounds.minimum.z, 0.0f));
    CHECK(near(bounds.maximum.z, 0.165f));

    // The shared base corner is north-west: one end meets the north-facing
    // straight at y=0 and the other meets the west-facing straight at x=0.
    // TileTypes rotates this source mesh for the other three connector masks.
    CHECK(hasVertexNear(mesh, 1, 0.32f, 0.0f, 0.12f, 0.05f));
    CHECK(hasVertexNear(mesh, 1, 0.68f, 0.0f, 0.12f, 0.05f));
    CHECK(hasVertexNear(mesh, 1, 0.0f, 0.32f, 0.12f, 0.05f));
    CHECK(hasVertexNear(mesh, 1, 0.0f, 0.68f, 0.12f, 0.05f));
}

void testStopRail()
{
    TEST("stop rail");
    const MeshData mesh = loadMinecartModel("minecart_rail_stop.glb");
    checkCommonMeshInvariants(mesh);
    const Bounds bounds = boundsOf(mesh);
    CHECK(bounds.minimum.x >= 0.0f && bounds.maximum.x <= 1.0f);
    CHECK(near(bounds.minimum.y, 0.0f));
    CHECK(near(bounds.maximum.y, 1.0f));
    CHECK(near(bounds.minimum.z, 0.0f));
    CHECK(near(bounds.maximum.z, 0.165f));

    for (float endpoint : { 0.0f, 1.0f }) {
        CHECK(hasVertexNear(mesh, 1, 0.32f, endpoint, 0.12f, 0.05f));
        CHECK(hasVertexNear(mesh, 1, 0.68f, endpoint, 0.12f, 0.05f));
    }
    CHECK(hasVertexNear(mesh, 3, 0.50f, 0.50f, 0.145f, 0.10f));
    const auto& stopMaterial = mesh.materials[3];
    CHECK(stopMaterial.baseColorFactor.x >= 0.95f);
    CHECK(stopMaterial.baseColorFactor.y <= 0.05f);
    CHECK(stopMaterial.baseColorFactor.z <= 0.05f);
    CHECK(stopMaterial.emissiveFactor.x >= 0.15f);
}

void testHandcar()
{
    TEST("platform cart");
    const MeshData mesh = loadMinecartModel("minecart_handcar.glb");
    checkCommonMeshInvariants(mesh);
    const Bounds bounds = boundsOf(mesh);
    CHECK(bounds.minimum.x >= 0.0f && bounds.maximum.x <= 1.0f);
    CHECK(bounds.minimum.y >= 0.0f && bounds.maximum.y <= 1.0f);
    CHECK(bounds.minimum.z >= 0.0f);
    CHECK(near(bounds.maximum.z, 0.335f));

    // The dark-iron wheel geometry shares the rail centres. Front and rear
    // wheel sets must remain on both gauge lines when the art is regenerated.
    for (float x : { 0.32f, 0.68f }) {
        for (float y : { 0.29f, 0.71f }) {
            CHECK(hasVertexNear(mesh, 2, x, y, 0.145f, 0.11f));
        }
    }
}

} // namespace

int main()
{
    testStraightRail();
    testCornerRail();
    testStopRail();
    testHandcar();

    if (failures == 0) {
        std::cout << "MinecartAssetTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "MinecartAssetTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
