#include "engine/Rules.hpp"
#include "engine/TurretRayTrace.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <ranges>
#include <utility>
#include <vector>

namespace sokoban::rules {
namespace {

// Compare against the immutable input rather than copying every intermediate
// board. Once a physical plate differs, later reactions cannot undo the fact
// that this action changed a signal along the way.
struct PressurePlateActivityTracker {
    const Level& level;
    const GameState& before;
    bool* changed = nullptr;

    void observe(const GameState& state) const
    {
        if (changed != nullptr && !*changed) {
            *changed = pressurePlateActivityChanged(level, before, state);
        }
    }
};

[[nodiscard]] MoveDirection oppositeDirection(MoveDirection direction)
{
    switch (direction) {
    case MoveDirection::Up:
        return MoveDirection::Down;
    case MoveDirection::Down:
        return MoveDirection::Up;
    case MoveDirection::Left:
        return MoveDirection::Right;
    case MoveDirection::Right:
        return MoveDirection::Left;
    }
    return MoveDirection::Up;
}

struct FallResult {
    GridPosition3 cell {};
    bool fallen = false;
    // False when the fall ran out of layers without hitting anything that can
    // hold an entity (an all-air column). Moves that would land unsupported
    // are rejected instead of leaving the entity standing on nothing.
    bool supported = true;
};

TileType tileAt(const Level& level, GridPosition3 position)
{
    return level.tileAt(
        static_cast<uint32_t>(position.x),
        static_cast<uint32_t>(position.y),
        static_cast<uint32_t>(position.z));
}

std::optional<TileType> fallenTileAt(const GameState& state, GridPosition3 position)
{
    if (const GameState::Movable* movable = fallenMovableAt(state, position)) {
        return movable->type;
    }

    return std::nullopt;
}

bool isIceFloor(const Level& level, const GameState& state, GridPosition3 position)
{
    if (!level.inBounds(position)) {
        return false;
    }

    if (tileAt(level, position) == TileType::Ice) {
        return true;
    }

    return fallenTileAt(state, position) == TileType::Ice;
}

bool movableBlocksAt(const GameState& state, GridPosition3 position, size_t ignoreIndex)
{
    for (size_t i = 0; i < state.movables.size(); ++i) {
        if (i != ignoreIndex && !state.movables[i].fallen &&
            !state.movables[i].dead &&
            state.movables[i].cell == position) {
            return true;
        }
    }

    return false;
}

bool enemyBlocksAt(
    const GameState& state,
    GridPosition3 position,
    std::optional<std::size_t> ignoredEnemy = std::nullopt)
{
    for (std::size_t i = 0; i < state.enemies.size(); ++i) {
        if ((!ignoredEnemy || i != *ignoredEnemy) &&
            !state.enemies[i].fallen && !state.enemies[i].dead &&
            state.enemies[i].cell == position) {
            return true;
        }
    }
    return false;
}

const GridPosition3& playerCell(const GameState& state, std::size_t playerIndex)
{
    return state.players.at(playerIndex).cell;
}

GridPosition3& playerCell(GameState& state, std::size_t playerIndex)
{
    return state.players.at(playerIndex).cell;
}

bool playerDead(const GameState& state, std::size_t playerIndex)
{
    return state.players.at(playerIndex).dead;
}

bool& playerDead(GameState& state, std::size_t playerIndex)
{
    return state.players.at(playerIndex).dead;
}

const std::optional<MoveDirection>& playerSliding(
    const GameState& state,
    std::size_t playerIndex)
{
    return state.players.at(playerIndex).sliding;
}

std::optional<MoveDirection>& playerSliding(
    GameState& state,
    std::size_t playerIndex)
{
    return state.players.at(playerIndex).sliding;
}

bool playerBlocksAt(
    const GameState& state,
    GridPosition3 position,
    std::optional<std::size_t> ignoredPlayer = std::nullopt)
{
    for (std::size_t i = 0; i < state.players.size(); ++i) {
        if ((!ignoredPlayer || i != *ignoredPlayer) &&
            !playerDead(state, i) && playerCell(state, i) == position) {
            return true;
        }
    }
    return false;
}

// Walks an entity down from `position` until something can hold it. The
// player and movables fall identically apart from who counts as an occupying
// blocker below, which `occupiedBelow` supplies.
template <typename OccupiedBelow>
FallResult fallTarget(
    const Level& level,
    const GameState& state,
    GridPosition3 position,
    OccupiedBelow occupiedBelow,
    bool floats = false)
{
    GridPosition3 current = position;
    // A minecart is a low platform inside its rail cell, so a unit occupying
    // that same cell is supported even when the rail is on the bottom layer.
    if (minecartPlatformAt(level, state, current)) {
        return { .cell = current, .fallen = false };
    }
    while (current.z > 0) {
        const GridPosition3 below { current.x, current.y, current.z - 1 };
        const TileType support = liveTileAt(level, state, below);

        if (tileTypeIsSolidBlock(support) || occupiedBelow(below)) {
            return { .cell = current, .fallen = false };
        }
        if (tileTypeIsWater(support)) {
            return {
                .cell = current,
                .fallen = !floats && isUnfilledWater(level, state, current),
            };
        }
        if (!cellAllowsEntity(level, state, below)) {
            return { .cell = current, .fallen = false };
        }

        current = below;
    }

    return { .cell = current, .fallen = false, .supported = false };
}

FallResult playerFallTarget(
    const Level& level,
    const GameState& state,
    std::size_t playerIndex,
    GridPosition3 position)
{
    return fallTarget(level, state, position, [&](GridPosition3 below) {
        return movableAt(state, below) != nullptr ||
            fallenMovableAt(state, below) != nullptr ||
            enemyAt(state, below) != nullptr ||
            fallenEnemyAt(state, below) != nullptr ||
            playerBlocksAt(state, below, playerIndex);
    });
}

FallResult movableFallTarget(const Level& level, const GameState& state, size_t movableIndex, GridPosition3 position)
{
    return fallTarget(level, state, position, [&](GridPosition3 below) {
        for (size_t i = 0; i < state.movables.size(); ++i) {
            if (i != movableIndex && !state.movables[i].dead &&
                state.movables[i].cell == below) {
                return true;
            }
        }
        return playerBlocksAt(state, below) || enemyBlocksAt(state, below);
    }, state.movables[movableIndex].type == TileType::LilyPad);
}

FallResult enemyFallTarget(
    const Level& level,
    const GameState& state,
    std::size_t enemyIndex,
    GridPosition3 position)
{
    return fallTarget(level, state, position, [&](GridPosition3 below) {
        for (std::size_t i = 0; i < state.enemies.size(); ++i) {
            if (i != enemyIndex && !state.enemies[i].dead &&
                state.enemies[i].cell == below) {
                return true;
            }
        }
        return movableAt(state, below) != nullptr ||
            fallenMovableAt(state, below) != nullptr ||
            playerBlocksAt(state, below);
    });
}

void resolveEnemyAttacks(GameState& state)
{
    for (std::size_t playerIndex = 0;
         playerIndex < state.players.size();
         ++playerIndex) {
        GameState::Player& player = state.players[playerIndex];
        if (player.dead) {
            continue;
        }
        const bool threatened = std::ranges::any_of(
            state.enemies,
            [&](const GameState::Enemy& enemy) {
                if (enemy.fallen || enemy.dead ||
                    enemy.cell.z != player.cell.z) {
                    return false;
                }
                return std::abs(enemy.cell.x - player.cell.x) +
                        std::abs(enemy.cell.y - player.cell.y) ==
                    1;
            });
        if (threatened) {
            player.dead = true;
            player.drowned = false;
            player.sliding.reset();
        }
    }
}

std::optional<GridPosition3> ladderClimbTarget(
    const Level& level,
    const GameState& state,
    GridPosition3 ladderCell,
    GridPosition3 groundCell)
{
    if (ladderCell.z != groundCell.z ||
        !level.inBounds(ladderCell) ||
        !level.inBounds(groundCell)) {
        return std::nullopt;
    }

    if (tileAt(level, ladderCell) != TileType::Ladder ||
        !tileTypeIsGround(tileAt(level, groundCell))) {
        return std::nullopt;
    }

    const GridPosition3 topCell {
        groundCell.x,
        groundCell.y,
        groundCell.z + 1,
    };
    if (!cellAllowsEntity(level, state, topCell)) {
        return std::nullopt;
    }
    if (movableAt(state, topCell) != nullptr) {
        return std::nullopt;
    }
    if (playerBlocksAt(state, topCell)) {
        return std::nullopt;
    }
    if (enemyAt(state, topCell) != nullptr) {
        return std::nullopt;
    }

    return topCell;
}

std::optional<GridPosition3> playerLadderClimbTarget(
    const Level& level,
    const GameState& state,
    std::size_t playerIndex,
    MoveDirection direction)
{
    const GridPosition3 current = playerCell(state, playerIndex);
    const GridPosition3 flatTarget = movementTarget(current, direction);
    if (!level.inBounds(flatTarget) || !level.inBounds(current)) {
        return std::nullopt;
    }

    if (tileAt(level, current) == TileType::Ladder) {
        return ladderClimbTarget(level, state, current, flatTarget);
    }

    return std::nullopt;
}

} // namespace

GameState initialState(const Level& level)
{
    GameState state;
    EntityId nextId = 1;
    state.players.reserve(level.playerStarts().size());
    for (const Level::PlayerStart& start : level.playerStarts()) {
        const EntityId id = nextId++;
        state.players.push_back({
            .id = id,
            .cell = start.position,
            .character = start.character,
            .controller = id,
            .quarterTurns = static_cast<uint8_t>(level.tileRotationQuarterTurns(start.position)),
        });
    }
    state.movables.reserve(level.movableTiles().size());
    for (const Level::MovableTile& movable : level.movableTiles()) {
        GameState::Movable entry;
        entry.id = nextId++;
        entry.type = movable.type;
        entry.cell = movable.position;
        entry.quarterTurns = static_cast<uint8_t>(level.tileRotationQuarterTurns(movable.position));
        state.movables.push_back(entry);
    }
    state.enemies.reserve(level.enemyStarts().size());
    for (GridPosition3 position : level.enemyStarts()) {
        state.enemies.push_back({ .id = nextId++, .cell = position,
            .quarterTurns = static_cast<uint8_t>(level.tileRotationQuarterTurns(position)) });
    }
    state.elevators.reserve(level.elevators().size());
    for (const Level::Elevator& elevator : level.elevators()) {
        state.elevators.push_back({
            .cell = elevator.cell,
            .phase = elevatorInitialPhase(elevator),
        });
    }
    state.minecarts.reserve(level.minecarts().size());
    for (const Level::Minecart& minecart : level.minecarts()) {
        state.minecarts.push_back({ .cell = minecart.cell, .phase = 0 });
    }
    state.wardrobes.reserve(level.wardrobes().size());
    for (const Level::Wardrobe& wardrobe : level.wardrobes()) {
        state.wardrobes.push_back({
            .cell = wardrobe.cell,
            .character = wardrobe.character,
        });
    }

    return state;
}

bool anyPlayerDead(const GameState& state)
{
    return std::ranges::any_of(
        state.players,
        [](const GameState::Player& player) { return player.dead; });
}

EntityId playerControllerId(const GameState& state, std::size_t playerIndex)
{
    const GameState::Player& player = state.players.at(playerIndex);
    return player.controller != invalidEntityId
        ? player.controller
        : resolvedEntityId(EntityKind::Player, player.id, playerIndex);
}

GridPosition directionOffset(MoveDirection direction)
{
    switch (direction) {
    case MoveDirection::Up:
        return { 0, -1 };
    case MoveDirection::Down:
        return { 0, 1 };
    case MoveDirection::Left:
        return { -1, 0 };
    case MoveDirection::Right:
        return { 1, 0 };
    }
    return {};
}

GridPosition3 movementTarget(GridPosition3 origin, MoveDirection direction)
{
    const GridPosition offset = directionOffset(direction);
    return {
        origin.x + offset.x,
        origin.y + offset.y,
        origin.z,
    };
}

std::optional<MoveDirection> conveyorDirectionForTile(TileType tile)
{
    switch (tile) {
    case TileType::ConveyorUp:
        return MoveDirection::Up;
    case TileType::ConveyorDown:
        return MoveDirection::Down;
    case TileType::ConveyorRight:
        return MoveDirection::Right;
    case TileType::ConveyorLeft:
        return MoveDirection::Left;
    default:
        return std::nullopt;
    }
}

std::optional<MoveDirection> conveyorDirectionAt(const Level& level, GridPosition3 position)
{
    if (!level.inBounds(position)) {
        return std::nullopt;
    }

    if (const auto belt = conveyorDirectionForTile(tileAt(level, position))) {
        return belt;
    }
    return waterStreamDirectionAt(level, position);
}

std::optional<MoveDirection> waterStreamDirectionForTile(TileType tile)
{
    switch (tile) {
    case TileType::StreamUp: return MoveDirection::Up;
    case TileType::StreamRight: return MoveDirection::Right;
    case TileType::StreamDown: return MoveDirection::Down;
    case TileType::StreamLeft: return MoveDirection::Left;
    default: return std::nullopt;
    }
}

std::optional<MoveDirection> conveyorDirectionAt(
    const Level& level, const GameState& state, GridPosition3 position)
{
    if (!level.inBounds(position)) {
        return std::nullopt;
    }
    if (const auto belt = conveyorDirectionForTile(tileAt(level, position))) {
        return belt;
    }
    return fallenMovableAt(state, position) == nullptr
        ? waterStreamDirectionAt(level, position) : std::nullopt;
}

std::optional<MoveDirection> waterStreamDirectionAt(const Level& level, GridPosition3 position)
{
    if (!level.inBounds(position)) {
        return std::nullopt;
    }
    const auto support = level.supportingTileAt(position);
    return support ? waterStreamDirectionForTile(*support) : std::nullopt;
}

std::optional<MoveDirection> turretDirectionForTile(TileType tile)
{
    switch (tile) {
    case TileType::TurretNorth:
        return MoveDirection::Up;
    case TileType::TurretEast:
        return MoveDirection::Right;
    case TileType::TurretSouth:
        return MoveDirection::Down;
    case TileType::TurretWest:
        return MoveDirection::Left;
    default:
        return std::nullopt;
    }
}

std::optional<MoveDirection> robotDirectionForTile(TileType tile)
{
    switch (tile) {
    case TileType::RobotNorth: return MoveDirection::Up;
    case TileType::RobotEast: return MoveDirection::Right;
    case TileType::RobotSouth: return MoveDirection::Down;
    case TileType::RobotWest: return MoveDirection::Left;
    default: return std::nullopt;
    }
}

std::optional<MoveDirection> robotDirection(const GameState::Movable& movable)
{
    const auto authored = robotDirectionForTile(movable.type);
    return authored ? std::optional(rotateDirection(*authored, movable.quarterTurns))
                    : std::nullopt;
}

MoveDirection rotateDirection(MoveDirection direction, int quarterTurns)
{
    // Clockwise order seen from above: north, east, south, west.
    constexpr std::array clockwise {
        MoveDirection::Up,
        MoveDirection::Right,
        MoveDirection::Down,
        MoveDirection::Left,
    };
    const auto found = std::ranges::find(clockwise, direction);
    const int index = static_cast<int>(found - clockwise.begin());
    const int turned = ((index + quarterTurns) % 4 + 4) % 4;
    return clockwise[static_cast<std::size_t>(turned)];
}

uint8_t addQuarterTurns(uint8_t quarterTurns, int delta)
{
    return static_cast<uint8_t>(
        ((static_cast<int>(quarterTurns) + delta) % 4 + 4) % 4);
}

std::optional<MoveDirection> turretDirection(const GameState::Movable& movable)
{
    const std::optional<MoveDirection> authored =
        turretDirectionForTile(movable.type);
    if (!authored) {
        return std::nullopt;
    }
    return rotateDirection(*authored, movable.quarterTurns);
}

TileType rotateMirrorTile(TileType tile, int quarterTurns)
{
    // Clockwise order: the corner a mirror faces turns NW, NE, SE, SW.
    constexpr std::array clockwise {
        TileType::MirrorNorthWest,
        TileType::MirrorNorthEast,
        TileType::MirrorSouthEast,
        TileType::MirrorSouthWest,
    };
    const auto found = std::ranges::find(clockwise, tile);
    if (found == clockwise.end()) {
        return tile;
    }
    const int index = static_cast<int>(found - clockwise.begin());
    return clockwise[static_cast<std::size_t>(
        ((index + quarterTurns) % 4 + 4) % 4)];
}

uint8_t mirrorQuarterTurnsAt(const GameState& state, GridPosition3 cell)
{
    if (const GameState::Movable* mirror = movableAt(state, cell);
        mirror && tileTypeIsMirror(mirror->type)) {
        return mirror->quarterTurns;
    }
    const auto found = std::ranges::find(
        state.turnedMirrors, cell, &GameState::TurnedMirror::cell);
    return found == state.turnedMirrors.end() ? uint8_t { 0 } : found->quarterTurns;
}

void setMirrorQuarterTurns(
    GameState& state, GridPosition3 cell, uint8_t quarterTurns)
{
    for (GameState::Movable& mirror : state.movables) {
        if (!mirror.fallen && !mirror.dead && mirror.cell == cell &&
            tileTypeIsMirror(mirror.type)) {
            mirror.quarterTurns = addQuarterTurns(quarterTurns, 0);
            return;
        }
    }
    std::erase_if(state.turnedMirrors, [&](const GameState::TurnedMirror& mirror) {
        return mirror.cell == cell;
    });
    quarterTurns = addQuarterTurns(quarterTurns, 0);
    if (quarterTurns == 0) {
        return;
    }
    const auto order = [](GridPosition3 position) {
        return std::array { position.z, position.y, position.x };
    };
    const auto insertAt = std::ranges::upper_bound(
        state.turnedMirrors,
        order(cell),
        {},
        [&](const GameState::TurnedMirror& mirror) { return order(mirror.cell); });
    state.turnedMirrors.insert(insertAt, { .cell = cell, .quarterTurns = quarterTurns });
}

std::optional<TileType> mirrorTileAt(
    const Level&, const GameState& state, GridPosition3 cell)
{
    if (const GameState::Movable* mirror = movableAt(state, cell);
        mirror && tileTypeIsMirror(mirror->type)) {
        return rotateMirrorTile(mirror->type, mirror->quarterTurns);
    }
    return std::nullopt;
}

std::size_t elevatorStopIndex(std::size_t stopCount, uint8_t phase)
{
    if (stopCount <= 1) {
        return 0;
    }
    const std::size_t period = 2 * (stopCount - 1);
    const std::size_t wrapped = static_cast<std::size_t>(phase) % period;
    return wrapped < stopCount ? wrapped : period - wrapped;
}

uint8_t elevatorNextPhase(std::size_t stopCount, uint8_t phase)
{
    if (stopCount <= 1) {
        return 0;
    }
    const std::size_t period = 2 * (stopCount - 1);
    return static_cast<uint8_t>(
        (static_cast<std::size_t>(phase) % period + 1) % period);
}

uint8_t elevatorInitialPhase(const Level::Elevator& elevator)
{
    return static_cast<uint8_t>(elevator.startStop());
}

GridPosition3 elevatorPlatformCell(
    const Level& level, const GameState& state, std::size_t index)
{
    return index < state.elevators.size()
        ? state.elevators[index].cell
        : level.elevators().at(index).cell;
}

std::optional<std::size_t> elevatorPlatformAt(
    const Level& level, const GameState& state, GridPosition3 cell)
{
    for (std::size_t i = 0; i < level.elevators().size(); ++i) {
        if (elevatorPlatformCell(level, state, i) == cell) {
            return i;
        }
    }
    return std::nullopt;
}

std::size_t minecartStopIndex(
    const Level::MinecartRoute& route, uint16_t phase)
{
    const std::size_t count = route.stops.size();
    if (count <= 1) {
        return 0;
    }
    if (route.loop) {
        return static_cast<std::size_t>(phase) % count;
    }
    const std::size_t period = 2 * (count - 1);
    const std::size_t wrapped = static_cast<std::size_t>(phase) % period;
    return wrapped < count ? wrapped : period - wrapped;
}

uint16_t minecartNextPhase(
    const Level::MinecartRoute& route, uint16_t phase)
{
    const std::size_t count = route.stops.size();
    if (count <= 1) {
        return 0;
    }
    const std::size_t period = route.loop ? count : 2 * (count - 1);
    return static_cast<uint16_t>(
        (static_cast<std::size_t>(phase) % period + 1) % period);
}

GridPosition3 minecartPlatformCell(
    const Level& level, const GameState& state, std::size_t index)
{
    return index < state.minecarts.size()
        ? state.minecarts[index].cell
        : level.minecarts().at(index).cell;
}

std::optional<std::size_t> minecartPlatformAt(
    const Level& level, const GameState& state, GridPosition3 cell)
{
    for (std::size_t i = 0; i < level.minecarts().size(); ++i) {
        if (minecartPlatformCell(level, state, i) == cell) {
            return i;
        }
    }
    return std::nullopt;
}

TileType liveTileAt(
    const Level& level, const GameState& state, GridPosition3 position)
{
    if (!level.inBounds(position)) {
        return TileType::Air;
    }
    const TileType tile = tileAt(level, position);
    if (level.elevators().empty() && level.minecarts().empty()) {
        return tile;
    }
    if (elevatorPlatformAt(level, state, position)) {
        return TileType::Elevator;
    }
    if (minecartPlatformAt(level, state, position)) {
        return TileType::Minecart;
    }
    if (tile == TileType::Elevator) {
        return TileType::Air;
    }
    if (tile == TileType::Minecart) {
        return level.plateAt(position).value_or(TileType::Air);
    }
    return tile;
}

bool staticCellAllowsEntity(const Level& level, GridPosition3 position)
{
    if (position.x < 0 ||
        position.y < 0 ||
        position.z < 0 ||
        position.x >= static_cast<int>(level.width()) ||
        position.y >= static_cast<int>(level.height()) ||
        position.z > static_cast<int>(level.depth())) {
        return false;
    }
    if (position.z == static_cast<int>(level.depth())) {
        return true;
    }

    const TileType tile = tileAt(level, position);
    return tileTypeAllowsEntity(tile) || tile == TileType::Gate ||
        tile == TileType::Elevator || tile == TileType::Minecart;
}

bool cellAllowsEntity(
    const Level& level,
    const GameState& state,
    GridPosition3 position)
{
    // A cart at the gate does not grant entry to units walking or being
    // pushed in from outside. Carried characters are admitted by the sweep.
    if (level.inBounds(position) &&
        tileAt(level, position) == TileType::MinecartGate) {
        return false;
    }
    if ((!level.elevators().empty() || !level.minecarts().empty()) &&
        level.inBounds(position)) {
        if (elevatorPlatformAt(level, state, position)) {
            return false;
        }
        if (tileAt(level, position) == TileType::Elevator) {
            // The authored cell, while the platform is somewhere else.
            return true;
        }
        if (minecartPlatformAt(level, state, position)) {
            // Unlike the full-height elevator block, the cart is a platform
            // within the rail cell and may be occupied by a unit.
            return true;
        }
        if (tileAt(level, position) == TileType::Minecart) {
            // Reveal the stop rail when the cart has moved away.
            return true;
        }
    }
    if (!level.inBounds(position) ||
        tileAt(level, position) != TileType::Gate) {
        return staticCellAllowsEntity(level, position);
    }
    const Level::Gate* gate = level.gateAt(position);
    return gate != nullptr && isGateOpen(level, state, *gate);
}

const GameState::Movable* movableAt(const GameState& state, GridPosition3 position)
{
    const auto movable = std::ranges::find_if(state.movables, [position](const GameState::Movable& candidate) {
        return !candidate.fallen && !candidate.dead &&
            candidate.cell == position;
    });

    return movable != state.movables.end() ? &*movable : nullptr;
}

const GameState::Movable* fallenMovableAt(const GameState& state, GridPosition3 position)
{
    const auto movable = std::ranges::find_if(state.movables, [position](const GameState::Movable& candidate) {
        return candidate.fallen && !candidate.dead &&
            candidate.cell == position;
    });

    return movable != state.movables.end() ? &*movable : nullptr;
}

const GameState::Enemy* enemyAt(const GameState& state, GridPosition3 position)
{
    const auto enemy = std::ranges::find_if(
        state.enemies,
        [position](const GameState::Enemy& candidate) {
            return !candidate.fallen && !candidate.dead &&
                candidate.cell == position;
        });
    return enemy != state.enemies.end() ? &*enemy : nullptr;
}

const GameState::Enemy* fallenEnemyAt(const GameState& state, GridPosition3 position)
{
    const auto enemy = std::ranges::find_if(
        state.enemies,
        [position](const GameState::Enemy& candidate) {
            return candidate.fallen && !candidate.dead &&
                candidate.cell == position;
        });
    return enemy != state.enemies.end() ? &*enemy : nullptr;
}

bool isUnfilledWater(const Level& level, const GameState& state, GridPosition3 position)
{
    if (!level.inBounds(position)) {
        return false;
    }

    const auto support = level.supportingTileAt(position);
    return support && tileTypeIsWater(*support) &&
        fallenMovableAt(state, position) == nullptr;
}

bool lilyPadDrivenByRock(const Level& level,
    const GameState& before, const GameState& after, std::size_t padIndex)
{
    if (padIndex >= before.movables.size() || padIndex >= after.movables.size()) {
        return false;
    }
    const auto& fromPad = before.movables[padIndex];
    const auto& toPad = after.movables[padIndex];
    if (fromPad.type != TileType::LilyPad || toPad.type != TileType::LilyPad ||
        fromPad.dead || fromPad.fallen || toPad.dead || toPad.fallen) {
        return false;
    }
    const bool moved = fromPad.cell != toPad.cell;
    const bool stopped = fromPad.sliding && !toPad.sliding;
    if (isUnfilledWater(level, before, fromPad.cell) &&
        ((moved && (fromPad.sliding || toPad.sliding)) || (!moved && stopped))) {
        return true;
    }
    if (!moved) return false;

    // Keep entry and exit directions distinct for a shove crossing a portal.
    const auto directions = [&](GridPosition3 from, GridPosition3 to)
        -> std::optional<std::pair<MoveDirection, MoveDirection>> {
        if (from.x == to.x && from.y == to.y) return std::nullopt;
        for (const auto direction : { MoveDirection::Up, MoveDirection::Down,
                 MoveDirection::Left, MoveDirection::Right }) {
            if (const auto portal = level.portalCrossing(from, directionOffset(direction));
                portal && portal->exit.x == to.x && portal->exit.y == to.y) {
                return std::pair { direction, rotateDirection(direction, portal->quarterTurns) };
            }
        }
        if (from.x == to.x) {
            const auto direction = to.y < from.y ? MoveDirection::Up : MoveDirection::Down;
            return std::pair { direction, direction };
        }
        if (from.y == to.y) {
            const auto direction = to.x < from.x ? MoveDirection::Left : MoveDirection::Right;
            return std::pair { direction, direction };
        }
        return std::nullopt;
    };
    const auto padDirections = directions(fromPad.cell, toPad.cell);
    if (!padDirections) return false;
    const auto movedRock = [&](std::size_t index, MoveDirection expectedDirection) {
        if (index >= after.movables.size()) return false;
        const auto& rock = before.movables[index];
        if (rock.dead || rock.fallen || after.movables[index].dead ||
            (rock.type != TileType::Rock && rock.type != TileType::RoundRock)) {
            return false;
        }
        const auto movement = directions(rock.cell, after.movables[index].cell);
        return movement && movement->second == expectedDirection;
    };
    for (std::size_t index = 0; index < before.movables.size(); ++index) {
        if (movedRock(index, padDirections->second) &&
            level.movablesAreLinked(index, padIndex)) return true;
    }

    // A Knight chain or a Bard shove can transmit a rock's force through
    // intervening blocks and enemies. Require every intervening unit to move.
    auto cell = movementTarget(fromPad.cell, oppositeDirection(padDirections->first));
    while (level.inBounds(cell)) {
        if (const auto* movable = movableAt(before, cell)) {
            const auto index = static_cast<std::size_t>(movable - before.movables.data());
            if (movedRock(index, padDirections->first)) return true;
            if (index >= after.movables.size()) return false;
            const auto movement = directions(movable->cell, after.movables[index].cell);
            if (!movement || movement->second != padDirections->first) return false;
        } else if (const auto* enemy = enemyAt(before, cell)) {
            const auto index = static_cast<std::size_t>(enemy - before.enemies.data());
            if (index >= after.enemies.size()) return false;
            const auto movement = directions(enemy->cell, after.enemies[index].cell);
            if (!movement || movement->second != padDirections->first) return false;
        } else {
            return false;
        }
        cell = movementTarget(cell, oppositeDirection(padDirections->first));
    }
    return false;
}

bool isPressurePlateActive(
    const Level& level,
    const GameState& state,
    GridPosition3 plate)
{
    if (tileTypeIsButton(level.plateAt(plate).value_or(TileType::Air))) {
        return std::ranges::find(state.activeButtons, plate) !=
            state.activeButtons.end();
    }
    if (tileTypeIsLever(level.plateAt(plate).value_or(TileType::Air))) {
        return std::ranges::find(state.activeLevers, plate) !=
            state.activeLevers.end();
    }
    return playerBlocksAt(state, plate) ||
        movableAt(state, plate) != nullptr ||
        enemyAt(state, plate) != nullptr;
}

bool pressurePlateActivityChanged(
    const Level& level,
    const GameState& before,
    const GameState& after)
{
    for (const GridPosition3 plate : level.pressurePlates()) {
        if (level.plateAt(plate).value_or(TileType::Air) ==
                TileType::PressurePlate &&
            isPressurePlateActive(level, before, plate) !=
                isPressurePlateActive(level, after, plate)) {
            return true;
        }
    }
    return false;
}

std::vector<GridPosition3> activatableButtons(
    const Level& level, const GameState& state)
{
    std::vector<GridPosition3> buttons;
    // Level order makes pulse state canonical regardless of hero vector order.
    for (const GridPosition3 cell : level.pressurePlates()) {
        if (tileTypeIsButton(level.plateAt(cell).value_or(TileType::Air)) &&
            playerBlocksAt(state, cell) && !isUnitLocked(level, state, cell)) {
            buttons.push_back(cell);
        }
    }
    return buttons;
}

std::vector<GridPosition3> activatableLevers(
    const Level& level, const GameState& state)
{
    std::vector<GridPosition3> levers;
    for (const GridPosition3 cell : level.pressurePlates()) {
        if (tileTypeIsLever(level.plateAt(cell).value_or(TileType::Air)) &&
            playerBlocksAt(state, cell) && !isUnitLocked(level, state, cell)) {
            levers.push_back(cell);
        }
    }
    return levers;
}

bool isGateOpen(
    const Level& level,
    const GameState& state,
    const Level::Gate& gate)
{
    // A closed gate is a solid block. Rocks and ice blocks cannot be
    // destroyed, so one standing in the cell holds the gate open until it
    // leaves. Heroes, enemies and turrets can die: the gate closes on them
    // and kills them (see applyGateChanges).
    if (const GameState::Movable* movable = movableAt(state, gate.cell);
        movable != nullptr && !tileTypeIsTurret(movable->type)) {
        return true;
    }
    const bool activated = !gate.pressurePlates.empty() &&
        std::ranges::all_of(
            gate.pressurePlates,
            [&](GridPosition3 plate) {
                return isPressurePlateActive(level, state, plate);
            });
    return activated != gate.startOpen;
}

bool isLockPlateEnabled(
    const Level& level, const GameState& state, const Level::LockPlate& plate)
{
    const bool pressed = !plate.pressurePlates.empty() &&
        std::ranges::all_of(plate.pressurePlates, [&](GridPosition3 cell) {
            return isPressurePlateActive(level, state, cell);
        });
    return pressed != plate.startEnabled;
}

bool isUnitLocked(const Level& level, const GameState& state, GridPosition3 cell)
{
    const auto* plate = level.lockPlateAt(cell);
    return plate && isLockPlateEnabled(level, state, *plate);
}

bool isRotatorEngaged(
    const Level& level,
    const GameState& state,
    const Level::Rotator& rotator)
{
    return !rotator.pressurePlates.empty() &&
        std::ranges::all_of(
            rotator.pressurePlates,
            [&](GridPosition3 plate) {
                return isPressurePlateActive(level, state, plate);
            });
}

bool isElevatorEngaged(
    const Level& level,
    const GameState& state,
    const Level::Elevator& elevator)
{
    return !elevator.pressurePlates.empty() &&
        std::ranges::all_of(
            elevator.pressurePlates,
            [&](GridPosition3 plate) {
                return isPressurePlateActive(level, state, plate);
            });
}

bool isMinecartEngaged(
    const Level& level,
    const GameState& state,
    const Level::Minecart& minecart)
{
    return !minecart.pressurePlates.empty() &&
        std::ranges::all_of(
            minecart.pressurePlates,
            [&](GridPosition3 plate) {
                return isPressurePlateActive(level, state, plate);
            });
}

namespace {

// Per-rotator engagement, in Level::rotators() order.
std::vector<char> rotatorEngagement(const Level& level, const GameState& state)
{
    std::vector<char> engaged(level.rotators().size(), 0);
    for (std::size_t i = 0; i < engaged.size(); ++i) {
        engaged[i] = static_cast<char>(
            isRotatorEngaged(level, state, level.rotators()[i]));
    }
    return engaged;
}

// Which state entities a rotator activation turned.
struct RotatedOccupants {
    std::vector<std::size_t> players;
    std::vector<std::size_t> movables;
    std::vector<std::size_t> enemies;

