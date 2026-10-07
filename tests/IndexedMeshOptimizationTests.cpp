#include "TestHarness.hpp"

#include "engine/render/IndexedMeshOptimization.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace {

using namespace sokoban;

// Every byte is an attribute, including an opaque word that would be unsafe
// to discard in a position-only weld.
struct TestVertex {
    std::array<float, 3> position {};
    std::array<float, 3> normal { 0.0f, 0.0f, 1.0f };
    std::array<float, 2> uv {};
    uint32_t material = 0;
    uint32_t opaque = 0;
};

static_assert(sizeof(TestVertex) == 40);

std::vector<std::byte> bytesOf(std::span<const TestVertex> vertices)
{
    const auto bytes = std::as_bytes(vertices);
    return { bytes.begin(), bytes.end() };
}

using TriangleKey = std::vector<std::byte>;

std::vector<TriangleKey> orientedTriangles(std::span<const std::byte> vertices,
    std::size_t stride, std::span<const uint32_t> indices)
{
    std::vector<TriangleKey> triangles;
    for (std::size_t i = 0; i < indices.size(); i += 3) {
        TriangleKey key;
        // Cyclic rotation retains winding. Reversal deliberately does not.
        for (std::size_t rotation = 0; rotation < 3; ++rotation) {
            TriangleKey candidate;
            candidate.reserve(stride * 3);
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const std::size_t index = indices[i + (corner + rotation) % 3];
                const auto record = vertices.subspan(index * stride, stride);
                candidate.insert(candidate.end(), record.begin(), record.end());
            }
            if (rotation == 0 || candidate < key) {
                key = candidate;
            }
        }
        triangles.push_back(key);
    }
    std::sort(triangles.begin(), triangles.end());
    return triangles;
}

void testLosslessWeldingAndSeams()
{
    TEST("Complete attributes and oriented triangles survive optimization");
    const TestVertex a { .position = { 0.0f, 0.0f, 0.0f }, .opaque = 0xabc01234U };
    const TestVertex b { .position = { 1.0f, 0.0f, 0.0f } };
    const TestVertex c { .position = { 0.0f, 1.0f, 0.0f } };
    TestVertex normalSeam = a;
    normalSeam.normal = { 1.0f, 0.0f, 0.0f };
    TestVertex uvSeam = a;
    uvSeam.uv = { 0.5f, 1.0f };
    TestVertex materialSeam = a;
    materialSeam.material = 2;
    TestVertex opaqueSeam = a;
    opaqueSeam.opaque ^= 1U;
    TestVertex signedZeroSeam = a;
    signedZeroSeam.position[0] = -0.0f;
    const std::vector<TestVertex> vertices {
        a, b, c, a, b, c, normalSeam, uvSeam, materialSeam,
        opaqueSeam, signedZeroSeam, TestVertex { .position = { 99.0f, 99.0f, 99.0f } },
    };
    const std::vector<uint32_t> indices {
        0, 1, 2, 3, 4, 5, 0, 2, 1, 6, 1, 2, 7, 1, 2,
        8, 1, 2, 9, 1, 2, 10, 1, 2, 0, 0, 1,
    };
    const auto bytes = bytesOf(vertices);
    const auto result = optimizeIndexedMesh(bytes, sizeof(TestVertex), indices);
    CHECK(result.inputVertexCount == 12);
    CHECK(result.outputVertexCount == 8);
    CHECK(result.indices.size() == indices.size());
    CHECK(result.vertices.size() == result.outputVertexCount * sizeof(TestVertex));
    CHECK(orientedTriangles(bytes, sizeof(TestVertex), indices) ==
        orientedTriangles(result.vertices, sizeof(TestVertex), result.indices));
    CHECK(result.outputAcmr <= result.inputAcmr);
    // Caller buffers remain untouched and the result is deterministic.
    CHECK(bytes == bytesOf(vertices));
    const auto repeat = optimizeIndexedMesh(bytes, sizeof(TestVertex), indices);
    CHECK(repeat.vertices == result.vertices);
    CHECK(repeat.indices == result.indices);
    CHECK(repeat.outputAcmr == result.outputAcmr);
}

void testDisabledAndEmptyInputs()
{
    TEST("Disabled output retains unreferenced vertices and exact buffer order");
    const std::vector<TestVertex> vertices {
        TestVertex {}, TestVertex { .position = { 1.0f, 0.0f, 0.0f } },
        TestVertex { .position = { 0.0f, 1.0f, 0.0f } }, TestVertex {},
    };
    const std::vector<uint32_t> indices { 2, 0, 1, 2, 3, 1 };
    const auto bytes = bytesOf(vertices);
    const auto disabled = optimizeIndexedMesh(bytes, sizeof(TestVertex), indices, false);
    CHECK(disabled.vertices == bytes);
    CHECK(disabled.indices == indices);
    CHECK(disabled.inputVertexCount == vertices.size());
    CHECK(disabled.outputVertexCount == vertices.size());
    CHECK(disabled.inputAcmr == disabled.outputAcmr);
    const auto empty = optimizeIndexedMesh({}, sizeof(TestVertex), {});
    CHECK(empty.vertices.empty());
    CHECK(empty.indices.empty());
    CHECK(empty.inputVertexCount == 0);
    CHECK(empty.outputVertexCount == 0);
    CHECK(empty.inputAcmr == 0.0f);
    CHECK(empty.outputAcmr == 0.0f);
    const auto unused = optimizeIndexedMesh(bytes, sizeof(TestVertex), {});
    CHECK(unused.inputVertexCount == vertices.size());
    CHECK(unused.outputVertexCount == 0);
    CHECK(unused.vertices.empty());
    const auto unusedDisabled = optimizeIndexedMesh(bytes, sizeof(TestVertex), {}, false);
    CHECK(unusedDisabled.vertices == bytes);
}

