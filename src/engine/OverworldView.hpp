#pragma once

#include "engine/OverworldMap.hpp"
#include "engine/Rules.hpp"
#include "engine/render/RenderTypes.hpp"

#include <optional>
#include <vector>

namespace sokoban {

// Headless rendering/navigation view of a composed overworld. A settled view
// frames only the active screen while still rendering its 3x3 neighborhood.
// While a player action crosses a seam, that screen-sized view translates
// from the source screen to the destination screen;
// the destination becomes active only after gameplay commits the action.
struct OverworldView {
    OverworldScreenId sourceScreen = 0;
    std::optional<OverworldScreenId> destinationScreen;
    float transitionProgress = 0.0f;
    RenderFrameData::CameraExtent cameraExtent;
    RenderFrameData::CameraExtent overviewCameraExtent;
    float overviewProgress = 0.0f;
    Vec2 cameraOffset {};
    std::vector<OverworldScreenId> visibleScreens;
};

// Action-admission invariant: every living player must be owned by the same
// authored screen. Dead players do not pin navigation, and an all-dead state
// remains valid so death/undo mechanics keep working.
[[nodiscard]] bool overworldActionStateAllowed(
    const OverworldMap& map, const GameState& state);

[[nodiscard]] OverworldView calculateOverworldView(
    const OverworldMap& map,
    OverworldScreenId activeScreen,
    const GameState& committedState,
    const GameState& projectedState,
    Vec3 primaryPlayerRenderPosition,
    float overviewProgress = 0.0f);

} // namespace sokoban
