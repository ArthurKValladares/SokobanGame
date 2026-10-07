#pragma once

#include "engine/Geometry.hpp"
#include "engine/render/GroundRimSurface.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sokoban {

struct ProcessedGroundArtifact;

inline constexpr int groundChunkCellWidth = 8;

// Coverage stores the complete source quad's weights; faceCoord interpolates
// them through the same two triangles used by the existing cap face path.
struct GroundChunkVertex {
    Vec3 position {};
    Vec3 normal {};
    Vec2 faceCoord {};
    Vec4 wallCoverage {};
    uint32_t tileSlot = 0;
    friend constexpr bool operator==(const GroundChunkVertex&, const GroundChunkVertex&) = default;
};
static_assert(sizeof(GroundChunkVertex) == 52);

struct GroundChunkTile {
    GroundRimSurfaceKey key {};
    GridPosition3 cell {};
    friend constexpr bool operator==(const GroundChunkTile&, const GroundChunkTile&) = default;
};

struct GroundChunk {
    // Stable slot order follows authored cells, not frame submission order.
    // Materials stay in the frame and are resolved separately by these keys.
    std::vector<GroundChunkTile> tiles;
    std::vector<GroundChunkVertex> vertices;
    std::vector<uint32_t> indices;
    Aabb bounds {};
    uint32_t inputVertexCount = 0;
    float inputAcmr = 0.0f;
    float outputAcmr = 0.0f;
};

struct GroundChunkGeometry {
    std::vector<GroundChunk> chunks;
    bool optimized = false;
    // Vertex/index payload bytes, excluding owning-container bookkeeping.
    uint64_t geometryBytes = 0;
    uint64_t originalBytes = 0;
};

[[nodiscard]] bool isGroundChunkTileEligible(const RenderFrameData::Tile& tile) noexcept;

// Compiles eligible opaque unit ground tops. Each authored layer is divided
// into 8x8 cells, including negative origins. Ambiguous duplicate cells are
// omitted so their existing individual cap path can remain authoritative.
// Optimization preserves triangle winding, degenerates and complete vertex
// attributes, while allowing triangle and vertex order to change.
[[nodiscard]] GroundChunkGeometry compileGroundChunkGeometry(
    std::span<const RenderFrameData::Tile> tiles,
    const ProcessedGroundArtifact* artifact = nullptr,
    bool optimize = true);

class GroundChunkGeometryCache {
public:
    // Camera/material changes and source order do not affect geometry. Hits
    // retain the same immutable owning result and allocate nothing after the
    // signature scratch space has warmed. The optional artifact is consulted
    // only on a geometry rebuild; no provider-owned pointers survive it.
    [[nodiscard]] std::shared_ptr<const GroundChunkGeometry> update(
        std::span<const RenderFrameData::Tile> tiles,
        const ProcessedGroundArtifact* artifact = nullptr,
        bool optimize = true);
    [[nodiscard]] bool lastUpdateReused() const noexcept { return lastUpdateReused_; }
    [[nodiscard]] uint64_t hitCount() const noexcept { return hitCount_; }
    [[nodiscard]] uint64_t rebuildCount() const noexcept { return rebuildCount_; }
    [[nodiscard]] std::size_t capacityBytes() const noexcept;
    void invalidate() noexcept;

private:
    struct Signature {
        GroundChunkTile tile {};
        std::size_t sourceIndex = 0;
    };
    std::vector<Signature> signatures_;
    std::vector<Signature> workingSignatures_;
    std::shared_ptr<const GroundChunkGeometry> geometry_;
    uint64_t hitCount_ = 0;
    uint64_t rebuildCount_ = 0;
    bool optimized_ = false;
    bool lastUpdateReused_ = false;
};

} // namespace sokoban
