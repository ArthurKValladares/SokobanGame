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

// Appends every entity whose value changed, which is what this leg wrote.
//
// Index-wise comparison is sound here because a step never adds or removes an
// entity - only mirror activation does that, and it does not come through this
// path.
template <EntityKind kind, typename Entity>
void addChanged(
    const std::vector<Entity>& before,
    const std::vector<Entity>& after,
    std::vector<EntityId>& into)
{
    const std::size_t count = std::min(before.size(), after.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (before[i] == after[i]) {
            continue;
        }
        const EntityId id = resolvedEntityId(kind, before[i].id, i);
        if (std::ranges::find(into, id) == into.end()) {
            into.push_back(id);
        }
    }
}

void addChanged(
    const GameState& before, const GameState& after, std::vector<EntityId>& into)
{
    addChanged<EntityKind::Player>(before.players, after.players, into);
    addChanged<EntityKind::Movable>(before.movables, after.movables, into);
    addChanged<EntityKind::Enemy>(before.enemies, after.enemies, into);
}

// Momentum among the entities this plan owns.
//
// Judging it over the whole board would let a block sliding under some *other*
// action keep this chain looping, producing legs in which nothing of this
// action's own moves.
[[nodiscard]] bool anySlideMomentumWithin(
    const GameState& state, const std::vector<EntityId>& closure)
{
    for (std::size_t i = 0; i < state.players.size(); ++i) {
        if (state.players[i].sliding &&
            std::ranges::find(
                closure,
                resolvedEntityId(EntityKind::Player, state.players[i].id, i)) !=
                closure.end()) {
            return true;
        }
    }
    for (std::size_t i = 0; i < state.movables.size(); ++i) {
        if (state.movables[i].sliding &&
            std::ranges::find(
                closure,
                resolvedEntityId(EntityKind::Movable, state.movables[i].id, i)) !=
                closure.end()) {
            return true;
        }
    }
    return false;
}

// The one planning core. Every planner is this with a different scope and a
// different answer to whether it chains, which is what keeps them from drifting
// apart - and what lets `worldStep` remain exactly what it always was, since
// every save on disk is validated by replaying it.
[[nodiscard]] std::optional<plans::PlannedAction> planScoped(
    const Level& level,
    const GameState& state,
    std::optional<MoveDirection> playerInput,
    const rules::StepRates& rates,
    float stepDurationSeconds,
    const rules::StepScope& scope,
    bool chainSlides)
{
    GameState current =
        rules::scopedStep(level, state, playerInput, rates, scope);
    if (current == state) {
        return std::nullopt;
    }

    plans::PlannedAction planned;
    planned.legs.push_back(current);

    if (chainSlides) {
        // The scope grows as planning discovers the closure: a block that gets
        // pushed onto ice has to keep sliding under this same action, and it
        // was not named when the action began.
        rules::StepScope chainScope = scope;
        std::vector<EntityId> closure = scope.actors;
        const bool wholeWorld = scope.wholeWorld();
        if (!wholeWorld) {
            addChanged(state, current, closure);
            chainScope.actors = closure;
        }

        while (static_cast<int>(planned.legs.size()) < plans::maxChainedSteps &&
            (wholeWorld ? plans::anySlideMomentum(current)
                        : anySlideMomentumWithin(current, closure))) {
            GameState next =
                rules::scopedStep(level, current, std::nullopt, rates, chainScope);
            if (next == current) {
                // Momentum that cannot be spent - nothing would change by
                // asking again, so stop rather than spin.
                break;
            }
            if (!wholeWorld) {
                addChanged(current, next, closure);
                chainScope.actors = closure;
            }
            current = std::move(next);
            planned.legs.push_back(current);
        }
    }

    ActionPlan& plan = planned.action;
    plan.before = state;
    plan.after = current;
    plan.durationSeconds =
        stepDurationSeconds * static_cast<float>(planned.legs.size());
    plan.facingDirection = playerInput;

    if (playerInput && plans::anyPlayerMoved(plan.before, planned.legs.front())) {
        // Only the first leg is input-driven, so a push is judged there.
        plan.playerPushing =
            derivePlayerPushing(plan.before, planned.legs.front(), *playerInput);
    }
    if (!playerInput) {
        // Nothing was driving a facing, so take it from whoever moved - a
        // conveyor rider or a sliding player still turns to face their travel.
        plan.facingDirection =
            plans::firstPlayerMovementDirection(plan.before, planned.legs.front());
    }
    return planned;
}

