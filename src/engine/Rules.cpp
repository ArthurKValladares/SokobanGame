#include "engine/Rules.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <ranges>
#include <utility>
#include <vector>

namespace sokoban::rules {
namespace {

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
    OccupiedBelow occupiedBelow)
{
    GridPosition3 current = position;
    while (current.z > 0) {
        const GridPosition3 below { current.x, current.y, current.z - 1 };
        const TileType support = tileAt(level, below);

        if (tileTypeIsSolidBlock(support) || occupiedBelow(below)) {
            return { .cell = current, .fallen = false };
        }
        if (support == TileType::Water) {
            return {
                .cell = current,
                .fallen = isUnfilledWater(level, state, current),
            };
        }
        if (!staticCellAllowsEntity(level, below)) {
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
    });
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
        tileAt(level, groundCell) != TileType::Ground) {
        return std::nullopt;
    }

    const GridPosition3 topCell {
        groundCell.x,
        groundCell.y,
        groundCell.z + 1,
    };
    if (!staticCellAllowsEntity(level, topCell)) {
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
        });
    }
    state.movables.reserve(level.movableTiles().size());
    for (const Level::MovableTile& movable : level.movableTiles()) {
        GameState::Movable entry;
        entry.id = nextId++;
        entry.type = movable.type;
        entry.cell = movable.position;
        state.movables.push_back(entry);
    }
    state.enemies.reserve(level.enemyStarts().size());
    for (GridPosition3 position : level.enemyStarts()) {
        state.enemies.push_back({ .id = nextId++, .cell = position });
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

    return conveyorDirectionForTile(tileAt(level, position));
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

    return tileTypeAllowsEntity(tileAt(level, position));
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

    return level.supportingTileAt(position) == TileType::Water &&
        fallenMovableAt(state, position) == nullptr;
}

bool isEndUnlocked(const Level& level, const GameState& state)
{
    return std::ranges::all_of(level.pressurePlates(), [&](GridPosition3 plate) {
        return playerBlocksAt(state, plate) ||
            movableAt(state, plate) != nullptr ||
            enemyAt(state, plate) != nullptr;
    });
}

bool isAtUnlockedEnd(const Level& level, const GameState& state)
{
    return !anyPlayerDead(state) && isEndUnlocked(level, state) &&
        [&] {
            for (const std::size_t i :
                 std::views::iota(std::size_t { 0 }, state.players.size())) {
                if (!level.isEnd(playerCell(state, i))) {
                    return false;
                }
            }
            return true;
        }();
}

namespace {

bool turretHasLineOfSight(
    const Level& level,
    const GameState& state,
    GridPosition3 turret,
    MoveDirection direction,
    GridPosition3 target)
{
    if (turret.z != target.z) {
        return false;
    }
    const GridPosition ray = directionOffset(direction);
    const int dx = target.x - turret.x;
    const int dy = target.y - turret.y;
    int distance = 0;
    if (ray.x != 0 && dy == 0 && dx * ray.x > 0) {
        distance = std::abs(dx);
    } else if (ray.y != 0 && dx == 0 && dy * ray.y > 0) {
        distance = std::abs(dy);
    } else {
        return false;
    }

    for (int step = 1; step < distance; ++step) {
        const GridPosition3 cell {
            turret.x + ray.x * step,
            turret.y + ray.y * step,
            turret.z,
        };
        if (!staticCellAllowsEntity(level, cell) ||
            movableAt(state, cell) != nullptr ||
            playerBlocksAt(state, cell) ||
            enemyAt(state, cell) != nullptr) {
            return false;
        }
    }
    return true;
}

} // namespace

std::vector<EntityId> mutuallyFacingTurrets(
    const Level& level,
    const GameState& state)
{
    std::vector<EntityId> participants;
    for (std::size_t first = 0; first < state.movables.size(); ++first) {
        const GameState::Movable& a = state.movables[first];
        const std::optional<MoveDirection> aDirection =
            turretDirectionForTile(a.type);
        if (a.dead || a.fallen || !aDirection) {
            continue;
        }
        for (std::size_t second = first + 1;
             second < state.movables.size();
             ++second) {
            const GameState::Movable& b = state.movables[second];
            const std::optional<MoveDirection> bDirection =
                turretDirectionForTile(b.type);
            if (b.dead || b.fallen || !bDirection ||
                !turretHasLineOfSight(
                    level, state, a.cell, *aDirection, b.cell) ||
                !turretHasLineOfSight(
                    level, state, b.cell, *bDirection, a.cell)) {
                continue;
            }
            const EntityId aId = resolvedEntityId(
                EntityKind::Movable, a.id, first);
            const EntityId bId = resolvedEntityId(
                EntityKind::Movable, b.id, second);
            if (std::ranges::find(participants, aId) == participants.end()) {
                participants.push_back(aId);
            }
            if (std::ranges::find(participants, bId) == participants.end()) {
                participants.push_back(bId);
            }
        }
    }
    return participants;
}

