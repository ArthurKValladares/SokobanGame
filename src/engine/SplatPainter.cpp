#include "engine/SplatPainter.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/Log.hpp"
#include "engine/render/ImageData.hpp"
#include "engine/render/PngWriter.hpp"

#include <charconv>
#include <exception>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace sokoban {
namespace {

// `<prefix><digits><suffix>` -> the digits, or nothing.
[[nodiscard]] std::optional<int> parseNumbered(
    std::string_view value,
    std::string_view prefix,
    std::string_view suffix = {})
{
    if (!value.starts_with(prefix) ||
        value.size() < prefix.size() + suffix.size()) {
        return std::nullopt;
    }
    if (!suffix.empty() && !value.ends_with(suffix)) {
        return std::nullopt;
    }
    const std::size_t begin = prefix.size();
    const std::size_t end = value.size() - suffix.size();
    if (begin == end) {
        return std::nullopt;
    }
    int number = 0;
    const auto result =
        std::from_chars(value.data() + begin, value.data() + end, number);
    if (result.ec != std::errc {} ||
        result.ptr != value.data() + end ||
        number < 0) {
        return std::nullopt;
    }
    return number;
}

} // namespace

std::optional<LevelLocation> levelLocationFromScreenPath(
    const std::filesystem::path& documentPath)
{
    if (!documentPath.has_parent_path()) {
        return std::nullopt;
    }
    const std::optional<int> screen = parseNumbered(
        documentPath.filename().string(), "screen", ".scr");
    const std::optional<int> level = parseNumbered(
        documentPath.parent_path().filename().string(), "level");
    if (!screen || !level) {
        return std::nullopt;
    }
    return LevelLocation { .level = *level, .screen = *screen };
}

bool SplatPainter::open(
    const OpenRequest& request, const AssetManifest& manifest)
{
    close();

    const std::optional<LevelLocation> location =
        levelLocationFromScreenPath(request.documentPath);
    if (!location) {
        status_ = "Ground painting needs a saved screen: open a "
                  "level<N>/screen<M>.scr document first.";
        return false;
    }
    if (request.boardTilesWide == 0 || request.boardTilesHigh == 0) {
        status_ = "This document has no board to paint.";
        return false;
    }

    // The manifest owns the file name, so the convention lives in one place
    // (and in the generator that writes it) rather than being rebuilt here.
    const std::string textureName =
        groundSplatMapTextureNameForScreen(*location);
    const RenderTexture texture = manifest.findTextureIdByName(textureName);
    if (texture.isNone()) {
        status_ = "No '" + textureName +
            "' texture in the manifest, so this screen shares the fallback "
            "map and has nowhere of its own to paint. Add the entry and "
            "re-run tools/make_ground_textures.py.";
        return false;
    }

    const std::filesystem::path relative =
        manifest.textures()[texture.index()].path;
    sourcePath_ = request.sourceAssetRoot / relative;
    runtimePath_ = request.runtimeAssetRoot.empty()
        ? std::filesystem::path {}
        : request.runtimeAssetRoot / relative;

    std::optional<SplatCanvas> loaded;
    std::error_code error;
    if (std::filesystem::exists(sourcePath_, error)) {
        try {
            loaded = SplatCanvas::fromImage(loadRgbaImage(sourcePath_));
        } catch (const std::exception& failure) {
            log::warning(log::Category::Assets)
                << "Could not read splat map " << sourcePath_.string()
                << ": " << failure.what();
        }
    }

    if (loaded) {
        canvas_ = *loaded;
        // A map whose size disagrees with the board would paint offset from
        // the cursor, because the shader derives coverage from the texture's
        // own dimensions. Resizing a board is the usual cause.
        const Vec2 tiles = canvas_.boardTiles();
        const auto coveredWide = static_cast<uint32_t>(tiles.x);
        const auto coveredHigh = static_cast<uint32_t>(tiles.y);
        if (coveredWide != request.boardTilesWide ||
            coveredHigh != request.boardTilesHigh) {
            status_ = "Splat map covers " + std::to_string(coveredWide) + "x" +
                std::to_string(coveredHigh) + " tiles but the board is " +
                std::to_string(request.boardTilesWide) + "x" +
                std::to_string(request.boardTilesHigh) +
                "; re-run tools/make_ground_textures.py. Painting anyway.";
        } else {
            status_ = "Painting " + textureName + ".";
        }
    } else {
        canvas_ = SplatCanvas::createForBoard(
            request.boardTilesWide, request.boardTilesHigh);
        status_ = "Started a blank " + textureName + ".";
    }

    active_ = true;
    dirty_ = false;
    location_ = location;
    texture_ = texture;
    ++revision_;
    return true;
}