    [[nodiscard]] bool empty() const
    {
        return players.empty() && movables.empty() && enemies.empty();
    }
};

// Turns the live occupant of every rotator whose linked plates became fully
// pressed since `engagedBefore` was sampled. A rotator is a floor tile, so at
// most one live unit stands on it; the loops below do not rely on that.
RotatedOccupants applyRotatorActivations(
    const Level& level,
    GameState& state,
    const std::vector<char>& engagedBefore)
{
    RotatedOccupants rotated;
    for (std::size_t r = 0; r < level.rotators().size(); ++r) {
        const Level::Rotator& rotator = level.rotators()[r];
        if ((r < engagedBefore.size() && engagedBefore[r]) ||
            !isRotatorEngaged(level, state, rotator) ||
            !level.inBounds(rotator.cell)) {
            continue;
        }
        // The rotator may be covered by a mirror, so ask for the plate.
        const std::optional<int> turns = rotatorQuarterTurns(
            level.plateAt(rotator.cell).value_or(TileType::Air));
        if (!turns) {
            continue;
        }
        for (std::size_t i = 0; i < state.players.size(); ++i) {
            GameState::Player& player = state.players[i];
            if (!player.dead && player.cell == rotator.cell) {
                player.quarterTurns = addQuarterTurns(player.quarterTurns, *turns);
                rotated.players.push_back(i);
            }
        }
        for (std::size_t i = 0; i < state.movables.size(); ++i) {
            GameState::Movable& movable = state.movables[i];
            if (!movable.fallen && !movable.dead &&
                movable.cell == rotator.cell) {
                movable.quarterTurns = addQuarterTurns(movable.quarterTurns, *turns);
                rotated.movables.push_back(i);
            }
        }
        for (std::size_t i = 0; i < state.enemies.size(); ++i) {
            GameState::Enemy& enemy = state.enemies[i];
            if (!enemy.fallen && !enemy.dead && enemy.cell == rotator.cell) {
                enemy.quarterTurns = addQuarterTurns(enemy.quarterTurns, *turns);
                rotated.enemies.push_back(i);
            }
        }
    }
    return rotated;
}

// Per-elevator engagement, in Level::elevators() order.
std::vector<char> elevatorEngagement(const Level& level, const GameState& state)
{
    std::vector<char> engaged(level.elevators().size(), 0);
    for (std::size_t i = 0; i < engaged.size(); ++i) {
        engaged[i] = static_cast<char>(
            isElevatorEngaged(level, state, level.elevators()[i]));
    }
    return engaged;
}

std::vector<char> minecartEngagement(const Level& level, const GameState& state)
{
    std::vector<char> engaged(level.minecarts().size(), 0);
    for (std::size_t i = 0; i < engaged.size(); ++i) {
        engaged[i] = static_cast<char>(
            isMinecartEngaged(level, state, level.minecarts()[i]));
    }
    return engaged;
}

// Which state entities elevator activations carried.
struct ElevatorRiders {
    std::vector<std::size_t> players;
    std::vector<std::size_t> movables;
    std::vector<std::size_t> enemies;
    std::vector<std::size_t> elevators;

    [[nodiscard]] bool empty() const { return elevators.empty(); }
};

// One live unit standing in `cell`, as a state index. Units never share a
// cell, so the first match is the only one.
struct LiveUnitAt {
    EntityKind kind = EntityKind::Player;
    std::size_t index = 0;
};

GridPosition3 unitCell(const GameState& state, LiveUnitAt unit)
{
    switch (unit.kind) {
    case EntityKind::Player: return state.players[unit.index].cell;
    case EntityKind::Movable: return state.movables[unit.index].cell;
    case EntityKind::Enemy: return state.enemies[unit.index].cell;
    default: return {};
    }
}

std::optional<LiveUnitAt> liveUnitAt(const GameState& state, GridPosition3 cell)
{
    for (std::size_t i = 0; i < state.players.size(); ++i) {
        if (!state.players[i].dead && state.players[i].cell == cell) {
            return LiveUnitAt { EntityKind::Player, i };
        }
    }
    for (std::size_t i = 0; i < state.movables.size(); ++i) {
        const GameState::Movable& movable = state.movables[i];
        if (!movable.fallen && !movable.dead && movable.cell == cell) {
            return LiveUnitAt { EntityKind::Movable, i };
        }
    }
    for (std::size_t i = 0; i < state.enemies.size(); ++i) {
        const GameState::Enemy& enemy = state.enemies[i];
        if (!enemy.fallen && !enemy.dead && enemy.cell == cell) {
            return LiveUnitAt { EntityKind::Enemy, i };
        }
    }
    return std::nullopt;
}

// Anything at all in `cell`, including fallen units filling water. A cell an
// elevator moves through must hold none of it.
bool anyUnitIn(const GameState& state, GridPosition3 cell)
{
    return liveUnitAt(state, cell).has_value() ||
        fallenMovableAt(state, cell) != nullptr ||
        fallenEnemyAt(state, cell) != nullptr;
}

// A movable platform supports every consecutive live unit above it. Each unit
// rides along, including stacks of blocks with a hero on top.
std::vector<LiveUnitAt> platformRiders(const GameState& state, std::size_t index)
{
    std::vector<LiveUnitAt> riders;
    if (!tileTypeIsMovablePlatform(state.movables[index].type)) {
        return riders;
    }
    GridPosition3 cell = state.movables[index].cell;
    for (++cell.z; ; ++cell.z) {
        const auto rider = liveUnitAt(state, cell);
        if (!rider) {
            return riders;
        }
        riders.push_back(*rider);
    }
}

void setUnitCell(GameState& state, LiveUnitAt unit, GridPosition3 cell)
{
    switch (unit.kind) {
    case EntityKind::Player: state.players[unit.index].cell = cell; break;
    case EntityKind::Movable: state.movables[unit.index].cell = cell; break;
    case EntityKind::Enemy: state.enemies[unit.index].cell = cell; break;
    default: break;
    }
}

bool platformRidersCanMove(
    const Level& level, const GameState& state, std::size_t index,
    GridPosition3 landing, int entryZ = -1)
{
    const auto riders = platformRiders(state, index);
    const GridPosition3 from = state.movables[index].cell;
    for (const LiveUnitAt rider : riders) {
        const GridPosition3 cell = unitCell(state, rider);
        if (isUnitLocked(level, state, cell)) {
            return false;
        }
        // A forced shove can drop the robot after entering the next column.
        // Check the rider's entry height and the entire fall, so an overhang
        // cannot be crossed merely because the final landing has headroom.
        for (int z = std::max(landing.z, entryZ); z >= landing.z; --z) {
            const GridPosition3 target { landing.x, landing.y, z + cell.z - from.z };
            if (!cellAllowsEntity(level, state, target)) {
                return false;
            }
            if (const auto occupant = liveUnitAt(state, target)) {
                const bool inStack = std::ranges::any_of(riders, [&](LiveUnitAt other) {
                    return occupant->kind == other.kind && occupant->index == other.index;
                });
                if (!inStack && !(occupant->kind == EntityKind::Movable &&
                        occupant->index == index)) {
                    return false;
                }
            }
            if (fallenMovableAt(state, target) || fallenEnemyAt(state, target)) {
                return false;
            }
        }
    }
    return true;
}

// Per-gate open state, in Level::gates() order.
std::vector<char> gateOpenness(const Level& level, const GameState& state)
{
    std::vector<char> open(level.gates().size(), 0);
    for (std::size_t i = 0; i < open.size(); ++i) {
        open[i] = static_cast<char>(isGateOpen(level, state, level.gates()[i]));
    }
    return open;
}

// State indices of units, by kind.
struct UnitIndices {
    std::vector<std::size_t> players;
    std::vector<std::size_t> movables;
    std::vector<std::size_t> enemies;

    [[nodiscard]] bool empty() const
    {
        return players.empty() && movables.empty() && enemies.empty();
    }

    void add(LiveUnitAt unit)
    {
        std::vector<std::size_t>& into = unit.kind == EntityKind::Player
            ? players
            : unit.kind == EntityKind::Movable ? movables
                                               : enemies;
        if (std::ranges::find(into, unit.index) == into.end()) {
            into.push_back(unit.index);
        }
    }
};

// What gates opening and closing did to the units around them.
struct GateChanges {
    // Fell through a gate that opened beneath them.
    UnitIndices dropped;
    // Killed by a gate that closed on them.
    UnitIndices crushed;

