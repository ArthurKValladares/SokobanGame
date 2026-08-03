#pragma once

#include "engine/EntityId.hpp"
#include "engine/Rules.hpp"

#include <optional>
#include <vector>

namespace sokoban {

// What one action changed, entity by entity.
//
// Completing an action used to assign a whole GameState. That is only safe
// while exactly one action is ever in flight: once actions can overlap, the
// last one to finish would overwrite everything the others had done, because
// its `after` is a snapshot of a world that no longer exists. Applying a delta
// touches only the entities the action actually changed, so actions with
// disjoint effects compose.
//
// Entities are keyed by id, never by vector position. Mirror activation
// appends players, so an action that completes first can shift the indices out
// from under a delta computed earlier. States authored without ids (tests,
// editor previews) fall back to `resolvedEntityId`, which derives a stable key
// from the index - see EntityId.hpp, which exists for exactly this reason.
struct StateDelta {
    template <typename Entity>
    struct Change {
        EntityId id = invalidEntityId;
        // An unset `before` is an entity the action created; an unset `after`
        // is one it removed. Both are needed: mirror activation creates
        // players, and undoing it has to take them back out again.
        std::optional<Entity> before;
        std::optional<Entity> after;

        bool operator==(const Change&) const = default;
    };

    // Entities that differ between the two states. Unchanged entities are
    // absent, which is the whole point - they are what a concurrent action is
    // free to be touching.
    [[nodiscard]] static StateDelta between(
        const GameState& before,
        const GameState& after);

    // Writes only this delta's entities into `state`, leaving everything else
    // as it is.
    //
    // Applying to the same `before` it was computed from reproduces `after`
    // exactly, including vector order, as long as entities were only appended
    // rather than inserted mid-vector. The rules only ever push_back, so that
    // holds; `StateDeltaTests` pins it for the paths the game actually takes.
    void applyTo(GameState& state) const;

    // Swaps each change's endpoints, so undo is the same machinery run
    // backwards.
    [[nodiscard]] StateDelta inverted() const;

    [[nodiscard]] bool empty() const;

    bool operator==(const StateDelta&) const = default;

    std::vector<Change<GameState::Player>> players;
    std::vector<Change<GameState::Movable>> movables;
    std::vector<Change<GameState::Enemy>> enemies;
};

} // namespace sokoban
