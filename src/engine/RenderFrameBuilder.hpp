#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/GameplayPresentation.hpp"
#include "engine/Level.hpp"
#include "engine/LevelEditor.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/render/RenderTypes.hpp"

#include <optional>

namespace sokoban {

class AnimationCatalog;

// How a tile type looks: footprint, height, colour, model, rotation and scale.
//
// The single definition of that, shared by the editor and the thumbnail bake.
// It exists because the bake originally re-derived these rules and quietly got
// them wrong - conveyors lost their rotation and their height, so all four
// baked identically and squashed. Anything that needs to draw a lone tile
// should come through here rather than restating the branches.
//
// Callers own placement: `baseElevation` is set from the cell, and preview or
// pick-only flags are theirs to apply afterwards.
[[nodiscard]] RenderFrameData::Tile tileVisual(
    TileType tile,
    GridPosition3 cell,
    const AssetManifest& manifest,
    const PresentationSettings& settings);

class RenderFrameBuilder {
public:
    struct GameplayInput {
        const AssetManifest& manifest;
        const Level& level;
        const GameState& state;
        bool moving = false;
        const GameplaySession::Action& activeAction;
        const GameplayPresentation& presentation;
        const PresentationSettings& settings;
        const AnimationCatalog* animations = nullptr;
        float conveyorBeltScrollOffset = 0.0f;
        std::optional<float> cameraPitchDegrees;
        // Selects this screen's ground splat map. Unset falls back to the
        // shared map, which is also what draft playback outside a campaign
        // gets.
        std::optional<LevelLocation> levelLocation;
    };

    struct EditorInput {
        const AssetManifest& manifest;
        const LevelEditor& editor;
        const PresentationSettings& settings;
        const AnimationCatalog* animations = nullptr;
        std::optional<GridPosition3> hoverCell;
        std::optional<std::size_t> hoverDecoration;
        bool deleting = false;
        float worldAnimationTimeSeconds = 0.0f;
        float conveyorBeltScrollOffset = 0.0f;
        // The screen this document belongs to, when it is one. Set so the
        // editor previews (and paints on) that screen's own splat map rather
        // than the shared fallback; unset for scratch documents.
        std::optional<LevelLocation> levelLocation;
    };

    [[nodiscard]] static RenderFrameData buildGameplay(const GameplayInput& input);
    [[nodiscard]] static RenderFrameData buildEditor(const EditorInput& input);
};

} // namespace sokoban
