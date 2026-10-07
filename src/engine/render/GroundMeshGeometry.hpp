#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/render/GltfMesh.hpp"

#include <array>
#include <cstdint>

namespace sokoban {

// North is y=0, east x=1, south y=1, west x=0 in engine tile space.
// Bottom geometry is retained by every variant; the painted top is separate.
struct GroundMeshIndexRange {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;

    bool operator==(const GroundMeshIndexRange&) const = default;
};

struct GroundMeshVariants {
    std::array<GroundMeshIndexRange, 16> ranges {};
    bool processed = false;
};

// Only the authored, unit-scale ground-rock family has the coverage contract
// required by the experiment. Custom models retain their original geometry.
[[nodiscard]] bool isProcessableGroundRockModel(
    const AssetManifest::Model& model) noexcept;

// Candidate hidden sides only disappear after their corresponding neighbor
// has published a validated mesh. Exposed sides remain exposed throughout
// streaming and residency changes; unknown neighbors always fail open.
[[nodiscard]] constexpr uint8_t effectiveGroundSideMask(
    uint8_t candidateMask,
    const std::array<bool, 4>& validatedNeighbors) noexcept
{
    uint8_t result = candidateMask & groundAllSides;
    for (uint32_t side = 0; side < validatedNeighbors.size(); ++side) {
        if (!validatedNeighbors[side]) {
            result |= static_cast<uint8_t>(1U << side);
        }
    }
    return result;
}

// Validates the complete source mesh before changing anything. Recognizes
// material-separated shallow side slabs and the square bottom, then appends
// index-only variants. The original index sequence stays at offset zero for
// thumbnails, decorations, and unprocessed draws. Any ambiguity fails open:
// all ranges select the complete original mesh and no buffers are modified.
[[nodiscard]] GroundMeshVariants appendGroundMeshVariants(MeshData& mesh);

} // namespace sokoban
