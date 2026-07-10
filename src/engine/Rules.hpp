#pragma once

#include "engine/Level.hpp"
#include "engine/Math.hpp"
#include "engine/TileTypes.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace sokoban {

enum class MoveDirection {
    Up,
    Down,
    Left,
    Right,
};

// Complete gameplay state for one screen: everything the rules need besides
// the static Level. Values of this type are cheap to copy and compare, which
// is what move application, undo history, and tests are built on.
struct GameState {
    struct Movable {
        TileType type = TileType::Rock;
        GridPosition3 cell {};
        bool fallen = false;

        bool operator==(const Movable&) const = default;
    };

    GridPosition3 player {};
    bool playerDead = false;
    std::vector<Movable> movables;

    bool operator==(const GameState&) const = default;
};

// Pure, headless gameplay rules. Nothing here touches rendering, input,
// animation, or the file system; every function is a deterministic function
// of its arguments.
namespace rules {

[[nodiscard]] GameState initialState(const Level& level);

[[nodiscard]] GridPosition directionOffset(MoveDirection direction);
[[nodiscard]] GridPosition3 movementTarget(GridPosition3 origin, MoveDirection direction);
[[nodiscard]] std::optional<MoveDirection> conveyorDirectionForTile(TileType tile);
[[nodiscard]] std::optional<MoveDirection> conveyorDirectionAt(const Level& level, GridPosition3 position);

// A cell entities may occupy, ignoring movables. The plane directly above the
// top layer (z == depth) is intentionally allowed so entities can stand on
// top-layer blocks.
[[nodiscard]] bool staticCellAllowsEntity(const Level& level, GridPosition3 position);

[[nodiscard]] const GameState::Movable* movableAt(const GameState& state, GridPosition3 position);
[[nodiscard]] const GameState::Movable* fallenMovableAt(const GameState& state, GridPosition3 position);

// Water that has not been filled by a fallen movable (or the drowned player).
[[nodiscard]] bool isUnfilledWater(const Level& level, const GameState& state, GridPosition3 position);

[[nodiscard]] bool isEndUnlocked(const Level& level, const GameState& state);
[[nodiscard]] bool isAtUnlockedEnd(const Level& level, const GameState& state);

// Applies one player move (including pushing, ladder climbing, ice sliding,
// falling, and water) and returns the resulting state, or nullopt if the move
// is not possible.
[[nodiscard]] std::optional<GameState> tryMove(
    const Level& level,
    const GameState& state,
    MoveDirection direction,
    bool allowPush = true);

// True when the (living) player or any surfaced movable stands on a conveyor.
[[nodiscard]] bool anyEntityOnConveyor(const Level& level, const GameState& state);

// Applies one conveyor tick: every entity standing on a conveyor tile moves
// one tile in the belt direction, simultaneously, with the usual slide/fall/
// water treatment. Conveyors never push entities into each other; blocked
// entities stay put. Returns nullopt if nothing moves.
[[nodiscard]] std::optional<GameState> applyConveyorStep(const Level& level, const GameState& state);

} // namespace rules

} // namespace sokoban
