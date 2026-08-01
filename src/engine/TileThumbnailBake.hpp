#pragma once

#include "engine/TileTypes.hpp"
#include "engine/render/RenderTypes.hpp"

#include <filesystem>
#include <string>

namespace sokoban {

class AssetManifest;
class AnimationCatalog;
class PresentationSettings;

// Offline baking of tile palette thumbnails.
//
// Each tile is rendered through the game's own frame path - same shaders,
// lighting, shadows, SSAO and MSAA - and the result is read back and saved as
// a PNG. That is the point: a thumbnail is a screenshot of the real render,
// so it cannot drift from how the board actually looks, and the files can be
// inspected outside the game.
namespace tileThumbnails {

// Where a baked thumbnail lives, relative to the assets root.
[[nodiscard]] std::string assetPathFor(TileType tile);

// Board of neutral ground the subject tile stands on. Odd, so there is a
// single centre cell, and big enough that the subject's shadow and ambient
// occlusion have somewhere to land instead of ending at a void.
inline constexpr uint32_t bedSize = 3;
inline constexpr uint32_t bedCentre = bedSize / 2;

// A light neutral grey, so the subject reads against it whatever colour the
// tile is, and so the bed itself is obviously not part of the tile.
inline constexpr Vec4 bedColor { 0.78f, 0.78f, 0.80f, 1.0f };

// A longer lens for the bake.
//
// Camera distance is derived from the size of the area being framed, so a
// camera fitted to a 3x3 bed sits about four times closer than one fitted to a
// board. At that range the perspective is strong enough to see: a tile's
// vertical edges splayed by nearly 4% of the picture width, which reads as the
// tile leaning or bulging. On a real board the same edges lean well under 1%.
//
// Multiplying the distance pulls the camera back while the fit rescales to
// compensate, so the tile stays the same size on screen and only the
// divergence changes. 4x lands the lean between what a 9-wide and a 13-wide
// board produce - an ordinary level. Going much further would approach an
// orthographic view, which would be *less* like the game, not more.
inline constexpr float cameraDistanceMultiplier = 4.0f;

// The subject tile standing on that bed.
//
// The whole bed drives the camera fit, which is the reason it exists as much
// as the shadows are: fitting to the subject alone framed every tile
// differently - a flat tile filled the view while a tall one was pushed back -
// so no two thumbnails shared a scale. A fixed bed gives every tile the same
// camera.
//
// Takes the game's live `settings` rather than loose values: they carry the
// lighting (the RenderFrameData::Lighting defaults have shadows and ambient
// occlusion off, which would bake flat, contact-less pictures) and the per-tile
// scales, and they are what `tileVisual` needs to build the subject the same
// way the editor does.
[[nodiscard]] RenderFrameData buildBakeFrame(
    TileType tile,
    const AssetManifest& manifest,
    const PresentationSettings& settings,
    const AnimationCatalog* animations = nullptr);

struct CropRect {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Square of a `width` x `height` render extent holding the centre cell of
// `frame`, with a little room around it.
//
// Derived by projecting the cell through the same camera the frame will be
// rendered with, rather than guessing a fraction of the extent, so the crop
// follows the subject when the camera or bed changes.
[[nodiscard]] CropRect cropFor(
    const RenderFrameData& frame, uint32_t width, uint32_t height);

// Tiles worth baking: everything the palette offers that is actually drawn.
[[nodiscard]] bool shouldBake(TileType tile);

} // namespace tileThumbnails
} // namespace sokoban
