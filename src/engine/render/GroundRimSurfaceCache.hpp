#pragma once

#include "engine/render/GroundRimSurface.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sokoban {

// Owning, renderer-independent compiler output in source-tile order. No
// pointers into the input frame survive compilation; flat tiles are omitted.
struct CompiledGroundRimSurface {
    std::size_t tileIndex = 0;
    GroundRimSurface surface {};
};

[[nodiscard]] std::vector<CompiledGroundRimSurface> compileGroundRimSurfaces(
    std::span<const RenderFrameData::Tile> tiles);

// Retains completed world-space cap patches, not projected faces or materials.
// Only effective hasGroundRimSurface tiles occupy surface storage. Exact keys
// cover their geometry inputs and model identity; camera, paint, color, and
// other per-frame presentation data do not participate.
//
// Update once after rim readiness/budget resolution, before preparation
// workers start. Const lookups neither allocate nor change telemetry. Returned
// pointers are immutable and valid until the next update or invalidate; scene
// preparation must copy their patches into its own leased frame storage.
class GroundRimSurfaceCache {
public:
    void update(std::span<const RenderFrameData::Tile> tiles);
    [[nodiscard]] const GroundRimSurface* surfaceForTileIndex(
        std::size_t tileIndex) const noexcept;

    [[nodiscard]] std::size_t surfaceCount() const noexcept { return entries_.size(); }
    [[nodiscard]] bool lastUpdateReused() const noexcept { return lastUpdateReused_; }
    // Surface counters describe source-tile occurrences in the latest update.
    // Changed keys regenerate only their own patches; unchanged keys reuse
    // retained slots even when the source tiles were reordered.
    [[nodiscard]] std::size_t reusedSurfaceCount() const noexcept { return reusedSurfaceCount_; }
    [[nodiscard]] std::size_t generatedSurfaceCount() const noexcept { return generatedSurfaceCount_; }
    // Cumulative counters describe update calls, never read-only lookups. A
    // hit has the same complete active key set; a rebuild has a changed set
    // or follows invalidation. The first update, including flat-only, rebuilds.
    [[nodiscard]] uint64_t hitCount() const noexcept { return hitCount_; }
    [[nodiscard]] uint64_t rebuildCount() const noexcept { return rebuildCount_; }
    // Retained heap capacity, including surfaces, mappings, and scratch keys.
    [[nodiscard]] std::size_t capacityBytes() const noexcept;

    // Keeps allocated capacity for reuse and excludes all previous surfaces.
    void invalidate() noexcept;

private:
    using Key = std::array<uint32_t, 9>;

    struct Entry {
        Key key {};
        std::size_t surfaceSlot = 0;
    };

    struct Candidate {
        Key key {};
        std::size_t tileIndex = 0;
    };

    struct Mapping {
        std::size_t tileIndex = 0;
        std::size_t surfaceSlot = 0;
    };

    [[nodiscard]] static Key keyFor(const RenderFrameData::Tile& tile) noexcept;
    void rebuildMappings();

    // Surface slots remain stable through edits. Removed entries return slots
    // to the free list, so same-capacity edits need no allocation or copies of
    // large patch arrays. Sorted keys and compact source-index mappings keep a
    // newly constructed flat-only cache at zero heap capacity.
    std::vector<GroundRimSurface> surfaces_;
    std::vector<std::size_t> freeSlots_;
    std::vector<Entry> entries_;
    std::vector<Entry> workingEntries_;
    std::vector<Candidate> candidates_;
    std::vector<Mapping> mappings_;
    uint64_t hitCount_ = 0;
    uint64_t rebuildCount_ = 0;
    std::size_t reusedSurfaceCount_ = 0;
    std::size_t generatedSurfaceCount_ = 0;
    bool valid_ = false;
    bool lastUpdateReused_ = false;
};

} // namespace sokoban