void SplatPainter::close()
{
    active_ = false;
    dirty_ = false;
    strokeActive_ = false;
    strokeChanged_ = false;
    location_.reset();
    texture_ = noTexture;
    canvas_ = SplatCanvas {};
    undoHistory_.clear();
    strokeSnapshot_.clear();
    sourcePath_.clear();
    runtimePath_.clear();
    ++revision_;
}

std::optional<LevelLocation> SplatPainter::location() const
{
    return location_;
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
    strokeSnapshot_ = canvas_.snapshot();
    strokeActive_ = true;
    strokeChanged_ = false;
    lastPosition_ = worldTile;

    strokeChanged_ = canvas_.stamp(worldTile, brush_);
    if (strokeChanged_) {
        dirty_ = true;
        ++revision_;
    }
    return strokeChanged_;
}

bool SplatPainter::paintTo(Vec2 worldTile)
{
    if (!active_ || !strokeActive_) {
        return false;
    }
    const bool changed = canvas_.stampLine(lastPosition_, worldTile, brush_);
    lastPosition_ = worldTile;
    if (changed) {
        strokeChanged_ = true;
        dirty_ = true;
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

bool SplatPainter::undo()
{
    if (!active_ || undoHistory_.empty()) {
        return false;
    }
    // Undoing mid-stroke would restore under the brush; finish first.
    if (strokeActive_) {
        endStroke();
    }
    const bool restored = canvas_.restore(undoHistory_.back());
    undoHistory_.pop_back();
    if (!restored) {
        return false;
    }
    dirty_ = true;
    ++revision_;
    status_ = "Undid a paint stroke.";
    return true;
}

bool SplatPainter::save()
{
    if (!active_) {
        return false;
    }
    if (canvas_.empty()) {
        status_ = "Nothing to save.";
        return false;
    }

    try {
        std::error_code error;
        std::filesystem::create_directories(sourcePath_.parent_path(), error);
        writeGrayscalePng(
            sourcePath_, canvas_.width(), canvas_.height(), canvas_.weights());
    } catch (const std::exception& failure) {
        status_ = "Could not save splat map: " + std::string(failure.what());
        log::error(log::Category::Assets)
            << "Splat map save failed for " << sourcePath_.string()
            << ": " << failure.what();
        return false;
    }

    // Mirroring into the staged tree is a convenience, not the save itself:
    // the committed copy is already written, so a failure here is a warning
    // rather than a lost edit.
    if (!runtimePath_.empty()) {
        try {
            std::error_code error;
            std::filesystem::create_directories(
                runtimePath_.parent_path(), error);
            writeGrayscalePng(
                runtimePath_,
                canvas_.width(),
                canvas_.height(),
                canvas_.weights());
        } catch (const std::exception& failure) {
            log::warning(log::Category::Assets)
                << "Saved the splat map but could not update the staged copy "
                << runtimePath_.string() << ": " << failure.what();
        }
    }

    dirty_ = false;
    status_ = "Saved " + sourcePath_.filename().string() + ".";
    return true;
}

} // namespace sokoban