    [[nodiscard]] bool empty() const
    {
        return dropped.empty() && crushed.empty();
    }
};

// Lets one live unit fall from where it stands, as though it had just
// stepped there. Returns false when it stays put: something still holds it,
// or nothing below can (an all-air column), the same case in which a move is
// rejected rather than leaving a unit standing on nothing.
bool dropUnit(const Level& level, GameState& state, LiveUnitAt unit)
{
    if (isUnitLocked(level, state, unitCell(state, unit))) {
        return false;
    }
    switch (unit.kind) {
    case EntityKind::Player: {
        GameState::Player& player = state.players[unit.index];
        const FallResult fall =
            playerFallTarget(level, state, unit.index, player.cell);
        if (!fall.supported || (fall.cell == player.cell && !fall.fallen)) {
            return false;
        }
        player.cell = fall.cell;
        player.dead = fall.fallen;
        player.drowned = fall.fallen;
        player.sliding.reset();
        return true;
    }
    case EntityKind::Movable: {
        GameState::Movable& movable = state.movables[unit.index];
        const FallResult fall =
            movableFallTarget(level, state, unit.index, movable.cell);
        if (!fall.supported || (fall.cell == movable.cell && !fall.fallen)) {
            return false;
        }
        movable.cell = fall.cell;
        movable.fallen = fall.fallen;
        movable.sliding.reset();
        return true;
    }
    case EntityKind::Enemy: {
        GameState::Enemy& enemy = state.enemies[unit.index];
        const FallResult fall =
            enemyFallTarget(level, state, unit.index, enemy.cell);
        if (!fall.supported || (fall.cell == enemy.cell && !fall.fallen)) {
            return false;
        }
        enemy.cell = fall.cell;
        enemy.fallen = fall.fallen;
        enemy.sliding.reset();
        return true;
    }
    default:
        return false;
    }
}

// Kills a hero, enemy or turret a gate has closed on. The body stays where
// it is: a dead hero is left on the board like one an enemy struck, and dead
// enemies and turrets stop blocking and rendering.
void crushUnit(GameState& state, LiveUnitAt unit)
{
    switch (unit.kind) {
    case EntityKind::Player:
        state.players[unit.index].dead = true;
        state.players[unit.index].sliding.reset();
        break;
    case EntityKind::Movable:
        state.movables[unit.index].dead = true;
        state.movables[unit.index].sliding.reset();
        break;
    case EntityKind::Enemy:
        state.enemies[unit.index].dead = true;
        state.enemies[unit.index].sliding.reset();
        break;
    default:
        break;
    }
}

// A closed gate is a solid block and an open gate is empty space.
//
// A gate that opens drops the column of live units resting on top of it,
// bottom first so each lands on the one beneath it. `openBefore` holds each
// gate's state when it was last looked at and is brought up to date; only a
// gate that was closed then and is open now drops anything (the rotator edge
// rule), so a unit that a bard holds over an already-open gate keeps floating
// exactly as it would over any other gap.
//
// A closed gate with a live unit in its cell has just closed on it. Only
// heroes, enemies and turrets can be there (a rock or ice block holds its
// gate open, see isGateOpen), and the gate kills them.
//
// Landings and deaths can press or release further plates, so this repeats
// until nothing changes.
GateChanges applyGateChanges(
    const Level& level,
    GameState& state,
    std::vector<char>& openBefore,
    const PressurePlateActivityTracker& plateActivity)
{
    plateActivity.observe(state);
    GateChanges changes;
    const std::vector<Level::Gate>& gates = level.gates();
    openBefore.resize(gates.size(), 0);
    bool changed = !gates.empty();
    while (changed) {
        changed = false;
        std::vector<char> openNow = gateOpenness(level, state);
        for (std::size_t g = 0; g < gates.size(); ++g) {
            if (!openNow[g]) {
                if (const std::optional<LiveUnitAt> occupant =
                        liveUnitAt(state, gates[g].cell)) {
                    crushUnit(state, *occupant);
                    plateActivity.observe(state);
                    changes.crushed.add(*occupant);
                    changed = true;
                }
                continue;
            }
            if (openBefore[g]) {
                continue;
            }
            GridPosition3 cell = gates[g].cell;
            ++cell.z;
            for (std::optional<LiveUnitAt> unit = liveUnitAt(state, cell);
                 unit;
                 ++cell.z, unit = liveUnitAt(state, cell)) {
                if (dropUnit(level, state, *unit)) {
                    plateActivity.observe(state);
                    changes.dropped.add(*unit);
                    changed = true;
                }
            }
        }
        openBefore = std::move(openNow);
    }
    return changes;
}

// Moves every elevator whose linked plates became fully pressed since
// `engagedBefore` was sampled one stop along its cycle, carrying the column
// of live units stacked on its platform. The move is all or nothing: when any
// cell the platform or its riders would pass through is blocked (by a static
// tile, a closed gate, another platform or a unit that is not riding), the
// elevator stays where it is and keeps its phase, so the activation is lost.
// Elevators resolve in Level::elevators() order, each against the board the
// previous one left.
ElevatorRiders applyElevatorActivations(
    const Level& level,
    GameState& state,
    const std::vector<char>& engagedBefore,
    const PressurePlateActivityTracker& plateActivity)
{
    plateActivity.observe(state);
    ElevatorRiders riders;
    if (state.elevators.size() != level.elevators().size()) {
        return riders;
    }
    for (std::size_t e = 0; e < level.elevators().size(); ++e) {
        const Level::Elevator& elevator = level.elevators()[e];
        if ((e < engagedBefore.size() && engagedBefore[e]) ||
            elevator.levels.size() < 2 ||
            !isElevatorEngaged(level, state, elevator)) {
            continue;
        }
        GameState::Elevator& platform = state.elevators[e];
        const uint8_t nextPhase =
            elevatorNextPhase(elevator.levels.size(), platform.phase);
        const int targetZ = elevator.levels[
            elevatorStopIndex(elevator.levels.size(), nextPhase)];
        const GridPosition3 from = platform.cell;
        const int delta = targetZ - from.z;
        if (delta == 0) {
            platform.phase = nextPhase;
            continue;
        }

        // The stack standing on the platform, bottom first.
        std::vector<LiveUnitAt> stack;
        // The plane above the top layer holds units too (see
        // staticCellAllowsEntity), so the stack may reach z == depth.
        for (GridPosition3 cell { from.x, from.y, from.z + 1 };
             cell.z <= static_cast<int>(level.depth());
             ++cell.z) {
            const std::optional<LiveUnitAt> unit = liveUnitAt(state, cell);
            if (!unit) {
                break;
            }
            stack.push_back(*unit);
        }
        const int stackHeight = static_cast<int>(stack.size());

        // Cells the moving column sweeps into: above the stack going up,
        // below the platform going down.
        const int firstSwept = delta > 0 ? from.z + 1 + stackHeight : targetZ;
        const int lastSwept = delta > 0 ? targetZ + stackHeight : from.z - 1;
        bool blocked = std::ranges::any_of(stack, [&](LiveUnitAt unit) {
            return isUnitLocked(level, state, unitCell(state, unit));
        });
        for (int z = firstSwept; z <= lastSwept && !blocked; ++z) {
            const GridPosition3 cell { from.x, from.y, z };
            blocked = !cellAllowsEntity(level, state, cell) ||
                anyUnitIn(state, cell);
        }
        if (blocked) {
            continue;
        }

        platform.cell.z = targetZ;
        platform.phase = nextPhase;
        riders.elevators.push_back(e);
        for (const LiveUnitAt& unit : stack) {
            switch (unit.kind) {
            case EntityKind::Player:
                state.players[unit.index].cell.z += delta;
                riders.players.push_back(unit.index);
                break;
            case EntityKind::Movable:
                state.movables[unit.index].cell.z += delta;
                riders.movables.push_back(unit.index);
                break;
            case EntityKind::Enemy:
                state.enemies[unit.index].cell.z += delta;
                riders.enemies.push_back(unit.index);
                break;
            default:
                break;
            }
        }
        plateActivity.observe(state);
    }
    return riders;
}

struct MinecartRiders {
    std::vector<std::size_t> players;
    std::vector<std::size_t> movables;
    std::vector<std::size_t> enemies;
    std::vector<std::size_t> minecarts;

