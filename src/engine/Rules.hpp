#pragma once

#include "engine/Level.hpp"
#include "engine/Math.hpp"
#include "engine/EntityId.hpp"
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
    struct Player {
        EntityId id = invalidEntityId;
        GridPosition3 cell {};
        bool dead = false;
        // Death and submersion are separate facts: enemy attacks leave the
        // actor on the board, while water deaths render below the surface.
        bool drowned = false;
        std::optional<MoveDirection> sliding;

        bool operator==(const Player&) const = default;
    };

    struct Movable {
        EntityId id = invalidEntityId;
        TileType type = TileType::Rock;
        GridPosition3 cell {};
        bool fallen = false;
        std::optional<MoveDirection> sliding;

        bool operator==(const Movable&) const = default;
    };

    struct Enemy {
        EntityId id = invalidEntityId;
        GridPosition3 cell {};
        bool fallen = false;

        bool operator==(const Enemy&) const = default;
    };

    std::vector<Player> players;
    std::vector<Movable> movables;
    std::vector<Enemy> enemies;

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

[[nodiscard]] bool anyPlayerDead(const GameState& state);

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
[[nodiscard]] const GameState::Enemy* enemyAt(const GameState& state, GridPosition3 position);
[[nodiscard]] const GameState::Enemy* fallenEnemyAt(const GameState& state, GridPosition3 position);

// Water that has not been filled by a fallen movable. A drowned player remains
// below the water surface and does not displace it.
[[nodiscard]] bool isUnfilledWater(const Level& level, const GameState& state, GridPosition3 position);

[[nodiscard]] bool isEndUnlocked(const Level& level, const GameState& state);
[[nodiscard]] bool isAtUnlockedEnd(const Level& level, const GameState& state);

// True when the world would keep moving without player input: any surviving
// entity has slide momentum or stands on a conveyor.
[[nodiscard]] bool hasPendingMotion(const Level& level, const GameState& state);

struct MirrorBeamSegment {
    GridPosition3 from {};
    GridPosition3 to {};

    bool operator==(const MirrorBeamSegment&) const = default;
};

struct MirrorEntityPreview {
    bool player = false;
    std::size_t playerIndex = 0;
    std::size_t reflectionIndex = 0;
    std::size_t resultPlayerIndex = 0;
    std::size_t movableIndex = 0;
    GridPosition3 start {};
    GridPosition3 destination {};
    bool fallen = false;
    std::vector<MirrorBeamSegment> beamSegments;

    bool operator==(const MirrorEntityPreview&) const = default;
};

struct MirrorActivationPreview {
    GameState after;
    std::vector<MirrorEntityPreview> entities;

    bool operator==(const MirrorActivationPreview&) const = default;
};

// Describes the exact valid transaction activation would commit, including
// every input/output leg used to visualize chained reflections.
[[nodiscard]] std::optional<MirrorActivationPreview> previewMirrorActivation(
    const Level& level,
    const GameState& state);

// Reflects every visible, non-fallen movable entity through mirrors as one
// atomic transaction. Returns no state when nothing is reflected or any
// reflected destination/chain is invalid.
[[nodiscard]] std::optional<GameState> activateMirrors(
    const Level& level,
    const GameState& state);

// Which entities a step is allowed to move.
//
// An entity outside the scope is scenery. It blocks, supports, occludes and
// stops a slide exactly as it always did, but it forms no intent of its own and
// is never written. That is what lets one action move a player without also
// re-deciding what every other entity in the world is doing.
//
// Why this exists: planning used to be whole-world, so a plan made while a
// slide was in flight re-planned that slide too, and its claims collided
// head-on with the copy already running. Nothing could ever be admitted
// alongside anything else. A scope is the seam that a per-entity planner needs.
//
// The scope grows during resolution, because the causal closure is not knowable
// before the step is resolved: whether a move turns out to be a push, and what
// that push sets off, is decided while resolving it. A movable that gets
// pushed, an enemy that movable shoves, and a bystander an enemy is shoved next
// to all join the closure - they are written, so they belong to the action.
struct StepScope {
    // Entities permitted to act. Empty means every entity, which is the
    // whole-world step this game has always taken.
    std::vector<EntityId> actors;

    [[nodiscard]] bool wholeWorld() const { return actors.empty(); }
};

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

// The same step, restricted to the entities the scope names.
//
// `step` is exactly this with an empty scope, and the two must stay that way:
// every save in existence is validated by replaying it through the whole-world
// form, so it cannot be allowed to drift.
[[nodiscard]] GameState scopedStep(
    const Level& level,
    const GameState& state,
    std::optional<MoveDirection> playerInput,
    const StepRates& rates,
    const StepScope& scope);

} // namespace rules

} // namespace sokoban
