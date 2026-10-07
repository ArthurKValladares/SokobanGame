#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/render/RenderTypes.hpp"

#include <span>
#include <vector>

namespace sokoban {

struct ProcessedGroundCell {
    GridPosition3 cell {};
    RenderModel model = cubeModel;
    uint8_t groundSideMask = groundAllSides;
    std::array<RenderModel, 4> sideNeighbors {};
    // Corner bits use NW, NE, SE, SW order, with grid offsets
    // (-1,-1), (1,-1), (1,1), (-1,1).
    uint8_t diagonalNeighbors = 0;
    std::array<RenderModel, 4> diagonalNeighborModels {};
    // A convex corner has neither incident cardinal neighbour. A concave
    // corner has both cardinal neighbours, but lacks their shared diagonal.
    // Diagonal-only contacts remain separate convex corners; the diagonal
    // mask identifies that contact so a future rim need not weld them.
    uint8_t convexCorners = 0;
    uint8_t concaveCorners = 0;

    bool operator==(const ProcessedGroundCell&) const = default;
};

// Vulkan-free boundary description, sorted by layer, row, then column. Cells
// describe authored occupancy rather than GPU readiness. Tops and bottom
// geometry remain separate from these horizontal boundary connections.
struct ProcessedGround {
    std::vector<ProcessedGroundCell> cells;
    uint32_t exposedSideCount = 0;
    uint32_t convexCornerCount = 0;
    uint32_t concaveCornerCount = 0;

    bool operator==(const ProcessedGround&) const = default;
};

class GroundGeometryCache {
public:
    [[nodiscard]] const ProcessedGround& processed() const noexcept { return processed_; }
    [[nodiscard]] uint64_t hitCount() const noexcept { return hitCount_; }
    [[nodiscard]] uint64_t rebuildCount() const noexcept { return rebuildCount_; }
    [[nodiscard]] bool lastResultReused() const noexcept { return lastResultReused_; }
    [[nodiscard]] std::size_t capacityBytes() const noexcept;

    // Release no capacity; the next processing request rebuilds the descriptor.
    void invalidate() noexcept;

private:
    struct Signature {
        GridPosition3 cell {};
        RenderModel model = cubeModel;

        bool operator==(const Signature&) const = default;
    };

    void update(std::span<const RenderFrameData::Tile> tiles, const AssetManifest& manifest);
    [[nodiscard]] std::size_t slotFor(GridPosition3 cell) const noexcept;
    [[nodiscard]] const ProcessedGroundCell* cellAt(GridPosition3 cell) const noexcept;

    // Exact sorted signatures prevent hash collisions from accepting stale
    // geometry. Working and compiled storage are retained across frame builds.
    std::vector<Signature> signatures_;
    std::vector<Signature> workingSignatures_;
    std::vector<uint32_t> adjacency_;
    ProcessedGround processed_;
    uint64_t hitCount_ = 0;
    uint64_t rebuildCount_ = 0;
    bool valid_ = false;
    bool lastResultReused_ = false;

    friend void processGroundGeometry(std::span<RenderFrameData::Tile>,
        const AssetManifest&, FrameArena*, GroundGeometryCache*);
    friend ProcessedGround compileGroundGeometry(std::span<const RenderFrameData::Tile>,
        const AssetManifest&);
};

// An owning description for offline tools. It retains no pointers into tiles
// or the manifest, and does not alter the input render list.
[[nodiscard]] ProcessedGround compileGroundGeometry(
    std::span<const RenderFrameData::Tile> tiles,
    const AssetManifest& manifest);

// Derive exposed sides from the actual render list, after screen visibility,
// layer visibility and editor previews have been resolved. Gameplay cells that
// were not emitted cannot conceal a side at a rendered screen boundary.
// Only fixed, opaque, untransformed unit ground on the same layer participates;
// tops, bottoms, logical cells, and the authored level are left intact.
// Neighbour model identities accompany hidden sides so the renderer can defer
// removal until that neighbouring source mesh is loaded and validated.
//
// A cache retains its descriptor and scratch capacity, reusing them whenever
// the exact eligible ground set is unchanged. Reordering tiles, actors, and
// material colors do not invalidate it; edits and visibility/eligibility
// changes do. GPU readiness is checked later by the renderer.
// Without a cache, live builders provide a frame arena for the O(n) adjacency
// table. Arena exhaustion preserves full meshes; offline callers own scratch.
void processGroundGeometry(
    std::span<RenderFrameData::Tile> tiles,
    const AssetManifest& manifest,
    FrameArena* arena = nullptr,
    GroundGeometryCache* cache = nullptr);

} // namespace sokoban
