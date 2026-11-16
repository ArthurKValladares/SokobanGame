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
// Render the game's tile models and materials under fixed studio lighting
// and save the result as a PNG for the palettes.
namespace tileThumbnails {

// Where a baked thumbnail lives, relative to the assets root.
[[nodiscard]] std::string assetPathFor(TileType tile);

// A virtual camera extent with the subject in its middle cell.
inline constexpr uint32_t studioSize = 3;
inline constexpr uint32_t subjectCell = studioSize / 2;
inline constexpr Vec3 backdropColor { 0.70f, 0.70f, 0.70f };

// A longer lens for the bake.
//
// Camera distance is derived from the size of the area being framed, so a
// camera fitted to a 3x3 studio sits about four times closer than one fitted to a
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

// A fixed camera extent keeps the scale consistent across tiles. The live
// settings supply the same geometry and tile scales as the editor.
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

// A square crop in the render extent, sized from the subject's projection.
// Water crops sit inside the surface so it fills the entire icon.
[[nodiscard]] CropRect cropFor(
    const RenderFrameData& frame, uint32_t width, uint32_t height);

// Tiles worth baking: everything the palette offers that is actually drawn.
[[nodiscard]] bool shouldBake(TileType tile);

} // namespace tileThumbnails
} // namespace sokoban
