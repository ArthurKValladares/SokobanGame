#pragma once

#include "engine/ActionPresentation.hpp"
#include "engine/GameplayConfig.hpp"
#include "engine/Level.hpp"
#include "engine/Rules.hpp"

#include <optional>
#include <vector>

namespace sokoban {

// Everything one action will do, decided in full before it starts.
//
// Deciding and executing are separate on purpose. A plan is produced by a pure
// function of the world at the instant the action begins, so its outcome cannot
// drift afterwards; `GameplaySession` owns timing, history and bookkeeping, and
// does not decide outcomes. That split is what the concurrent scheduler will be
// built on - it needs to ask "what would this do?" without doing it.
//
// `before` and `after` are whole states because `GameplayPresentation` animates
// by pairing entities between them. What completion actually commits is the
// `StateDelta` between the two; it is deliberately not stored here, because
// this type is persisted inside `GameplaySession::Snapshot` in save files and
// a per-action delta would bloat every save for no gain.
struct ActionPlan {
    GameState before;
    GameState after;
    float durationSeconds = config::stepDurationSeconds;
    bool playerPushing = false;
    bool reversed = false;
    int playerMoveCountBefore = 0;
    int playerMoveCountAfter = 0;
    std::optional<MoveDirection> facingDirection;
    ActionPresentationTimeline presentation;

    bool operator==(const ActionPlan&) const = default;
};

// Pure planning. Nothing here reads or writes session state; every function is
// a deterministic function of its arguments, in the same spirit as `rules::`.
//
// Move counts are left at zero: only the session knows the running total, so it
// fills those in. Everything else about the action is settled here.
namespace plans {

// A plan together with the intermediate states it passes through.
//
// The legs exist so the presentation can animate a chain tile by tile instead
// of interpolating once from start to finish. They are a planning artifact and
// are deliberately not part of `ActionPlan`: actions are persisted inside save
// files, and a restored action already carries the built timeline, so it has no
// use for them.
struct PlannedAction {
    ActionPlan action;
    // One state per world step. `legs.back()` is always `action.after`, and the
    // state before `legs[i]` is `legs[i - 1]`, or `action.before` when i is 0.
    std::vector<GameState> legs;

