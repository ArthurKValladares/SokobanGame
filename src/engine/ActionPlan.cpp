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

bool anySlideMomentum(const GameState& state)
{
    for (const GameState::Player& player : state.players) {
        if (player.sliding.has_value()) {
            return true;
        }
    }
    for (const GameState::Movable& movable : state.movables) {
        if (movable.sliding.has_value()) {
            return true;
        }
    }
    return false;
}

std::optional<PlannedAction> worldStep(
    const Level& level,
    const GameState& state,
    std::optional<MoveDirection> playerInput,
    const rules::StepRates& rates,
    float stepDurationSeconds)
{
    GameState current = rules::step(level, state, playerInput, rates);
    if (current == state) {
        return std::nullopt;
    }

    PlannedAction planned;
    planned.legs.push_back(current);

    // Everything that inevitably follows. Momentum is resolved here rather than
    // one step at a time so that the destination is settled before the block
    // has moved a single tile.
    while (anySlideMomentum(current) &&
        static_cast<int>(planned.legs.size()) < maxChainedSteps) {
        GameState next = rules::step(level, current, std::nullopt, rates);
        if (next == current) {
            // Momentum that cannot be spent - nothing would change by asking
            // again, so stop rather than spin.
            break;
        }
        current = std::move(next);
        planned.legs.push_back(current);
    }

    ActionPlan& plan = planned.action;
    plan.before = state;
    plan.after = current;
    plan.durationSeconds =
        stepDurationSeconds * static_cast<float>(planned.legs.size());
    plan.facingDirection = playerInput;

    if (playerInput && anyPlayerMoved(plan.before, planned.legs.front())) {
        // Only the first leg is input-driven, so a push is judged there.
        plan.playerPushing =
            derivePlayerPushing(plan.before, planned.legs.front(), *playerInput);
    }
    if (!playerInput) {
        // Nothing was driving a facing, so take it from whoever moved - a
        // conveyor rider or a sliding player still turns to face their travel.
        plan.facingDirection =
            firstPlayerMovementDirection(plan.before, planned.legs.front());
    }
    return planned;
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