void testUnalignedAndOddStride()
{
    TEST("Byte spans and odd vertex strides do not require typed alignment");
    const std::array<std::byte, 5> a {
        std::byte { 1 }, std::byte { 2 }, std::byte { 3 }, std::byte { 4 }, std::byte { 5 },
    };
    std::vector<std::byte> storage { std::byte { 255 } };
    for (uint8_t vertex = 0; vertex < 4; ++vertex) {
        storage.insert(storage.end(), a.begin(), a.end());
        if (vertex < 3) {
            storage.back() = static_cast<std::byte>(vertex);
        }
    }
    storage.back() = std::byte { 0 };
    const auto unaligned = std::span<const std::byte>(storage).subspan(1);
    const std::array<uint32_t, 6> indices { 0, 1, 2, 3, 2, 1 };
    const auto result = optimizeIndexedMesh(unaligned, a.size(), indices);
    CHECK(result.outputVertexCount == 3);
    CHECK(orientedTriangles(unaligned, a.size(), indices) ==
        orientedTriangles(result.vertices, a.size(), result.indices));
}

void testInvalidInputs()
{
    TEST("Malformed buffers are rejected before entering meshoptimizer");
    const std::array<std::byte, 12> vertices {};
    const std::array<uint32_t, 3> triangle { 0, 1, 2 };
    checkThrows([&] { (void)optimizeIndexedMesh(vertices, 0, triangle); }, "zero stride");
    checkThrows([&] { (void)optimizeIndexedMesh({}, 257, {}); }, "oversized stride");
    checkThrows([&] { (void)optimizeIndexedMesh(vertices, 5, triangle); }, "partial vertex");
    checkThrows([&] { (void)optimizeIndexedMesh(vertices, 4,
        std::span<const uint32_t>(triangle).first(2)); }, "partial triangle");
    checkThrows([&] { (void)optimizeIndexedMesh(vertices, 6, triangle); }, "index at vertex count");
    checkThrows([&] { (void)optimizeIndexedMesh({}, 4, triangle); }, "indices without vertices");
    checkThrows([&] { (void)optimizeIndexedMesh(vertices, 6, triangle, false); },
        "disabled path still validates");
    const std::array<uint32_t, 3> invalid { 0, 1, 0xffffffffU };
    checkThrows([&] { (void)optimizeIndexedMesh(vertices, 4, invalid); }, "oversized index");
}

void testCacheLocality()
{
    TEST("A spatially scrambled grid improves modeled vertex-cache reuse");
    constexpr uint32_t width = 16;
    std::vector<TestVertex> vertices;
    for (uint32_t y = 0; y <= width; ++y) {
        for (uint32_t x = 0; x <= width; ++x) {
            vertices.push_back(TestVertex {
                .position = { static_cast<float>(x), static_cast<float>(y), 0.0f },
            });
        }
    }
    std::vector<std::array<uint32_t, 3>> triangles;
    for (uint32_t y = 0; y < width; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t a = y * (width + 1) + x;
            triangles.push_back({ a, a + 1, a + width + 2 });
            triangles.push_back({ a, a + width + 2, a + width + 1 });
        }
    }
    std::vector<uint32_t> indices;
    // An odd multiplier permutes this power-of-two triangle count.
    for (std::size_t i = 0; i < triangles.size(); ++i) {
        const auto& triangle = triangles[(i * 137) % triangles.size()];
        indices.insert(indices.end(), triangle.begin(), triangle.end());
    }
    const auto bytes = bytesOf(vertices);
    const auto result = optimizeIndexedMesh(bytes, sizeof(TestVertex), indices);
    CHECK(result.outputVertexCount == vertices.size());
    CHECK(result.outputAcmr < result.inputAcmr * 0.75f);
    CHECK(orientedTriangles(bytes, sizeof(TestVertex), indices) ==
        orientedTriangles(result.vertices, sizeof(TestVertex), result.indices));
}

} // namespace

int main()
{
    testLosslessWeldingAndSeams();
    testDisabledAndEmptyInputs();
    testUnalignedAndOddStride();
    testInvalidInputs();
    testCacheLocality();
    if (failures != 0) {
        std::cerr << "IndexedMeshOptimizationTests: " << failures << " checks failed\n";
        return 1;
    }
    std::cout << "IndexedMeshOptimizationTests passed (" << checks << " checks)\n";
    return 0;
}
