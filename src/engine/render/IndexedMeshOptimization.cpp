#include "engine/render/IndexedMeshOptimization.hpp"

#include <meshoptimizer.h>

#include <limits>
#include <stdexcept>

namespace sokoban {
namespace {

constexpr unsigned int comparisonCacheSize = 16;

float analyzeAcmr(std::span<const uint32_t> indices, std::size_t vertexCount)
{
    if (indices.empty()) {
        return 0.0f;
    }
    return meshopt_analyzeVertexCache(indices.data(), indices.size(), vertexCount,
        comparisonCacheSize, 0, 0).acmr;
}

} // namespace

IndexedMeshOptimizationResult optimizeIndexedMesh(
    std::span<const std::byte> vertices,
    std::size_t vertexStride,
    std::span<const uint32_t> indices,
    bool optimize)
{
    if (vertexStride == 0 || vertexStride > 256) {
        throw std::invalid_argument("Indexed mesh vertex stride must be in [1, 256]");
    }
    if (vertices.size() % vertexStride != 0) {
        throw std::invalid_argument("Indexed mesh vertex bytes contain an incomplete record");
    }
    const std::size_t vertexCount = vertices.size() / vertexStride;
    if (vertexCount > std::numeric_limits<uint32_t>::max() ||
        indices.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::invalid_argument("Indexed mesh exceeds 32-bit vertex or index counts");
    }
    if (indices.size() % 3 != 0) {
        throw std::invalid_argument("Indexed mesh index count must be a multiple of three");
    }
    for (uint32_t index : indices) {
        if (index >= vertexCount) {
            throw std::invalid_argument("Indexed mesh index is outside the vertex buffer");
        }
    }

    IndexedMeshOptimizationResult result;
    // Own all input buffers before passing them to the library. The vertex
    // operations use byte-wise comparisons and memcpy, never aligned typed
    // accesses into the caller's byte span.
    result.vertices.assign(vertices.begin(), vertices.end());
    result.indices.assign(indices.begin(), indices.end());
    result.inputVertexCount = static_cast<uint32_t>(vertexCount);
    result.outputVertexCount = result.inputVertexCount;
    result.inputAcmr = analyzeAcmr(result.indices, vertexCount);
    result.outputAcmr = result.inputAcmr;
    if (!optimize) {
        return result;
    }
    if (result.indices.empty()) {
        result.vertices.clear();
        result.outputVertexCount = 0;
        return result;
    }

    std::vector<uint32_t> remap(vertexCount);
    const std::size_t uniqueCount = meshopt_generateVertexRemap(remap.data(),
        result.indices.data(), result.indices.size(), result.vertices.data(),
        vertexCount, vertexStride);
    std::vector<std::byte> uniqueVertices(uniqueCount * vertexStride);
    meshopt_remapVertexBuffer(uniqueVertices.data(), result.vertices.data(),
        vertexCount, vertexStride, remap.data());
    meshopt_remapIndexBuffer(result.indices.data(), result.indices.data(),
        result.indices.size(), remap.data());
    meshopt_optimizeVertexCache(result.indices.data(), result.indices.data(),
        result.indices.size(), uniqueCount);

    result.vertices.resize(uniqueVertices.size());
    const std::size_t fetchedCount = meshopt_optimizeVertexFetch(result.vertices.data(),
        result.indices.data(), result.indices.size(), uniqueVertices.data(),
        uniqueCount, vertexStride);
    result.vertices.resize(fetchedCount * vertexStride);
    result.outputVertexCount = static_cast<uint32_t>(fetchedCount);
    result.outputAcmr = analyzeAcmr(result.indices, fetchedCount);
    return result;
}

} // namespace sokoban
