#include "engine/ActionPlan.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace sokoban::plans {
namespace {

std::optional<MoveDirection> movementDirection(
    GridPosition3 from, GridPosition3 to)
{
    const int deltaX = to.x - from.x;
    const int deltaY = to.y - from.y;
    if (deltaX < 0) {
        return MoveDirection::Left;
    }
    if (deltaX > 0) {
        return MoveDirection::Right;
    }
    if (deltaY < 0) {
        return MoveDirection::Up;
    }
    if (deltaY > 0) {
        return MoveDirection::Down;
    }
    return std::nullopt;
}

// A push is a movable that was in the cell the player stepped into and is no
// longer there. Only direct input pushes, so this is not derived for automatic
// steps.
[[nodiscard]] bool derivePlayerPushing(
    const GameState& before,
    const GameState& after,
    MoveDirection playerInput)
{
    for (const GameState::Player& player : before.players) {
        const GridPosition3 pushCell =
            rules::movementTarget(player.cell, playerInput);
        const std::size_t count =
            std::min(before.movables.size(), after.movables.size());
        for (std::size_t i = 0; i < count; ++i) {
            if (before.movables[i].cell == pushCell &&
                !(after.movables[i].cell == pushCell)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

bool anyPlayerMoved(const GameState& before, const GameState& after)
{
    const std::size_t count = std::min(
        before.players.size(), after.players.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (!(before.players[i].cell == after.players[i].cell)) {
            return true;
        }
    }
    return before.players.size() != after.players.size();
}

std::optional<MoveDirection> firstPlayerMovementDirection(
    const GameState& before,
    const GameState& after)
{
    const std::size_t count = std::min(
        before.players.size(), after.players.size());
    for (std::size_t i = 0; i < count; ++i) {
        const std::optional<MoveDirection> direction = movementDirection(
            before.players[i].cell,
            after.players[i].cell);
        if (direction) {
            return direction;
        }
    }
    return std::nullopt;
}

std::optional<ActionPlan> worldStep(
    const Level& level,
    const GameState& state,
    std::optional<MoveDirection> playerInput,
    const rules::StepRates& rates,
    float durationSeconds)
{
    GameState after = rules::step(level, state, playerInput, rates);
    if (after == state) {
        return std::nullopt;
    }

    ActionPlan plan {
        .before = state,
        .after = std::move(after),
        .durationSeconds = durationSeconds,
        .facingDirection = playerInput,
    };

    if (playerInput && anyPlayerMoved(plan.before, plan.after)) {
        plan.playerPushing =
            derivePlayerPushing(plan.before, plan.after, *playerInput);
    }
    if (!playerInput) {
        // Nothing was driving a facing, so take it from whoever moved - a
        // conveyor rider or a sliding player still turns to face their travel.
        plan.facingDirection =
            firstPlayerMovementDirection(plan.before, plan.after);
    }
    return plan;
}

ActionPlan fromMirrorPreview(
    const GameState& before,
    const rules::MirrorActivationPreview& preview)
{
    return {
        .before = before,
        .after = preview.after,
        // Instant: the mirror's own presentation timeline drives the visuals,
        // and is installed after the action starts.
        .durationSeconds = 0.0f,
    };
}

std::optional<ActionPlan> restart(
    const Level& level,
    const GameState& state,
    float durationSeconds)
{
    if (rules::anyPlayerDead(state)) {
        return std::nullopt;
    }
    GameState restarted = rules::initialState(level);
    if (state == restarted) {
        return std::nullopt;
    }
    return ActionPlan {
        .before = state,
        .after = std::move(restarted),
        .durationSeconds = durationSeconds,
    };
}

ActionPlan inverted(const ActionPlan& plan)
{
    return {
        .before = plan.after,
        .after = plan.before,
        .playerPushing = plan.playerPushing,
        .reversed = true,
        .playerMoveCountBefore = plan.playerMoveCountAfter,
        .playerMoveCountAfter = plan.playerMoveCountBefore,
        .presentation = plan.presentation,
    };
}

} // namespace sokoban::plans