// Everyone still alive. Player input is shared, so they plan as one.
[[nodiscard]] std::vector<EntityId> livingPlayers(const GameState& state)
{
    std::vector<EntityId> ids;
    for (std::size_t i = 0; i < state.players.size(); ++i) {
        if (!state.players[i].dead) {
            ids.push_back(
                resolvedEntityId(EntityKind::Player, state.players[i].id, i));
        }
    }
    return ids;
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
    // The whole-world composition: everything acts, and momentum is resolved
    // here rather than one step at a time so that a slide's destination is
    // settled before the block has moved a single tile.
    //
    // This cannot become one of the scoped planners, however tempting. Every
    // save is validated by replaying its history through this exact function,
    // so its behaviour is part of the save format.
    return planScoped(
        level,
        state,
        playerInput,
        rates,
        stepDurationSeconds,
        rules::StepScope {},
        true);
}

std::optional<PlannedAction> planPlayerStep(
    const Level& level,
    const GameState& state,
    MoveDirection input,
    const rules::StepRates& rates,
    float stepDurationSeconds)
{
    std::vector<EntityId> players = livingPlayers(state);
    if (players.empty()) {
        return std::nullopt;
    }
    return planScoped(
        level,
        state,
        input,
        rates,
        stepDurationSeconds,
        rules::StepScope { .actors = std::move(players) },
        false);
}

std::optional<PlannedAction> planSlide(
    const Level& level,
    const GameState& state,
    EntityId slider,
    const rules::StepRates& rates,
    float stepDurationSeconds)
{
    if (slider == invalidEntityId) {
        return std::nullopt;
    }
    return planSlides(level, state, { slider }, rates, stepDurationSeconds);
}

std::optional<PlannedAction> planSlides(
    const Level& level,
    const GameState& state,
    std::vector<EntityId> sliders,
    const rules::StepRates& rates,
    float stepDurationSeconds)
{
    std::erase(sliders, invalidEntityId);
    if (sliders.empty()) {
        // An empty scope is the whole world, which is emphatically not what an
        // empty set of sliders means.
        return std::nullopt;
    }
    return planScoped(
        level,
        state,
        std::nullopt,
        rates,
        stepDurationSeconds,
        rules::StepScope { .actors = std::move(sliders) },
        true);
}

std::optional<PlannedAction> planConveyorRide(
    const Level& level,
    const GameState& state,
    EntityId rider,
    const rules::StepRates& rates,
    float stepDurationSeconds)
{
    if (rider == invalidEntityId) {
        return std::nullopt;
    }
    return planConveyorRides(
        level, state, { rider }, rates, stepDurationSeconds);
}

std::optional<PlannedAction> planConveyorRides(
    const Level& level,
    const GameState& state,
    std::vector<EntityId> riders,
    const rules::StepRates& rates,
    float stepDurationSeconds)
{
    std::erase(riders, invalidEntityId);
    if (riders.empty()) {
        return std::nullopt;
    }
    return planScoped(
        level,
        state,
        std::nullopt,
        rates,
        stepDurationSeconds,
        rules::StepScope { .actors = std::move(riders) },
        false);
}

std::vector<EntityId> slidingEntities(const GameState& state)
{
    std::vector<EntityId> ids;
    for (std::size_t i = 0; i < state.players.size(); ++i) {
        if (!state.players[i].dead && state.players[i].sliding) {
            ids.push_back(
                resolvedEntityId(EntityKind::Player, state.players[i].id, i));
        }
    }
    for (std::size_t i = 0; i < state.movables.size(); ++i) {
        if (!state.movables[i].fallen && state.movables[i].sliding) {
            ids.push_back(
                resolvedEntityId(EntityKind::Movable, state.movables[i].id, i));
        }
    }
    return ids;
}

std::vector<EntityId> conveyorRiders(
    const Level& level, const GameState& state)
{
    std::vector<EntityId> ids;
    for (std::size_t i = 0; i < state.players.size(); ++i) {
        const GameState::Player& player = state.players[i];
        // Momentum overrides the belt, so a sliding rider is not a rider yet.
        if (!player.dead && !player.sliding &&
            rules::conveyorDirectionAt(level, player.cell)) {
            ids.push_back(
                resolvedEntityId(EntityKind::Player, player.id, i));
        }
    }
    for (std::size_t i = 0; i < state.movables.size(); ++i) {
        const GameState::Movable& movable = state.movables[i];
        if (!movable.fallen && !movable.sliding &&
            rules::conveyorDirectionAt(level, movable.cell)) {
            ids.push_back(
                resolvedEntityId(EntityKind::Movable, movable.id, i));
        }
    }
    return ids;
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