bool hasPendingMotion(const Level& level, const GameState& state)
{
    if (!mutuallyFacingTurrets(level, state).empty()) {
        return true;
    }
    for (std::size_t i = 0; i < state.players.size(); ++i) {
        if (!playerDead(state, i) &&
            (playerSliding(state, i) ||
                conveyorDirectionAt(level, playerCell(state, i)))) {
            return true;
        }
    }

    return std::ranges::any_of(state.movables, [&](const GameState::Movable& movable) {
        return !movable.fallen && !movable.dead &&
            (movable.sliding || conveyorDirectionAt(level, movable.cell).has_value());
    }) || std::ranges::any_of(
        state.enemies,
        [&](const GameState::Enemy& enemy) {
            return !enemy.fallen && !enemy.dead &&
                (enemy.sliding ||
                    conveyorDirectionAt(level, enemy.cell).has_value());
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
    int distance = 0;
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

GridPosition3 rayCell(GridPosition3 origin, GridPosition ray, int distance)
{
    return {
        origin.x + ray.x * distance,
        origin.y + ray.y * distance,
        origin.z,
    };
}

std::optional<int> distanceAlongRay(
    GridPosition3 origin,
    GridPosition3 target,
    GridPosition ray)
{
    if (origin.z != target.z) {
        return std::nullopt;
    }
    const int dx = target.x - origin.x;
    const int dy = target.y - origin.y;
    if (ray.x != 0 && dy == 0 && dx * ray.x > 0) {
        return std::abs(dx);
    }
    if (ray.y != 0 && dx == 0 && dy * ray.y > 0) {
        return std::abs(dy);
    }
    return std::nullopt;
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

bool inputRayIsClear(
    const Level& level,
    const GameState& state,
    GridPosition3 mirror,
    GridPosition ray,
    int distance,
    std::size_t entityIndex)
{
    for (int step = 1; step < distance; ++step) {
        const GridPosition3 cell = rayCell(mirror, ray, step);
        if (!staticCellAllowsEntity(level, cell) ||
            entityBlocksSight(state, cell, entityIndex)) {
            return false;
        }
    }
    return true;
}

bool outputRayIsClear(
    const Level& level,
    GridPosition3 mirror,
    GridPosition ray,
    int distance)
{
    for (int step = 1; step <= distance; ++step) {
        if (!staticCellAllowsEntity(level, rayCell(mirror, ray, step))) {
            return false;
        }
    }
    return true;
}

std::vector<MirrorHit> nearestMirrors(
    const Level& level,
    const GameState& state,
    GridPosition3 entityCell,
    std::size_t entityIndex,
    const std::vector<GridPosition3>& usedMirrors)
{
    std::vector<MirrorHit> nearest;
    int nearestDistance = 0;
    if (entityCell.z < 0 || entityCell.z >= static_cast<int>(level.depth())) {
        return nearest;
    }

    for (uint32_t y = 0; y < level.height(); ++y) {
        for (uint32_t x = 0; x < level.width(); ++x) {
            const GridPosition3 mirror {
                static_cast<int>(x),
                static_cast<int>(y),
                entityCell.z,
            };
            if (std::ranges::find(usedMirrors, mirror) != usedMirrors.end()) {
                continue;
            }
            const std::optional<MirrorRays> rays = mirrorRays(tileAt(level, mirror));
            if (!rays) {
                continue;
            }

            const std::array pairs {
                std::pair { rays->first, rays->second },
                std::pair { rays->second, rays->first },
            };
            for (const auto& [input, output] : pairs) {
                const std::optional<int> distance =
                    distanceAlongRay(mirror, entityCell, input);
                if (!distance ||
                    !inputRayIsClear(
                        level, state, mirror, input, *distance, entityIndex)) {
                    continue;
                }
                if (nearest.empty() || *distance < nearestDistance) {
                    nearest.clear();
                    nearest.push_back({ mirror, output, *distance });
                    nearestDistance = *distance;
                } else if (*distance == nearestDistance &&
                    std::ranges::none_of(
                        nearest,
                        [&](const MirrorHit& existing) {
                            return existing.cell == mirror;
                        })) {
                    nearest.push_back({ mirror, output, *distance });
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
    bool allowBranches)
{
    const std::size_t maximumChain =
        static_cast<std::size_t>(level.width()) * level.height();
    constexpr std::size_t maximumBranches = 256;
    std::vector<PendingReflectionPath> pending {
        { .cell = start },
    };
    std::vector<ReflectedPath> results;

    while (!pending.empty()) {
        PendingReflectionPath path = std::move(pending.back());
        pending.pop_back();
        if (path.usedMirrors.size() >= maximumChain) {
            return std::nullopt;
        }

        const std::vector<MirrorHit> hits = nearestMirrors(
            level, state, path.cell, entityIndex, path.usedMirrors);
        if (hits.empty()) {
            results.push_back({
                .cell = path.cell,
                .reflected = !path.usedMirrors.empty(),
                .beamSegments = std::move(path.beamSegments),
            });
            continue;
        }
        if (!allowBranches && hits.size() > 1) {
            return std::nullopt;
        }
        if (pending.size() + results.size() + hits.size() > maximumBranches) {
            return std::nullopt;
        }

        // Reverse insertion keeps the level scan order stable when paths are
        // later popped from this depth-first stack.
        for (auto hit = hits.rbegin(); hit != hits.rend(); ++hit) {
            if (!outputRayIsClear(
                    level, hit->cell, hit->output, hit->distance)) {
                return std::nullopt;
            }
            PendingReflectionPath branch = path;
            const GridPosition3 destination =
                rayCell(hit->cell, hit->output, hit->distance);
            branch.usedMirrors.push_back(hit->cell);
            branch.beamSegments.push_back({ path.cell, hit->cell });
            branch.beamSegments.push_back({ hit->cell, destination });
            branch.cell = destination;
            pending.push_back(std::move(branch));
        }
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

std::optional<MirrorActivationPreview> previewMirrorActivation(
    const Level& level,
    const GameState& state)
{
    GameState after = state;
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
         sourcePlayer < originalPlayerCount;
         ++sourcePlayer) {
        if (playerDead(state, sourcePlayer)) {
            continue;
        }
        const std::optional<std::vector<ReflectedPath>> reflectedPaths =
            reflectedPathsForEntity(
                level,
                state,
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

    for (std::size_t i = 0; i < state.movables.size(); ++i) {
        if (state.movables[i].fallen || state.movables[i].dead) {
            continue;
        }
        const std::optional<std::vector<ReflectedPath>> reflectedPaths =
            reflectedPathsForEntity(
                level, state, state.movables[i].cell, i, false);
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

    for (std::size_t i = 0; i < state.enemies.size(); ++i) {
        if (state.enemies[i].fallen || state.enemies[i].dead) {
            continue;
        }
        const std::optional<std::vector<ReflectedPath>> reflectedPaths =
            reflectedPathsForEntity(
                level,
                state,
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

    if (!anyReflected || !liveCellsAreUnique(after)) {
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

    if (!liveCellsAreUnique(after) || after == state) {
        return std::nullopt;
    }
    resolveEnemyAttacks(after);
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
    };
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
//   settleBlocked  - anything with an intent that could not move is in a
//                    mutual block; blocked slide momentum does not survive.
//
// Micro-steps repeat until one completes with no movement at all.
//
// A scope restricts which entities may act. Out-of-scope entities keep every
// passive role they had - they block, support, and stop slides - but derive no
// intent and are never written, so a step can move one entity without deciding
// the fate of the whole board. An empty scope means everything acts, which is
// the behaviour every existing save was written against.
class MicroStepResolver {
public:
    MicroStepResolver(
        const Level& level,
        GameState& after,
        std::optional<MoveDirection> playerInput,
        const StepRates& rates,
        const StepScope& scope,
        std::vector<TurretShot>* turretShots = nullptr)
        : level_(level)
        , after_(after)
        , playerInput_(playerInput)
        , rates_(rates)
        , movableCount_(after.movables.size())
        , playerCount_(after.players.size())
        , enemyCount_(after.enemies.size())
        , status_(movableCount_ + playerCount_ + enemyCount_)
        , enemyMoved_(after.enemies.size(), 0)
        , enemyMovedThisMicro_(after.enemies.size(), 0)
        , turretShots_(turretShots)
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
        // A mutual face-off is itself an event; it does not need some unrelated
        // actor to move before both turrets fire.
        resolveTurretShots();
        markDeadEntitiesDone();
        bool anyMovement = true;
        while (anyMovement) {
            const bool mayPulse = anyBardHasMovementIntent();
            const std::optional<GameState> beforeBardPulse = mayPulse
                ? std::optional<GameState> { after_ }
                : std::nullopt;
            const std::optional<std::vector<Status>> statusesBeforeBardPulse =
                mayPulse
                ? std::optional<std::vector<Status>> { status_ }
                : std::nullopt;
            const std::optional<std::vector<char>> enemiesBeforeBardPulse =
                mayPulse
                ? std::optional<std::vector<char>> { enemyMoved_ }
                : std::nullopt;
            deriveIntents();
            const bool hasBardPulse = std::ranges::any_of(
                status_,
                [](const Status& status) { return status.bardDriven; });
            markContested();
            anyMovement = resolveMoves();
            settleBlocked();
            if (hasBardPulse && !anyBardMovedThisMicro()) {
                after_ = *beforeBardPulse;
                status_ = *statusesBeforeBardPulse;
                enemyMoved_ = *enemiesBeforeBardPulse;
                suppressBardInfluences_ = true;
                deriveIntents();
                markContested();
                anyMovement = resolveMoves();
                settleBlocked();
                suppressBardInfluences_ = false;
            }
            if (anyMovement) {
                resolveTurretShots();
                resolveAttacks();
                markDeadEntitiesDone();
            }
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
        const GridPosition ray = directionOffset(direction);
        const GridPosition3 origin = playerCell(after_, playerIndex);
        for (int distance = 1;; ++distance) {
            const GridPosition3 cell {
                origin.x + ray.x * distance,
                origin.y + ray.y * distance,
                origin.z,
            };
            if (!staticCellAllowsEntity(level_, cell)) {
                return std::nullopt;
            }
            if (const GameState::Movable* movable = movableAt(after_, cell)) {
                return ChainEntity {
                    .movable = true,
                    .index = static_cast<std::size_t>(
                        movable - after_.movables.data()),
                };
            }
            if (const GameState::Enemy* enemy = enemyAt(after_, cell)) {
                return ChainEntity {
                    .movable = false,
                    .index = static_cast<std::size_t>(
                        enemy - after_.enemies.data()),
                };
            }
            if (playerBlocksAt(after_, cell, playerIndex)) {
                return std::nullopt;
            }
        }
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
        if (status.done || !status.active || playerDead(after_, playerIndex)) {
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
                    .direction = *playerInput_,
                    .budget = rates_.playerMove,
                    .inputDriven = true,
                };
            }
            return std::nullopt;
        }
        if (const std::optional<MoveDirection> belt =
                conveyorDirectionAt(level_, playerCell(after_, playerIndex))) {
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
            if (after_.players[playerIndex].character.value_or(
                    level_.character()) != CharacterType::Bard) {
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
            if (after_.players[playerIndex].character.value_or(
                    level_.character()) == CharacterType::Bard &&
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
            if (after_.players[playerIndex].character.value_or(
                    level_.character()) == CharacterType::Bard &&
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
            status.witchSwapTarget.reset();
        }

        deriveBardInfluences();

        for (std::size_t i = 0; i < status_.size(); ++i) {
            Status& status = status_[i];

            // Out of scope means scenery: still an obstacle to everyone else,
            // but it wants nothing and will not be written.
            if (status.done || !status.active) {
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
                                   level_, after_.enemies[enemyIndex].cell)) {
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
                               conveyorDirectionAt(level_, after_.movables[i].cell)) {
                    if (status.consumed < rates_.conveyor) {
                        status.intent = belt;
                    }
                }
            }

            if (status.intent) {
                status.target = movementTarget(cellOf(i), *status.intent);
                if (isPlayer(i) && status.inputDriven) {
                    const std::size_t playerIndex =
                        playerIndexForEntity(i);
                    if (after_.players[playerIndex].character.value_or(
                            level_.character()) == CharacterType::Witch) {
                        status.witchSwapTarget = visibleMovableForWitch(
                            playerIndex, *status.intent);
                    }
                    if (status.witchSwapTarget) {
                        const ChainEntity swap = *status.witchSwapTarget;
                        status.target = swap.movable
                            ? after_.movables[swap.index].cell
                            : after_.enemies[swap.index].cell;
                    } else {
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
                if (status_[j].target && *status_[i].target == *status_[j].target) {
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
            slidingOf(index) = std::nullopt;
            status_[index].done = true;
        } else if (!onlyWhenSliding) {
            status_[index].done = true;
        }
    }

    // A bard-driven movable unit has the same one-block shove available to an
    // ordinary hero. This is deliberately not a chain push: the knight keeps
    // that distinct ability, while every aura participant can still advance
    // through a single loose rock, ice block, or turret when space permits.
    [[nodiscard]] bool pushOneMovableForBard(
        std::size_t blockerIndex,
        MoveDirection direction)
    {
        Status& blockerStatus = status_[blockerIndex];
        if (blockerStatus.movedThisMicro) {
            return false;
        }
        const GridPosition3 destination = movementTarget(
            after_.movables[blockerIndex].cell, direction);
        if (!staticCellAllowsEntity(level_, destination) ||
            movableBlocksAt(after_, destination, blockerIndex) ||
            playerBlocksAt(after_, destination) ||
            enemyBlocksAt(after_, destination) ||
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

    [[nodiscard]] bool resolveMovable(std::size_t index, bool& anyMovement)
    {
        Status& status = status_[index];
        const MoveDirection direction = *status.intent;
        const GridPosition3 target = *status.target;

        if (status.contested) {
            cancelAndFinish(index, true);
            status.resolved = true;
            return true;
        }
        if (!staticCellAllowsEntity(level_, target) ||
            (!status.bardDriven &&
                !movableFallTarget(level_, after_, index, target).supported)) {
            slidingOf(index) = std::nullopt;
            status.done = true;
            status.resolved = true;
            return true;
        }
        if (const GameState::Movable* blocker = movableAt(after_, target)) {
            const std::size_t blockerIndex = static_cast<std::size_t>(
                blocker - after_.movables.data());
            if (blockerIndex != index && !status_[blockerIndex].resolved) {
                return false;
            }
            if (!status.bardDriven || blockerIndex == index ||
                !pushOneMovableForBard(blockerIndex, direction)) {
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

    [[nodiscard]] bool resolveEnemy(
        std::size_t entityIndex,
        bool& anyMovement)
    {
        Status& status = status_[entityIndex];
        const std::size_t enemyIndex = enemyIndexForEntity(entityIndex);
        const MoveDirection direction = *status.intent;
        const GridPosition3 target = *status.target;

        if (status.contested) {
            cancelAndFinish(entityIndex, true);
            status.resolved = true;
            return true;
        }
        if (!staticCellAllowsEntity(level_, target) ||
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
        const std::size_t playerIndex = playerIndexForEntity(entityIndex);
        const MoveDirection direction = *status.intent;
        const GridPosition3 target = *status.target;

        if (status.contested) {
            cancelAndFinish(entityIndex, true);
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
        if (!staticCellAllowsEntity(level_, target) ||
            !playerFallTarget(level_, after_, playerIndex, target).supported) {
            playerSliding(after_, playerIndex) = std::nullopt;
            status.done = true;
            status.resolved = true;
            return true;
        }
        if (playerBlocksAt(after_, target, playerIndex)) {
            return false;
        }
        if (after_.players[playerIndex].character.value_or(
                level_.character()) == CharacterType::Druid) {
            const GridPosition3 pullSource = movementTarget(
                playerCell(after_, playerIndex),
                oppositeDirection(direction));
            if (const GameState::Enemy* enemy = enemyAt(after_, pullSource)) {
                const std::size_t enemyIndex = static_cast<std::size_t>(
                    enemy - after_.enemies.data());
                if (!status_[entityIndexForEnemy(enemyIndex)].resolved) {
                    return false;
                }
            }
        }
        if (status.inputDriven &&
            after_.players[playerIndex].character.value_or(
                level_.character()) == CharacterType::Knight) {
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
            const bool rogue = after_.players[playerIndex].character.value_or(
                level_.character()) == CharacterType::Rogue;
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
            const GridPosition3 pushTarget = movementTarget(target, direction);
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
            const bool druid = after_.players[playerIndex].character.value_or(
                level_.character()) == CharacterType::Druid;
            if (status.inputDriven && !druid &&
                !status_[blockerIndex].movedThisMicro &&
                staticCellAllowsEntity(level_, pushTarget) &&
                !movableBlocksAt(after_, pushTarget, blockerIndex) &&
                !playerBlocksAt(after_, pushTarget, playerIndex) &&
                enemyCanMove &&
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
            chain.push_back(*entity);
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
            if (entity.movable) {
                if (status_[entity.index].movedThisMicro) {
                    return false;
                }
                const GridPosition3 destination = movementTarget(
                    trial.movables[entity.index].cell, direction);
                if (!staticCellAllowsEntity(level_, destination) ||
                    movableBlocksAt(trial, destination, entity.index) ||
                    enemyBlocksAt(trial, destination) ||
                    playerBlocksAt(trial, destination)) {
                    return false;
                }
                const FallResult fall = movableFallTarget(
                    level_, trial, entity.index, destination);
                if (!fall.supported) {
                    return false;
                }
                trial.movables[entity.index].cell = fall.cell;
                trial.movables[entity.index].fallen = fall.fallen;
            } else {
                const GridPosition3 destination = movementTarget(
                    trial.enemies[entity.index].cell, direction);
                if (!staticCellAllowsEntity(level_, destination) ||
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
                const GridPosition3 destination = movementTarget(
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
        const GridPosition3 destination = movementTarget(
            after_.enemies[enemyIndex].cell,
            direction);
        return staticCellAllowsEntity(level_, destination) &&
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
        const GridPosition3 destination = movementTarget(
            after_.enemies[enemyIndex].cell,
            direction);
        after_.enemies[enemyIndex].cell = destination;
        const FallResult fall = preserveElevation
            ? FallResult { .cell = destination, .supported = true }
            : enemyFallTarget(level_, after_, enemyIndex, destination);
        after_.enemies[enemyIndex].cell = fall.cell;
        after_.enemies[enemyIndex].fallen = fall.fallen;
        const bool fell = fall.cell != destination || fall.fallen;
        after_.enemies[enemyIndex].sliding =
            (!fell && isIceFloor(level_, after_, fall.cell) &&
                staticCellAllowsEntity(
                    level_, movementTarget(fall.cell, direction)))
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

    // A turret normally reacts to movement, while two turrets aimed directly
    // at one another form an ambient volley without waiting for another actor.
    // Every live entity is still an occluder, so a rock between a turret and a
    // target keeps that target safe. Kills are collected before they are
    // applied to make a volley observe one consistent board rather than
    // letting the first death open a ray for the next turret.
    void resolveTurretShots()
    {
        std::vector<char> killedPlayers(playerCount_, 0);
        std::vector<char> killedMovables(movableCount_, 0);
        std::vector<char> killedEnemies(after_.enemies.size(), 0);
        for (std::size_t turretIndex = 0;
             turretIndex < after_.movables.size();
             ++turretIndex) {
            const GameState::Movable& turret = after_.movables[turretIndex];
            const std::optional<MoveDirection> direction =
                turretDirectionForTile(turret.type);
            if (turret.fallen || turret.dead || !direction) {
                continue;
            }
            for (std::size_t targetIndex = 0;
                 targetIndex < movableCount_;
                 ++targetIndex) {
                if (targetIndex == turretIndex) {
                    continue;
                }
                const GameState::Movable& target =
                    after_.movables[targetIndex];
                const std::optional<MoveDirection> targetDirection =
                    turretDirectionForTile(target.type);
                if (target.fallen || target.dead || !targetDirection ||
                    !turretHasLineOfSight(
                        level_,
                        after_,
                        turret.cell,
                        *direction,
                        target.cell)) {
                    continue;
                }
                const bool faceEachOther = turretHasLineOfSight(
                    level_,
                    after_,
                    target.cell,
                    *targetDirection,
                    turret.cell);
                if (!status_[targetIndex].movedThisMicro &&
                    !(faceEachOther &&
                        (status_[turretIndex].active ||
                            status_[targetIndex].active))) {
                    continue;
                }
                killedMovables[targetIndex] = true;
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
                if (!player.dead && status_[entityIndex].movedThisMicro &&
                    turretHasLineOfSight(
                        level_, after_, turret.cell, *direction, player.cell)) {
                    killedPlayers[playerIndex] = true;
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
                        });
                    }
                }
            }
            for (std::size_t enemyIndex = 0;
                 enemyIndex < after_.enemies.size();
                 ++enemyIndex) {
                const GameState::Enemy& enemy = after_.enemies[enemyIndex];
                if (!enemy.dead && !enemy.fallen &&
                    enemyMovedThisMicro_[enemyIndex] &&
                    turretHasLineOfSight(
                        level_, after_, turret.cell, *direction, enemy.cell)) {
                    killedEnemies[enemyIndex] = true;
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
                        });
                    }
                }
            }
        }

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
    void resolveAttacks()
    {
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
        }
    }

    // Moves one tile, resolves the fall, and updates slide momentum.
    // Momentum continues while the entity is icy (an ice block, or anything
    // standing on an ice floor), did not fall, and the next cell is not
    // statically blocked.
    void applyMovableMove(
        std::size_t index,
        MoveDirection direction,
        GridPosition3 target,
        bool preserveElevation = false)
    {
        after_.movables[index].cell = target;
        const FallResult fall = preserveElevation
            ? FallResult { .cell = target, .supported = true }
            : movableFallTarget(level_, after_, index, target);
        const bool fell = fall.cell.z != target.z || fall.fallen;
        after_.movables[index].cell = fall.cell;
        after_.movables[index].fallen = fall.fallen;
        const bool slippery = after_.movables[index].type == TileType::Ice ||
            isIceFloor(level_, after_, fall.cell);
        after_.movables[index].sliding =
            (!fell && slippery &&
                staticCellAllowsEntity(level_, movementTarget(fall.cell, direction)))
                ? std::optional<MoveDirection>(direction)
                : std::nullopt;
        ++status_[index].consumed;
    }

    void applyPlayerMove(
        std::size_t entityIndex,
        MoveDirection direction,
        GridPosition3 target)
    {
        const std::size_t playerIndex = playerIndexForEntity(entityIndex);
        playerCell(after_, playerIndex) = target;
        const FallResult fall =
            playerFallTarget(level_, after_, playerIndex, target);
        const bool fell = fall.cell.z != target.z || fall.fallen;
        playerCell(after_, playerIndex) = fall.cell;
        playerDead(after_, playerIndex) = fall.fallen;
        after_.players[playerIndex].drowned = fall.fallen;
        playerSliding(after_, playerIndex) =
            (!fell && !playerDead(after_, playerIndex) &&
                isIceFloor(level_, after_, fall.cell) &&
                staticCellAllowsEntity(level_, movementTarget(fall.cell, direction)))
                ? std::optional<MoveDirection>(direction)
                : std::nullopt;
        ++status_[entityIndex].consumed;
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
        if (after_.players[playerIndex].character.value_or(
                level_.character()) == CharacterType::Druid) {
            const GridPosition3 pullSource =
                movementTarget(vacated, oppositeDirection(direction));
            if (const std::optional<ChainEntity> entity =
                    pushableAt(after_, pullSource)) {
                const bool alreadyMoved = entity->movable
                    ? status_[entity->index].movedThisMicro
                    : enemyMovedThisMicro_[entity->index];
                if (!alreadyMoved) {
                    pulled = entity;
                }
            }
        }

        applyPlayerMove(entityIndex, direction, target);
        if (!pulled) {
            return;
        }

        // The destination is the cell a live player just vacated, so the
        // resolver has already established that it is valid and supported.
        if (pulled->movable) {
            applyMovableMove(pulled->index, direction, vacated);
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
            }
        }
    }

    const Level& level_;
    GameState& after_;
    const std::optional<MoveDirection> playerInput_;
    const StepRates& rates_;
    const std::size_t movableCount_;
    const std::size_t playerCount_;
    const std::size_t enemyCount_;
    std::vector<Status> status_;
    // Tracks whether enemy movement in this action caused a new attack.
    std::vector<char> enemyMoved_;
    // Unlike the attack closure above, ordinary turret triggers are edge
    // events: only motion in the current micro-step counts. Mutual turret
    // volleys are the deliberate exception resolved before movement begins.
    std::vector<char> enemyMovedThisMicro_;
    std::vector<TurretShot>* turretShots_ = nullptr;
    bool suppressBardInfluences_ = false;
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
    MicroStepResolver resolver(level, after, playerInput, rates, scope);
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
        result.state,
        playerInput,
        rates,
        scope,
        &result.turretShots);
    resolver.run();
    return result;
}

} // namespace sokoban::rules
