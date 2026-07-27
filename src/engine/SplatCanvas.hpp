#pragma once

#include "engine/Math.hpp"
#include "engine/render/ImageData.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace sokoban {

// Editable ground splat weight map: one 8-bit weight per texel, covering one
// screen's board exactly. 0 is pure base material (grass), 255 is pure detail
// (rock); the shader reads this as the blend factor.
//
// Deliberately headless and renderer-free. All the brush maths lives here so
// it can be unit-tested without a device, a window, or a running editor - the
// editor layer only decides *when* to call it.
class SplatCanvas {
public:
    // Texels per board tile. Must match GROUND_SPLAT_TEXELS_PER_TILE in
    // shaders/ground_splat.frag.glsl and SPLAT_TEXELS_PER_TILE in
    // tools/make_ground_textures.py: the shader recovers the board size by
    // dividing the map's dimensions by this, so a mismatch silently offsets
    // and rescales every painted stroke.
    static constexpr uint32_t texelsPerTile = 32;

    enum class BrushColor {
        // Paints toward the detail layer (rock).
        White,
        // Paints toward the base layer (grass).
        Black,
    };

    struct Brush {
        // Radius in board tiles, so a brush keeps its physical size no matter
        // how far the camera is zoomed or how large the board is.
        float radiusTiles = 1.0f;
        // 0 = fully feathered from the centre out, 1 = hard edge.
        float hardness = 0.5f;
        // Maximum weight change a single stroke can apply at full strength.
        float opacity = 1.0f;
        BrushColor color = BrushColor::White;
    };

    // A board-sized canvas filled with `initialWeight` everywhere.
    [[nodiscard]] static SplatCanvas createForBoard(
        uint32_t boardTilesWide,
        uint32_t boardTilesHigh,
        uint8_t initialWeight = 0);

    // Reads the red channel of an already-decoded map. Fails (returns empty)
    // on a zero-sized image; any other size is accepted, since a map authored
    // at a different density is still paintable - it just covers a different
    // number of tiles.
    [[nodiscard]] static std::optional<SplatCanvas> fromImage(
        const ImageData& image);

    [[nodiscard]] uint32_t width() const { return width_; }
    [[nodiscard]] uint32_t height() const { return height_; }
    [[nodiscard]] bool empty() const { return width_ == 0 || height_ == 0; }
    // Board coverage implied by the canvas size, matching what the shader
    // derives from the texture's dimensions.
    [[nodiscard]] Vec2 boardTiles() const;

    [[nodiscard]] uint8_t weightAt(uint32_t x, uint32_t y) const;
    [[nodiscard]] const std::vector<uint8_t>& weights() const { return weights_; }

    // Expands the single weight channel into the RGBA the uploader and PNG
    // writer both expect. Weight rides in red; green and blue mirror it so the
    // map stays readable as greyscale in any image viewer.
    [[nodiscard]] ImageData toImage() const;

    // Stamps the brush centred on a board-tile position. Returns true when any
    // texel actually changed, so callers can skip re-uploading and skip
    // recording an undo step for a no-op stroke.
    bool stamp(Vec2 centerTiles, const Brush& brush);

    // Stamps along a segment, spacing stamps closely enough that a fast mouse
    // drag paints a continuous line rather than a dotted one.
    bool stampLine(Vec2 fromTiles, Vec2 toTiles, const Brush& brush);

    // Rescales the canvas to cover a different board, keeping what is already
    // painted anchored at the origin: growing fills the new strip with
    // `fill`, shrinking crops. Returns false when the size is unchanged or
    // degenerate.
    //
    // A resized board otherwise leaves the map covering the old extent, and
    // because the shader derives coverage from the texture's dimensions, the
    // extra tiles just repeat the clamped edge.
    bool resizeToBoard(
        uint32_t boardTilesWide,
        uint32_t boardTilesHigh,
        uint8_t fill = 0);

    // Whole-canvas snapshot/restore, used for stroke-level undo.
    [[nodiscard]] std::vector<uint8_t> snapshot() const { return weights_; }
    bool restore(const std::vector<uint8_t>& snapshot);

private:
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    std::vector<uint8_t> weights_;
};

} // namespace sokoban
