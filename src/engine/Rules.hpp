#pragma once

#include "engine/Level.hpp"
#include "engine/Math.hpp"
#include "engine/EntityId.hpp"
#include "engine/TileTypes.hpp"

#include <cstddef>
#include <cstdint>
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
        // Authored heroes carry a concrete character. Null is accepted only
        // while upgrading legacy checkpoints, whose level-wide character is
        // applied by GameplaySession::restore.
        std::optional<CharacterType> character;
        // Independently controlled authored heroes use their own id. Mirror
        // copies inherit this value from their source and therefore continue
        // to share that source's input.
        EntityId controller = invalidEntityId;
        bool dead = false;
        // Death and submersion are separate facts: enemy attacks leave the
        // actor on the board, while water deaths render below the surface.
        bool drowned = false;
        std::optional<MoveDirection> sliding;
        // Clockwise quarter turns (0-3) rotator plates have applied to this
        // hero. Presentation-only for heroes, but kept in the state so undo,
        // saves and concurrent actions agree on it.
        uint8_t quarterTurns = 0;

        bool operator==(const Player&) const = default;
    };

    struct Movable {
        EntityId id = invalidEntityId;
        TileType type = TileType::Rock;
        GridPosition3 cell {};
        bool fallen = false;
        // Destroyed turrets stay in the indexed state for stable identity and
        // undo/save deltas, but no longer block, move, fire, or render.
        bool dead = false;
        std::optional<MoveDirection> sliding;
        // Clockwise quarter turns (0-3) rotator plates have applied since the
        // unit was authored. A turret fires along its authored direction
        // turned by this amount (see rules::turretDirection); other movables
        // only change how they are drawn.
        uint8_t quarterTurns = 0;

        bool operator==(const Movable&) const = default;
    };

    struct Enemy {
        EntityId id = invalidEntityId;
        GridPosition3 cell {};
        bool fallen = false;
        // Killed enemies leave gameplay without masquerading as water falls.
        // They no longer block, support, attack, occlude, or render.
        bool dead = false;
        std::optional<MoveDirection> sliding;
        // Clockwise quarter turns (0-3) applied by rotator plates. Enemies
        // still turn to track heroes; this only drives the visible spin.
        uint8_t quarterTurns = 0;

        bool operator==(const Enemy&) const = default;
    };

    // A mirror a rotator plate has turned. Mirrors are static level tiles;
    // only their orientation can change, so only turned mirrors are listed,
    // sorted by cell (z, y, x) and never with zero quarter turns. An empty
    // list means every mirror is as authored.
    struct TurnedMirror {
        GridPosition3 cell {};
        // Clockwise quarter turns (1-3) from the authored mirror tile.
        uint8_t quarterTurns = 0;

        bool operator==(const TurnedMirror&) const = default;
    };

    std::vector<Player> players;
    std::vector<Movable> movables;
    std::vector<Enemy> enemies;
    std::vector<TurnedMirror> turnedMirrors;

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
    int slide = 1;      // ice-slide momentum (all movable units)
    int conveyor = 1;   // belt riders

    bool operator==(const StepRates&) const = default;
};

// A presentation cue emitted at the exact point where rule resolution decides
// that a turret has a clear shot. Keeping this in the rules result means
// visuals never have to guess whether a death came from a turret, an enemy, or
// water, and a volley can retain every firing turret.
struct TurretShot {
    EntityTarget turret;
    EntityTarget target;
    GridPosition3 turretCell {};
    GridPosition3 targetCell {};
    MoveDirection direction = MoveDirection::Up;

    bool operator==(const TurretShot&) const = default;
};

struct StepResult {
    GameState state;
    std::vector<TurretShot> turretShots;

    bool operator==(const StepResult&) const = default;
};

[[nodiscard]] GameState initialState(const Level& level);

[[nodiscard]] bool anyPlayerDead(const GameState& state);

[[nodiscard]] EntityId playerControllerId(
    const GameState& state, std::size_t playerIndex);

[[nodiscard]] GridPosition directionOffset(MoveDirection direction);
[[nodiscard]] GridPosition3 movementTarget(GridPosition3 origin, MoveDirection direction);
[[nodiscard]] std::optional<MoveDirection> conveyorDirectionForTile(TileType tile);
[[nodiscard]] std::optional<MoveDirection> conveyorDirectionAt(const Level& level, GridPosition3 position);
[[nodiscard]] std::optional<MoveDirection> turretDirectionForTile(TileType tile);
// `direction` turned by `quarterTurns` clockwise quarter turns (negative
// values turn counter-clockwise).
[[nodiscard]] MoveDirection rotateDirection(
    MoveDirection direction, int quarterTurns);
// `quarterTurns` advanced by a signed number of quarter turns, kept in 0-3.
[[nodiscard]] uint8_t addQuarterTurns(uint8_t quarterTurns, int delta);
// The direction a live turret movable fires: its authored tile direction
// turned by the quarter turns rotator plates have applied. Empty for
// anything that is not a turret.
[[nodiscard]] std::optional<MoveDirection> turretDirection(
    const GameState::Movable& movable);
