#pragma once

#include "engine/LevelLocation.hpp"
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

// Paint session for one screen's ground splat map and its tiled neighbours.
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
    // Maximum number of strokes that can be undone. Each entry copies all
    // participating canvases, so the history is capped rather than unbounded.
    static constexpr std::size_t maxUndoSteps = 32;

    struct OpenRequest {
        struct TiledMap {
            std::string textureName;
            // Position of this map's upper-left corner relative to the
            // active map. All maps use the board dimensions below.
            Vec2 originTiles {};
        };

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
        // The selected splat definition supplies its manifest mask name.
        // Also supports stable-ID overworld maps and legacy screen conventions.
        std::optional<std::string> textureName;
        // Distinct adjacent maps receive brush spillover in their own local
        // coordinates. The active map is always at (0,0).
        std::vector<TiledMap> tiledMaps;
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
    [[nodiscard]] const std::filesystem::path& documentPath() const
    {
        return documentPath_;
    }
    [[nodiscard]] RenderTexture texture() const { return mapTexture(0); }
    [[nodiscard]] const SplatCanvas& canvas() const
    {
        return maps_.empty() ? emptyCanvas_ : maps_.front().canvas;
    }
    // Index 0 is the active map, followed by OpenRequest::tiledMaps order.
    [[nodiscard]] std::size_t mapCount() const { return maps_.size(); }
    [[nodiscard]] RenderTexture mapTexture(std::size_t index) const
    {
        return index < maps_.size() ? maps_[index].texture : noTexture;
    }
    [[nodiscard]] const SplatCanvas& mapCanvas(std::size_t index) const
    {
        return maps_.at(index).canvas;
    }
    [[nodiscard]] const std::string& status() const { return status_; }
    [[nodiscard]] bool dirty() const;

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

    // Grows or crops every canvas to a board that changed size underneath the
    // session, keeping existing paint anchored at each local origin. Explicit
    // tile origins are preserved; reopen if the layout also changed.
    bool followBoardResize(uint32_t boardTilesWide, uint32_t boardTilesHigh);

    // Reverts the most recent completed stroke. Returns false when there is
    // nothing to undo.
    bool undo();
    [[nodiscard]] std::size_t undoDepth() const { return undoHistory_.size(); }

    // Writes the active map and changed tiled neighbours to the source assets
    // tree, and mirrors them into the staged tree when one was given. Returns
    // false and sets `status()` on failure, keeping all paint for a retry.
    bool save();

private:
    struct Map {
        RenderTexture texture = noTexture;
        Vec2 originTiles {};
        SplatCanvas canvas;
        std::filesystem::path sourcePath;
        std::filesystem::path runtimePath;
        bool dirty = false;
    };
    using Snapshot = std::vector<std::vector<uint8_t>>;

    void recordUndoSnapshot();

    bool active_ = false;
    bool strokeActive_ = false;
    bool strokeChanged_ = false;
    Vec2 lastPosition_ {};
    std::optional<LevelLocation> location_;
    std::vector<Map> maps_;
    SplatCanvas emptyCanvas_;
    SplatCanvas::Brush brush_;
    std::vector<Snapshot> undoHistory_;
    Snapshot strokeSnapshot_;
    std::filesystem::path runtimeAssetRoot_;
    std::filesystem::path documentPath_;
    std::string status_;
    uint64_t revision_ = 0;
};

struct CreatedSplatMap {
    bool created = false;
    // Manifest-relative path, ready to become a texture entry.
    std::string relativePath;
    std::string message;
    // Filled by createUniqueBlankSplatMap so the caller can register the new
    // file and assign its manifest name to the selected splat definition.
    std::string textureName;
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

[[nodiscard]] CreatedSplatMap createBlankSplatMapAt(
    std::string relativePath,
    uint32_t boardTilesWide,
    uint32_t boardTilesHigh,
    const std::filesystem::path& sourceAssetRoot,
    const std::filesystem::path& runtimeAssetRoot);

// Allocates a fresh mask name and path, skipping both registered textures and
// files left in either asset tree. Unlike createBlankSplatMapAt, this always
// creates a new blank map instead of reusing an existing painted file.
[[nodiscard]] CreatedSplatMap createUniqueBlankSplatMap(
    std::string textureNamePrefix,
    uint32_t boardTilesWide,
    uint32_t boardTilesHigh,
    const std::filesystem::path& sourceAssetRoot,
    const std::filesystem::path& runtimeAssetRoot,
    const AssetManifest& manifest);

} // namespace sokoban
