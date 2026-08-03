#pragma once

#include "engine/ActionPresentation.hpp"
#include "engine/GameplayConfig.hpp"
#include "engine/Level.hpp"
#include "engine/Rules.hpp"

#include <optional>

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

// One discrete world step - slide momentum, then any player input, then
// conveyors. No plan when nothing would move.
[[nodiscard]] std::optional<ActionPlan> worldStep(
    const Level& level,
    const GameState& state,
    std::optional<MoveDirection> playerInput,
    const rules::StepRates& rates,
    float durationSeconds);

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