// A mirror tile turned by `quarterTurns` clockwise quarter turns; any other
// tile is returned unchanged.
[[nodiscard]] TileType rotateMirrorTile(TileType tile, int quarterTurns);
// Quarter turns rotators have applied to the mirror at `cell` (0 if none).
[[nodiscard]] uint8_t mirrorQuarterTurnsAt(
    const GameState& state, GridPosition3 cell);
// Records the mirror at `cell` as turned by `quarterTurns`, keeping
// GameState::turnedMirrors sorted and free of zero entries.
void setMirrorQuarterTurns(
    GameState& state, GridPosition3 cell, uint8_t quarterTurns);
// The mirror tile currently at `cell`, including rotator turns. Empty when
// the cell holds no mirror.
[[nodiscard]] std::optional<TileType> mirrorTileAt(
    const Level& level, const GameState& state, GridPosition3 cell);

// Live turret ids participating in at least one unobstructed pair where both
// barrels face the other turret. These pairs fire without requiring movement.
[[nodiscard]] std::vector<EntityId> mutuallyFacingTurrets(
    const Level& level,
    const GameState& state);

// A cell entities may occupy, ignoring movables. The plane directly above the
// top layer (z == depth) is intentionally allowed so entities can stand on
// top-layer blocks. Gates are treated as potentially passable so static solver
// estimates remain admissible; gameplay movement uses cellAllowsEntity.
[[nodiscard]] bool staticCellAllowsEntity(const Level& level, GridPosition3 position);
// State-aware static collision. A Gate cell allows entry only while every
// pressure plate linked by its authored gate record has a live occupant.
[[nodiscard]] bool cellAllowsEntity(
    const Level& level,
    const GameState& state,
    GridPosition3 position);

[[nodiscard]] const GameState::Movable* movableAt(const GameState& state, GridPosition3 position);
[[nodiscard]] const GameState::Movable* fallenMovableAt(const GameState& state, GridPosition3 position);
[[nodiscard]] const GameState::Enemy* enemyAt(const GameState& state, GridPosition3 position);
[[nodiscard]] const GameState::Enemy* fallenEnemyAt(const GameState& state, GridPosition3 position);

// Water that has not been filled by a fallen movable. A drowned player remains
// below the water surface and does not displace it.
[[nodiscard]] bool isUnfilledWater(const Level& level, const GameState& state, GridPosition3 position);

// A pressure plate is pressed by a live hero, movable or enemy, or by a
// mirror authored on top of it (which holds it pressed for good).
[[nodiscard]] bool isPressurePlateActive(
    const Level& level,
    const GameState& state,
    GridPosition3 plate);
[[nodiscard]] bool isGateOpen(
    const Level& level,
    const GameState& state,
    const Level::Gate& gate);
// True while every pressure plate linked to the rotator has a live occupant.
// A rotator turns its occupant only on the step this becomes true, so a unit
// left standing on a pressed plate does not keep the rotator spinning.
[[nodiscard]] bool isRotatorEngaged(
    const Level& level,
    const GameState& state,
    const Level::Rotator& rotator);
// Kept as a compatibility query for debug/presentation callers. End tiles no
// longer have a locked state, so this always returns true.
[[nodiscard]] bool isEndUnlocked(const Level& level, const GameState& state);
// A screen is solved when every living hero stands on an End and every End
// holds a hero. A level with more Ends than heroes needs hero copies (mirrors)
// to be solved. Pressure plates now control Gates rather than completion.
[[nodiscard]] bool isAtUnlockedEnd(const Level& level, const GameState& state);

// True when the world has an automatic action to resolve: a mutual turret
// volley, slide momentum, or a surviving entity standing on a conveyor.
[[nodiscard]] bool hasPendingMotion(const Level& level, const GameState& state);

struct MirrorBeamSegment {
    GridPosition3 from {};
    GridPosition3 to {};

    bool operator==(const MirrorBeamSegment&) const = default;
};

struct MirrorEntityPreview {
    bool player = false;
    bool enemy = false;
    std::size_t playerIndex = 0;
    std::size_t reflectionIndex = 0;
    std::size_t resultPlayerIndex = 0;
    std::size_t movableIndex = 0;
    std::size_t enemyIndex = 0;
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

// Reflects every visible, non-fallen movable unit through mirrors as one
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

// Event-bearing forms used by action planning. The ordinary state-only entry
// points remain the stable gameplay/save replay API.
[[nodiscard]] StepResult stepWithEvents(
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

[[nodiscard]] StepResult scopedStepWithEvents(
    const Level& level,
    const GameState& state,
    std::optional<MoveDirection> playerInput,
    const StepRates& rates,
    const StepScope& scope);

} // namespace rules

} // namespace sokoban
