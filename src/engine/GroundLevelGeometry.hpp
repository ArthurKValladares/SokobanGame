#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/Level.hpp"
#include "engine/render/ProcessedGroundArtifact.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace sokoban {

// Compile authored static ground in the level's final world coordinates. A
// composed overworld must be normalized/composed before it reaches this API;
// fracture patterns cannot be translated from independent screen-local bakes.
[[nodiscard]] ProcessedGroundArtifact compileLevelGroundGeometry(
    const Level& level, const AssetManifest& manifest);

// Native modules already contain their final edges/sides. Only legacy source
// models need a generated .grm package artifact or runtime artifact lookup.
[[nodiscard]] bool levelHasLegacyGroundGeometry(const Level& level, const AssetManifest& manifest);

// Stable semantic identity of the authored ground and its model eligibility.
// Camera, paint, actors, non-ground cells and surrounding empty extent are
// excluded. Compiler revision and the baked rim profile are included.
[[nodiscard]] uint64_t levelGroundGeometryFingerprint(
    const Level& level, const AssetManifest& manifest);

// Source paths are relative to levels/. Returns a path relative to the content
// root, and rejects absolute paths, parent traversal and unsupported sources.
[[nodiscard]] std::filesystem::path groundGeometryArtifactPath(
    const std::filesystem::path& relativeToLevels);

// A main-thread loader retaining only the current level's owning artifact.
// Frames can retain their shared pointer after a level switch or invalidation.
// The revision is a caller-supplied change hint; a stable path/root/revision
// performs neither filesystem work nor semantic scans or allocations. Source
// publication and manifest reloads must invalidate the store to retry files.
class RuntimeGroundGeometryStore {
public:
    [[nodiscard]] std::shared_ptr<const ProcessedGroundArtifact> get(
        const std::filesystem::path& relativeSource,
        const Level& level,
        const AssetManifest& manifest,
        uint64_t sourceRevision,
        const std::filesystem::path& contentRoot);

    void invalidate() noexcept;

private:
    std::filesystem::path sourcePath_;
    std::filesystem::path contentRoot_;
    uint64_t sourceRevision_ = 0;
    std::shared_ptr<const ProcessedGroundArtifact> artifact_;
    bool initialized_ = false;
};

} // namespace sokoban
