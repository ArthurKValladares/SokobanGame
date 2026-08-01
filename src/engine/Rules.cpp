#include "engine/Rules.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <utility>
#include <vector>

namespace sokoban::rules {
namespace {

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
        if (i != ignoreIndex && !state.movables[i].fallen && state.movables[i].cell == position) {
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
            !state.enemies[i].fallen && state.enemies[i].cell == position) {
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
            if (i != movableIndex && state.movables[i].cell == below) {
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
            if (i != enemyIndex && state.enemies[i].cell == below) {
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
                if (enemy.fallen || enemy.cell.z != player.cell.z) {
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
    state.players.push_back({ .cell = level.playerStart() });
    state.movables.reserve(level.movableTiles().size());
    for (const Level::MovableTile& movable : level.movableTiles()) {
        GameState::Movable entry;
        entry.type = movable.type;
        entry.cell = movable.position;
        state.movables.push_back(entry);
    }
    state.enemies.reserve(level.enemyStarts().size());
    for (GridPosition3 position : level.enemyStarts()) {
        state.enemies.push_back({ .cell = position });
    }

    return state;
}

bool anyPlayerDead(const GameState& state)
{
    return std::ranges::any_of(
        state.players,
        [](const GameState::Player& player) { return player.dead; });
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
        return !candidate.fallen && candidate.cell == position;
    });

    return movable != state.movables.end() ? &*movable : nullptr;
}

const GameState::Movable* fallenMovableAt(const GameState& state, GridPosition3 position)
{
    const auto movable = std::ranges::find_if(state.movables, [position](const GameState::Movable& candidate) {
        return candidate.fallen && candidate.cell == position;
    });

    return movable != state.movables.end() ? &*movable : nullptr;
}

const GameState::Enemy* enemyAt(const GameState& state, GridPosition3 position)
{
    const auto enemy = std::ranges::find_if(
        state.enemies,
        [position](const GameState::Enemy& candidate) {
            return !candidate.fallen && candidate.cell == position;
        });
    return enemy != state.enemies.end() ? &*enemy : nullptr;
}

const GameState::Enemy* fallenEnemyAt(const GameState& state, GridPosition3 position)
{
    const auto enemy = std::ranges::find_if(
        state.enemies,
        [position](const GameState::Enemy& candidate) {
            return candidate.fallen && candidate.cell == position;
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
        return playerBlocksAt(state, plate) || movableAt(state, plate) != nullptr;
    });
}

bool isAtUnlockedEnd(const Level& level, const GameState& state)
{
    return !anyPlayerDead(state) && isEndUnlocked(level, state) &&
        [&] {
            for (std::size_t i = 0; i < state.players.size(); ++i) {
                if (!level.isEnd(playerCell(state, i))) {
                    return false;
                }
            }
            return true;
        }();
}

bool hasPendingMotion(const Level& level, const GameState& state)
{
    for (std::size_t i = 0; i < state.players.size(); ++i) {
        if (!playerDead(state, i) &&
            (playerSliding(state, i) ||
                conveyorDirectionAt(level, playerCell(state, i)))) {
            return true;
        }
    }

    return std::ranges::any_of(state.movables, [&](const GameState::Movable& movable) {
        return !movable.fallen &&
            (movable.sliding || conveyorDirectionAt(level, movable.cell).has_value());
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
            state.movables[i].cell == cell) {
            return true;
        }
    }
    if (enemyBlocksAt(state, cell)) {
        return true;
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
        if (movable.fallen) {
            continue;
        }
        if (std::ranges::find(occupied, movable.cell) != occupied.end()) {
            return false;
        }
        occupied.push_back(movable.cell);
    }
    for (const GameState::Enemy& enemy : state.enemies) {
        if (enemy.fallen) {
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
                    .cell = reflected.cell,
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
        if (state.movables[i].fallen) {
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

    if (!liveCellsAreUnique(after) || after == state) {
        return std::nullopt;
    }
    resolveEnemyAttacks(after);
    for (MirrorEntityPreview& entity : entities) {
        if (entity.player) {
            entity.destination = playerCell(after, entity.resultPlayerIndex);
            entity.fallen = after.players[entity.resultPlayerIndex].drowned;
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
class MicroStepResolver {
public:
    MicroStepResolver(
        const Level& level,
        GameState& after,
        std::optional<MoveDirection> playerInput,
        const StepRates& rates)
        : level_(level)
        , after_(after)
        , playerInput_(playerInput)
        , rates_(rates)
        , movableCount_(after.movables.size())
        , playerCount_(after.players.size())
        , status_(movableCount_ + playerCount_)
    {
        for (std::size_t i = 0; i < playerCount_; ++i) {
            status_[entityIndexForPlayer(i)].done = playerDead(after_, i);
        }
    }

    void run()
    {
        bool anyMovement = true;
        while (anyMovement) {
            deriveIntents();
            markContested();
            anyMovement = resolveMoves();
            settleBlocked();
            if (anyMovement) {
                resolveEnemyAttacks(after_);
                for (std::size_t i = 0; i < playerCount_; ++i) {
                    if (playerDead(after_, i)) {
                        status_[entityIndexForPlayer(i)].done = true;
                    }
                }
            }
        }
    }

private:
    struct Status {
        // Persistent across micro-steps: movement budget consumed and
        // whether this entity's movement source is finished for the step.
        int consumed = 0;
        bool done = false;
        // Re-derived every micro-step.
        std::optional<MoveDirection> intent;
        std::optional<GridPosition3> target;
        bool contested = false;
        bool resolved = false;
        bool movedThisMicro = false;
        bool inputDriven = false; // player only
    };

    [[nodiscard]] std::size_t entityIndexForPlayer(
        std::size_t playerIndex) const
    {
        return movableCount_ + playerIndex;
    }
    [[nodiscard]] bool isPlayer(std::size_t index) const
    {
        return index >= movableCount_;
    }
    [[nodiscard]] std::size_t playerIndexForEntity(std::size_t index) const
    {
        return index - movableCount_;
    }

    [[nodiscard]] std::optional<MoveDirection>& slidingOf(std::size_t index)
    {
        return isPlayer(index)
            ? playerSliding(after_, playerIndexForEntity(index))
            : after_.movables[index].sliding;
    }

    [[nodiscard]] GridPosition3 cellOf(std::size_t index) const
    {
        return isPlayer(index)
            ? playerCell(after_, playerIndexForEntity(index))
            : after_.movables[index].cell;
    }

    void deriveIntents()
    {
        for (std::size_t i = 0; i < status_.size(); ++i) {
            Status& status = status_[i];
            status.intent.reset();
            status.target.reset();
            status.contested = false;
            status.resolved = false;
            status.movedThisMicro = false;
            status.inputDriven = false;

            if (status.done) {
                continue;
            }
            if (isPlayer(i)) {
                const std::size_t playerIndex = playerIndexForEntity(i);
                if (playerDead(after_, playerIndex)) {
                    continue;
                }
                if (playerSliding(after_, playerIndex)) {
                    if (status.consumed < rates_.slide) {
                        status.intent = playerSliding(after_, playerIndex);
                    }
                } else if (playerInput_) {
                    if (status.consumed < rates_.playerMove) {
                        status.intent = playerInput_;
                        status.inputDriven = true;
                    }
                } else if (const std::optional<MoveDirection> belt =
                               conveyorDirectionAt(
                                   level_, playerCell(after_, playerIndex))) {
                    if (status.consumed < rates_.conveyor) {
                        status.intent = belt;
                    }
                }
            } else {
                if (after_.movables[i].fallen) {
                    continue;
                }
                if (after_.movables[i].sliding) {
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
                    status.target =
                        playerLadderClimbTarget(
                            level_,
                            after_,
                            playerIndexForEntity(i),
                            *status.intent)
                            .value_or(*status.target);
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
                    status_[i].contested = true;
                    status_[j].contested = true;
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
                    : resolveMovable(i, anyMovement);
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
            !movableFallTarget(level_, after_, index, target).supported) {
            slidingOf(index) = std::nullopt;
            status.done = true;
            status.resolved = true;
            return true;
        }
        if (movableBlocksAt(after_, target, index) ||
            playerBlocksAt(after_, target)) {
            return false; // the blocking entity may still move this micro-step
        }
        if (const GameState::Enemy* enemy = enemyAt(after_, target)) {
            const std::size_t enemyIndex =
                static_cast<std::size_t>(enemy - after_.enemies.data());
            if (!pushEnemy(enemyIndex, direction)) {
                slidingOf(index) = std::nullopt;
                status.done = true;
                status.resolved = true;
                return true;
            }
        }
        applyMovableMove(index, direction, target);
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
        if (enemyAt(after_, target) != nullptr) {
            if (playerSliding(after_, playerIndex)) {
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
            const bool enemyCanMove = pushedEnemy == nullptr ||
                canPushEnemy(
                    static_cast<std::size_t>(pushedEnemy - after_.enemies.data()),
                    direction);
            if (status.inputDriven &&
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
                applyPlayerMove(entityIndex, direction, target);
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
        applyPlayerMove(entityIndex, direction, target);
        status.resolved = true;
        status.movedThisMicro = true;
        anyMovement = true;
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
        const GridPosition3 destination = movementTarget(
            after_.enemies[enemyIndex].cell,
            direction);
        after_.enemies[enemyIndex].cell = destination;
        const FallResult fall = enemyFallTarget(
            level_, after_, enemyIndex, destination);
        after_.enemies[enemyIndex].cell = fall.cell;
        after_.enemies[enemyIndex].fallen = fall.fallen;
        return true;
    }

    // Moves one tile, resolves the fall, and updates slide momentum.
    // Momentum continues while the entity is icy (an ice block, or anything
    // standing on an ice floor), did not fall, and the next cell is not
    // statically blocked.
    void applyMovableMove(std::size_t index, MoveDirection direction, GridPosition3 target)
    {
        after_.movables[index].cell = target;
        const FallResult fall = movableFallTarget(level_, after_, index, target);
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
    std::vector<Status> status_;
};

} // namespace

GameState step(
    const Level& level,
    const GameState& state,
    std::optional<MoveDirection> playerInput,
    const StepRates& rates)
{
    GameState after = state;
    MicroStepResolver resolver(level, after, playerInput, rates);
    resolver.run();
    return after;
}

} // namespace sokoban::rules