    bool operator==(const PlannedAction&) const = default;
};

// How many world steps a single action may chain through before planning gives
// up. Slides travel in straight lines and are bounded by the board, so this is
// a guard against an unforeseen cycle rather than an expected limit.
inline constexpr int maxChainedSteps = 512;

// A world step and everything that inevitably follows from it.
//
// The first step applies `playerInput`; after that the world keeps stepping for
// as long as anything still carries slide momentum, and the whole run becomes
// one action. That is what makes a slide's destination final the moment it is
// pushed: the outcome is computed here, in full, and nothing that happens later
// can change it.
//
// Conveyor riders are deliberately left out. Belt motion is ambient and never
// terminates, so chaining it would produce an action that never ends; a rider
// gets one step per action instead, and hands back over to the caller.
//
// `durationSeconds` is per world step, so the action's total duration is that
// times the number of legs. No plan when nothing would move.
[[nodiscard]] std::optional<PlannedAction> worldStep(
    const Level& level,
    const GameState& state,
    std::optional<MoveDirection> playerInput,
    const rules::StepRates& rates,
    float stepDurationSeconds);

// Every living player takes one input-driven step, together with everything
// that step drags in: a block they push, an enemy that block shoves, a player
// the enemy kills. Players are planned as one action rather than one each
// because they are one character - mirror copies share a single input.
//
// One step, deliberately. The slide a push sets off is planned separately, so
// that the player is released after their own tile instead of being held for
// the length of the slide. The slide is still settled at the moment of the
// push - both plans are made from the same instant - which is what the
// guarantee actually requires. There is no separate `planPush`: whether a move
// turns out to be a push is for the rules to decide, not the caller.
[[nodiscard]] std::optional<PlannedAction> planPlayerStep(
    const Level& level,
    const GameState& state,
    MoveDirection input,
    const rules::StepRates& rates,
    float stepDurationSeconds);

// One entity's slide, resolved to its end as a single committed chain, plus
// whatever it runs into on the way. Nothing that happens while it travels can
// change where it stops.
[[nodiscard]] std::optional<PlannedAction> planSlide(
    const Level& level,
    const GameState& state,
    EntityId slider,
    const rules::StepRates& rates,
    float stepDurationSeconds);

// Several entities sliding at once, as one action.
//
// They must be planned together rather than one plan each. Two entities sliding
// from the same instant are outside each other's scope, so each treats the
// other as scenery standing where it started - and both plans then agree that a
// cell neither will be occupying is blocked, while a third cell both cross at
// the same step looks free to each of them. Planning them in one scope hands
// the arbitration back to `MicroStepResolver`, which is the only thing that
// knows how to resolve simultaneous intents.
[[nodiscard]] std::optional<PlannedAction> planSlides(
    const Level& level,
    const GameState& state,
    std::vector<EntityId> sliders,
    const rules::StepRates& rates,
    float stepDurationSeconds);

// One belt rider, one step.
//
// Never chained: belt motion is ambient and does not terminate, so a chained
// ride would be an action that never ends. Re-planning each step also keeps a
// rider's claims one cell and one interval long, without which the area around
// any belt would be permanently unusable and two riders could not follow each
// other down the same belt.
[[nodiscard]] std::optional<PlannedAction> planConveyorRide(
    const Level& level,
    const GameState& state,
    EntityId rider,
    const rules::StepRates& rates,
    float stepDurationSeconds);

// Every belt rider, one step, as one action.
//
// Together for the same reason slides are, and for one more: riders following
// each other down the same belt only work if they move in one scope. Planned
// separately, the follower sees the leader as scenery in the cell it is about
// to leave and refuses to move, and a queue on a belt would never advance.
[[nodiscard]] std::optional<PlannedAction> planConveyorRides(
    const Level& level,
    const GameState& state,
    std::vector<EntityId> riders,
    const rules::StepRates& rates,
    float stepDurationSeconds);

// Entities carrying slide momentum, and entities standing on a belt. What the
// session needs in order to know which ambient actions to plan; the split
// between them matters because only the first may be chained.
[[nodiscard]] std::vector<EntityId> slidingEntities(const GameState& state);
[[nodiscard]] std::vector<EntityId> conveyorRiders(
    const Level& level, const GameState& state);

// Whether any entity is still carrying slide momentum, which is what decides
// whether a chain keeps going. Distinct from `rules::hasPendingMotion`, which
// also reports conveyor riders.
[[nodiscard]] bool anySlideMomentum(const GameState& state);

// Mirror activation, from the transaction `rules::previewMirrorActivation`
// already validated. Takes the preview rather than recomputing it, because the
// caller also needs its per-entity destinations.
[[nodiscard]] ActionPlan fromMirrorPreview(
    const GameState& before,
    const rules::MirrorActivationPreview& preview);

// Back to the level's opening state. No plan when a player is dead, or when
// the state is already the initial one.
[[nodiscard]] std::optional<ActionPlan> restart(
    const Level& level,
    const GameState& state,
    float durationSeconds);

// The same action run backwards. Undo is the ordinary execution path applied to
// an inverted plan rather than a special case.
[[nodiscard]] ActionPlan inverted(const ActionPlan& plan);

// Whether any player changed cell, including players added or removed by a
// mirror activation.
[[nodiscard]] bool anyPlayerMoved(
    const GameState& before, const GameState& after);

// The direction the first player that moved travelled in. Used to face the
// character during steps the player did not directly drive.
[[nodiscard]] std::optional<MoveDirection> firstPlayerMovementDirection(
    const GameState& before, const GameState& after);

} // namespace plans

} // namespace sokoban
