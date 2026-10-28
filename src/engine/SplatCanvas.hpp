#pragma once

#include "engine/Math.hpp"
#include "engine/render/ImageData.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace sokoban {

// Editable ground splat map: four 8-bit material weights per texel, mapped to
// RGBA. Weights sum to 255; alpha is a material weight, not transparency.
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
    static constexpr uint32_t layerCount = 4;

    enum class BrushColor : uint8_t {
        Layer0 = 0,
        Layer1 = 1,
        Layer2 = 2,
        Layer3 = 3,
        // Legacy names retained for callers painting the original two layers.
        Black = Layer0,
        White = Layer1,
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

    // A board-sized canvas with the legacy layer-1 `initialWeight`; the rest
    // starts in layer 0. New blank canvases are therefore solid red.
    [[nodiscard]] static SplatCanvas createForBoard(
        uint32_t boardTilesWide,
        uint32_t boardTilesHigh,
        uint8_t initialWeight = 0);

    // Imports RGBA weights, normalizing each texel. Legacy grayscale sources
    // become (255-gray, gray, 0, 0), preserving their original two-layer blend.
    // Fails (returns empty)
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
    [[nodiscard]] uint8_t weightAt(uint32_t x, uint32_t y, uint32_t layer) const;
    // Interleaved RGBA bytes, also used for undo and lossless PNG persistence.
    [[nodiscard]] const std::vector<uint8_t>& weights() const { return weights_; }

    // Returns RGBA weights unchanged for the uploader and PNG writer.
    [[nodiscard]] ImageData toImage() const;

    // A stroke is one press-drag-release. Within it, each texel accumulates
    // the *strongest* brush coverage it has seen, composited once against the
    // state the stroke started from - it does not composite onto its own
    // output.
    //
    // This is what makes hardness and opacity mean what they look like. The
    // pointer is sampled every frame, so holding the button still re-stamps
    // the same spot; compositing repeatedly would drive `1-(1-f)^N` toward
    // full strength and turn any soft brush into a hard disc after a few
    // frames, while opacity merely saturated. Overlapping passes within one
    // stroke are likewise flat, and releasing and pressing again builds up,
    // which is how painting tools generally behave.
    void beginStroke();
    void endStroke();
    [[nodiscard]] bool strokeActive() const { return strokeActive_; }

    // Coverage the brush applies at `distanceTiles` from its centre, in 0..1 -
    // the same value a stamp would write there.
    //
    // Exposed so the editor's preview can be drawn from the real brush profile
    // instead of an approximation of it. An independently written preview
    // would drift from what painting actually does, which is worse than no
    // preview: it would look correct and be wrong.
    [[nodiscard]] static float coverageAt(
        float distanceTiles, const Brush& brush);

    // Stamps the brush centred on a board-tile position. Returns true when any
    // texel actually changed, so callers can skip re-uploading and skip
    // recording an undo step for a no-op stroke. Outside a stroke this behaves
    // as a stroke containing a single stamp.
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
    // Stroke state: the canvas as the stroke found it, and the strongest
    // coverage applied to each texel so far during it.
    bool strokeActive_ = false;
    std::vector<uint8_t> strokeBase_;
    std::vector<uint8_t> strokeCoverage_;
};

} // namespace sokoban