    [[nodiscard]] bool empty() const { return minecarts.empty(); }
};

// Grid cells swept by one transition between adjacent stops in route order.
// The starting cell is excluded and the destination is included.
std::vector<GridPosition3> minecartSweepCells(
    const Level::MinecartRoute& route,
    std::size_t fromStop,
    std::size_t toStop)
{
    std::vector<GridPosition3> result;
    if (fromStop >= route.stopCellIndices.size() ||
        toStop >= route.stopCellIndices.size() || route.cells.empty()) {
        return result;
    }
    const std::size_t from = route.stopCellIndices[fromStop];
    const std::size_t to = route.stopCellIndices[toStop];
    if (to > from) {
        result.insert(
            result.end(),
            route.cells.begin() + static_cast<std::ptrdiff_t>(from + 1),
            route.cells.begin() + static_cast<std::ptrdiff_t>(to + 1));
    } else if (route.loop && fromStop + 1 == route.stops.size() && toStop == 0) {
        result.insert(
            result.end(),
            route.cells.begin() + static_cast<std::ptrdiff_t>(from + 1),
            route.cells.end());
        result.push_back(route.cells.front());
    } else {
        for (std::size_t index = from; index > to; --index) {
            result.push_back(route.cells[index - 1]);
        }
    }
    return result;
}

MinecartRiders applyMinecartActivations(
    const Level& level,
    GameState& state,
    const std::vector<char>& engagedBefore,
    const PressurePlateActivityTracker& plateActivity)
{
    plateActivity.observe(state);
    MinecartRiders riders;
    if (state.minecarts.size() != level.minecarts().size() ||
        level.minecartRoutes().size() != level.minecarts().size()) {
        return riders;
    }
    for (std::size_t m = 0; m < level.minecarts().size(); ++m) {
        const Level::Minecart& minecart = level.minecarts()[m];
        const Level::MinecartRoute& route = level.minecartRoutes()[m];
        if ((m < engagedBefore.size() && engagedBefore[m]) ||
            route.stops.size() < 2 ||
            !isMinecartEngaged(level, state, minecart)) {
            continue;
        }

        GameState::Minecart& platform = state.minecarts[m];
        const std::size_t fromStop = minecartStopIndex(route, platform.phase);
        const uint16_t nextPhase = minecartNextPhase(route, platform.phase);
        const std::size_t toStop = minecartStopIndex(route, nextPhase);
        const GridPosition3 from = platform.cell;
        const GridPosition3 target = route.stops[toStop];
        const std::vector<GridPosition3> sweep =
            minecartSweepCells(route, fromStop, toStop);
        if (sweep.empty() || target == from) {
            platform.phase = nextPhase;
            continue;
        }

        // New carts carry an occupant in their own rail cell. Preserve the
        // older above-cart form as well so existing authored levels continue
        // to work; either form may have a contiguous stack above it.
        const int firstRiderHeight = liveUnitAt(state, from) ? 0 : 1;
        std::vector<LiveUnitAt> stack;
        for (GridPosition3 cell {
                 from.x, from.y, from.z + firstRiderHeight,
             };
             cell.z <= static_cast<int>(level.depth());
             ++cell.z) {
            const std::optional<LiveUnitAt> unit = liveUnitAt(state, cell);
            if (!unit) {
                break;
            }
            stack.push_back(*unit);
        }

        bool blocked = std::ranges::any_of(stack, [&](LiveUnitAt unit) {
            return isUnitLocked(level, state, unitCell(state, unit));
        });
        if (blocked) {
            continue;
        }
        for (const GridPosition3 railCell : sweep) {
            const bool cartGate = tileAt(level, railCell) == TileType::MinecartGate;
            if (cartGate && std::ranges::any_of(stack, [](LiveUnitAt unit) {
                    return unit.kind == EntityKind::Movable;
                })) {
                blocked = true;
                break;
            }
            const std::optional<std::size_t> other =
                minecartPlatformAt(level, state, railCell);
            if ((other && *other != m) || anyUnitIn(state, railCell)) {
                blocked = true;
                break;
            }
            for (int height = firstRiderHeight;
                 height < firstRiderHeight + static_cast<int>(stack.size());
                 ++height) {
                const GridPosition3 riderCell {
                    railCell.x, railCell.y, railCell.z + height,
                };
                const bool carriedThroughGate = cartGate && height == 0;
                if ((!carriedThroughGate &&
                        !cellAllowsEntity(level, state, riderCell)) ||
                    anyUnitIn(state, riderCell)) {
                    blocked = true;
                    break;
                }
            }
            if (blocked) {
                break;
            }
        }
        if (blocked) {
            continue;
        }

        const int dx = target.x - from.x;
        const int dy = target.y - from.y;
        platform.cell = target;
        platform.phase = nextPhase;
        riders.minecarts.push_back(m);
        for (const LiveUnitAt& unit : stack) {
            switch (unit.kind) {
            case EntityKind::Player:
                state.players[unit.index].cell.x += dx;
                state.players[unit.index].cell.y += dy;
                riders.players.push_back(unit.index);
                break;
            case EntityKind::Movable:
                state.movables[unit.index].cell.x += dx;
                state.movables[unit.index].cell.y += dy;
                riders.movables.push_back(unit.index);
                break;
            case EntityKind::Enemy:
                state.enemies[unit.index].cell.x += dx;
                state.enemies[unit.index].cell.y += dy;
                riders.enemies.push_back(unit.index);
                break;
            default:
                break;
            }
        }
        plateActivity.observe(state);
    }
    return riders;
}

} // namespace

bool isAtUnlockedEnd(const Level& level, const GameState& state)
{
    if (level.ends().empty() || anyPlayerDead(state)) {
        return false;
    }
    // Every hero stands on an End, and every End holds a hero.
    for (const std::size_t i :
         std::views::iota(std::size_t { 0 }, state.players.size())) {
        if (!level.isEnd(playerCell(state, i))) {
            return false;
        }
    }
    return std::ranges::all_of(level.ends(), [&](GridPosition3 end) {
        return playerBlocksAt(state, end);
    });
}

namespace {

bool turretHasLineOfSight(
    const Level& level,
    const GameState& state,
    GridPosition3 turret,
    MoveDirection direction,
    GridPosition3 target,
    std::vector<TurretRaySegment>* segments = nullptr)
{
    return traceTurretRay(turret, direction, target,
        [&](GridPosition3 cell) {
            if (!cellAllowsEntity(level, state, cell)) {
                return TurretRayCell::Blocked;
            }
            return movableAt(state, cell) || playerBlocksAt(state, cell) || enemyAt(state, cell)
                ? TurretRayCell::Occupied : TurretRayCell::Open;
        },
        [&](GridPosition3 cell, MoveDirection ray) {
            return level.portalCrossing(cell, directionOffset(ray));
        }, segments);
}

std::vector<TurretRaySegment> turretBeamSegments(
    const Level& level,
    const GameState& state,
    GridPosition3 from,
    MoveDirection direction,
    GridPosition3 to)
{
    std::vector<TurretRaySegment> segments;
    if (!turretHasLineOfSight(level, state, from, direction, to, &segments) ||
        segments.size() <= 1) {
        return {};
    }
    return segments;
}

} // namespace

namespace {

template <typename Visitor>
bool visitMutuallyFacingTurrets(
    const Level& level, const GameState& state, Visitor visitor)
{
    for (std::size_t first = 0; first < state.movables.size(); ++first) {
        const GameState::Movable& a = state.movables[first];
        const std::optional<MoveDirection> aDirection =
            turretDirection(a);
        if (a.dead || a.fallen || !aDirection) {
            continue;
        }
        for (std::size_t second = first + 1;
             second < state.movables.size();
             ++second) {
            const GameState::Movable& b = state.movables[second];
            const std::optional<MoveDirection> bDirection =
                turretDirection(b);
            if (b.dead || b.fallen || !bDirection ||
                !turretHasLineOfSight(
                    level, state, a.cell, *aDirection, b.cell) ||
                !turretHasLineOfSight(
                    level, state, b.cell, *bDirection, a.cell)) {
                continue;
            }
            if (visitor(first, second)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

std::vector<EntityId> mutuallyFacingTurrets(const Level& level, const GameState& state)
{
    std::vector<EntityId> participants;
    visitMutuallyFacingTurrets(level, state, [&](std::size_t first, std::size_t second) {
        for (std::size_t index : { first, second }) {
            const EntityId id = resolvedEntityId(EntityKind::Movable,
                state.movables[index].id, index);
            if (std::ranges::find(participants, id) == participants.end()) {
                participants.push_back(id);
            }
        }
        return false;
    });
    return participants;
}

bool hasPendingMotion(const Level& level, const GameState& state, bool includeRobots)
{
    if (visitMutuallyFacingTurrets(level, state,
            [](std::size_t, std::size_t) { return true; })) {
        return true;
    }
    for (std::size_t i = 0; i < state.players.size(); ++i) {
        if (!playerDead(state, i) &&
            !isUnitLocked(level, state, playerCell(state, i)) &&
            (playerSliding(state, i) ||
                conveyorDirectionAt(level, state, playerCell(state, i)))) {
            return true;
        }
    }

    return std::ranges::any_of(state.movables, [&](const GameState::Movable& movable) {
        return !movable.fallen && !movable.dead &&
            !isUnitLocked(level, state, movable.cell) &&
            (movable.sliding || conveyorDirectionAt(level, state, movable.cell).has_value() ||
                (includeRobots && tileTypeIsRobot(movable.type)));
    }) || std::ranges::any_of(
        state.enemies,
        [&](const GameState::Enemy& enemy) {
            return !enemy.fallen && !enemy.dead &&
                !isUnitLocked(level, state, enemy.cell) &&
                (enemy.sliding ||
                    conveyorDirectionAt(level, state, enemy.cell).has_value());
        });
}

namespace {

struct MirrorRays {
    GridPosition first {};
    GridPosition second {};
};

struct MirrorHit {
    GridPosition3 cell {};
    GridPosition output {};
    GridPosition input {};
    int distance = 0;
    bool inputBlocked = false;
    bool conflict = false;
};

std::optional<MirrorRays> mirrorRays(TileType tile)
{
    switch (tile) {
    case TileType::MirrorNorthWest:
        return MirrorRays { { 0, -1 }, { -1, 0 } };
    case TileType::MirrorNorthEast:
        return MirrorRays { { 0, -1 }, { 1, 0 } };
    case TileType::MirrorSouthWest:
        return MirrorRays { { 0, 1 }, { -1, 0 } };
    case TileType::MirrorSouthEast:
        return MirrorRays { { 0, 1 }, { 1, 0 } };
    default:
        return std::nullopt;
    }
}

struct PortalRayPath {
    GridPosition3 destination {};
    std::vector<GridPosition3> cells;
    std::vector<MirrorBeamSegment> segments;
};

PortalRayPath portalRayPath(
    const Level& level,
    GridPosition3 origin,
    GridPosition ray,
    int distance,
    std::optional<GridPosition3> stop = std::nullopt)
{
    PortalRayPath path { .destination = origin };
    GridPosition3 segmentStart = origin;
    GridPosition startEdge {};
    for (int step = 0; step < distance; ++step) {
        auto& cell = path.destination;
        if (const auto crossing = level.portalCrossing(cell, ray)) {
            path.segments.push_back({ segmentStart, cell, startEdge, ray });
            cell = crossing->exit;
            ray = crossing->direction;
            segmentStart = cell;
            startEdge = portalEdgeOffset(*level.plateAt(cell));
        } else {
            cell = { cell.x + ray.x, cell.y + ray.y, cell.z };
        }
        path.cells.push_back(cell);
        if (stop && cell == *stop) {
            break;
        }
    }
    path.segments.push_back({ segmentStart, path.destination, startEdge, {} });
    return path;
}

std::optional<int> distanceAlongRay(
    const Level& level,
    GridPosition3 origin,
    GridPosition3 target,
    GridPosition ray)
{
    if (level.portals().empty()) {
        const int dx = target.x - origin.x;
        const int dy = target.y - origin.y;
        if (origin.z == target.z && ray.x != 0 && dy == 0 && dx * ray.x > 0) {
            return std::abs(dx);
        }
        if (origin.z == target.z && ray.y != 0 && dx == 0 && dy * ray.y > 0) {
            return std::abs(dy);
        }
        return std::nullopt;
    }
    // A portal can change elevation and apparent alignment, so distance is
    // the number of physical ray steps rather than a coordinate difference.
    GridPosition3 cell = origin;
    std::vector<std::pair<GridPosition3, GridPosition>> visited;
    for (int distance = 1;; ++distance) {
        const auto key = std::pair { cell, ray };
        if (std::ranges::find(visited, key) != visited.end()) {
            return std::nullopt;
        }
        visited.push_back(key);
        if (const auto crossing = level.portalCrossing(cell, ray)) {
            cell = crossing->exit;
            ray = crossing->direction;
        } else {
            cell = { cell.x + ray.x, cell.y + ray.y, cell.z };
        }
        if (!level.inBounds(cell)) {
            return std::nullopt;
        }
        if (cell == target) {
            return distance;
        }
    }
}

bool entityBlocksSight(
    const GameState& state,
    GridPosition3 cell,
    std::size_t ignoredEntity)
{
    for (std::size_t i = 0; i < state.players.size(); ++i) {
        const std::size_t entityIndex = state.movables.size() + i;
        if (ignoredEntity != entityIndex && !playerDead(state, i) &&
            playerCell(state, i) == cell) {
            return true;
        }
    }
    for (std::size_t i = 0; i < state.movables.size(); ++i) {
        if (i != ignoredEntity && !state.movables[i].fallen &&
            !state.movables[i].dead &&
            state.movables[i].cell == cell) {
            return true;
        }
    }
    for (std::size_t i = 0; i < state.enemies.size(); ++i) {
        const std::size_t entityIndex =
            state.movables.size() + state.players.size() + i;
        if (ignoredEntity != entityIndex && !state.enemies[i].fallen &&
            !state.enemies[i].dead && state.enemies[i].cell == cell) {
            return true;
        }
    }
    return false;
}

enum class MirrorInputSight { Clear, GeometryBlocked, UnitBlocked };

MirrorInputSight inputRaySight(
    const Level& level,
    const GameState& state,
    GridPosition3 mirror,
    GridPosition ray,
    int distance,
    std::size_t entityIndex)
{
    const PortalRayPath path = portalRayPath(level, mirror, ray, distance);
    bool geometryBlocked = false;
    for (const auto cell : path.cells) {
        if (entityBlocksSight(state, cell, entityIndex)) {
            return MirrorInputSight::UnitBlocked;
        }
        geometryBlocked = geometryBlocked ||
            !cellAllowsEntity(level, state, cell);
        // The ignored source may occupy an entrance before it teleports.
        const auto matchesSource = [&](const auto& units, std::size_t offset) {
            return entityIndex >= offset &&
                entityIndex - offset < units.size() &&
                units[entityIndex - offset].cell == cell;
        };
        if (matchesSource(state.movables, 0) ||
            matchesSource(state.players, state.movables.size()) ||
            matchesSource(
                state.enemies, state.movables.size() + state.players.size())) {
            break;
        }
    }
    return geometryBlocked ? MirrorInputSight::GeometryBlocked
                           : MirrorInputSight::Clear;
}

bool outputRayIsClear(
    const Level& level,
    const GameState& state,
    GridPosition3 mirror,
    GridPosition ray,
    int distance)
{
    for (const auto cell : portalRayPath(level, mirror, ray, distance).cells) {
        if (!cellAllowsEntity(level, state, cell) ||
            mirrorTileAt(level, state, cell)) {
            return false;
        }
    }
    return true;
}

std::optional<int> firstUnitOnRay(
    const Level& level,
    const GameState& state,
    GridPosition3 mirror,
    GridPosition ray,
    int distance,
    std::size_t entityIndex)
{
    int step = 0;
    for (const auto cell : portalRayPath(level, mirror, ray, distance).cells) {
        ++step;
        if (!cellAllowsEntity(level, state, cell) ||
            mirrorTileAt(level, state, cell)) {
            break;
        }
        // Ignore the source's original cell during chained reflections.
        if (entityBlocksSight(state, cell, entityIndex)) {
            return step;
        }
    }
    return std::nullopt;
}

std::vector<MirrorHit> nearestMirrors(
    const Level& level,
    const GameState& state,
    GridPosition3 entityCell,
    std::size_t entityIndex,
    const std::vector<GridPosition3>& usedMirrors,
    bool includeBlockedInputs = false)
{
    std::vector<MirrorHit> nearest;
    int nearestDistance = 0;
    if (entityCell.z < 0 || entityCell.z >= static_cast<int>(level.depth())) {
        return nearest;
    }

    const uint32_t firstLayer =
        level.portals().empty() ? static_cast<uint32_t>(entityCell.z) : 0;
    const uint32_t lastLayer =
        level.portals().empty() ? firstLayer + 1 : level.depth() + 1;
    for (uint32_t z = firstLayer; z < lastLayer; ++z) {
        for (uint32_t y = 0; y < level.height(); ++y) {
            for (uint32_t x = 0; x < level.width(); ++x) {
                const GridPosition3 mirror {
                    static_cast<int>(x),
                    static_cast<int>(y),
                    static_cast<int>(z),
                };
                if (std::ranges::find(usedMirrors, mirror) !=
                    usedMirrors.end()) {
                    continue;
                }
                const std::optional<TileType> mirrorTile =
                    mirrorTileAt(level, state, mirror);
                const std::optional<MirrorRays> rays =
                    mirrorTile ? mirrorRays(*mirrorTile) : std::nullopt;
                if (!rays) {
                    continue;
                }

                const std::array pairs {
                    std::pair { rays->first, rays->second },
                    std::pair { rays->second, rays->first },
                };
                for (const auto& [input, output] : pairs) {
                    const std::optional<int> distance =
                        distanceAlongRay(level, mirror, entityCell, input);
                    if (!distance) {
                        continue;
                    }
                    const MirrorInputSight sight = inputRaySight(
                        level, state, mirror, input, *distance, entityIndex);
                    if (sight == MirrorInputSight::UnitBlocked ||
                        (sight == MirrorInputSight::GeometryBlocked &&
                            !includeBlockedInputs)) {
                        continue;
                    }
                    const auto opposite = firstUnitOnRay(
                        level, state, mirror, output, *distance, entityIndex);
                    if (opposite && *opposite < *distance) {
                        continue;
                    }
                    const MirrorHit hit { mirror, output, input, *distance,
                        sight == MirrorInputSight::GeometryBlocked,
                        opposite && *opposite == *distance };
                    if (nearest.empty() || *distance < nearestDistance) {
                        nearest.clear();
                        nearest.push_back(hit);
                        nearestDistance = *distance;
                    } else if (
                        *distance == nearestDistance &&
                        std::ranges::none_of(
                            nearest, [&](const MirrorHit& existing) {
                                return existing.cell == mirror;
                            })) {
                        nearest.push_back(hit);
                    }
                }
            }
        }
    }
    return nearest;
}

struct ReflectedPath {
    GridPosition3 cell {};
    bool reflected = false;
    std::vector<MirrorBeamSegment> beamSegments;
};

struct PendingReflectionPath {
    GridPosition3 cell {};
    std::vector<GridPosition3> usedMirrors;
    std::vector<MirrorBeamSegment> beamSegments;
};

std::optional<std::vector<ReflectedPath>> reflectedPathsForEntity(
    const Level& level,
    const GameState& state,
    GridPosition3 start,
    std::size_t entityIndex,
    bool allowBranches,
    bool includeBlockedInputs = false,
    std::vector<ReflectedPath>* attemptedPaths = nullptr,
    std::vector<ReflectedPath>* invalidPaths = nullptr)
{
    const std::size_t maximumChain =
        static_cast<std::size_t>(level.width()) * level.height();
    constexpr std::size_t maximumBranches = 256;
    std::vector<PendingReflectionPath> pending {
        { .cell = start },
    };
    std::vector<ReflectedPath> results;
    bool invalid = false;
    const bool collecting = attemptedPaths || invalidPaths;
    const auto appendPath = [](std::vector<ReflectedPath>* destination,
                              const PendingReflectionPath& source) {
        if (destination) {
            destination->push_back({
                .cell = source.cell,
                .reflected = !source.usedMirrors.empty(),
                .beamSegments = source.beamSegments,
            });
        }
    };

    while (!pending.empty()) {
        PendingReflectionPath path = std::move(pending.back());
        pending.pop_back();
        if (path.usedMirrors.size() >= maximumChain) {
            if (!collecting) {
                return std::nullopt;
            }
            appendPath(attemptedPaths, path);
            appendPath(invalidPaths, path);
            invalid = true;
            continue;
        }

        const std::vector<MirrorHit> hits = nearestMirrors(
            level, state, path.cell, entityIndex, path.usedMirrors,
            includeBlockedInputs);
        if (hits.empty()) {
            appendPath(attemptedPaths, path);
            results.push_back({
                .cell = path.cell,
                .reflected = !path.usedMirrors.empty(),
                .beamSegments = std::move(path.beamSegments),
            });
            continue;
        }
        const bool invalidBranches = (!allowBranches && hits.size() > 1) ||
            pending.size() + results.size() + hits.size() > maximumBranches;
        if (invalidBranches && !collecting) {
            return std::nullopt;
        }

        // Reverse insertion keeps the level scan order stable when paths are
        // later popped from this depth-first stack.
        for (auto hit = hits.rbegin(); hit != hits.rend(); ++hit) {
            const bool blocked = hit->inputBlocked || hit->conflict ||
                invalidBranches || !outputRayIsClear(
                    level, state, hit->cell, hit->output, hit->distance);
            if (blocked && !collecting) {
                return std::nullopt;
            }
            PendingReflectionPath branch = path;
            const PortalRayPath output =
                portalRayPath(level, hit->cell, hit->output, hit->distance);
            const GridPosition3 destination = output.destination;
            const PortalRayPath input = portalRayPath(
                level, hit->cell, hit->input, hit->distance, path.cell);
            branch.usedMirrors.push_back(hit->cell);
            for (auto segment = input.segments.rbegin();
                 segment != input.segments.rend();
                 ++segment) {
                branch.beamSegments.push_back({ segment->to,
                                                segment->from,
                                                segment->toEdge,
                                                segment->fromEdge });
            }
            branch.beamSegments.insert(
                branch.beamSegments.end(),
                output.segments.begin(),
                output.segments.end());
            branch.cell = destination;
            if (blocked) {
                appendPath(attemptedPaths, branch);
                appendPath(invalidPaths, branch);
                invalid = true;
                continue;
            }
            pending.push_back(std::move(branch));
        }
    }

    if (invalid) {
        return std::nullopt;
    }
    return results;
}

bool liveCellsAreUnique(const GameState& state)
{
    std::vector<GridPosition3> occupied;
    for (std::size_t i = 0; i < state.players.size(); ++i) {
        if (playerDead(state, i)) {
            continue;
        }
        if (std::ranges::find(occupied, playerCell(state, i)) != occupied.end()) {
            return false;
        }
        occupied.push_back(playerCell(state, i));
    }
    for (const GameState::Movable& movable : state.movables) {
        if (movable.fallen || movable.dead) {
            continue;
        }
        if (std::ranges::find(occupied, movable.cell) != occupied.end()) {
            return false;
        }
        occupied.push_back(movable.cell);
    }
    for (const GameState::Enemy& enemy : state.enemies) {
        if (enemy.fallen || enemy.dead) {
            continue;
        }
        if (std::ranges::find(occupied, enemy.cell) != occupied.end()) {
            return false;
        }
        occupied.push_back(enemy.cell);
    }
    return true;
}

} // namespace

namespace {

struct ActivationSignals {
    GameState state;
    bool pulse = false;
    bool toggled = false;
};

ActivationSignals activationSignals(const Level& level, const GameState& state)
{
    ActivationSignals result { .state = state };
    result.state.activeButtons = activatableButtons(level, state);
    result.pulse = !result.state.activeButtons.empty();
    const auto levers = activatableLevers(level, state);
    result.toggled = !levers.empty();
    if (result.toggled) {
        result.state.activeLevers.clear();
        // Sample switches from the starting board, before any reflections.
        for (const GridPosition3 cell : level.pressurePlates()) {
            if (!tileTypeIsLever(level.plateAt(cell).value_or(TileType::Air))) {
                continue;
            }
            const bool wasOn = std::ranges::find(state.activeLevers, cell) !=
                state.activeLevers.end();
            const bool flip = std::ranges::find(levers, cell) != levers.end();
            if (wasOn != flip) {
                result.state.activeLevers.push_back(cell);
            }
        }
    }
    return result;
}

std::optional<MirrorActivationPreview> previewActivationImpl(
    const Level& level,
    const GameState& state,
    const GameState& signals,
    bool reflect,
    bool pulse,
    bool toggled = false)
{
    GameState after = signals;
    bool pressurePlateActivityChanged = false;
    const PressurePlateActivityTracker plateActivity {
        level, state, &pressurePlateActivityChanged,
    };
    std::vector<MirrorEntityPreview> entities;
    bool anyReflected = false;
    const std::size_t originalPlayerCount = state.players.size();
    std::vector<std::size_t> reflectedPlayerIndices;
    std::vector<bool> movableReflected(state.movables.size(), false);
    std::vector<bool> enemyReflected(state.enemies.size(), false);
    EntityId nextEntityId = 1;
    for (const GameState::Player& player : state.players) {
        nextEntityId = std::max(nextEntityId, player.id + 1);
    }
    for (const GameState::Movable& movable : state.movables) {
        nextEntityId = std::max(nextEntityId, movable.id + 1);
    }
    for (const GameState::Enemy& enemy : state.enemies) {
        nextEntityId = std::max(nextEntityId, enemy.id + 1);
    }

    for (std::size_t sourcePlayer = 0;
         reflect && sourcePlayer < originalPlayerCount;
         ++sourcePlayer) {
        if (playerDead(state, sourcePlayer) ||
            isUnitLocked(level, state, playerCell(state, sourcePlayer))) {
            continue;
        }
        const std::optional<std::vector<ReflectedPath>> reflectedPaths =
            reflectedPathsForEntity(
                level,
                signals,
                playerCell(state, sourcePlayer),
                state.movables.size() + sourcePlayer,
                true);
        if (!reflectedPaths) {
            return std::nullopt;
        }
        std::size_t reflectionIndex = 0;
        for (const ReflectedPath& reflected : *reflectedPaths) {
            if (!reflected.reflected) {
                continue;
            }
            std::size_t resultPlayer = sourcePlayer;
            if (reflectionIndex == 0) {
                playerCell(after, resultPlayer) = reflected.cell;
                playerSliding(after, resultPlayer).reset();
            } else {
                const GameState::Player& source = state.players[sourcePlayer];
                after.players.push_back({
                    .id = nextEntityId++,
                    .cell = reflected.cell,
                    .character = source.character,
                    .controller = playerControllerId(state, sourcePlayer),
                    .dead = source.dead,
                    .drowned = source.drowned,
                    .sliding = std::nullopt,
                });
                resultPlayer = after.players.size() - 1;
            }
            reflectedPlayerIndices.push_back(resultPlayer);
            entities.push_back({
                .player = true,
                .playerIndex = sourcePlayer,
                .reflectionIndex = reflectionIndex,
                .resultPlayerIndex = resultPlayer,
                .start = playerCell(state, sourcePlayer),
                .destination = reflected.cell,
                .beamSegments = reflected.beamSegments,
            });
            anyReflected = true;
            ++reflectionIndex;
        }
    }

    for (std::size_t i = 0; reflect && i < state.movables.size(); ++i) {
        // Mirrors are reflectors, even though they now move as units.
        if (state.movables[i].fallen || state.movables[i].dead ||
            tileTypeIsMirror(state.movables[i].type) ||
            isUnitLocked(level, state, state.movables[i].cell)) {
            continue;
        }
        const std::optional<std::vector<ReflectedPath>> reflectedPaths =
            reflectedPathsForEntity(
                level, signals, state.movables[i].cell, i, false);
        if (!reflectedPaths) {
            return std::nullopt;
        }
        const ReflectedPath& reflected = reflectedPaths->front();
        if (reflected.reflected) {
            after.movables[i].cell = reflected.cell;
            after.movables[i].sliding.reset();
            movableReflected[i] = true;
            entities.push_back({
                .movableIndex = i,
                .start = state.movables[i].cell,
                .destination = reflected.cell,
                .beamSegments = reflected.beamSegments,
            });
            anyReflected = true;
        }
    }

    for (std::size_t i = 0; reflect && i < state.enemies.size(); ++i) {
        if (state.enemies[i].fallen || state.enemies[i].dead ||
            isUnitLocked(level, state, state.enemies[i].cell)) {
            continue;
        }
        const std::optional<std::vector<ReflectedPath>> reflectedPaths =
            reflectedPathsForEntity(
                level,
                signals,
                state.enemies[i].cell,
                state.movables.size() + state.players.size() + i,
                false);
        if (!reflectedPaths) {
            return std::nullopt;
        }
        const ReflectedPath& reflected = reflectedPaths->front();
        if (reflected.reflected) {
            after.enemies[i].cell = reflected.cell;
            after.enemies[i].sliding.reset();
            enemyReflected[i] = true;
            entities.push_back({
                .enemy = true,
                .enemyIndex = i,
                .start = state.enemies[i].cell,
                .destination = reflected.cell,
                .beamSegments = reflected.beamSegments,
            });
            anyReflected = true;
        }
    }

    if ((!anyReflected && !pulse && !toggled) || !liveCellsAreUnique(after)) {
        return std::nullopt;
    }

    for (std::size_t playerIndex : reflectedPlayerIndices) {
        const FallResult fall = playerFallTarget(
            level, after, playerIndex, playerCell(after, playerIndex));
        if (!fall.supported) {
            return std::nullopt;
        }
        playerCell(after, playerIndex) = fall.cell;
        playerDead(after, playerIndex) = fall.fallen;
        after.players[playerIndex].drowned = fall.fallen;
    }
    for (std::size_t i = 0; i < after.movables.size(); ++i) {
        if (!movableReflected[i]) {
            continue;
        }
        const FallResult fall = movableFallTarget(
            level, after, i, after.movables[i].cell);
        if (!fall.supported) {
            return std::nullopt;
        }
        after.movables[i].cell = fall.cell;
        after.movables[i].fallen = fall.fallen;
    }
    for (std::size_t i = 0; i < after.enemies.size(); ++i) {
        if (!enemyReflected[i]) {
            continue;
        }
        const FallResult fall = enemyFallTarget(
            level, after, i, after.enemies[i].cell);
        if (!fall.supported) {
            return std::nullopt;
        }
        after.enemies[i].cell = fall.cell;
        after.enemies[i].fallen = fall.fallen;
    }

    if (!liveCellsAreUnique(after) || (!pulse && !toggled && after == state)) {
        return std::nullopt;
    }
    // Reflected units can open or close gates, dropping whatever rests on
    // them or crushing whatever stands in them.
    std::vector<char> gatesOpen = gateOpenness(level, state);
    (void)applyGateChanges(level, after, gatesOpen, plateActivity);
    GameState engagementBefore = state;
    if (pulse) {
        // Each key press is a new edge, including consecutive presses while
        // the hero remains on the same button.
        engagementBefore.activeButtons.clear();
    }
    // Reflected units can land on pressure plates or rotators exactly as a
    // step can, so linked rotators fire here too.
    (void)applyRotatorActivations(
        level, after, rotatorEngagement(level, engagementBefore));
    (void)applyElevatorActivations(
        level, after, elevatorEngagement(level, engagementBefore), plateActivity);
    (void)applyMinecartActivations(
        level, after, minecartEngagement(level, engagementBefore), plateActivity);
    (void)applyGateChanges(level, after, gatesOpen, plateActivity);
    resolveEnemyAttacks(after);
    // An attack can empty a plate, opening or closing its gates.
    (void)applyGateChanges(level, after, gatesOpen, plateActivity);
    for (MirrorEntityPreview& entity : entities) {
        if (entity.player) {
            entity.destination = playerCell(after, entity.resultPlayerIndex);
            entity.fallen = after.players[entity.resultPlayerIndex].drowned;
        } else if (entity.enemy) {
            entity.destination = after.enemies[entity.enemyIndex].cell;
            entity.fallen = after.enemies[entity.enemyIndex].fallen;
        } else {
            entity.destination = after.movables[entity.movableIndex].cell;
            entity.fallen = after.movables[entity.movableIndex].fallen;
        }
    }
    return MirrorActivationPreview {
        .after = std::move(after),
        .entities = std::move(entities),
        .pressurePlateActivityChanged = pressurePlateActivityChanged,
    };
}

} // namespace

std::optional<MirrorActivationPreview> previewMirrorActivation(
    const Level& level, const GameState& state)
{
    return previewActivationImpl(level, state, state, true, false);
}

std::optional<MirrorActivationPreview> previewActivation(
    const Level& level, const GameState& state)
{
    const ActivationSignals signals = activationSignals(level, state);
    auto preview = previewActivationImpl(
        level, state, signals.state, true, signals.pulse, signals.toggled);
    // An invalid reflection has no eligible mirror transaction, but must not
    // discard independent switches occupied by other heroes.
    if (!preview && (signals.pulse || signals.toggled)) {
        preview = previewActivationImpl(
            level, state, signals.state, false, signals.pulse, signals.toggled);
    }
    return preview;
}

InvalidMirrorPreview previewInvalidMirrors(
    const Level& level, const GameState& state)
{
    if (anyPlayerDead(state)) {
        return {};
    }
    const ActivationSignals signals = activationSignals(level, state);
    // Validate the full reflection transaction, before the independent switch
    // fallback. This also detects unsupported landings and destination clashes.
    const auto valid = previewActivationImpl(
        level, state, signals.state, true, signals.pulse, signals.toggled);
    InvalidMirrorPreview result;
    std::size_t nextGhostPlayerIndex = valid
        ? valid->after.players.size() : state.players.size();
    const auto inspect = [&](const MirrorEntityPreview& source, std::size_t entityIndex,
                             bool allowBranches) {
        std::vector<ReflectedPath> attempted;
        std::vector<ReflectedPath> invalid;
        (void)reflectedPathsForEntity(level, signals.state, source.start, entityIndex,
            allowBranches, true, &attempted, &invalid);
        const bool allInvalid = !valid || isUnitLocked(level, state, source.start);
        for (std::size_t index = 0; index < attempted.size(); ++index) {
            const ReflectedPath& path = attempted[index];
            if (!path.reflected || (!allInvalid &&
                std::ranges::none_of(invalid, [&](const ReflectedPath& rejected) {
                    return path.cell == rejected.cell &&
                        path.beamSegments == rejected.beamSegments;
                }))) {
                continue;
            }
            MirrorEntityPreview entity = source;
            entity.reflectionIndex = index;
            entity.resultPlayerIndex = source.player ? nextGhostPlayerIndex++ : 0;
            // Keep the geometric target even when it has no support or is
            // occupied. Warning ghosts describe the attempt, never a state
            // that activation is allowed to commit.
            entity.destination = path.cell;
            entity.beamSegments = path.beamSegments;
            result.entities.push_back(std::move(entity));
        }
    };
    for (std::size_t i = 0; i < state.players.size(); ++i) {
        if (!playerDead(state, i)) {
            inspect({ .player = true, .playerIndex = i,
                .start = playerCell(state, i) }, state.movables.size() + i, true);
        }
    }
    for (std::size_t i = 0; i < state.movables.size(); ++i) {
        const auto& movable = state.movables[i];
        if (!movable.fallen && !movable.dead &&
            !tileTypeIsMirror(movable.type)) {
            inspect({ .movableIndex = i, .start = movable.cell }, i, false);
        }
    }
    for (std::size_t i = 0; i < state.enemies.size(); ++i) {
        const auto& enemy = state.enemies[i];
        if (!enemy.fallen && !enemy.dead) {
            inspect({ .enemy = true, .enemyIndex = i, .start = enemy.cell },
                state.movables.size() + state.players.size() + i, false);
        }
    }
    for (const auto& entity : result.entities) {
        for (const auto& segment : entity.beamSegments) {
            if (segment.from == segment.to && segment.fromEdge == segment.toEdge) {
                continue;
            }
            if (std::ranges::none_of(result.beamSegments, [&](const auto& existing) {
                    return existing == segment ||
                        (existing.from == segment.to && existing.to == segment.from &&
                            existing.fromEdge == segment.toEdge &&
                            existing.toEdge == segment.fromEdge);
                })) {
                result.beamSegments.push_back(segment);
            }
        }
    }
    return result;
}

std::vector<MirrorBeamSegment> invalidMirrorBeamSegments(
    const Level& level, const GameState& state)
{
    return previewInvalidMirrors(level, state).beamSegments;
}

std::optional<GameState> activate(const Level& level, const GameState& state)
{
    auto preview = previewActivation(level, state);
    if (!preview) {
        return std::nullopt;
    }
    return std::move(preview->after);
}

std::optional<GameState> activateMirrors(
    const Level& level,
    const GameState& state)
{
    std::optional<MirrorActivationPreview> preview =
        previewMirrorActivation(level, state);
    if (!preview) {
        return std::nullopt;
    }
    return std::move(preview->after);
}

namespace {

// Resolves one world step as repeated simultaneous one-tile micro-steps.
//
// Entities are indexed uniformly: 0..movableCount-1 are the movables and the
// last index is the player. The player deliberately resolves after the
// movables inside each pass - the same order the pre-refactor loop used -
// so mid-pass fall interactions stay byte-for-byte identical.
//
// A micro-step runs four named phases:
//   deriveIntents  - what does each entity want, given momentum, input,
//                    belts, and its movement source's remaining budget?
//   markContested  - simultaneous intents for one destination all lose;
//                    without this pre-pass, storage order would pick a winner.
//   resolveMoves   - multi-pass move resolution, so an entity blocked only
//                    by another entity that vacates its cell this micro-step
//                    still advances; direct input may push a resolved
//                    blocker. Each entity moves at most once per micro-step.
//   linkedMoves    - successful movable displacements are repeated by the
//                    other members of their color group when space permits.
//   settleBlocked  - anything with an intent that could not move is in a
//                    mutual block; blocked slide momentum does not survive.
//
// Micro-steps repeat until one completes with no movement at all.
//
// A scope restricts which entities may act. Out-of-scope entities keep every
// passive role they had - they block, support, and stop slides - but derive no
// intent. An action can still pull a bystander into its closure by exposing it
// to a turret or enemy. An empty scope means everything acts, which is the
// behaviour every existing save was written against.
class MicroStepResolver {
public:
    MicroStepResolver(
        const Level& level,
        const GameState& before,
        GameState& after,
        std::optional<MoveDirection> playerInput,
        const StepRates& rates,
        const StepScope& scope,
        std::vector<TurretShot>* turretShots = nullptr,
        std::vector<PortalTransit>* portalTransits = nullptr,
        bool* pressurePlateActivityChanged = nullptr)
        : level_(level)
        , after_(after)
        , plateActivity_ { level, before, pressurePlateActivityChanged }
        , playerInput_(playerInput)
        , rates_(rates)
        , movableCount_(after.movables.size())
        , playerCount_(after.players.size())
        , enemyCount_(after.enemies.size())
        , status_(movableCount_ + playerCount_ + enemyCount_)
        , enemyMoved_(after.enemies.size(), 0)
        , enemyMovedThisMicro_(after.enemies.size(), 0)
        , movableMovedDirections_(movableCount_)
        , turretShots_(turretShots)
        , portalTransits_(portalTransits)
        , rotatorsEngagedAtStart_(rotatorEngagement(level, after))
        , elevatorsEngagedAtStart_(elevatorEngagement(level, after))
        , minecartsEngagedAtStart_(minecartEngagement(level, after))
        , gatesOpen_(gateOpenness(level, after))
    {
        for (Status& status : status_) {
            status.active = scope.wholeWorld();
        }
        for (const EntityId actor : scope.actors) {
            activate(actor);
        }
        for (std::size_t i = 0; i < playerCount_; ++i) {
            status_[entityIndexForPlayer(i)].done = playerDead(after_, i);
        }
        for (std::size_t i = 0; i < movableCount_; ++i) {
            status_[i].done = after_.movables[i].fallen ||
                after_.movables[i].dead;
        }
        for (std::size_t i = 0; i < enemyCount_; ++i) {
            status_[entityIndexForEnemy(i)].done = after_.enemies[i].fallen ||
                after_.enemies[i].dead;
        }
    }

    void run()
    {
        turretSightlines_ = captureTurretSightlines();
        // A mutual face-off is itself an event; it does not need some unrelated
        // actor to move before both turrets fire.
        while (resolveTurretShots()) {
        }
        markDeadEntitiesDone();
        bool anyMovement = true;
        while (anyMovement) {
            const bool mayPulse = anyBardHasMovementIntent();
            GameState beforeBardPulse;
            std::vector<Status> statusesBeforeBardPulse;
            std::vector<char> enemiesBeforeBardPulse;
            if (mayPulse) {
                beforeBardPulse = after_;
                statusesBeforeBardPulse = status_;
                enemiesBeforeBardPulse = enemyMoved_;
            }
            const std::size_t transitCount =
                portalTransits_ ? portalTransits_->size() : 0;
            deriveIntents();
            const bool hasBardPulse = std::ranges::any_of(
                status_,
                [](const Status& status) { return status.bardDriven; });
            markContested();
            anyMovement = resolveMoves();
            anyMovement = resolveLinkedMoves() || anyMovement;
            settleBlocked();
            if (mayPulse && hasBardPulse && !anyBardMovedThisMicro()) {
                if (portalTransits_) {
                    portalTransits_->resize(transitCount);
                }
                after_ = beforeBardPulse;
                status_ = statusesBeforeBardPulse;
                enemyMoved_ = enemiesBeforeBardPulse;
                suppressBardInfluences_ = true;
                deriveIntents();
                markContested();
                anyMovement = resolveMoves();
                anyMovement = resolveLinkedMoves() || anyMovement;
                settleBlocked();
                suppressBardInfluences_ = false;
            }
            anyMovement = resolveGateChanges() || anyMovement;
            if (anyMovement) {
                resolveReactions();
            }
        }
        resolveRotators();
        resolveElevators();
        while (resolveGateChanges()) {
            resolveReactions();
        }
        resolveMinecarts();
        while (resolveGateChanges()) {
            resolveReactions();
        }
        // A pulse supplies input for one complete step, including movement
        // through its gates. Expiration uses normal gate crush/fall reactions.
        after_.activeButtons.clear();
        while (resolveGateChanges()) {
            resolveReactions();
        }
    }

private:
    // Rocks, ice, turrets, and enemies all participate in forced movement.
    // The storage stays separate because enemies retain attack/death state.
    struct ChainEntity {
        bool movable = false;
        std::size_t index = 0;
    };

    struct Status {
        // Persistent across micro-steps: movement budget consumed and
        // whether this entity's movement source is finished for the step.
        int consumed = 0;
        std::optional<MoveDirection> inputDirection;
        bool done = false;
        // Whether this entity may act at all. Deliberately not folded into
        // `done`, which means "has finished acting": an entity that gets pushed
        // joins the causal closure part-way through and has to start acting,
        // and reusing one flag for both would make that indistinguishable from
        // an entity resuming after it had already stopped.
        bool active = false;
        // Re-derived every micro-step.
        std::optional<MoveDirection> intent;
        std::optional<GridPosition3> target;
        bool contested = false;
        bool resolved = false;
        bool movedThisMicro = false;
        bool inputDriven = false; // player only
        // A live bard moving within range overrides this movable unit's
        // ordinary slide or conveyor intent for the current micro-step.
        // The budget mirrors the bard's movement source so faster configured
        // movement still produces exactly one pulse per bard tile.
        std::optional<MoveDirection> bardDirection;
        int bardMoveBudget = 0;
        bool bardDirectionContested = false;
        bool bardDriven = false;
        bool robotDriven = false;
        // A witch replaces an input-driven walk with one teleport to the
        // nearest visible movable unit on that ray.
        std::optional<ChainEntity> witchSwapTarget;
    };

    // Brings one entity into the causal closure, by the id the rest of the
    // engine knows it by. Unmatched ids are ignored rather than rejected: a
    // caller may name an entity a previous action has already removed from the
    // world, and that is not an error, it is just nothing left to move.
    void activate(EntityId actor)
    {
        if (actor == invalidEntityId) {
            return;
        }
        for (std::size_t i = 0; i < movableCount_; ++i) {
            if (resolvedEntityId(
                    EntityKind::Movable, after_.movables[i].id, i) == actor) {
                status_[i].active = true;
                return;
            }
        }
        for (std::size_t i = 0; i < playerCount_; ++i) {
            if (resolvedEntityId(
                    EntityKind::Player, after_.players[i].id, i) == actor) {
                status_[entityIndexForPlayer(i)].active = true;
                return;
            }
        }
        for (std::size_t i = 0; i < enemyCount_; ++i) {
            if (resolvedEntityId(
                    EntityKind::Enemy, after_.enemies[i].id, i) == actor) {
                status_[entityIndexForEnemy(i)].active = true;
                return;
            }
        }
    }

    [[nodiscard]] std::size_t entityIndexForPlayer(
        std::size_t playerIndex) const
    {
        return movableCount_ + playerIndex;
    }
    [[nodiscard]] bool isPlayer(std::size_t index) const
    {
        return index >= movableCount_ &&
            index < movableCount_ + playerCount_;
    }
    [[nodiscard]] std::size_t entityIndexForEnemy(
        std::size_t enemyIndex) const
    {
        return movableCount_ + playerCount_ + enemyIndex;
    }
    [[nodiscard]] bool isEnemy(std::size_t index) const
    {
        return index >= movableCount_ + playerCount_;
    }
    [[nodiscard]] std::size_t playerIndexForEntity(std::size_t index) const
    {
        return index - movableCount_;
    }
    [[nodiscard]] std::size_t enemyIndexForEntity(std::size_t index) const
    {
        return index - movableCount_ - playerCount_;
    }

    [[nodiscard]] GridPosition3 movementDestination(
        GridPosition3 origin,
        MoveDirection direction) const
    {
        const auto crossing =
            level_.portalCrossing(origin, directionOffset(direction));
        return crossing ? crossing->exit : movementTarget(origin, direction);
    }

    [[nodiscard]] bool portalEntryClear(
        GridPosition3 from,
        MoveDirection direction,
        const GameState& state) const
    {
        const auto crossing =
            level_.portalCrossing(from, directionOffset(direction));
        return !crossing ||
            (!movableAt(state, crossing->exit) &&
             !playerBlocksAt(state, crossing->exit) &&
             !enemyAt(state, crossing->exit));
    }

    [[nodiscard]] MoveDirection outgoingDirection(
        GridPosition3 from,
        MoveDirection direction) const
    {
        const auto crossing =
            level_.portalCrossing(from, directionOffset(direction));
        return crossing ? rotateDirection(direction, crossing->quarterTurns)
                        : direction;
    }

    [[nodiscard]] bool occupied(GridPosition3 cell) const
    {
        return movableAt(after_, cell) || playerBlocksAt(after_, cell) ||
            enemyAt(after_, cell);
    }

    [[nodiscard]] GridPosition3 transportedLanding(
        GridPosition3 from,
        MoveDirection direction,
        GridPosition3 target) const
    {
        const auto crossing =
            level_.portalCrossing(from, directionOffset(direction));
        if (crossing && target == movementTarget(from, direction)) {
            return crossing->exit;
        }
        return target;
    }

    void recordPortalTransit(
        EntityKind kind,
        EntityId id,
        std::size_t index,
        GridPosition3 from,
        MoveDirection direction,
        GridPosition3 to)
    {
        const auto crossing =
            level_.portalCrossing(from, directionOffset(direction));
        if (portalTransits_ && crossing && crossing->exit == to) {
            portalTransits_->push_back({
                .target = { kind, resolvedEntityId(kind, id, index) },
                .from = from,
                .entrance = from,
                .exit = crossing->exit,
                .direction = rotateDirection(direction, crossing->quarterTurns),
                .entryDirection = direction,
            });
        }
    }

    [[nodiscard]] bool portalCarriesMomentum(
        GridPosition3 origin,
        MoveDirection direction,
        const std::optional<MoveDirection>& sliding) const
    {
        return (level_.portalCrossing(origin, directionOffset(direction)) ||
                level_.portalCrossing(
                    movementTarget(origin, direction),
                    directionOffset(direction))) &&
            (sliding.has_value() ||
             conveyorDirectionAt(level_, after_, origin).has_value());
    }

    [[nodiscard]] std::optional<MoveDirection>& slidingOf(std::size_t index)
    {
        if (isPlayer(index)) {
            return playerSliding(after_, playerIndexForEntity(index));
        }
        if (isEnemy(index)) {
            return after_.enemies[enemyIndexForEntity(index)].sliding;
        }
        return after_.movables[index].sliding;
    }

    [[nodiscard]] GridPosition3 cellOf(std::size_t index) const
    {
        if (isPlayer(index)) {
            return playerCell(after_, playerIndexForEntity(index));
        }
        if (isEnemy(index)) {
            return after_.enemies[enemyIndexForEntity(index)].cell;
        }
        return after_.movables[index].cell;
    }

    [[nodiscard]] std::optional<ChainEntity> visibleMovableForWitch(
        std::size_t playerIndex,
        MoveDirection direction) const
    {
        GridPosition3 current = playerCell(after_, playerIndex);
        std::vector<std::pair<GridPosition3, MoveDirection>> visited;
        const auto inspect =
            [&](GridPosition3 cell) -> std::optional<ChainEntity> {
            if (const auto* movable = movableAt(after_, cell)) {
                return ChainEntity { .movable = true,
                                     .index = static_cast<std::size_t>(
                                         movable - after_.movables.data()) };
            }
            if (const auto* enemy = enemyAt(after_, cell)) {
                return ChainEntity { .movable = false,
                                     .index = static_cast<std::size_t>(
                                         enemy - after_.enemies.data()) };
            }
            return std::nullopt;
        };
        while (true) {
            const auto key = std::pair { current, direction };
            if (std::ranges::find(visited, key) != visited.end()) {
                return std::nullopt;
            }
            visited.push_back(key);
            if (const auto crossing = level_.portalCrossing(
                    current, directionOffset(direction))) {
                current = crossing->exit;
                direction = rotateDirection(direction, crossing->quarterTurns);
            } else {
                current = movementTarget(current, direction);
            }
            if (!cellAllowsEntity(level_, after_, current)) {
                return std::nullopt;
            }
            if (const auto entity = inspect(current)) {
                return entity;
            }
            if (playerBlocksAt(after_, current, playerIndex)) {
                return std::nullopt;
            }
        }
    }

    [[nodiscard]] std::optional<GridPosition3> lilyPadBoardTarget(
        std::size_t playerIndex, MoveDirection direction) const
    {
        const GridPosition3 from = playerCell(after_, playerIndex);
        // Boarding is a local shore step. An occupied portal exit retains
        // the ordinary portal collision rules.
        if (level_.portalCrossing(from, directionOffset(direction))) {
            return std::nullopt;
        }
        const GridPosition3 adjacent = movementTarget(from, direction);
        const auto* pad = movableAt(after_, adjacent);
        const auto support = level_.supportingTileAt(adjacent);
        if (pad == nullptr || pad->type != TileType::LilyPad ||
            !support || !tileTypeIsWater(*support)) {
            return std::nullopt;
        }
        const GridPosition3 top { adjacent.x, adjacent.y, adjacent.z + 1 };
        if (!cellAllowsEntity(level_, after_, top) ||
            movableAt(after_, top) || fallenMovableAt(after_, top) ||
            enemyAt(after_, top) || fallenEnemyAt(after_, top) ||
            playerBlocksAt(after_, top)) {
            return std::nullopt;
        }
        return top;
    }

    struct PlayerMovementIntent {
        MoveDirection direction = MoveDirection::Up;
        int budget = 0;
        bool inputDriven = false;
    };

    [[nodiscard]] std::optional<PlayerMovementIntent> movementIntentForPlayer(
        std::size_t playerIndex) const
    {
        const Status& status = status_[entityIndexForPlayer(playerIndex)];
        if (status.done || !status.active || playerDead(after_, playerIndex) ||
            isUnitLocked(level_, after_, playerCell(after_, playerIndex))) {
            return std::nullopt;
        }
        if (playerSliding(after_, playerIndex)) {
            if (status.consumed < rates_.slide) {
                return PlayerMovementIntent {
                    .direction = *playerSliding(after_, playerIndex),
                    .budget = rates_.slide,
                };
            }
            return std::nullopt;
        }
        if (playerInput_) {
            if (status.consumed < rates_.playerMove) {
                return PlayerMovementIntent {
                    .direction = status.inputDirection.value_or(*playerInput_),
                    .budget = rates_.playerMove,
                    .inputDriven = true,
                };
            }
            return std::nullopt;
        }
        if (const std::optional<MoveDirection> belt =
                conveyorDirectionAt(level_, after_, playerCell(after_, playerIndex))) {
            if (status.consumed < rates_.conveyor) {
                return PlayerMovementIntent {
                    .direction = *belt,
                    .budget = rates_.conveyor,
                };
            }
        }
        return std::nullopt;
    }

    void addBardInfluence(
        std::size_t entityIndex,
        MoveDirection direction,
        int budget)
    {
        Status& status = status_[entityIndex];
        if (status.done) {
            return;
        }
        status.active = true;
        if (status.bardDirection && *status.bardDirection != direction) {
            status.bardDirectionContested = true;
            return;
        }
        status.bardDirection = direction;
        status.bardMoveBudget = std::max(status.bardMoveBudget, budget);
    }

    void deriveBardInfluences()
    {
        if (suppressBardInfluences_) {
            return;
        }
        for (std::size_t playerIndex = 0;
             playerIndex < playerCount_;
             ++playerIndex) {
            if (characterBehavior(
                    after_.players[playerIndex].character.value_or(
                        level_.character())) != CharacterType::Bard) {
                continue;
            }
            const std::optional<PlayerMovementIntent> bardIntent =
                movementIntentForPlayer(playerIndex);
            if (!bardIntent) {
                continue;
            }

            const GridPosition3 bardCell = playerCell(after_, playerIndex);
            auto inAura = [&](GridPosition3 cell) {
                return std::abs(cell.x - bardCell.x) <= 2 &&
                    std::abs(cell.y - bardCell.y) <= 2 &&
                    std::abs(cell.z - bardCell.z) <= 1;
            };
            for (std::size_t i = 0; i < movableCount_; ++i) {
                if (!after_.movables[i].fallen &&
                    !after_.movables[i].dead &&
                    inAura(after_.movables[i].cell)) {
                    addBardInfluence(
                        i, bardIntent->direction, bardIntent->budget);
                }
            }
            for (std::size_t i = 0; i < enemyCount_; ++i) {
                if (!after_.enemies[i].fallen && !after_.enemies[i].dead &&
                    inAura(after_.enemies[i].cell)) {
                    addBardInfluence(
                        entityIndexForEnemy(i),
                        bardIntent->direction,
                        bardIntent->budget);
                }
            }
        }
    }

    [[nodiscard]] bool anyBardMovedThisMicro() const
    {
        for (std::size_t playerIndex = 0;
             playerIndex < playerCount_;
             ++playerIndex) {
            if (characterBehavior(
                    after_.players[playerIndex].character.value_or(
                        level_.character())) == CharacterType::Bard &&
                status_[entityIndexForPlayer(playerIndex)].movedThisMicro) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool anyBardHasMovementIntent() const
    {
        for (std::size_t playerIndex = 0;
             playerIndex < playerCount_;
             ++playerIndex) {
            if (characterBehavior(
                    after_.players[playerIndex].character.value_or(
                        level_.character())) == CharacterType::Bard &&
                movementIntentForPlayer(playerIndex)) {
                return true;
            }
        }
        return false;
    }

    void deriveIntents()
    {
        std::fill(
            enemyMovedThisMicro_.begin(),
            enemyMovedThisMicro_.end(),
            0);
        for (std::size_t i = 0; i < status_.size(); ++i) {
            Status& status = status_[i];
            status.intent.reset();
            status.target.reset();
            status.contested = false;
            status.resolved = false;
            status.movedThisMicro = false;
            status.inputDriven = false;
            status.bardDirection.reset();
            status.bardMoveBudget = 0;
            status.bardDirectionContested = false;
            status.bardDriven = false;
            status.robotDriven = false;
            status.witchSwapTarget.reset();
        }
        std::fill(
            movableMovedDirections_.begin(),
            movableMovedDirections_.end(),
            std::nullopt);

        deriveBardInfluences();

        for (std::size_t i = 0; i < status_.size(); ++i) {
            Status& status = status_[i];

            // Out of scope means scenery: still an obstacle to everyone else,
            // but it wants nothing and will not be written.
            if (status.done || !status.active) {
                continue;
            }
            if (isUnitLocked(level_, after_, cellOf(i))) {
                slidingOf(i).reset();
                continue;
            }
            if (isPlayer(i)) {
                const std::size_t playerIndex = playerIndexForEntity(i);
                if (const std::optional<PlayerMovementIntent> movement =
                        movementIntentForPlayer(playerIndex)) {
                    status.intent = movement->direction;
                    status.inputDriven = movement->inputDriven;
                }
            } else if (isEnemy(i)) {
                const std::size_t enemyIndex = enemyIndexForEntity(i);
                if (after_.enemies[enemyIndex].fallen ||
                    after_.enemies[enemyIndex].dead) {
                    continue;
                }
                if (status.bardDirection &&
                    !status.bardDirectionContested &&
                    status.consumed < status.bardMoveBudget) {
                    status.intent = status.bardDirection;
                    status.bardDriven = true;
                } else if (after_.enemies[enemyIndex].sliding) {
                    if (status.consumed < rates_.slide) {
                        status.intent = after_.enemies[enemyIndex].sliding;
                    }
                } else if (const std::optional<MoveDirection> belt =
                               conveyorDirectionAt(
                                   level_, after_, after_.enemies[enemyIndex].cell)) {
                    if (status.consumed < rates_.conveyor) {
                        status.intent = belt;
                    }
                }
            } else {
                if (after_.movables[i].fallen || after_.movables[i].dead) {
                    continue;
                }
                if (status.bardDirection &&
                    !status.bardDirectionContested &&
                    status.consumed < status.bardMoveBudget) {
                    status.intent = status.bardDirection;
                    status.bardDriven = true;
                } else if (after_.movables[i].sliding) {
                    if (status.consumed < rates_.slide) {
                        status.intent = after_.movables[i].sliding;
                    }
                } else if (const std::optional<MoveDirection> belt =
                               conveyorDirectionAt(level_, after_, after_.movables[i].cell)) {
                    if (status.consumed < rates_.conveyor) {
                        status.intent = belt;
                    }
                } else if (const auto patrol = robotDirection(after_.movables[i]);
                           patrol && status.consumed < rates_.robot) {
                    status.intent = patrol;
                    status.robotDriven = true;
                }
            }

            if (status.intent) {
                status.target = movementDestination(cellOf(i), *status.intent);
                if (isPlayer(i) && status.inputDriven) {
                    const std::size_t playerIndex =
                        playerIndexForEntity(i);
                    const auto boarding = lilyPadBoardTarget(playerIndex, *status.intent);
                    if (boarding) {
                        status.target = boarding;
                    } else if (characterBehavior(
                            after_.players[playerIndex].character.value_or(
                                level_.character())) == CharacterType::Witch) {
                        status.witchSwapTarget = visibleMovableForWitch(
                            playerIndex, *status.intent);
                    }
                    if (status.witchSwapTarget) {
                        const ChainEntity swap = *status.witchSwapTarget;
                        status.target = swap.movable
                            ? after_.movables[swap.index].cell
                            : after_.enemies[swap.index].cell;
                    } else if (!boarding) {
                        status.target =
                            playerLadderClimbTarget(
                                level_,
                                after_,
                                playerIndex,
                                *status.intent)
                                .value_or(*status.target);
                    }
                }
            }
        }
    }

    void markContested()
    {
        for (std::size_t i = 0; i < status_.size(); ++i) {
            if (!status_[i].target) {
                continue;
            }
            for (std::size_t j = i + 1; j < status_.size(); ++j) {
                if (status_[j].target &&
                    *status_[i].target == *status_[j].target) {
                    if (status_[i].bardDriven != status_[j].bardDriven) {
                        Status& follower = status_[i].bardDriven
                            ? status_[i]
                            : status_[j];
                        follower.contested = true;
                    } else {
                        status_[i].contested = true;
                        status_[j].contested = true;
                    }
                }
            }
        }
    }

    // Cancels slide momentum and finishes the entity's step: the treatment
    // for contested destinations and statically impossible moves.
    void cancelAndFinish(std::size_t index, bool onlyWhenSliding)
    {
        if (slidingOf(index)) {
            // A skipping stone loses its support when its forward travel is
            // stopped. Fallen stones fill water exactly like ordinary rocks.
            if (index < movableCount_ &&
                after_.movables[index].type == TileType::RoundRock &&
                isUnfilledWater(level_, after_, cellOf(index))) {
                after_.movables[index].fallen = true;
            }
            slidingOf(index) = std::nullopt;
            status_[index].done = true;
        } else if (!onlyWhenSliding) {
            status_[index].done = true;
        }
    }

    [[nodiscard]] std::optional<std::size_t> roundRockPadAt(
        std::size_t index, GridPosition3 target) const
    {
        if (after_.movables[index].type != TileType::RoundRock) {
            return std::nullopt;
        }
        const auto* pad = movableAt(after_, target);
        if (pad == nullptr || pad->type != TileType::LilyPad ||
            !isUnfilledWater(level_, after_, target) ||
            isUnitLocked(level_, after_, target)) {
            return std::nullopt;
        }
        ++target.z;
        if (!cellAllowsEntity(level_, after_, target) ||
            anyUnitIn(after_, target)) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(pad - after_.movables.data());
    }

    // The wake travels sideways away from the stone on either side of its
    // path. Momentum is ordinary saved state, so these pads join the same
    // causal closure and remain deterministic through undo and replay.
    void pushRoundRockWake(std::size_t index, MoveDirection direction)
    {
        const auto cell = after_.movables[index].cell;
        const bool horizontal = direction == MoveDirection::Left ||
            direction == MoveDirection::Right;
        for (const auto side : horizontal
                 ? std::array { MoveDirection::Up, MoveDirection::Down }
                 : std::array { MoveDirection::Left, MoveDirection::Right }) {
            const auto neighbor = movementTarget(cell, side);
            const auto* pad = movableAt(after_, neighbor);
            if (pad == nullptr || pad->type != TileType::LilyPad ||
                !isUnfilledWater(level_, after_, neighbor) ||
                isUnitLocked(level_, after_, neighbor)) {
                continue;
            }
            const auto padIndex = static_cast<std::size_t>(
                pad - after_.movables.data());
            after_.movables[padIndex].sliding = side;
            status_[padIndex].active = true;
            status_[padIndex].done = false;
        }
    }

    // A bard-driven movable unit has the same one-block shove available to an
    // ordinary hero. This is deliberately not a chain push: the knight keeps
    // that distinct ability, while every aura participant can still advance
    // through a single loose rock, ice block, or turret when space permits.
    [[nodiscard]] bool pushOneMovableForBard(
        std::size_t blockerIndex,
        MoveDirection direction,
        bool pushedByRock = false)
    {
        Status& blockerStatus = status_[blockerIndex];
        if (blockerStatus.movedThisMicro ||
            isUnitLocked(level_, after_, after_.movables[blockerIndex].cell)) {
            return false;
        }
        const GridPosition3 destination =
            movementDestination(after_.movables[blockerIndex].cell, direction);
        if (pushedByRock && after_.movables[blockerIndex].type == TileType::LilyPad &&
            !isUnfilledWater(level_, after_,
                movableFallTarget(level_, after_, blockerIndex, destination).cell)) {
            return false;
        }
        if (!portalEntryClear(
                after_.movables[blockerIndex].cell, direction, after_) ||
            !cellAllowsEntity(level_, after_, destination) ||
            (movableBlocksAt(after_, destination, blockerIndex) &&
                !roundRockPadAt(blockerIndex, destination)) ||
            playerBlocksAt(after_, destination) ||
            enemyBlocksAt(after_, destination) ||
            !canCarryPlatformRiders(blockerIndex, destination, blockerStatus.bardDriven) ||
            (!blockerStatus.bardDriven &&
                !movableFallTarget(
                    level_, after_, blockerIndex, destination)
                     .supported)) {
            return false;
        }

        applyMovableMove(
            blockerIndex,
            direction,
            destination,
            blockerStatus.bardDriven);
        blockerStatus.active = true;
        blockerStatus.resolved = true;
        blockerStatus.movedThisMicro = true;
        blockerStatus.done = false;
        return true;
    }

    [[nodiscard]] bool resolveMoves()
    {
        for (Status& status : status_) {
            status.resolved = !status.intent.has_value();
        }

        bool anyMovement = false;
        bool progressed = true;
        while (progressed) {
            progressed = false;
            for (std::size_t i = 0; i < status_.size(); ++i) {
                if (status_[i].resolved) {
                    continue;
                }
                progressed |= isPlayer(i)
                    ? resolvePlayer(i, anyMovement)
                    : (isEnemy(i)
                            ? resolveEnemy(i, anyMovement)
                            : resolveMovable(i, anyMovement));
            }
        }
        return anyMovement;
    }

    // A successful move is the trigger, not an intent: this catches pushes,
    // pulls and chain movement as well as slides and conveyors. Followers are
    // attempted farthest-first along the movement direction, allowing a row
    // of linked objects to vacate into one another's cells. A blocked follower
    // simply stays put and never cancels the move that triggered it.
    [[nodiscard]] bool resolveLinkedMoves()
    {
        bool anyMovement = false;
        std::vector<char> handled(movableCount_, 0);
        for (std::size_t source = 0; source < movableCount_; ++source) {
            if (handled[source] || !movableMovedDirections_[source] ||
                !level_.movableLinkColor(source)) {
                continue;
            }

            const MoveDirection direction = *movableMovedDirections_[source];
            std::vector<std::size_t> members;
            bool conflictingDirections = false;
            for (std::size_t index = 0; index < movableCount_; ++index) {
                if (!level_.movablesAreLinked(source, index)) {
                    continue;
                }
                handled[index] = 1;
                members.push_back(index);
                if (movableMovedDirections_[index] &&
                    *movableMovedDirections_[index] != direction) {
                    conflictingDirections = true;
                }
            }
            if (conflictingDirections) {
                continue;
            }
            const bool rockDriven = std::ranges::any_of(members, [&](std::size_t index) {
                const auto type = after_.movables[index].type;
                return movableMovedDirections_[index] &&
                    (type == TileType::Rock || type == TileType::RoundRock);
            });

            const GridPosition offset = directionOffset(direction);
            std::ranges::sort(members, {}, [&](std::size_t index) {
                const GridPosition3 cell = after_.movables[index].cell;
                return -(cell.x * offset.x + cell.y * offset.y);
            });
            for (const std::size_t index : members) {
                GameState::Movable& movable = after_.movables[index];
                Status& status = status_[index];
                if (status.movedThisMicro || movable.fallen || movable.dead ||
                    isUnitLocked(level_, after_, movable.cell)) {
                    continue;
                }
                const GridPosition3 destination =
                    movementDestination(movable.cell, direction);
                if (rockDriven && movable.type == TileType::LilyPad &&
                    !isUnfilledWater(level_, after_,
                        movableFallTarget(level_, after_, index, destination).cell)) {
                    continue;
                }
                if (!portalEntryClear(movable.cell, direction, after_) ||
                    !cellAllowsEntity(level_, after_, destination) ||
                    movableBlocksAt(after_, destination, index) ||
                    playerBlocksAt(after_, destination) ||
                    enemyBlocksAt(after_, destination) ||
                    !canCarryPlatformRiders(index, destination) ||
                    !movableFallTarget(level_, after_, index, destination)
                         .supported) {
                    continue;
                }
                applyMovableMove(index, direction, destination);
                status.active = true;
                status.resolved = true;
                status.movedThisMicro = true;
                status.done = false;
                anyMovement = true;
            }
        }
        return anyMovement;
    }

    [[nodiscard]] bool resolveMovable(std::size_t index, bool& anyMovement)
    {
        Status& status = status_[index];
        if (isUnitLocked(level_, after_, cellOf(index))) {
            slidingOf(index).reset();
            status.resolved = true;
            return true;
        }
        const MoveDirection direction = *status.intent;
        const GridPosition3 target = *status.target;

        const auto crossing =
            level_.portalCrossing(cellOf(index), directionOffset(direction));
        if (!status.contested && crossing && target == crossing->exit &&
            occupied(crossing->exit)) {
            return false;
        }

        if (status.contested) {
            cancelAndFinish(index, true);
            status.resolved = true;
            return true;
        }
        if (status.robotDriven) {
            return resolveRobot(index, anyMovement);
        }
        if (!cellAllowsEntity(level_, after_, target) ||
            (!status.bardDriven &&
                !movableFallTarget(level_, after_, index, target).supported)) {
            cancelAndFinish(index, false);
            status.resolved = true;
            return true;
        }
        const auto boardingPad = roundRockPadAt(index, target);
        const bool skipping = after_.movables[index].type == TileType::RoundRock &&
            after_.movables[index].sliding &&
            isUnfilledWater(level_, after_, cellOf(index));
        const bool coasting = after_.movables[index].type == TileType::LilyPad &&
            after_.movables[index].sliding &&
            isUnfilledWater(level_, after_, cellOf(index));
        if ((skipping || coasting) && !boardingPad) {
            if (const auto blocker = liveUnitAt(after_, target)) {
                if (!status_[entityIndexForUnit(*blocker)].resolved) {
                    return false;
                }
                cancelAndFinish(index, false);
                status.resolved = true;
                return true;
            }
        }
        // Water currents and stone impulses stop on the water side of the
        // bank. A hero's ordinary shove can still beach an idle leaf.
        const bool padCurrent = after_.movables[index].type == TileType::LilyPad &&
            !status.bardDriven && !after_.movables[index].sliding &&
            !conveyorDirectionForTile(tileAt(level_, cellOf(index))) &&
            waterStreamDirectionAt(level_, cellOf(index)).has_value();
        if ((padCurrent || coasting) && !isUnfilledWater(level_, after_,
                movableFallTarget(level_, after_, index, target).cell)) {
            cancelAndFinish(index, false);
            status.resolved = true;
            return true;
        }
        // A rider in the destination column may still leave with another
        // platform this micro-step. Retry after that platform has resolved.
        if (!canCarryPlatformRiders(index, target, status.bardDriven)) {
            return false;
        }
        if (const GameState::Movable* blocker = movableAt(after_, target);
            blocker != nullptr && !boardingPad) {
            const std::size_t blockerIndex = static_cast<std::size_t>(
                blocker - after_.movables.data());
            if (blockerIndex != index && !status_[blockerIndex].resolved) {
                return false;
            }
            if (!status.bardDriven || blockerIndex == index ||
                !pushOneMovableForBard(blockerIndex, direction,
                    after_.movables[index].type == TileType::Rock ||
                        after_.movables[index].type == TileType::RoundRock)) {
                cancelAndFinish(index, true);
                status.resolved = true;
                return true;
            }
        }
        if (playerBlocksAt(after_, target)) {
            return false; // the blocking entity may still move this micro-step
        }
        if (const GameState::Enemy* enemy = enemyAt(after_, target)) {
            const std::size_t enemyIndex =
                static_cast<std::size_t>(enemy - after_.enemies.data());
            if (!status_[entityIndexForEnemy(enemyIndex)].resolved) {
                return false;
            }
            if (!pushEnemy(enemyIndex, direction)) {
                slidingOf(index) = std::nullopt;
                status.done = true;
                status.resolved = true;
                return true;
            }
        }
        applyMovableMove(index, direction, target, status.bardDriven);
        status.resolved = true;
        status.movedThisMicro = true;
        anyMovement = true;
        return true;
    }

    [[nodiscard]] std::size_t entityIndexForUnit(LiveUnitAt unit) const
    {
        return unit.kind == EntityKind::Player ? entityIndexForPlayer(unit.index)
            : unit.kind == EntityKind::Enemy ? entityIndexForEnemy(unit.index)
                                             : unit.index;
    }

    [[nodiscard]] bool canCarryPlatformRiders(
        std::size_t index, GridPosition3 target, bool preserveElevation = false) const
    {
        if (!tileTypeIsMovablePlatform(after_.movables[index].type)) {
            return true;
        }
        for (const LiveUnitAt rider : platformRiders(after_, index)) {
            if (status_[entityIndexForUnit(rider)].movedThisMicro) {
                return false;
            }
        }
        const auto landing = preserveElevation ? target
            : movableFallTarget(level_, after_, index, target).cell;
        return platformRidersCanMove(level_, after_, index, landing, target.z);
    }

    // Validate a robot's one-unit shove on a scratch board. This deliberately
    // never recurses: a second unit in front of the victim blocks the motor.
    [[nodiscard]] bool previewRobotPush(
        GameState& trial, LiveUnitAt unit, MoveDirection direction) const
    {
        const GridPosition3 from = unitCell(trial, unit);
        GridPosition3 target = movementDestination(from, direction);
        const bool boardingPad = unit.kind == EntityKind::Movable &&
            roundRockPadAt(unit.index, target).has_value();
        if (isUnitLocked(level_, trial, from) ||
            !portalEntryClear(from, direction, trial) ||
            !cellAllowsEntity(level_, trial, target) ||
            (liveUnitAt(trial, target) && !boardingPad)) {
            return false;
        }
        if (boardingPad) ++target.z;
        FallResult fall;
        if (unit.kind == EntityKind::Player) {
            fall = playerFallTarget(level_, trial, unit.index, target);
        } else if (unit.kind == EntityKind::Enemy) {
            fall = enemyFallTarget(level_, trial, unit.index, target);
        } else {
            fall = movableFallTarget(level_, trial, unit.index, target);
        }
        if (!fall.supported) {
            return false;
        }
        if (unit.kind == EntityKind::Movable &&
            tileTypeIsMovablePlatform(trial.movables[unit.index].type)) {
            if (!platformRidersCanMove(level_, trial, unit.index, fall.cell, target.z)) {
                return false;
            }
            for (const auto rider : platformRiders(trial, unit.index)) {
                if (status_[entityIndexForUnit(rider)].movedThisMicro) {
                    return false;
                }
                const auto cell = unitCell(trial, rider);
                setUnitCell(trial, rider,
                    { fall.cell.x, fall.cell.y, fall.cell.z + cell.z - from.z });
            }
        }
        setUnitCell(trial, unit, fall.cell);
        return true;
    }

    [[nodiscard]] bool resolveRobot(std::size_t index, bool& anyMovement)
    {
        Status& status = status_[index];
        const MoveDirection direction = *status.intent;
        const GridPosition3 target = *status.target;
        if (!cellAllowsEntity(level_, after_, target)) {
            status.resolved = true;
            return true;
        }
        const auto blocker = liveUnitAt(after_, target);
        if (blocker && !status_[entityIndexForUnit(*blocker)].resolved) {
            return false;
        }
        // Portals require an empty exit, just as they do for hero pushes.
        if (blocker && level_.portalCrossing(
                cellOf(index), directionOffset(direction))) {
            status.resolved = true;
            return true;
        }
        GameState trial = after_;
        if (blocker &&
            (status_[entityIndexForUnit(*blocker)].movedThisMicro ||
                !previewRobotPush(trial, *blocker, direction))) {
            status.resolved = true;
            return true;
        }
        const FallResult fall = movableFallTarget(level_, trial, index, target);
        // A patrol never deliberately changes elevation or enters water.
        // Being pushed retains the ordinary movable fall rules.
        if (!fall.supported || fall.fallen || fall.cell != target ||
            !platformRidersCanMove(level_, trial, index, target)) {
            status.resolved = true;
            return true;
        }
        for (const auto rider : platformRiders(after_, index)) {
            if (status_[entityIndexForUnit(rider)].movedThisMicro) {
                status.resolved = true;
                return true;
            }
        }
        if (blocker) {
            if (blocker->kind == EntityKind::Movable) {
                applyMovableMove(blocker->index, direction,
                    movementDestination(unitCell(after_, *blocker), direction));
            } else if (blocker->kind == EntityKind::Enemy) {
                applyEnemyMove(blocker->index, direction);
            } else {
                applyPlayerMove(entityIndexForPlayer(blocker->index), direction,
                    movementDestination(unitCell(after_, *blocker), direction));
            }
            Status& pushed = status_[entityIndexForUnit(*blocker)];
            pushed.active = true;
            pushed.resolved = true;
            pushed.movedThisMicro = true;
            pushed.done = false;
        }
        applyMovableMove(index, direction, target);
        status.resolved = true;
        status.movedThisMicro = true;
        anyMovement = true;
        return true;
    }

    [[nodiscard]] bool resolveEnemy(
        std::size_t entityIndex,
        bool& anyMovement)
    {
        Status& status = status_[entityIndex];
        if (isUnitLocked(level_, after_, cellOf(entityIndex))) {
            slidingOf(entityIndex).reset();
            status.resolved = true;
            return true;
        }
        const std::size_t enemyIndex = enemyIndexForEntity(entityIndex);
        const MoveDirection direction = *status.intent;
        const GridPosition3 target = *status.target;

        const auto crossing = level_.portalCrossing(
            cellOf(entityIndex), directionOffset(direction));
        if (!status.contested && crossing && target == crossing->exit &&
            occupied(crossing->exit)) {
            return false;
        }

        if (status.contested) {
            cancelAndFinish(entityIndex, true);
            status.resolved = true;
            return true;
        }
        if (!cellAllowsEntity(level_, after_, target) ||
            (!status.bardDriven &&
                !enemyFallTarget(level_, after_, enemyIndex, target).supported)) {
            after_.enemies[enemyIndex].sliding.reset();
            status.done = true;
            status.resolved = true;
            return true;
        }

        if (const GameState::Movable* movable = movableAt(after_, target)) {
            const std::size_t blockerIndex = static_cast<std::size_t>(
                movable - after_.movables.data());
            if (!status_[blockerIndex].resolved) {
                return false;
            }
            if (!status.bardDriven ||
                !pushOneMovableForBard(blockerIndex, direction)) {
                cancelAndFinish(entityIndex, true);
                status.resolved = true;
                return true;
            }
        }
        for (std::size_t i = 0; i < playerCount_; ++i) {
            if (playerDead(after_, i) || !(playerCell(after_, i) == target)) {
                continue;
            }
            if (!status_[entityIndexForPlayer(i)].resolved) {
                return false;
            }
            cancelAndFinish(entityIndex, true);
            status.resolved = true;
            return true;
        }
        if (const GameState::Enemy* enemy = enemyAt(after_, target)) {
            const std::size_t blockerIndex = static_cast<std::size_t>(
                enemy - after_.enemies.data());
            if (blockerIndex != enemyIndex &&
                !status_[entityIndexForEnemy(blockerIndex)].resolved) {
                return false;
            }
            cancelAndFinish(entityIndex, true);
            status.resolved = true;
            return true;
        }

        applyEnemyMove(enemyIndex, direction, status.bardDriven);
        status.resolved = true;
        status.movedThisMicro = true;
        anyMovement = true;
        return true;
    }

    [[nodiscard]] bool resolvePlayer(
        std::size_t entityIndex,
        bool& anyMovement)
    {
        Status& status = status_[entityIndex];
        if (isUnitLocked(level_, after_, cellOf(entityIndex))) {
            slidingOf(entityIndex).reset();
            status.resolved = true;
            return true;
        }
        const std::size_t playerIndex = playerIndexForEntity(entityIndex);
        const MoveDirection direction = *status.intent;
        const GridPosition3 target = *status.target;

        const auto crossing = level_.portalCrossing(
            cellOf(entityIndex), directionOffset(direction));
        if (!status.contested && !status.witchSwapTarget && crossing &&
            target == crossing->exit &&
            occupied(crossing->exit)) {
            return false;
        }

        if (status.contested) {
            cancelAndFinish(entityIndex, true);
            status.resolved = true;
            return true;
        }
        if (status.witchSwapTarget && isUnitLocked(level_, after_, target)) {
            status.resolved = true;
            return true;
        }
        if (status.witchSwapTarget) {
            const ChainEntity swap = *status.witchSwapTarget;
            // In a whole-world step the target may also have an automatic
            // intent. Let it resolve first, then refuse a stale spell rather
            // than teleporting an entity the witch can no longer see.
            if (swap.movable) {
                if (!status_[swap.index].resolved) {
                    return false;
                }
                if (status_[swap.index].movedThisMicro ||
                    !(after_.movables[swap.index].cell == target)) {
                    status.resolved = true;
                    return true;
                }
            } else {
                const std::size_t enemyEntityIndex =
                    entityIndexForEnemy(swap.index);
                if (!status_[enemyEntityIndex].resolved) {
                    return false;
                }
                if (status_[enemyEntityIndex].movedThisMicro ||
                    !(after_.enemies[swap.index].cell == target)) {
                    status.resolved = true;
                    return true;
                }
            }

            const GridPosition3 origin = playerCell(after_, playerIndex);
            playerCell(after_, playerIndex) = target;
            playerSliding(after_, playerIndex).reset();
            if (swap.movable) {
                after_.movables[swap.index].cell = origin;
                after_.movables[swap.index].sliding.reset();
            } else {
                after_.enemies[swap.index].cell = origin;
                after_.enemies[swap.index].sliding.reset();
                enemyMoved_[swap.index] = true;
                enemyMovedThisMicro_[swap.index] = true;
                Status& enemyStatus =
                    status_[entityIndexForEnemy(swap.index)];
                enemyStatus.active = true;
                enemyStatus.resolved = true;
                enemyStatus.movedThisMicro = true;
                enemyStatus.done = true;
            }

            ++status.consumed;
            status.resolved = true;
            status.movedThisMicro = true;
            status.done = true;
            if (swap.movable) {
                status_[swap.index].active = true;
                status_[swap.index].resolved = true;
                status_[swap.index].movedThisMicro = true;
                status_[swap.index].done = true;
            }
            anyMovement = true;
            return true;
        }
        if (!cellAllowsEntity(level_, after_, target)) {
            playerSliding(after_, playerIndex) = std::nullopt;
            status.done = true;
            status.resolved = true;
            return true;
        }
        const FallResult fall =
            playerFallTarget(level_, after_, playerIndex, target);
        // A deliberate walk stops at unfilled water. Forced motion keeps the
        // ordinary fall result, so slides and conveyor rides can still drown.
        if (!fall.supported || (status.inputDriven && fall.fallen)) {
            playerSliding(after_, playerIndex) = std::nullopt;
            status.done = true;
            status.resolved = true;
            return true;
        }
        if (playerBlocksAt(after_, target, playerIndex)) {
            return false;
        }
        if (characterBehavior(
                after_.players[playerIndex].character.value_or(
                    level_.character())) == CharacterType::Druid) {
            const GridPosition3 pullSource = movementDestination(
                playerCell(after_, playerIndex), oppositeDirection(direction));
            if (const GameState::Enemy* enemy = enemyAt(after_, pullSource)) {
                const std::size_t enemyIndex = static_cast<std::size_t>(
                    enemy - after_.enemies.data());
                if (!status_[entityIndexForEnemy(enemyIndex)].resolved) {
                    return false;
                }
            }
        }
        if (status.inputDriven &&
            characterBehavior(
                after_.players[playerIndex].character.value_or(
                    level_.character())) == CharacterType::Knight) {
            const std::vector<ChainEntity> chain = pushChainAt(target, direction);
            if (!chain.empty()) {
                // A movable with its own unresolved intent gets the same chance
                // to vacate that the ordinary one-block push gives it.
                for (const ChainEntity& entity : chain) {
                    const std::size_t statusIndex = entity.movable
                        ? entity.index
                        : entityIndexForEnemy(entity.index);
                    if (!status_[statusIndex].resolved) {
                        return false;
                    }
                }
                if (pushKnightChain(chain, direction)) {
                    applyPlayerMoveAndPull(entityIndex, direction, target);
                    status.movedThisMicro = true;
                    anyMovement = true;
                }
                status.resolved = true;
                return true;
            }
        }
        if (const GameState::Enemy* blocker = enemyAt(after_, target)) {
            const std::size_t enemyIndex = static_cast<std::size_t>(
                blocker - after_.enemies.data());
            if (!status_[entityIndexForEnemy(enemyIndex)].resolved) {
                return false;
            }
            const bool rogue = characterBehavior(
                after_.players[playerIndex].character.value_or(
                    level_.character())) == CharacterType::Rogue;
            if (status.inputDriven && rogue &&
                !enemyMovedThisMicro_[enemyIndex] &&
                pushEnemy(enemyIndex, direction)) {
                applyPlayerMoveAndPull(entityIndex, direction, target);
                status.movedThisMicro = true;
                anyMovement = true;
            } else if (playerSliding(after_, playerIndex)) {
                playerSliding(after_, playerIndex).reset();
                status.done = true;
            }
            status.resolved = true;
            return true;
        }
        if (const GameState::Movable* blocker = movableAt(after_, target)) {
            const auto blockerIndex =
                static_cast<std::size_t>(blocker - after_.movables.data());
            if (!status_[blockerIndex].resolved) {
                return false; // wait for the blocker to resolve first
            }
            // The blocker has finished its own movement for this micro-step.
            // Direct input may push it.
            const GridPosition3 pushTarget =
                movementDestination(target, direction);
            const GameState::Enemy* pushedEnemy = enemyAt(after_, pushTarget);
            if (pushedEnemy != nullptr) {
                const std::size_t enemyIndex = static_cast<std::size_t>(
                    pushedEnemy - after_.enemies.data());
                if (!status_[entityIndexForEnemy(enemyIndex)].resolved) {
                    return false;
                }
            }
            const bool enemyCanMove = pushedEnemy == nullptr ||
                canPushEnemy(
                    static_cast<std::size_t>(pushedEnemy - after_.enemies.data()),
                    direction);
            const bool druid = characterBehavior(
                after_.players[playerIndex].character.value_or(
                    level_.character())) == CharacterType::Druid;
            const bool occupiedPortalExit =
                level_.portalCrossing(target, directionOffset(direction))
                    .has_value() &&
                occupied(pushTarget);
            if (status.inputDriven && !druid &&
                !isUnitLocked(level_, after_, target) &&
                !status_[blockerIndex].movedThisMicro &&
                !occupiedPortalExit &&
                portalEntryClear(target, direction, after_) &&
                cellAllowsEntity(level_, after_, pushTarget) &&
                (!movableBlocksAt(after_, pushTarget, blockerIndex) ||
                    roundRockPadAt(blockerIndex, pushTarget)) &&
                !playerBlocksAt(after_, pushTarget, playerIndex) &&
                enemyCanMove &&
                canCarryPlatformRiders(blockerIndex, pushTarget) &&
                movableFallTarget(level_, after_, blockerIndex, pushTarget)
                    .supported) {
                if (pushedEnemy != nullptr) {
                    (void)pushEnemy(
                        static_cast<std::size_t>(pushedEnemy - after_.enemies.data()),
                        direction);
                }
                applyMovableMove(blockerIndex, direction, pushTarget);
                status_[blockerIndex].movedThisMicro = true;
                status_[blockerIndex].done = false;
                // Pushing it is what makes it part of this action: it has been
                // written, and if it lands on ice it has to keep sliding under
                // the same action rather than be left for whatever comes next.
                status_[blockerIndex].active = true;
                applyPlayerMoveAndPull(entityIndex, direction, target);
                status.movedThisMicro = true;
                anyMovement = true;
            } else if (playerSliding(after_, playerIndex)) {
                // Blocked slides stop for good.
                playerSliding(after_, playerIndex) = std::nullopt;
                status.done = true;
            }
            status.resolved = true;
            return true;
        }
        applyPlayerMoveAndPull(entityIndex, direction, target);
        status.resolved = true;
        status.movedThisMicro = true;
        anyMovement = true;
        return true;
    }

    [[nodiscard]] std::optional<ChainEntity> pushableAt(
        const GameState& state,
        GridPosition3 cell) const
    {
        if (const GameState::Movable* movable = movableAt(state, cell)) {
            return ChainEntity {
                .movable = true,
                .index = static_cast<std::size_t>(
                    movable - state.movables.data()),
            };
        }
        if (const GameState::Enemy* enemy = enemyAt(state, cell)) {
            return ChainEntity {
                .movable = false,
                .index = static_cast<std::size_t>(
                    enemy - state.enemies.data()),
            };
        }
        return std::nullopt;
    }

    [[nodiscard]] std::vector<ChainEntity> pushChainAt(
        GridPosition3 firstCell,
        MoveDirection direction) const
    {
        std::vector<ChainEntity> chain;
        GridPosition3 cell = firstCell;
        while (const std::optional<ChainEntity> entity =
                   pushableAt(after_, cell)) {
            if (std::ranges::any_of(chain, [&](const ChainEntity& seen) {
                    return seen.movable == entity->movable &&
                        seen.index == entity->index;
                })) {
                return {};
            }
            chain.push_back(*entity);
            // A floating leaf is the round stone's landing support. It does
            // not become another block in the Knight's shove chain.
            if (entity->movable && roundRockPadAt(entity->index,
                    movementTarget(cell, direction))) {
                break;
            }
            if (level_.portalCrossing(cell, directionOffset(direction))) {
                break;
            }
            cell = movementTarget(cell, direction);
        }
        return chain;
    }

    // Validates the complete mixed chain against a copy before committing any
    // entity. Farthest-first movement then gives every block, turret, or enemy
    // exactly one newly vacated destination and keeps a failed push atomic.
    [[nodiscard]] bool pushKnightChain(
        const std::vector<ChainEntity>& chain,
        MoveDirection direction)
    {
        GameState trial = after_;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const ChainEntity entity = *it;
            const GridPosition3 origin = entity.movable
                ? trial.movables[entity.index].cell
                : trial.enemies[entity.index].cell;
            if (isUnitLocked(level_, trial, origin)) {
                return false;
            }
            if (entity.movable) {
                if (status_[entity.index].movedThisMicro) {
                    return false;
                }
                GridPosition3 destination = movementDestination(
                    trial.movables[entity.index].cell, direction);
                const bool boardingPad =
                    roundRockPadAt(entity.index, destination).has_value();
                if (!portalEntryClear(
                        trial.movables[entity.index].cell, direction, trial) ||
                    !cellAllowsEntity(level_, trial, destination) ||
                    (movableBlocksAt(trial, destination, entity.index) && !boardingPad) ||
                    enemyBlocksAt(trial, destination) ||
                    playerBlocksAt(trial, destination)) {
                    return false;
                }
                if (boardingPad) ++destination.z;
                const FallResult fall = movableFallTarget(
                    level_, trial, entity.index, destination);
                const bool rockDrivenPad = trial.movables[entity.index].type == TileType::LilyPad &&
                    std::ranges::any_of(chain.begin(), it.base() - 1,
                        [&](const ChainEntity& pusher) {
                            if (!pusher.movable) return false;
                            const auto type = trial.movables[pusher.index].type;
                            return type == TileType::Rock || type == TileType::RoundRock;
                        });
                if (rockDrivenPad && !isUnfilledWater(level_, trial, fall.cell)) {
                    return false;
                }
                if (!fall.supported ||
                    !platformRidersCanMove(level_, trial, entity.index, fall.cell,
                        destination.z)) {
                    return false;
                }
                for (const auto rider : platformRiders(trial, entity.index)) {
                    if (status_[entityIndexForUnit(rider)].movedThisMicro) {
                        return false;
                    }
                    const GridPosition3 cell = unitCell(trial, rider);
                    setUnitCell(trial, rider, { fall.cell.x, fall.cell.y,
                        fall.cell.z + cell.z - origin.z });
                }
                trial.movables[entity.index].cell = fall.cell;
                trial.movables[entity.index].fallen = fall.fallen;
            } else {
                const GridPosition3 destination = movementDestination(
                    trial.enemies[entity.index].cell, direction);
                if (!portalEntryClear(
                        trial.enemies[entity.index].cell, direction, trial) ||
                    !cellAllowsEntity(level_, trial, destination) ||
                    movableBlocksAt(
                        trial, destination, trial.movables.size()) ||
                    enemyBlocksAt(trial, destination, entity.index) ||
                    playerBlocksAt(trial, destination)) {
                    return false;
                }
                const FallResult fall = enemyFallTarget(
                    level_, trial, entity.index, destination);
                if (!fall.supported) {
                    return false;
                }
                trial.enemies[entity.index].cell = fall.cell;
                trial.enemies[entity.index].fallen = fall.fallen;
            }
        }

        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const ChainEntity entity = *it;
            if (entity.movable) {
                const GridPosition3 destination = movementDestination(
                    after_.movables[entity.index].cell, direction);
                applyMovableMove(entity.index, direction, destination);
                status_[entity.index].movedThisMicro = true;
                status_[entity.index].done = false;
                status_[entity.index].active = true;
            } else {
                applyEnemyMove(entity.index, direction);
            }
        }
        return true;
    }

    [[nodiscard]] bool canPushEnemy(
        std::size_t enemyIndex,
        MoveDirection direction) const
    {
        const GridPosition3 destination =
            movementDestination(after_.enemies[enemyIndex].cell, direction);
        return !isUnitLocked(level_, after_, after_.enemies[enemyIndex].cell) &&
            portalEntryClear(
                after_.enemies[enemyIndex].cell, direction, after_) &&
            cellAllowsEntity(level_, after_, destination) &&
            !movableBlocksAt(after_, destination, after_.movables.size()) &&
            !playerBlocksAt(after_, destination) &&
            !enemyBlocksAt(after_, destination, enemyIndex) &&
            enemyFallTarget(level_, after_, enemyIndex, destination).supported;
    }

    [[nodiscard]] bool pushEnemy(
        std::size_t enemyIndex,
        MoveDirection direction)
    {
        if (!canPushEnemy(enemyIndex, direction)) {
            return false;
        }
        applyEnemyMove(enemyIndex, direction);
        return true;
    }

    void applyEnemyMove(
        std::size_t enemyIndex,
        MoveDirection direction,
        bool preserveElevation = false)
    {
        const GridPosition3 destination =
            movementDestination(after_.enemies[enemyIndex].cell, direction);
        const bool portalMomentum = portalCarriesMomentum(
            after_.enemies[enemyIndex].cell,
            direction,
            after_.enemies[enemyIndex].sliding);
        const MoveDirection outgoing =
            outgoingDirection(after_.enemies[enemyIndex].cell, direction);
        recordPortalTransit(
            EntityKind::Enemy,
            after_.enemies[enemyIndex].id,
            enemyIndex,
            after_.enemies[enemyIndex].cell,
            direction,
            destination);
        direction = outgoing;
        after_.enemies[enemyIndex].cell = destination;
        const FallResult fall = preserveElevation
            ? FallResult { .cell = destination, .supported = true }
            : enemyFallTarget(level_, after_, enemyIndex, destination);
        after_.enemies[enemyIndex].cell = fall.cell;
        after_.enemies[enemyIndex].fallen = fall.fallen;
        const bool fell = fall.cell != destination || fall.fallen;
        after_.enemies[enemyIndex].sliding =
            (!fell &&
             (portalMomentum || isIceFloor(level_, after_, fall.cell)) &&
             cellAllowsEntity(
                 level_, after_, movementDestination(fall.cell, direction)))
            ? std::optional<MoveDirection>(direction)
            : std::nullopt;
        Status& status = status_[entityIndexForEnemy(enemyIndex)];
        ++status.consumed;
        status.active = true;
        status.resolved = true;
        status.movedThisMicro = true;
        status.done = fall.fallen;
        // Shoved, therefore written, therefore this action's responsibility -
        // including for whoever it has just been parked next to.
        enemyMoved_[enemyIndex] = true;
        enemyMovedThisMicro_[enemyIndex] = true;
    }

    // Gates act as soon as each micro-step's movement settles, so a unit
    // sliding over a gate that opens falls through it there, and a hero in a
    // gate that closes dies there. Dropped units join the closure because
    // they are written, and count as having just moved for turrets and
    // attacks. One that was not acting (scenery to this step) only falls: it
    // must not pick up this step's input, and a belt it lands on moves it as
    // pending motion on the next step. Crushed units are written too, and
    // are finished.
    [[nodiscard]] bool resolveGateChanges()
    {
        if (gatesOpen_.empty()) {
            // Locks and moving platforms also read physical plate activity,
            // as can a later action on a board without gates.
            plateActivity_.observe(after_);
            return false;
        }
        const GateChanges changes =
            applyGateChanges(level_, after_, gatesOpen_, plateActivity_);
        const auto joinClosure = [this](std::size_t entity, bool finished) {
            Status& status = status_[entity];
            if (!status.active || finished) {
                status.done = true;
            }
            status.active = true;
            status.movedThisMicro = true;
        };
        const UnitIndices& dropped = changes.dropped;
        for (const std::size_t index : dropped.players) {
            joinClosure(entityIndexForPlayer(index), playerDead(after_, index));
        }
        for (const std::size_t index : dropped.movables) {
            joinClosure(index, after_.movables[index].fallen);
        }
        for (const std::size_t index : dropped.enemies) {
            joinClosure(entityIndexForEnemy(index), after_.enemies[index].fallen);
            enemyMoved_[index] = true;
            enemyMovedThisMicro_[index] = true;
        }
        const auto finish = [this](std::size_t entity) {
            Status& status = status_[entity];
            status.active = true;
            status.done = true;
        };
        for (const std::size_t index : changes.crushed.players) {
            finish(entityIndexForPlayer(index));
        }
        for (const std::size_t index : changes.crushed.movables) {
            finish(index);
        }
        for (const std::size_t index : changes.crushed.enemies) {
            finish(entityIndexForEnemy(index));
        }
        return !changes.empty();
    }

    // Rotator plates act once the step's movement has settled: a plate is
    // pressed by where units end up, not by what passes over it mid-step.
    // Turned units join the action's closure because they are written. A
    // turret that turns sweeps its new line of fire once, so it shoots a
    // target already standing there rather than waiting for it to move.
    void resolveRotators()
    {
        const RotatedOccupants rotated = applyRotatorActivations(
            level_, after_, rotatorsEngagedAtStart_);
        if (rotated.empty()) {
            return;
        }
        for (const std::size_t index : rotated.players) {
            status_[entityIndexForPlayer(index)].active = true;
        }
        for (const std::size_t index : rotated.enemies) {
            status_[entityIndexForEnemy(index)].active = true;
        }
        std::vector<char> sweeping(movableCount_, 0);
        bool anyTurret = false;
        for (const std::size_t index : rotated.movables) {
            status_[index].active = true;
            if (turretDirection(after_.movables[index])) {
                sweeping[index] = 1;
                anyTurret = true;
            }
        }
        if (anyTurret) {
            resolveReactions(&sweeping);
        }
    }

    // Elevators act after rotators, once the step's movement has settled, on
    // the same edge rule. Carried units join the closure because they are
    // written, and count as having just moved: a turret sees a unit carried
    // into its line, and an enemy carried next to a hero (or a hero carried
    // next to an enemy) attacks. A carried turret sweeps its line at the new
    // level, like a turret a rotator has turned.
    void resolveElevators()
    {
        const ElevatorRiders carried = applyElevatorActivations(
            level_, after_, elevatorsEngagedAtStart_, plateActivity_);
        if (carried.empty()) {
            return;
        }
        for (const std::size_t index : carried.players) {
            Status& status = status_[entityIndexForPlayer(index)];
            status.active = true;
            status.movedThisMicro = true;
        }
        for (const std::size_t index : carried.enemies) {
            Status& status = status_[entityIndexForEnemy(index)];
            status.active = true;
            status.movedThisMicro = true;
            enemyMoved_[index] = true;
            enemyMovedThisMicro_[index] = true;
        }
        for (const std::size_t index : carried.movables) {
            status_[index].active = true;
            status_[index].movedThisMicro = true;
        }
        // An empty platform can also uncover a ray or change a gate's state.
        resolveReactions();
    }

    // Minecarts use the elevator's edge-trigger and rider semantics, but move
    // horizontally between stops derived from their oriented rail route.
    void resolveMinecarts()
    {
        const MinecartRiders carried = applyMinecartActivations(
            level_, after_, minecartsEngagedAtStart_, plateActivity_);
        if (carried.empty()) {
            return;
        }
        for (const std::size_t index : carried.players) {
            Status& status = status_[entityIndexForPlayer(index)];
            status.active = true;
            status.movedThisMicro = true;
        }
        for (const std::size_t index : carried.enemies) {
            Status& status = status_[entityIndexForEnemy(index)];
            status.active = true;
            status.movedThisMicro = true;
            enemyMoved_[index] = true;
            enemyMovedThisMicro_[index] = true;
        }
        for (const std::size_t index : carried.movables) {
            status_[index].active = true;
            status_[index].movedThisMicro = true;
        }
        resolveReactions();
    }

    // Track visibility by entity, including inactive targets. Comparing with
    // the previous reaction catches a blocker moving/falling, a gate opening,
    // or a platform moving away, without firing at unchanged idle sightlines.
    using TurretSightlines = std::vector<std::vector<char>>;

    [[nodiscard]] TurretSightlines captureTurretSightlines() const
    {
        TurretSightlines visible(movableCount_);
        for (std::size_t turretIndex = 0; turretIndex < movableCount_;
             ++turretIndex) {
            const auto& turret = after_.movables[turretIndex];
            const auto direction = turretDirection(turret);
            if (turret.dead || turret.fallen || !direction) {
                continue;
            }
            visible[turretIndex].resize(status_.size(), 0);
            for (std::size_t target = 0; target < status_.size(); ++target) {
                bool aliveTarget;
                if (isPlayer(target)) {
                    aliveTarget =
                        !after_.players[playerIndexForEntity(target)].dead;
                } else if (isEnemy(target)) {
                    const auto& enemy =
                        after_.enemies[enemyIndexForEntity(target)];
                    aliveTarget = !enemy.dead && !enemy.fallen;
                } else {
                    const auto& movable = after_.movables[target];
                    aliveTarget = target != turretIndex && !movable.dead &&
                        !movable.fallen && turretDirection(movable).has_value();
                }
                visible[turretIndex][target] = static_cast<char>(aliveTarget &&
                    turretHasLineOfSight(
                        level_, after_, turret.cell, *direction, cellOf(target)));
            }
        }
        return visible;
    }

    // Finish reactions before the next movement/device phase. Each volley
    // sees one consistent board; its deaths can expose targets for the next
    // volley. Each repeat kills at least one live entity, so chains terminate.
    void resolveReactions(const std::vector<char>* sweepingTurrets = nullptr)
    {
        bool changed;
        do {
            changed = resolveTurretShots(sweepingTurrets);
            changed = resolveAttacks() || changed;
            sweepingTurrets = nullptr;
        } while (changed);
        markDeadEntitiesDone();
    }

    // Movement and newly opened sightlines trigger shots. Mutually facing
    // turrets remain an ambient event. Collect kills before applying them so
    // the result never depends on turret iteration order within a volley.
    [[nodiscard]] bool resolveTurretShots(
        const std::vector<char>* sweepingTurrets = nullptr)
    {
        TurretSightlines visible = captureTurretSightlines();
        bool anyKilled = false;
        std::vector<char> killedPlayers(playerCount_, 0);
        std::vector<char> killedMovables(movableCount_, 0);
        std::vector<char> killedEnemies(after_.enemies.size(), 0);
        for (std::size_t turretIndex = 0;
             turretIndex < after_.movables.size();
             ++turretIndex) {
            const GameState::Movable& turret = after_.movables[turretIndex];
            const std::optional<MoveDirection> direction =
                turretDirection(turret);
            if (turret.fallen || turret.dead || !direction) {
                continue;
            }
            const bool sweeping = status_[turretIndex].movedThisMicro ||
                (sweepingTurrets != nullptr &&
                    (*sweepingTurrets)[turretIndex] != 0);
            const auto newlyVisible = [&](std::size_t target) {
                return turretSightlines_[turretIndex].empty() ||
                    !turretSightlines_[turretIndex][target];
            };
            for (std::size_t targetIndex = 0;
                 targetIndex < movableCount_;
                 ++targetIndex) {
                if (targetIndex == turretIndex) {
                    continue;
                }
                const GameState::Movable& target =
                    after_.movables[targetIndex];
                const std::optional<MoveDirection> targetDirection =
                    turretDirection(target);
                if (!visible[turretIndex][targetIndex]) {
                    continue;
                }
                const bool faceEachOther = turretHasLineOfSight(
                    level_,
                    after_,
                    target.cell,
                    *targetDirection,
                    turret.cell);
                if (!sweeping && !status_[targetIndex].movedThisMicro &&
                    !newlyVisible(targetIndex) &&
                    !(faceEachOther &&
                        (status_[turretIndex].active ||
                            status_[targetIndex].active))) {
                    continue;
                }
                killedMovables[targetIndex] = true;
                anyKilled = true;
                if (turretShots_ != nullptr) {
                    turretShots_->push_back({
                        .turret = {
                            EntityKind::Movable,
                            resolvedEntityId(
                                EntityKind::Movable,
                                turret.id,
                                turretIndex),
                        },
                        .target = {
                            EntityKind::Movable,
                            resolvedEntityId(
                                EntityKind::Movable,
                                target.id,
                                targetIndex),
                        },
                        .turretCell = turret.cell,
                        .targetCell = target.cell,
                        .direction = *direction,
                        .beamSegments = turretBeamSegments(
                            level_, after_, turret.cell, *direction, target.cell),
                    });
                }
            }
            for (std::size_t playerIndex = 0;
                 playerIndex < playerCount_;
                 ++playerIndex) {
                const std::size_t entityIndex =
                    entityIndexForPlayer(playerIndex);
                const GameState::Player& player =
                    after_.players[playerIndex];
                if (!player.dead &&
                    (sweeping || status_[entityIndex].movedThisMicro ||
                        newlyVisible(entityIndex)) &&
                    visible[turretIndex][entityIndex]) {
                    killedPlayers[playerIndex] = true;
                    anyKilled = true;
                    if (turretShots_ != nullptr) {
                        turretShots_->push_back({
                            .turret = {
                                EntityKind::Movable,
                                resolvedEntityId(
                                    EntityKind::Movable,
                                    turret.id,
                                    turretIndex),
                            },
                            .target = {
                                EntityKind::Player,
                                resolvedEntityId(
                                    EntityKind::Player,
                                    player.id,
                                    playerIndex),
                            },
                            .turretCell = turret.cell,
                            .targetCell = player.cell,
                            .direction = *direction,
                            .beamSegments = turretBeamSegments(
                                level_, after_, turret.cell, *direction, player.cell),
                        });
                    }
                }
            }
            for (std::size_t enemyIndex = 0;
                 enemyIndex < after_.enemies.size();
                 ++enemyIndex) {
                const GameState::Enemy& enemy = after_.enemies[enemyIndex];
                if (!enemy.dead && !enemy.fallen &&
                    (sweeping || enemyMovedThisMicro_[enemyIndex] ||
                        newlyVisible(entityIndexForEnemy(enemyIndex))) &&
                    visible[turretIndex][entityIndexForEnemy(enemyIndex)]) {
                    killedEnemies[enemyIndex] = true;
                    anyKilled = true;
                    if (turretShots_ != nullptr) {
                        turretShots_->push_back({
                            .turret = {
                                EntityKind::Movable,
                                resolvedEntityId(
                                    EntityKind::Movable,
                                    turret.id,
                                    turretIndex),
                            },
                            .target = {
                                EntityKind::Enemy,
                                resolvedEntityId(
                                    EntityKind::Enemy,
                                    enemy.id,
                                    enemyIndex),
                            },
                            .turretCell = turret.cell,
                            .targetCell = enemy.cell,
                            .direction = *direction,
                            .beamSegments = turretBeamSegments(
                                level_, after_, turret.cell, *direction, enemy.cell),
                        });
                    }
                }
            }
        }

        turretSightlines_ = std::move(visible);
        for (std::size_t playerIndex = 0;
             playerIndex < playerCount_;
             ++playerIndex) {
            if (!killedPlayers[playerIndex]) {
                continue;
            }
            GameState::Player& player = after_.players[playerIndex];
            player.dead = true;
            player.drowned = false;
            player.sliding.reset();
        }
        for (std::size_t enemyIndex = 0;
             enemyIndex < after_.enemies.size();
             ++enemyIndex) {
            if (killedEnemies[enemyIndex]) {
                after_.enemies[enemyIndex].dead = true;
            }
        }
        for (std::size_t movableIndex = 0;
             movableIndex < movableCount_;
             ++movableIndex) {
            if (!killedMovables[movableIndex]) {
                continue;
            }
            GameState::Movable& movable = after_.movables[movableIndex];
            movable.dead = true;
            movable.sliding.reset();
        }
        plateActivity_.observe(after_);
        return anyKilled;
    }

    void markDeadEntitiesDone()
    {
        for (std::size_t i = 0; i < movableCount_; ++i) {
            if (after_.movables[i].dead) {
                status_[i].done = true;
            }
        }
        for (std::size_t i = 0; i < playerCount_; ++i) {
            if (playerDead(after_, i)) {
                status_[entityIndexForPlayer(i)].done = true;
            }
        }
        for (std::size_t i = 0; i < enemyCount_; ++i) {
            if (after_.enemies[i].dead || after_.enemies[i].fallen) {
                after_.enemies[i].sliding.reset();
                status_[entityIndexForEnemy(i)].done = true;
            }
        }
    }

    // Enemies kill orthogonally adjacent players.
    //
    // Scoping matters here in a way it does not elsewhere. Adjacency is a
    // standing fact about the board, so an unscoped sweep would have any action
    // anywhere kill a bystander who was already standing next to an enemy
    // before the action began - writing an entity far outside the closure and
    // attributing a death to the wrong action.
    //
    // An action is answerable for a death only when it caused the adjacency:
    // either it moved the player, or it shoved the enemy into place. The second
    // case pulls the victim into the closure, because it is about to be
    // written.
    [[nodiscard]] bool resolveAttacks()
    {
        bool anyKilled = false;
        for (std::size_t playerIndex = 0;
             playerIndex < playerCount_;
             ++playerIndex) {
            GameState::Player& player = after_.players[playerIndex];
            if (player.dead) {
                continue;
            }

            bool threatened = false;
            bool byMovedEnemy = false;
            for (std::size_t i = 0; i < after_.enemies.size(); ++i) {
                const GameState::Enemy& enemy = after_.enemies[i];
                if (enemy.fallen || enemy.dead ||
                    enemy.cell.z != player.cell.z) {
                    continue;
                }
                if (std::abs(enemy.cell.x - player.cell.x) +
                        std::abs(enemy.cell.y - player.cell.y) != 1) {
                    continue;
                }
                threatened = true;
                byMovedEnemy = byMovedEnemy || enemyMoved_[i];
            }
            if (!threatened) {
                continue;
            }

            const std::size_t entityIndex = entityIndexForPlayer(playerIndex);
            if (!status_[entityIndex].active && !byMovedEnemy) {
                continue;
            }
            status_[entityIndex].active = true;
            player.dead = true;
            player.drowned = false;
            player.sliding.reset();
            anyKilled = true;
        }
        plateActivity_.observe(after_);
        return anyKilled;
    }

    // Moves one tile, resolves the fall, and updates slide momentum.
    // Water momentum uses the same state and action legs as ice slides.
    // A leaf only coasts after an impulse and remains in water; an ordinary
    // hero shove keeps its existing one-tile behavior and may beach the leaf.
    void applyMovableMove(
        std::size_t index,
        MoveDirection direction,
        GridPosition3 target,
        bool preserveElevation = false)
    {
        const GridPosition3 robotFrom = after_.movables[index].cell;
        const bool padCoasting = after_.movables[index].type == TileType::LilyPad &&
            after_.movables[index].sliding.has_value();
        const auto riders = platformRiders(after_, index);
        target =
            transportedLanding(after_.movables[index].cell, direction, target);
        const auto boardingPad = roundRockPadAt(index, target);
        if (boardingPad) {
            ++target.z;
        }
        const bool portalMomentum = portalCarriesMomentum(
            after_.movables[index].cell,
            direction,
            after_.movables[index].sliding);
        const MoveDirection outgoing =
            outgoingDirection(after_.movables[index].cell, direction);
        recordPortalTransit(
            EntityKind::Movable,
            after_.movables[index].id,
            index,
            after_.movables[index].cell,
            direction,
            target);
        movableMovedDirections_[index] = direction;
        if (const auto crossing = level_.portalCrossing(
                after_.movables[index].cell, directionOffset(direction))) {
            after_.movables[index].quarterTurns = addQuarterTurns(
                after_.movables[index].quarterTurns, crossing->quarterTurns);
        }
        direction = outgoing;
        after_.movables[index].cell = target;
        const FallResult fall = preserveElevation
            ? FallResult { .cell = target, .supported = true }
            : movableFallTarget(level_, after_, index, target);
        const bool skipping = after_.movables[index].type == TileType::RoundRock &&
            !boardingPad && isUnfilledWater(level_, after_, fall.cell);
        const bool fell = fall.cell.z != target.z || (fall.fallen && !skipping);
        after_.movables[index].cell = fall.cell;
        after_.movables[index].fallen = fall.fallen && !skipping;
        const bool coasting = padCoasting &&
            isUnfilledWater(level_, after_, fall.cell);
        const bool slippery = portalMomentum ||
            after_.movables[index].type == TileType::Ice ||
            isIceFloor(level_, after_, fall.cell);
        after_.movables[index].sliding =
            (skipping || coasting || (!fell && slippery &&
             cellAllowsEntity(
                 level_, after_, movementDestination(fall.cell, direction))))
            ? std::optional<MoveDirection>(direction)
            : std::nullopt;
        if (boardingPad) {
            auto& pad = after_.movables[*boardingPad];
            after_.movables[index].sliding.reset();
            if (isUnfilledWater(level_, after_, pad.cell)) {
                pad.sliding = direction;
                auto& padStatus = status_[*boardingPad];
                padStatus.active = true;
                padStatus.done = false;
                padStatus.resolved = true;
                padStatus.movedThisMicro = true;
                ++padStatus.consumed;
            }
        }
        if (skipping) {
            pushRoundRockWake(index, direction);
        }
        ++status_[index].consumed;
        for (const LiveUnitAt rider : riders) {
            const GridPosition3 from = unitCell(after_, rider);
            const GridPosition3 to { fall.cell.x, fall.cell.y,
                fall.cell.z + from.z - robotFrom.z };
            setUnitCell(after_, rider, to);
            const std::size_t riderIndex = entityIndexForUnit(rider);
            slidingOf(riderIndex).reset();
            Status& riderStatus = status_[riderIndex];
            ++riderStatus.consumed;
            riderStatus.active = true;
            riderStatus.resolved = true;
            riderStatus.movedThisMicro = true;
            riderStatus.done = true;
            if (rider.kind == EntityKind::Enemy) {
                enemyMoved_[rider.index] = true;
                enemyMovedThisMicro_[rider.index] = true;
            } else if (rider.kind == EntityKind::Player) {
                applyWardrobeSwap(rider.index);
            }
        }
    }

    void applyPlayerMove(
        std::size_t entityIndex,
        MoveDirection direction,
        GridPosition3 target)
    {
        const std::size_t playerIndex = playerIndexForEntity(entityIndex);
        target = transportedLanding(
            playerCell(after_, playerIndex), direction, target);
        const bool portalMomentum = portalCarriesMomentum(
            playerCell(after_, playerIndex),
            direction,
            playerSliding(after_, playerIndex));
        const MoveDirection outgoing =
            outgoingDirection(playerCell(after_, playerIndex), direction);
        recordPortalTransit(
            EntityKind::Player,
            after_.players[playerIndex].id,
            playerIndex,
            playerCell(after_, playerIndex),
            direction,
            target);
        direction = outgoing;
        status_[entityIndex].inputDirection = direction;
        playerCell(after_, playerIndex) = target;
        const FallResult fall =
            playerFallTarget(level_, after_, playerIndex, target);
        const bool fell = fall.cell.z != target.z || fall.fallen;
        playerCell(after_, playerIndex) = fall.cell;
        playerDead(after_, playerIndex) = fall.fallen;
        after_.players[playerIndex].drowned = fall.fallen;
        playerSliding(after_, playerIndex) =
            (!fell && !playerDead(after_, playerIndex) &&
             (portalMomentum || isIceFloor(level_, after_, fall.cell)) &&
             cellAllowsEntity(
                 level_, after_, movementDestination(fall.cell, direction)))
            ? std::optional<MoveDirection>(direction)
            : std::nullopt;
        if (!fell && !playerDead(after_, playerIndex)) {
            applyWardrobeSwap(playerIndex);
        }
        ++status_[entityIndex].consumed;
    }

    void applyWardrobeSwap(std::size_t playerIndex)
    {
        const GridPosition3 cell = playerCell(after_, playerIndex);
        const auto found = std::ranges::find(
            after_.wardrobes, cell, &GameState::Wardrobe::cell);
        if (found == after_.wardrobes.end()) {
            return;
        }
        GameState::Player& player = after_.players[playerIndex];
        const CharacterType previous = player.character.value_or(
            level_.character());
        player.character = found->character;
        found->character = previous;
    }

    // Druids drag the movable unit immediately behind them into the cell they
    // vacate. The pull is part of the same micro-step as the player move, so
    // it is atomic for planning, undo, reservations, and presentation. Pulled
    // objects join the action's causal closure exactly as pushed objects do.
    void applyPlayerMoveAndPull(
        std::size_t entityIndex,
        MoveDirection direction,
        GridPosition3 target)
    {
        const std::size_t playerIndex = playerIndexForEntity(entityIndex);
        const GridPosition3 vacated = playerCell(after_, playerIndex);
        std::optional<ChainEntity> pulled;
        if (characterBehavior(
                after_.players[playerIndex].character.value_or(
                    level_.character())) == CharacterType::Druid) {
            const GridPosition3 pullSource =
                movementTarget(vacated, oppositeDirection(direction));
            if (const std::optional<ChainEntity> entity =
                    pushableAt(after_, pullSource)) {
                const bool alreadyMoved = entity->movable
                    ? status_[entity->index].movedThisMicro
                    : enemyMovedThisMicro_[entity->index];
                if (!alreadyMoved && !isUnitLocked(level_, after_, pullSource)) {
                    pulled = entity;
                }
            }
        }

        applyPlayerMove(entityIndex, direction, target);
        if (!pulled) {
            return;
        }

        // A follower enters the vacated tile normally; only crossing its
        // own active edge can transport it to a remote exit.
        const GridPosition3 pulledFrom = pulled->movable
            ? after_.movables[pulled->index].cell
            : after_.enemies[pulled->index].cell;
        const GridPosition3 landing =
            movementDestination(pulledFrom, direction);
        if (!cellAllowsEntity(level_, after_, landing) || occupied(landing) ||
            (pulled->movable && !canCarryPlatformRiders(pulled->index, landing)) ||
            (pulled->movable
                 ? !movableFallTarget(level_, after_, pulled->index, landing)
                        .supported
                 : !enemyFallTarget(level_, after_, pulled->index, landing)
                        .supported)) {
            return;
        }
        if (pulled->movable) {
            applyMovableMove(pulled->index, direction, landing);
            status_[pulled->index].resolved = true;
            status_[pulled->index].movedThisMicro = true;
            status_[pulled->index].done = false;
            status_[pulled->index].active = true;
        } else {
            applyEnemyMove(pulled->index, direction);
        }
    }

    void settleBlocked()
    {
        for (std::size_t i = 0; i < status_.size(); ++i) {
            if (status_[i].intent && !status_[i].movedThisMicro) {
                cancelAndFinish(i, true);
                if (status_[i].robotDriven) {
                    after_.movables[i].quarterTurns = addQuarterTurns(
                        after_.movables[i].quarterTurns, 2);
                    status_[i].done = true;
                }
            }
        }
    }

    const Level& level_;
    GameState& after_;
    const PressurePlateActivityTracker plateActivity_;
    const std::optional<MoveDirection> playerInput_;
    const StepRates& rates_;
    const std::size_t movableCount_;
    const std::size_t playerCount_;
    const std::size_t enemyCount_;
    std::vector<Status> status_;
    // Tracks whether enemy movement in this action caused a new attack.
    std::vector<char> enemyMoved_;
    // Motion triggers count only in the current micro-step; visibility edges
    // also catch targets exposed by blockers, devices, and previous deaths.
    std::vector<char> enemyMovedThisMicro_;
    // Last volley's visibility, sampled before its kills are applied.
    TurretSightlines turretSightlines_;
    // Direction of each successful movable displacement in the current
    // micro-step. Linked groups react after ordinary resolution completes.
    std::vector<std::optional<MoveDirection>> movableMovedDirections_;
    std::vector<TurretShot>* turretShots_ = nullptr;
    std::vector<PortalTransit>* portalTransits_ = nullptr;
    bool suppressBardInfluences_ = false;
    // Which rotators had every linked plate pressed when the step began. A
    // rotator activates on the step that presses its last plate, never on a
    // step that merely leaves it pressed.
    std::vector<char> rotatorsEngagedAtStart_;
    // Which elevators had every linked plate pressed when the step began; the
    // same edge rule as rotators.
    std::vector<char> elevatorsEngagedAtStart_;
    // Which minecarts had every linked plate pressed when the step began.
    std::vector<char> minecartsEngagedAtStart_;
    // Which gates were open when last looked at (see applyGateChanges). Unlike
    // the engagement samples above this is kept current through the step,
    // so a gate that closes and opens again drops what has since stepped on.
    std::vector<char> gatesOpen_;
};

} // namespace

GameState step(
    const Level& level,
    const GameState& state,
    std::optional<MoveDirection> playerInput,
    const StepRates& rates)
{
    return scopedStep(level, state, playerInput, rates, StepScope {});
}

StepResult stepWithEvents(
    const Level& level,
    const GameState& state,
    std::optional<MoveDirection> playerInput,
    const StepRates& rates)
{
    return scopedStepWithEvents(
        level, state, playerInput, rates, StepScope {});
}

GameState scopedStep(
    const Level& level,
    const GameState& state,
    std::optional<MoveDirection> playerInput,
    const StepRates& rates,
    const StepScope& scope)
{
    GameState after = state;
    MicroStepResolver resolver(level, state, after, playerInput, rates, scope);
    resolver.run();
    return after;
}

StepResult scopedStepWithEvents(
    const Level& level,
    const GameState& state,
    std::optional<MoveDirection> playerInput,
    const StepRates& rates,
    const StepScope& scope)
{
    StepResult result { .state = state };
    MicroStepResolver resolver(
        level,
        state,
        result.state,
        playerInput,
        rates,
        scope,
        &result.turretShots,
        &result.portalTransits,
        &result.pressurePlateActivityChanged);
    resolver.run();
    return result;
}

} // namespace sokoban::rules
