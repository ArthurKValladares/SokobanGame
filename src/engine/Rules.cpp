#include "engine/Rules.hpp"

#include <algorithm>

namespace sokoban::rules {
namespace {

struct FallResult {
    GridPosition3 cell {};
    bool fallen = false;
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

bool isPlayerWalkable(const Level& level, const GameState& state, GridPosition3 position)
{
    if (movableAt(state, position) != nullptr) {
        return false;
    }

    return staticCellAllowsEntity(level, position);
}

bool canPushMovable(const Level& level, const GameState& state, GridPosition3 position, MoveDirection direction)
{
    const GridPosition3 target = movementTarget(position, direction);
    if (movableAt(state, target) != nullptr) {
        return false;
    }

    return staticCellAllowsEntity(level, target);
}

bool canMovableOccupy(const Level& level, const GameState& state, GridPosition3 position, size_t movableIndex)
{
    if (!staticCellAllowsEntity(level, position)) {
        return false;
    }

    for (size_t i = 0; i < state.movables.size(); ++i) {
        if (i != movableIndex && !state.movables[i].fallen && state.movables[i].cell == position) {
            return false;
        }
    }

    return true;
}

FallResult playerFallTarget(const Level& level, const GameState& state, GridPosition3 position)
{
    GridPosition3 current = position;
    while (current.z > 0) {
        const GridPosition3 below { current.x, current.y, current.z - 1 };
        const TileType support = tileAt(level, below);

        if (tileTypeIsSolidBlock(support) ||
            movableAt(state, below) != nullptr ||
            fallenMovableAt(state, below) != nullptr) {
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

    return { .cell = current, .fallen = false };
}

FallResult movableFallTarget(const Level& level, const GameState& state, size_t movableIndex, GridPosition3 position)
{
    GridPosition3 current = position;
    while (current.z > 0) {
        const GridPosition3 below { current.x, current.y, current.z - 1 };
        const TileType support = tileAt(level, below);

        bool movableBelow = false;
        for (size_t i = 0; i < state.movables.size(); ++i) {
            if (i != movableIndex && state.movables[i].cell == below) {
                movableBelow = true;
                break;
            }
        }
        if (tileTypeIsSolidBlock(support) ||
            movableBelow ||
            (!state.playerDead && state.player == below)) {
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

    return { .cell = current, .fallen = false };
}

GridPosition3 movableSlidingTarget(const Level& level, const GameState& state, size_t movableIndex, MoveDirection direction)
{
    GridPosition3 current = state.movables[movableIndex].cell;
    const bool movableIsIce = state.movables[movableIndex].type == TileType::Ice;
    while (movableIsIce || isIceFloor(level, state, current)) {
        const GridPosition3 next = movementTarget(current, direction);
        if (!canMovableOccupy(level, state, next, movableIndex)) {
            return current;
        }

        current = next;
        const FallResult fall = movableFallTarget(level, state, movableIndex, current);
        if (fall.cell.z != current.z || fall.fallen) {
            return fall.cell;
        }
    }

    return current;
}

GridPosition3 playerSlidingTarget(const Level& level, const GameState& state, GridPosition3 position, MoveDirection direction)
{
    GridPosition3 current = position;
    while (isIceFloor(level, state, current)) {
        const GridPosition3 next = movementTarget(current, direction);
        if (!isPlayerWalkable(level, state, next)) {
            return current;
        }

        current = next;
        const FallResult fall = playerFallTarget(level, state, current);
        if (fall.cell.z != current.z || fall.fallen) {
            return fall.cell;
        }
    }

    return current;
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

    return topCell;
}

std::optional<GridPosition3> playerLadderClimbTarget(const Level& level, const GameState& state, MoveDirection direction)
{
    const GridPosition3 flatTarget = movementTarget(state.player, direction);
    if (!level.inBounds(flatTarget) || !level.inBounds(state.player)) {
        return std::nullopt;
    }

    if (tileAt(level, state.player) == TileType::Ladder) {
        return ladderClimbTarget(level, state, state.player, flatTarget);
    }

    return std::nullopt;
}

} // namespace

GameState initialState(const Level& level)
{
    GameState state;
    state.player = level.playerStart();
    state.movables.reserve(level.movableTiles().size());
    for (const Level::MovableTile& movable : level.movableTiles()) {
        state.movables.push_back({
            .type = movable.type,
            .cell = movable.position,
        });
    }

    return state;
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

bool isUnfilledWater(const Level& level, const GameState& state, GridPosition3 position)
{
    if (!level.inBounds(position)) {
        return false;
    }

    return level.supportingTileAt(position) == TileType::Water &&
        fallenMovableAt(state, position) == nullptr &&
        !(state.playerDead && state.player == position);
}

bool isEndUnlocked(const Level& level, const GameState& state)
{
    return std::ranges::all_of(level.pressurePlates(), [&](GridPosition3 plate) {
        return state.player == plate || movableAt(state, plate) != nullptr;
    });
}

bool isAtUnlockedEnd(const Level& level, const GameState& state)
{
    return level.isEnd(state.player) && isEndUnlocked(level, state);
}

std::optional<GameState> tryMove(
    const Level& level,
    const GameState& state,
    MoveDirection direction,
    bool allowPush)
{
    if (state.playerDead) {
        return std::nullopt;
    }

    const GridPosition3 target =
        playerLadderClimbTarget(level, state, direction)
            .value_or(movementTarget(state.player, direction));
    if (!staticCellAllowsEntity(level, target)) {
        return std::nullopt;
    }

    GameState after = state;
    after.player = target;

    if (const GameState::Movable* movable = movableAt(state, target)) {
        if (!allowPush) {
            return std::nullopt;
        }
        if (!canPushMovable(level, state, target, direction)) {
            return std::nullopt;
        }

        const auto movableIndex = static_cast<size_t>(movable - state.movables.data());
        after.movables[movableIndex].cell = movementTarget(movable->cell, direction);
        after.movables[movableIndex].fallen = false;
        after.movables[movableIndex].cell = movableSlidingTarget(level, after, movableIndex, direction);
        const FallResult movableFall =
            movableFallTarget(level, after, movableIndex, after.movables[movableIndex].cell);
        after.movables[movableIndex].cell = movableFall.cell;
        after.movables[movableIndex].fallen = movableFall.fallen;
    }

    after.player = playerSlidingTarget(level, after, after.player, direction);
    const FallResult playerFall = playerFallTarget(level, after, after.player);
    after.player = playerFall.cell;
    after.playerDead = playerFall.fallen;

    return after;
}

bool anyEntityOnConveyor(const Level& level, const GameState& state)
{
    if (!state.playerDead && conveyorDirectionAt(level, state.player)) {
        return true;
    }

    return std::ranges::any_of(state.movables, [&](const GameState::Movable& movable) {
        return !movable.fallen && conveyorDirectionAt(level, movable.cell).has_value();
    });
}

std::optional<GameState> applyConveyorStep(const Level& level, const GameState& state)
{
    GameState after = state;
    std::vector<bool> movableMoved(after.movables.size(), false);
    bool playerMoved = false;

    // Entities move simultaneously: an entity blocked only by another entity
    // that itself moves away this tick still advances, so keep making passes
    // until nothing else can move. Each entity moves at most once per tick.
    bool progressed = true;
    while (progressed) {
        progressed = false;

        for (size_t i = 0; i < after.movables.size(); ++i) {
            if (movableMoved[i] || after.movables[i].fallen) {
                continue;
            }
            const std::optional<MoveDirection> direction = conveyorDirectionAt(level, after.movables[i].cell);
            if (!direction) {
                continue;
            }
            const GridPosition3 target = movementTarget(after.movables[i].cell, *direction);
            if (!canMovableOccupy(level, after, target, i)) {
                continue;
            }
            if (!after.playerDead && after.player == target) {
                continue;
            }

            after.movables[i].cell = target;
            after.movables[i].cell = movableSlidingTarget(level, after, i, *direction);
            const FallResult fall = movableFallTarget(level, after, i, after.movables[i].cell);
            after.movables[i].cell = fall.cell;
            after.movables[i].fallen = fall.fallen;
            movableMoved[i] = true;
            progressed = true;
        }

        if (!playerMoved && !after.playerDead) {
            if (const std::optional<MoveDirection> direction = conveyorDirectionAt(level, after.player)) {
                const GridPosition3 target = movementTarget(after.player, *direction);
                if (isPlayerWalkable(level, after, target)) {
                    after.player = playerSlidingTarget(level, after, target, *direction);
                    const FallResult fall = playerFallTarget(level, after, after.player);
                    after.player = fall.cell;
                    after.playerDead = fall.fallen;
                    playerMoved = true;
                    progressed = true;
                }
            }
        }
    }

    if (after == state) {
        return std::nullopt;
    }

    return after;
}

} // namespace sokoban::rules
