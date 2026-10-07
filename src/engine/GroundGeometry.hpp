#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/render/RenderTypes.hpp"

#include <span>

namespace sokoban {

// Derive exposed sides from the actual render list, after screen visibility,
// layer visibility and editor previews have been resolved. Gameplay cells that
// were not emitted cannot conceal a side at a rendered screen boundary.
// Only fixed, opaque, untransformed unit ground on the same layer participates;
// tops, bottoms, logical cells, and the authored level are left intact.
// Neighbour model identities accompany hidden sides so the renderer can defer
// removal until that neighbouring source mesh is loaded and validated.
//
// Live builders provide their frame arena for the O(n) adjacency table. Arena
// exhaustion preserves full meshes; offline callers use owning scratch space.
void processGroundGeometry(
    std::span<RenderFrameData::Tile> tiles,
    const AssetManifest& manifest,
    FrameArena* arena = nullptr);

} // namespace sokoban
