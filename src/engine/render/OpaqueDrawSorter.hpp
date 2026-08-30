#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sokoban {

// The state that must remain identical across instances in one draw. Vertex
// transforms and normal rotations deliberately live outside this key in the
// model-instance buffer.
struct OpaqueDrawSortKey {
    uint32_t pipeline = 0;
    uint32_t material = 0;
    uint32_t mesh = 0;
    std::array<uint32_t, 29> fragmentState {};

    auto operator<=>(const OpaqueDrawSortKey&) const = default;
};

struct OpaqueDrawSortItem {
    OpaqueDrawSortKey key {};
    std::size_t drawIndex = 0;
    bool instancable = false;
};

struct OpaqueDrawBatch {
    std::size_t firstItem = 0;
    std::size_t itemCount = 0;
};

// Sorts opaque work by pipeline, material, and mesh. Items that cannot be
// instanced (currently skinned models) remain a one-item batch even if their
// key happens to match a neighbour.
// Writes into caller-owned storage so command recording can retain the batch
// allocation across frames. `batches` is cleared before use; its capacity is
// deliberately preserved.
void sortOpaqueDraws(
    std::vector<OpaqueDrawSortItem>& items,
    std::vector<OpaqueDrawBatch>& batches);

} // namespace sokoban
