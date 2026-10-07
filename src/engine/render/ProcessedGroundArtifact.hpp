#pragma once

#include "engine/render/GroundRimSurface.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace sokoban {

inline constexpr uint32_t processedGroundArtifactFormatVersion = 1;
// Bump when fracture selection, topology, normals, coverage, or geometry-key
// interpretation changes for the same authored inputs.
inline constexpr uint32_t processedGroundArtifactCompilerRevision = 1;
inline constexpr float processedGroundArtifactRimWidth = 0.12f;
inline constexpr float processedGroundArtifactRimDepth = 0.10f;
inline constexpr std::size_t processedGroundArtifactMaxEntries = RenderFrameData::tileCapacity;
inline constexpr std::size_t processedGroundArtifactMaxBytes = 64U * 1024U * 1024U;

struct ProcessedGroundArtifactEntry {
    GroundRimSurfaceKey key {};
    GroundRimSurface surface {};
};

// Owning world-space geometry, sorted by exact key with duplicates omitted.
// The source compiler supplies its semantic fingerprint; the caller checks
// it before offering an artifact to the runtime cache. No frame pointers,
// material handles, model meshes, or transient tile indices are stored.
struct ProcessedGroundArtifact {
    uint64_t sourceFingerprint = 0;
    std::vector<ProcessedGroundArtifactEntry> entries;

    [[nodiscard]] const GroundRimSurface* find(
        const GroundRimSurfaceKey& key) const noexcept;
};

[[nodiscard]] ProcessedGroundArtifact buildProcessedGroundArtifact(
    std::span<const RenderFrameData::Tile> tiles,
    uint64_t sourceFingerprint);

// Explicit little-endian integers and IEEE float bits, without C++ padding.
// Includes a versioned header and checksum over both metadata and payload.
// Invalid, noncanonical, or unbounded geometry throws with diagnostics.
// Header offsets: magic 0, format 8, compiler 12, fingerprint 16, entries 24,
// reserved 28, canonical width 32/depth 36, payload bytes 40, checksum 48;
// payload begins at 56. Entry profiles are independent exact keys; metadata
// identifies the canonical staging recipe even for explicitly supplied keys.
[[nodiscard]] std::vector<std::byte> serializeProcessedGroundArtifact(
    const ProcessedGroundArtifact& artifact);
[[nodiscard]] ProcessedGroundArtifact parseProcessedGroundArtifact(
    std::span<const std::byte> bytes,
    const std::filesystem::path& diagnosticPath = {});
[[nodiscard]] ProcessedGroundArtifact loadProcessedGroundArtifact(
    const std::filesystem::path& path);

} // namespace sokoban
