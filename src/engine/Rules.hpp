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
// is what step application, undo history, and tests are built on.
//
// `sliding` fields are ice-slide momentum: an entity with momentum moves one
// tile in that direction every world step until it is blocked, falls, or
// leaves slippery ground.
struct GameState {
    struct Movable {
        TileType type = TileType::Rock;
        GridPosition3 cell {};
        bool fallen = false;
        std::optional<MoveDirection> sliding;

        bool operator==(const Movable&) const = default;
    };

    GridPosition3 player {};
    bool playerDead = false;
    std::optional<MoveDirection> playerSliding;
    std::vector<Movable> movables;

    bool operator==(const GameState&) const = default;
};

// Pure, headless gameplay rules. Nothing here touches rendering, input,
// animation, or the file system; every function is a deterministic function
// of its arguments.
//
// Time advances in discrete world steps. Within one step, every entity moves
// at most its per-step rate in tiles (see StepRates; everything defaults to
// one), and all entities move simultaneously: an entity blocked only by
// another entity that vacates its cell this step still advances.
namespace rules {

// Movement rates in tiles per world step, by movement source. The step
// algorithm supports any non-negative rate: multi-tile movement resolves as
// repeated simultaneous one-tile micro-steps, so faster entities still
// interact correctly with slower ones (blocking, vacating, pushing).
struct StepRates {
    int playerMove = 1; // input-driven walking and pushing
    int slide = 1;      // ice-slide momentum (player and movables)
    int conveyor = 1;   // belt riders

    bool operator==(const StepRates&) const = default;
};

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

// True when the world would keep moving without player input: any surviving
// entity has slide momentum or stands on a conveyor.
[[nodiscard]] bool hasPendingMotion(const Level& level, const GameState& state);

// Advances the world one discrete step and returns the resulting state
// (unchanged if nothing can move). Movement intents per entity:
//   - slide momentum first (it overrides player input),
//   - then player input (which may push a movable; only direct input pushes),
//   - then conveyors for entities standing on them.
// Each entity moves at most its rate in tiles per step (intents and rates are
// re-derived between micro-steps, so e.g. an entity carried off a belt stops
// even with budget left). Falls resolve within the step and cancel momentum.
// Ladder climbing applies to input-driven moves only.
[[nodiscard]] GameState step(
    const Level& level,
    const GameState& state,
    std::optional<MoveDirection> playerInput = std::nullopt,
    const StepRates& rates = {});

} // namespace rules

} // namespace sokoban
