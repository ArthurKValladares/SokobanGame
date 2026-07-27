#pragma once

#include "engine/LevelCatalog.hpp"
#include "engine/Math.hpp"
#include "engine/SplatCanvas.hpp"
#include "engine/render/RenderTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sokoban {

class AssetManifest;

// Paint session for one screen's ground splat map.
//
// Owns the editable canvas, the brush, stroke-level undo, and saving. Headless
// and renderer-free: it takes world-tile positions and hands back a canvas, so
// the editor decides when to paint and the renderer decides when to re-upload.
//
// Deliberately separate from LevelEditor: that class is about tile documents,
// and a splat map is a different asset with a different lifetime. They are
// linked only by the document path, which names the screen.
class SplatPainter {
public:
    // Maximum number of strokes that can be undone. Each entry is a full
    // canvas copy - about 340KB for the largest current board - so this is
    // capped rather than unbounded.
    static constexpr std::size_t maxUndoSteps = 32;

    struct OpenRequest {
        // The .scr being edited; its name determines which screen's map to
        // open. A document outside the levels tree is not a screen and cannot
        // be painted.
        std::filesystem::path documentPath;
        uint32_t boardTilesWide = 0;
        uint32_t boardTilesHigh = 0;
        // Where committed assets live (the `assets/` checkout).
        std::filesystem::path sourceAssetRoot;
        // Staged assets beside the executable. Saves are mirrored here so a
        // painted map survives a restart without re-running the content
        // pipeline. Empty to skip.
        std::filesystem::path runtimeAssetRoot;
    };

    // Opens the map for `request`'s screen, creating a blank one if the file
    // is missing. Returns false and sets `status()` when the document is not a
    // screen, or when the manifest declares no map for it - the latter being
    // the "added a screen, forgot the manifest entry" case, which would
    // otherwise silently paint a map nothing reads.
    bool open(const OpenRequest& request, const AssetManifest& manifest);
    void close();

    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] std::optional<LevelLocation> location() const;
    [[nodiscard]] RenderTexture texture() const { return texture_; }
    [[nodiscard]] const SplatCanvas& canvas() const { return canvas_; }
    [[nodiscard]] const std::string& status() const { return status_; }
    [[nodiscard]] bool dirty() const { return dirty_; }

    [[nodiscard]] SplatCanvas::Brush& brush() { return brush_; }
    [[nodiscard]] const SplatCanvas::Brush& brush() const { return brush_; }

    // Bumped whenever the canvas changes, so the renderer can re-upload only
    // when there is something new rather than every frame.
    [[nodiscard]] uint64_t revision() const { return revision_; }

    // A stroke is one press-drag-release, and one undo step. `paintTo` may be
    // called any number of times between begin and end; it interpolates from
    // the previous position so a fast drag stays continuous.
    bool beginStroke(Vec2 worldTile);
    bool paintTo(Vec2 worldTile);
    void endStroke();
    [[nodiscard]] bool strokeInProgress() const { return strokeActive_; }

    // Grows or crops the canvas to a board that changed size underneath the
    // session, keeping existing paint anchored at the origin. Returns true
    // when it actually changed, which also marks the map dirty.
    bool followBoardResize(uint32_t boardTilesWide, uint32_t boardTilesHigh);

    // Reverts the most recent completed stroke. Returns false when there is
    // nothing to undo.
    bool undo();
    [[nodiscard]] std::size_t undoDepth() const { return undoHistory_.size(); }

    // Writes the map back to the source assets tree, and mirrors it into the
    // staged tree when one was given. Returns false and sets `status()` on
    // failure, leaving the canvas untouched so the work is not lost.
    bool save();

private:
    void recordUndoSnapshot();

    bool active_ = false;
    bool dirty_ = false;
    bool strokeActive_ = false;
    bool strokeChanged_ = false;
    Vec2 lastPosition_ {};
    std::optional<LevelLocation> location_;
    RenderTexture texture_ = noTexture;
    SplatCanvas canvas_;
    SplatCanvas::Brush brush_;
    std::vector<std::vector<uint8_t>> undoHistory_;
    std::vector<uint8_t> strokeSnapshot_;
    std::filesystem::path sourcePath_;
    std::filesystem::path runtimePath_;
    std::string status_;
    uint64_t revision_ = 0;
};

// Level/screen named by a `.../level<N>/screen<M>.scr` path, or nothing when
// the path does not follow that convention.
[[nodiscard]] std::optional<LevelLocation> levelLocationFromScreenPath(
    const std::filesystem::path& documentPath);

struct CreatedSplatMap {
    bool created = false;
    // Manifest-relative path, ready to become a texture entry.
    std::string relativePath;
    std::string message;
};

// Writes a blank board-sized splat map for `location` into the source assets
// tree, mirroring into the staged tree when one is given.
//
// Blank means all base material (grass), which is the predictable starting
// point for painting - unlike the generator, which fills new maps with noise.
// Refuses to overwrite an existing file, so this can never destroy painted
// work; that case still reports the path, since the caller only needs the
// entry to exist.
[[nodiscard]] CreatedSplatMap createBlankSplatMap(
    LevelLocation location,
    uint32_t boardTilesWide,
    uint32_t boardTilesHigh,
    const std::filesystem::path& sourceAssetRoot,
    const std::filesystem::path& runtimeAssetRoot);

} // namespace sokoban
