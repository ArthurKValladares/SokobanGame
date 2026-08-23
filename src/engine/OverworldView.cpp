#include "engine/OverworldView.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <stdexcept>

namespace sokoban {
namespace {

std::optional<OverworldScreenId> sharedScreen(
    const OverworldMap& map, const GameState& state)
{
    if (state.players.empty()) {
        return std::nullopt;
    }

    std::optional<OverworldScreenId> owner;
    for (const GameState::Player& player : state.players) {
        if (player.dead) {
            return std::nullopt;
        }
        const std::optional<OverworldScreenId> playerOwner =
            map.screenAt(player.cell);
        if (!playerOwner || (owner && owner != playerOwner)) {
            return std::nullopt;
        }
        owner = playerOwner;
    }
    return owner;
}

float playerTransitionProgress(
    const GameState& before,
    const GameState& after,
    Vec3 renderPosition)
{
    if (before.players.empty() || after.players.empty()) {
        return 0.0f;
    }
    const GridPosition3 from = before.players.front().cell;
    const GridPosition3 to = after.players.front().cell;
    const Vec3 delta {
        static_cast<float>(to.x - from.x),
        static_cast<float>(to.y - from.y),
        static_cast<float>(to.z - from.z),
    };
    const float lengthSquared =
        delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    if (lengthSquared <= 0.0001f) {
        return 0.0f;
    }
    const Vec3 travelled {
        renderPosition.x - static_cast<float>(from.x),
        renderPosition.y - static_cast<float>(from.y),
        renderPosition.z - static_cast<float>(from.z),
    };
    return std::clamp(
        (travelled.x * delta.x + travelled.y * delta.y +
            travelled.z * delta.z) / lengthSquared,
        0.0f,
        1.0f);
}

void includeNeighborhood(
    std::vector<OverworldScreenId>& result,
    const OverworldMap& map,
    OverworldScreenId center)
{
    for (OverworldScreenId screen : map.visibleNeighborhood(center)) {
        if (std::ranges::find(result, screen) == result.end()) {
            result.push_back(screen);
        }
    }
}

RenderFrameData::CameraExtent wholeMapExtent(const OverworldMap& map)
{
    int32_t minimumX = std::numeric_limits<int32_t>::max();
    int32_t minimumY = std::numeric_limits<int32_t>::max();
    int32_t maximumX = std::numeric_limits<int32_t>::lowest();
    int32_t maximumY = std::numeric_limits<int32_t>::lowest();
    uint32_t maximumDepth = 1;
    for (const OverworldScreenRuntime& screen : map.screens()) {
        minimumX = std::min(minimumX, screen.origin.x);
        minimumY = std::min(minimumY, screen.origin.y);
        maximumX = std::max(
            maximumX,
            screen.origin.x + static_cast<int32_t>(map.layout().screenWidth));
        maximumY = std::max(
            maximumY,
            screen.origin.y + static_cast<int32_t>(map.layout().screenHeight));
        maximumDepth = std::max(maximumDepth, screen.depth);
    }
    return {
        .originX = minimumX,
        .originY = minimumY,
        .originZ = 0,
        .width = static_cast<uint32_t>(maximumX - minimumX),
        .height = static_cast<uint32_t>(maximumY - minimumY),
        .depth = maximumDepth,
    };
}

} // namespace

bool overworldActionStateAllowed(
    const OverworldMap& map, const GameState& state)
{
    std::optional<OverworldScreenId> owner;
    for (const GameState::Player& player : state.players) {
        if (player.dead) {
            continue;
        }
        const std::optional<OverworldScreenId> playerOwner =
            map.screenAt(player.cell);
        if (!playerOwner || (owner && owner != playerOwner)) {
            return false;
        }
        owner = playerOwner;
    }
    return true;
}

OverworldView calculateOverworldView(
    const OverworldMap& map,
    OverworldScreenId activeScreen,
    const GameState& committedState,
    const GameState& projectedState,
    Vec3 primaryPlayerRenderPosition,
    float overviewProgress)
{
    const OverworldScreenRuntime* active = map.screen(activeScreen);
    if (active == nullptr) {
        throw std::runtime_error("active overworld screen does not exist");
    }

    const uint32_t screenWidth = map.layout().screenWidth;
    const uint32_t screenHeight = map.layout().screenHeight;
    OverworldView view {
        .sourceScreen = activeScreen,
        .cameraExtent = {
            .originX = active->origin.x,
            .originY = active->origin.y,
            .originZ = 0,
            .width = screenWidth,
            .height = screenHeight,
            .depth = active->depth,
        },
        .overviewCameraExtent = wholeMapExtent(map),
        .overviewProgress = std::clamp(overviewProgress, 0.0f, 1.0f),
    };
    includeNeighborhood(view.visibleScreens, map, activeScreen);
    if (view.overviewProgress > 0.0001f) {
        view.visibleScreens.clear();
        view.visibleScreens.reserve(map.screens().size());
        for (const OverworldScreenRuntime& screen : map.screens()) {
            view.visibleScreens.push_back(screen.id);
        }
        std::ranges::sort(view.visibleScreens);
    }

    const std::optional<OverworldScreenId> committedOwner =
        sharedScreen(map, committedState);
    const std::optional<OverworldScreenId> projectedOwner =
        sharedScreen(map, projectedState);
    if (committedOwner != std::optional<OverworldScreenId> { activeScreen } ||
        !projectedOwner || projectedOwner == committedOwner) {
        return view;
    }

    const OverworldScreenRuntime* destination = map.screen(*projectedOwner);
    if (destination == nullptr) {
        return view;
    }
    view.destinationScreen = *projectedOwner;
    view.transitionProgress = playerTransitionProgress(
        committedState, projectedState, primaryPlayerRenderPosition);
    view.cameraOffset = {
        static_cast<float>(destination->origin.x - active->origin.x) *
            view.transitionProgress,
        static_cast<float>(destination->origin.y - active->origin.y) *
            view.transitionProgress,
    };
    includeNeighborhood(view.visibleScreens, map, *projectedOwner);
    std::ranges::sort(view.visibleScreens);
    return view;
}

} // namespace sokoban
