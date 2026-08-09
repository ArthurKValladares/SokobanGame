#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/GameplayPresentation.hpp"
#include "engine/Level.hpp"
#include "engine/LevelEditor.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/render/RenderTypes.hpp"

#include <optional>
#include <functional>

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
        // The world as it will stand once everything in flight has committed.
        //
        // This used to be "the" active action's `before` and `after` read as
        // whole states, which was fine while exactly one action existed and
        // `before` was simply the live state. Now that the active action is
        // merely the oldest of several, its `after` describes a world that
        // never existed - it is a snapshot taken when that action started, with
        // no knowledge of what the others are doing. Projecting the live state
        // through every in-flight delta is the honest version of the same idea.
        const GameState& projectedState;
        const GameplayPresentation& presentation;
        const PresentationSettings& settings;
        const AnimationCatalog* animations = nullptr;
        float conveyorBeltScrollOffset = 0.0f;
        std::optional<float> cameraPitchDegrees;
        // Selects this screen's ground splat map. Unset falls back to the
        // shared map, which is also what draft playback outside a campaign
        // gets.
        std::optional<LevelLocation> levelLocation;
        std::function<bool(LevelLocation)> selectorSolved;
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
        std::function<bool(LevelLocation)> selectorSolved;
    };

    [[nodiscard]] static RenderFrameData buildGameplay(const GameplayInput& input);
    [[nodiscard]] static RenderFrameData buildGameplay(
        const GameplayInput& input,
        FrameArena& arena);
    [[nodiscard]] static RenderFrameData buildEditor(const EditorInput& input);
    [[nodiscard]] static RenderFrameData buildEditor(
        const EditorInput& input,
        FrameArena& arena);
};

} // namespace sokoban
