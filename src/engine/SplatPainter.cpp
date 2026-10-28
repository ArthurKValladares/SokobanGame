#include "engine/SplatPainter.hpp"
#include "engine/LevelCatalog.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/ContentPipeline.hpp"
#include "engine/Log.hpp"
#include "engine/render/ImageData.hpp"
#include "engine/render/CompressedTextureArtifact.hpp"
#include "engine/render/PngWriter.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <limits>
#include <system_error>
#include <utility>

namespace sokoban {

CreatedSplatMap createBlankSplatMap(
    LevelLocation location,
    uint32_t boardTilesWide,
    uint32_t boardTilesHigh,
    const std::filesystem::path& sourceAssetRoot,
    const std::filesystem::path& runtimeAssetRoot)
{
    return createBlankSplatMapAt(
        groundSplatMapAssetPathForScreen(location),
        boardTilesWide,
        boardTilesHigh,
        sourceAssetRoot,
        runtimeAssetRoot);
}

CreatedSplatMap createBlankSplatMapAt(
    std::string relativePath,
    uint32_t boardTilesWide,
    uint32_t boardTilesHigh,
    const std::filesystem::path& sourceAssetRoot,
    const std::filesystem::path& runtimeAssetRoot)
{
    CreatedSplatMap result;
    result.relativePath = std::move(relativePath);
    if (boardTilesWide == 0 || boardTilesHigh == 0) {
        result.message = "That document has no board to make a map for.";
        return result;
    }

    const std::filesystem::path sourcePath =
        sourceAssetRoot / result.relativePath;
    std::error_code error;
    const bool sourceExists = std::filesystem::exists(sourcePath, error);
    if (error) {
        result.message = "Could not inspect the splat map: " + error.message();
        return result;
    }

    if (!sourceExists) {
        const SplatCanvas blank =
            SplatCanvas::createForBoard(boardTilesWide, boardTilesHigh);
        try {
            std::filesystem::create_directories(sourcePath.parent_path(), error);
            writeRgbaPng(
                sourcePath, blank.width(), blank.height(), blank.weights());
        } catch (const std::exception& failure) {
            result.message =
                "Could not create the splat map: " + std::string(failure.what());
            log::error(log::Category::Assets)
                << "Splat map creation failed for " << sourcePath.string()
                << ": " << failure.what();
            return result;
        }
    }

    // The running game reads the staged tree, so without this copy the new map
    // would not load until the content pipeline ran again.
    if (!runtimeAssetRoot.empty()) {
        const std::filesystem::path runtimePath =
            runtimeAssetRoot / result.relativePath;
        try {
            std::filesystem::create_directories(
                runtimePath.parent_path(), error);
            if (sourcePath.lexically_normal() != runtimePath.lexically_normal()) {
                std::filesystem::copy_file(
                    sourcePath,
                    runtimePath,
                    std::filesystem::copy_options::overwrite_existing);
            }
            invalidateCompressedTextureArtifactsForSource(
                runtimeAssetRoot, result.relativePath);
            (void)refreshContentPackageIndex(runtimeAssetRoot);
        } catch (const std::exception& failure) {
            result.message = "Created the splat map but could not stage it: " +
                std::string(failure.what());
            log::warning(log::Category::Assets)
                << "Could not stage new splat map " << runtimePath.string()
                << ": " << failure.what();
            return result;
        }
    }

    result.created = true;
    result.message = sourceExists
        ? "Using the existing " + sourcePath.filename().string() + "."
        : "Created a blank " + sourcePath.filename().string() +
            " (" + std::to_string(boardTilesWide) + "x" +
            std::to_string(boardTilesHigh) + " tiles).";
    return result;
}

CreatedSplatMap createUniqueBlankSplatMap(
    std::string textureNamePrefix,
    uint32_t boardTilesWide,
    uint32_t boardTilesHigh,
    const std::filesystem::path& sourceAssetRoot,
    const std::filesystem::path& runtimeAssetRoot,
    const AssetManifest& manifest)
{
    CreatedSplatMap result;
    if (boardTilesWide == 0 || boardTilesHigh == 0) {
        result.message = "That document has no board to make a map for.";
        return result;
    }
    if (textureNamePrefix.empty()) {
        result.message = "A new blend mask needs a valid texture name prefix.";
        return result;
    }
    // Keep the entire generated filename within portable component limits.
    // Some filesystems report an oversized path as missing instead of setting
    // an inspection error, so check before attempting any file operation.
    constexpr std::size_t reservedFilenameLength =
        sizeof("_BlendMask_") - 1 +
        std::numeric_limits<uint64_t>::digits10 + 1 + sizeof(".png") - 1;
    if (textureNamePrefix.size() > 255 - reservedFilenameLength) {
        result.message =
            "The selected mask name is too long to create a new blend mask file.";
        return result;
    }
    // Manifest names are unrestricted strings, but mask files must stay in
    // custom/textures even when a map uses spaces or path-like punctuation.
    for (char& character : textureNamePrefix) {
        const bool safe = (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '_';
        if (!safe) {
            character = '_';
        }
    }

    const auto normalizedPath = [](std::string path) {
        std::replace(path.begin(), path.end(), '\\', '/');
        return std::filesystem::path(path).lexically_normal().generic_string();
    };

    for (uint64_t suffix = 1; suffix < std::numeric_limits<uint64_t>::max();
         ++suffix) {
        const std::string textureName = textureNamePrefix + "_BlendMask_" +
            std::to_string(suffix);
        const std::string relativePath =
            "custom/textures/" + textureName + ".png";
        bool occupied = !manifest.findTextureIdByName(textureName).isNone();
        for (const AssetManifest::Texture& texture : manifest.textures()) {
            occupied |= normalizedPath(texture.path) == relativePath;
        }
        if (occupied) {
            continue;
        }

        // An unregistered file can still contain someone's painted work.
        // Inspect both trees before creating anything, so a runtime-only mask
        // is never overwritten while publishing a newly allocated source map.
        for (const std::filesystem::path& root :
             { sourceAssetRoot, runtimeAssetRoot }) {
            if (root.empty()) {
                continue;
            }
            std::error_code error;
            occupied |= std::filesystem::exists(root / relativePath, error);
            if (error) {
                result.message = "Could not inspect the blend mask: " +
                    error.message();
                return result;
            }
        }
        if (occupied) {
            continue;
        }

        result = createBlankSplatMapAt(
            relativePath, boardTilesWide, boardTilesHigh,
            sourceAssetRoot, runtimeAssetRoot);
        result.textureName = textureName;
        return result;
    }

    result.message = "Could not find an unused blend mask name.";
    return result;
}

bool SplatPainter::open(
    const OpenRequest& request, const AssetManifest& manifest)
{
    close();

    const std::optional<LevelLocation> location =
        levelLocationFromScreenPath(request.documentPath);
    if (!location && !request.textureName) {
        status_ = "Ground painting needs a saved screen: open a "
                  "puzzle or composed-overworld screen document first.";
        return false;
    }
    if (request.boardTilesWide == 0 || request.boardTilesHigh == 0) {
        status_ = "This document has no board to paint.";
        return false;
    }

    // The manifest owns the file name, so the convention lives in one place
    // (and in the generator that writes it) rather than being rebuilt here.
    const std::string textureName = request.textureName
        ? *request.textureName
        : groundSplatMapTextureNameForScreen(*location);
    std::vector<OpenRequest::TiledMap> targets {
        { .textureName = textureName, .originTiles = {} },
    };
    targets.insert(targets.end(), request.tiledMaps.begin(), request.tiledMaps.end());
    std::vector<Map> maps;
    maps.reserve(targets.size());

    // Resolve every target before publishing a session. Two names can refer
    // to one image, which would otherwise make saving silently lose paint.
    for (const OpenRequest::TiledMap& target : targets) {
        if (!std::isfinite(target.originTiles.x) ||
            !std::isfinite(target.originTiles.y)) {
            status_ = "Invalid tiled origin for '" + target.textureName + "'.";
            return false;
        }
        const RenderTexture texture =
            manifest.findTextureIdByName(target.textureName);
        if (texture.isNone()) {
            status_ = "No '" + target.textureName +
                "' texture in the manifest, so this map has nowhere of its "
                "own to paint. Add the entry and re-run "
                "tools/make_ground_textures.py.";
            return false;
        }
        std::string relativeString = manifest.textures()[texture.index()].path;
        std::replace(relativeString.begin(), relativeString.end(), '\\', '/');
        const std::filesystem::path relative =
            std::filesystem::path(relativeString).lexically_normal();
        if (relative.empty() || relative.is_absolute() ||
            *relative.begin() == "..") {
            status_ = "Invalid splat map asset path for '" +
                target.textureName + "'.";
            return false;
        }
        Map map {
            .texture = texture,
            .originTiles = target.originTiles,
            .canvas = {},
            .sourcePath = (request.sourceAssetRoot / relative).lexically_normal(),
            .runtimePath = request.runtimeAssetRoot.empty()
                ? std::filesystem::path {}
                : (request.runtimeAssetRoot / relative).lexically_normal(),
        };
        for (const Map& existing : maps) {
            if (existing.texture == map.texture ||
                existing.sourcePath == map.sourcePath) {
                status_ = "Duplicate tiled splat map '" + target.textureName + "'.";
                return false;
            }
            const Vec2 delta = map.originTiles - existing.originTiles;
            if (std::abs(delta.x) < static_cast<float>(request.boardTilesWide) &&
                std::abs(delta.y) < static_cast<float>(request.boardTilesHigh)) {
                status_ = "Tiled splat map '" + target.textureName +
                    "' overlaps another map's board.";
                return false;
            }
        }
        maps.push_back(std::move(map));
    }

    bool anyResized = false;
    for (std::size_t index = 0; index < maps.size(); ++index) {
        Map& map = maps[index];
        std::optional<SplatCanvas> loaded;
        std::error_code error;
        if (std::filesystem::exists(map.sourcePath, error)) {
            try {
                loaded = SplatCanvas::fromImage(loadRgbaImage(map.sourcePath));
            } catch (const std::exception& failure) {
                log::warning(log::Category::Assets)
                    << "Could not read splat map " << map.sourcePath.string()
                    << ": " << failure.what();
            }
        }
        if (error) {
            status_ = "Could not inspect splat map '" +
                targets[index].textureName + "': " + error.message();
            return false;
        }

        if (loaded) {
            map.canvas = std::move(*loaded);
            const Vec2 tiles = map.canvas.boardTiles();
            map.dirty = map.canvas.resizeToBoard(
                request.boardTilesWide, request.boardTilesHigh);
            anyResized |= map.dirty;
            if (index == 0) {
                status_ = map.dirty
                    ? "Resized splat map from " +
                        std::to_string(static_cast<uint32_t>(tiles.x)) + "x" +
                        std::to_string(static_cast<uint32_t>(tiles.y)) + " to " +
                        std::to_string(request.boardTilesWide) + "x" +
                        std::to_string(request.boardTilesHigh) +
                        " tiles to match the board; save to keep it."
                    : "Painting " + textureName + ".";
            }
        } else {
            map.canvas = SplatCanvas::createForBoard(
                request.boardTilesWide, request.boardTilesHigh);
            if (index == 0) {
                status_ = "Started a blank " + textureName + ".";
            }
        }
    }
    if (maps.size() > 1) {
        status_ += " Brush spillover reaches " + std::to_string(maps.size()) +
            " tiled maps.";
        if (anyResized) {
            status_ += " Resized maps need saving.";
        }
    }

    maps_ = std::move(maps);
    runtimeAssetRoot_ = request.runtimeAssetRoot;
    documentPath_ = request.documentPath;
    location_ = location;
    active_ = true;
    ++revision_;
    return true;
}

void SplatPainter::close()
{
    active_ = false;
    strokeActive_ = false;
    strokeChanged_ = false;
    location_.reset();
    maps_.clear();
    undoHistory_.clear();
    strokeSnapshot_.clear();
    runtimeAssetRoot_.clear();
    documentPath_.clear();
    ++revision_;
}

std::optional<LevelLocation> SplatPainter::location() const
{
    return location_;
}

bool SplatPainter::dirty() const
{
    return std::any_of(maps_.begin(), maps_.end(),
        [](const Map& map) { return map.dirty; });
}

bool SplatPainter::beginStroke(Vec2 worldTile)
{
    if (!active_) {
        return false;
    }
    if (strokeActive_) {
        // A press without a matching release (focus loss mid-drag) must not
        // lose the earlier stroke's undo entry.
        endStroke();
    }

    // Captured before the first stamp, so undo restores the state the stroke
    // started from however many samples the drag produces.
    // The canvas needs to know too: it composites the whole stroke against
    // this state rather than against its own output, which is what keeps
    // hardness and opacity from creeping while the button is held.
    strokeSnapshot_.clear();
    strokeSnapshot_.reserve(maps_.size());
    for (Map& map : maps_) {
        strokeSnapshot_.push_back(map.canvas.snapshot());
        map.canvas.beginStroke();
    }
    strokeActive_ = true;
    strokeChanged_ = false;
    lastPosition_ = worldTile;

    for (Map& map : maps_) {
        const bool changed = map.canvas.stamp(worldTile - map.originTiles, brush_);
        map.dirty |= changed;
        strokeChanged_ |= changed;
    }
    if (strokeChanged_) {
        ++revision_;
    }
    return strokeChanged_;
}

bool SplatPainter::paintTo(Vec2 worldTile)
{
    if (!active_ || !strokeActive_) {
        return false;
    }
    bool changed = false;
    for (Map& map : maps_) {
        const bool mapChanged = map.canvas.stampLine(
            lastPosition_ - map.originTiles, worldTile - map.originTiles, brush_);
        map.dirty |= mapChanged;
        changed |= mapChanged;
    }
    lastPosition_ = worldTile;
    if (changed) {
        strokeChanged_ = true;
        ++revision_;
    }
    return changed;
}

void SplatPainter::endStroke()
{
    if (!strokeActive_) {
        return;
    }
    strokeActive_ = false;
    for (Map& map : maps_) {
        map.canvas.endStroke();
    }
    // A stroke that changed nothing (painting white on white, or a click off
    // the board) leaves no undo step; otherwise Ctrl+Z would appear to do
    // nothing while silently consuming history.
    if (strokeChanged_) {
        recordUndoSnapshot();
    }
    strokeChanged_ = false;
    strokeSnapshot_.clear();
}

void SplatPainter::recordUndoSnapshot()
{
    undoHistory_.push_back(std::move(strokeSnapshot_));
    if (undoHistory_.size() > maxUndoSteps) {
        undoHistory_.erase(undoHistory_.begin());
    }
}

bool SplatPainter::followBoardResize(
    uint32_t boardTilesWide, uint32_t boardTilesHigh)
{
    if (!active_ || boardTilesWide == 0 || boardTilesHigh == 0) {
        return false;
    }
    bool changed = false;
    for (Map& map : maps_) {
        const bool resized = map.canvas.resizeToBoard(boardTilesWide, boardTilesHigh);
        map.dirty |= resized;
        changed |= resized;
    }
    if (!changed) {
        return false;
    }

    // Undo entries are whole-canvas copies sized for the old board, so they
    // can no longer be restored. Dropping them is better than leaving entries
    // that would silently fail, or resizing history to a state that never
    // existed.
    undoHistory_.clear();
    strokeSnapshot_.clear();
    strokeActive_ = false;
    strokeChanged_ = false;
    ++revision_;
    status_ = "Board resized to " + std::to_string(boardTilesWide) + "x" +
        std::to_string(boardTilesHigh) +
        "; splat map followed it. Paint history was cleared.";
    return true;
}

bool SplatPainter::undo()
{
    if (!active_) {
        return false;
    }
    // Undoing mid-stroke would restore under the brush; finish first.
    if (strokeActive_) {
        endStroke();
    }
    if (undoHistory_.empty()) {
        return false;
    }
    const Snapshot& snapshot = undoHistory_.back();
    for (std::size_t index = 0; index < maps_.size(); ++index) {
        Map& map = maps_[index];
        if (map.canvas.weights() != snapshot[index]) {
            (void)map.canvas.restore(snapshot[index]);
            map.dirty = true;
        }
    }
    undoHistory_.pop_back();
    ++revision_;
    status_ = "Undid a paint stroke.";
    return true;
}

bool SplatPainter::save()
{
    if (!active_) {
        return false;
    }
    if (canvas().empty()) {
        status_ = "Nothing to save.";
        return false;
    }

    // Keep the active-map save behavior even for a clean/new canvas. Neighbour
    // files are written only when this session actually changed them.
    std::vector<Map*> targets { &maps_.front() };
    for (std::size_t index = 1; index < maps_.size(); ++index) {
        if (maps_[index].dirty) {
            targets.push_back(&maps_[index]);
        }
    }
    for (Map* map : targets) {
        try {
            std::filesystem::create_directories(map->sourcePath.parent_path());
            writeRgbaPng(map->sourcePath, map->canvas.width(),
                map->canvas.height(), map->canvas.weights());
        } catch (const std::exception& failure) {
            map->dirty = true;
            status_ = "Could not save splat map '" +
                map->sourcePath.filename().string() + "': " + failure.what();
            log::error(log::Category::Assets) << status_;
            return false;
        }
    }

    try {
        for (Map* map : targets) {
            if (map->runtimePath.empty()) {
                continue;
            }
            std::filesystem::create_directories(map->runtimePath.parent_path());
            writeRgbaPng(map->runtimePath, map->canvas.width(),
                map->canvas.height(), map->canvas.weights());
            invalidateCompressedTextureArtifactsForSource(runtimeAssetRoot_,
                map->runtimePath.lexically_relative(runtimeAssetRoot_));
        }
        if (!runtimeAssetRoot_.empty()) {
            (void)refreshContentPackageIndex(runtimeAssetRoot_);
        }
    } catch (const std::exception& failure) {
        // Publication can fail after some maps were written. Retain every
        // target for a full retry, including neighbour-only paint.
        for (Map* map : targets) {
            map->dirty = true;
        }
        status_ = "Saved the source splat maps, but runtime publication failed: " +
            std::string(failure.what());
        log::error(log::Category::Assets) << status_;
        return false;
    }

    for (Map* map : targets) {
        map->dirty = false;
    }
    status_ = targets.size() == 1
        ? "Saved " + maps_.front().sourcePath.filename().string() + "."
        : "Saved " + std::to_string(targets.size()) + " tiled splat maps.";
    return true;
}

} // namespace sokoban
