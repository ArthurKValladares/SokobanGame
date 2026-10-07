#include "engine/GroundLevelGeometry.hpp"

#include "engine/GroundGeometry.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/render/GroundMeshGeometry.hpp"

#include <bit>
#include <exception>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace sokoban {
namespace {

// Explicit byte order and lengths avoid native padding, platform std::hash
// behavior, and ambiguities between sequences of model strings/ground cells.
class GroundFingerprint {
public:
    void integer(uint64_t value) noexcept
    {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            byte(static_cast<uint8_t>(value >> shift));
        }
    }

    void scalar(float value) noexcept { integer(std::bit_cast<uint32_t>(value)); }

    void string(std::string_view value) noexcept
    {
        integer(value.size());
        for (const unsigned char character : value) byte(character);
    }

    [[nodiscard]] uint64_t value() const noexcept { return value_; }

private:
    void byte(uint8_t value) noexcept
    {
        value_ ^= value;
        value_ *= 1099511628211ULL;
    }

    uint64_t value_ = 14695981039346656037ULL;
};

template <typename Function>
void forEachAuthoredGround(const Level& level, Function&& function)
{
    // The order is canonical and independent of actor/material iteration.
    for (uint32_t z = 0; z < level.depth(); ++z) {
        for (uint32_t y = 0; y < level.height(); ++y) {
            for (uint32_t x = 0; x < level.width(); ++x) {
                const TileType type = level.authoredTileAt(x, y, z);
                if (tileTypeIsGround(type)) {
                    function(type, GridPosition3 {
                        static_cast<int>(x), static_cast<int>(y), static_cast<int>(z),
                    });
                }
            }
        }
    }
}

void fingerprintModel(GroundFingerprint& hash, RenderModel model,
    const AssetManifest& manifest)
{
    hash.integer(model.value);
    if (model.isCube()) return;
    const AssetManifest::Model& source = manifest.model(model);
    hash.string(source.name);
    hash.string(source.path);
    hash.integer(static_cast<uint8_t>(source.geometry));
    hash.integer(source.preserveSourceScale);
    hash.integer(source.rotateHalfTurn);
    hash.integer(!source.attachments.empty());
    hash.integer(source.hasScrollingMaterial());
    hash.integer(isProcessableGroundRockModel(source));
}

} // namespace

uint64_t levelGroundGeometryFingerprint(const Level& level, const AssetManifest& manifest)
{
    GroundFingerprint hash;
    hash.string("sokoban-authored-ground");
    hash.integer(processedGroundArtifactFormatVersion);
    hash.integer(processedGroundArtifactCompilerRevision);
    hash.scalar(processedGroundArtifactRimWidth);
    hash.scalar(processedGroundArtifactRimDepth);
    hash.scalar(GroundRimProfile {}.bodyBand);
    hash.integer(GroundRimSurface::capacity);

    PresentationSettings settings;
    settings.applyTileScales(manifest);
    uint64_t count = 0;
    forEachAuthoredGround(level, [&](TileType type, GridPosition3 cell) {
        ++count;
        hash.integer(static_cast<uint8_t>(type));
        hash.integer(static_cast<uint32_t>(cell.x));
        hash.integer(static_cast<uint32_t>(cell.y));
        hash.integer(static_cast<uint32_t>(cell.z));
        hash.scalar(manifest.tileScale(type));
        const auto tile = tileVisual(type, cell, manifest, settings);
        fingerprintModel(hash, tile.model, manifest);
        hash.scalar(tile.position.x);
        hash.scalar(tile.position.y);
        hash.scalar(tile.size.x);
        hash.scalar(tile.size.y);
        hash.scalar(tile.baseElevation);
        hash.scalar(tile.height);
        hash.scalar(tile.color.w);
        hash.integer(tile.groundTop);
        hash.integer(tile.modelRotationQuarterTurns);
        hash.scalar(tile.modelRotationOffsetRadians);
    });
    hash.integer(count);
    return hash.value();
}

ProcessedGroundArtifact compileLevelGroundGeometry(const Level& level,
    const AssetManifest& manifest)
{
    PresentationSettings settings;
    settings.applyTileScales(manifest);
    std::vector<RenderFrameData::Tile> tiles;
    forEachAuthoredGround(level, [&](TileType type, GridPosition3 cell) {
        if (tiles.size() == RenderFrameData::tileCapacity) {
            throw std::runtime_error("authored ground exceeds the render tile capacity");
        }
        tiles.push_back(tileVisual(type, cell, manifest, settings));
    });
    processGroundGeometry(tiles, manifest);
    for (auto& tile : tiles) {
        if (!tile.groundGeometryEligible) continue;
        tile.groundRimWidth = processedGroundArtifactRimWidth;
        tile.groundRimDepth = processedGroundArtifactRimDepth;
    }
    return buildProcessedGroundArtifact(tiles, levelGroundGeometryFingerprint(level, manifest));
}

std::filesystem::path groundGeometryArtifactPath(
    const std::filesystem::path& relativeToLevels)
{
    if (relativeToLevels.empty() || relativeToLevels.is_absolute() ||
        relativeToLevels.has_root_name() || relativeToLevels.has_root_directory()) {
        throw std::invalid_argument("ground geometry source must be relative to levels");
    }
    for (const auto& component : relativeToLevels) {
        if (component == "..") {
            throw std::invalid_argument("ground geometry source cannot traverse parents");
        }
    }
    const auto source = relativeToLevels.lexically_normal();
    const std::filesystem::path outputRoot = std::filesystem::path("geometry") / "ground";
    if (source == "overworld.scr" || source == std::filesystem::path("overworld") / "layout.json") {
        return outputRoot / "overworld.grm";
    }
    if (source.extension() != ".scr") {
        throw std::invalid_argument("unsupported ground geometry source path");
    }
    auto result = outputRoot / source;
    result.replace_extension(".grm");
    return result;
}

std::shared_ptr<const ProcessedGroundArtifact> RuntimeGroundGeometryStore::get(
    const std::filesystem::path& relativeSource,
    const Level& level,
    const AssetManifest& manifest,
    uint64_t sourceRevision,
    const std::filesystem::path& contentRoot)
{
    const bool sameSource = initialized_ && sourcePath_ == relativeSource &&
        contentRoot_ == contentRoot;
    if (sameSource && sourceRevision_ == sourceRevision) return artifact_;

    const uint64_t fingerprint = levelGroundGeometryFingerprint(level, manifest);
    if (sameSource && artifact_ && artifact_->sourceFingerprint == fingerprint) {
        sourceRevision_ = sourceRevision;
        return artifact_;
    }

    // Clear the previous artifact before attempting another load. A missing,
    // stale or invalid candidate never publishes partial/previous geometry.
    invalidate();
    sourcePath_ = relativeSource;
    contentRoot_ = contentRoot;
    sourceRevision_ = sourceRevision;
    initialized_ = true;
    try {
        auto candidate = loadProcessedGroundArtifact(contentRoot /
            groundGeometryArtifactPath(relativeSource));
        if (candidate.sourceFingerprint == fingerprint) {
            artifact_ = std::make_shared<const ProcessedGroundArtifact>(std::move(candidate));
        }
    } catch (const std::exception&) {
        // Runtime generation is the normal fallback for unavailable content.
        // Cache this outcome until a new source revision or explicit invalidate.
    }
    return artifact_;
}

void RuntimeGroundGeometryStore::invalidate() noexcept
{
    sourcePath_.clear();
    contentRoot_.clear();
    sourceRevision_ = 0;
    artifact_.reset();
    initialized_ = false;
}

} // namespace sokoban
