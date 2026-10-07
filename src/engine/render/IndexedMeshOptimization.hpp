#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sokoban {

struct IndexedMeshOptimizationResult {
    std::vector<std::byte> vertices;
    std::vector<uint32_t> indices;
    uint32_t inputVertexCount = 0;
    uint32_t outputVertexCount = 0;
    // A fixed 16-entry FIFO cache model, for comparison rather than GPU timing.
    float inputAcmr = 0.0f;
    float outputAcmr = 0.0f;
};

// Lossless for every indexed triangle and every byte of its vertex attributes.
// Exact complete vertex records are deduplicated, triangles are reordered for
// cache locality, and unreferenced vertices are omitted. Triangle order changes;
// callers must pass independent draw ranges separately if order is significant.
// Padding is part of the key and should be initialized deterministically by the
// caller. No simplification, quantization, or degenerate-triangle filtering runs.
// Disabled optimization still validates, but returns byte-identical buffers.
// Accepts vertex strides in [1, 256]; invalid input throws std::invalid_argument.
[[nodiscard]] IndexedMeshOptimizationResult optimizeIndexedMesh(
    std::span<const std::byte> vertices,
    std::size_t vertexStride,
    std::span<const uint32_t> indices,
    bool optimize = true);

} // namespace sokoban
