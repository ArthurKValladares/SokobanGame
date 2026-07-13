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

bool movableBlocksAt(const GameState& state, GridPosition3 position, size_t ignoreIndex)
{
    for (size_t i = 0; i < state.movables.size(); ++i) {
        if (i != ignoreIndex && !state.movables[i].fallen && state.movables[i].cell == position) {
            return true;
        }
    }

    return false;
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
        GameState::Movable entry;
        entry.type = movable.type;
        entry.cell = movable.position;
        state.movables.push_back(entry);
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

bool hasPendingMotion(const Level& level, const GameState& state)
{
    if (!state.playerDead &&
        (state.playerSliding || conveyorDirectionAt(level, state.player))) {
        return true;
    }

    return std::ranges::any_of(state.movables, [&](const GameState::Movable& movable) {
        return !movable.fallen &&
            (movable.sliding || conveyorDirectionAt(level, movable.cell).has_value());
    });
}

GameState step(const Level& level, const GameState& state, std::optional<MoveDirection> playerInput)
{
    GameState after = state;

    // Movement intents for this step. Slide momentum overrides player input;
    // input overrides conveyors.
    std::optional<MoveDirection> playerIntent;
    bool playerInputDriven = false;
    if (!after.playerDead) {
        if (after.playerSliding) {
            playerIntent = after.playerSliding;
        } else if (playerInput) {
            playerIntent = playerInput;
            playerInputDriven = true;
        } else {
            playerIntent = conveyorDirectionAt(level, after.player);
        }
    }

    const size_t movableCount = after.movables.size();
    std::vector<std::optional<MoveDirection>> movableIntent(movableCount);
    std::vector<bool> movableResolved(movableCount, false);
    std::vector<bool> movableMoved(movableCount, false);
    for (size_t i = 0; i < movableCount; ++i) {
        if (!after.movables[i].fallen) {
            movableIntent[i] = after.movables[i].sliding
                ? after.movables[i].sliding
                : conveyorDirectionAt(level, after.movables[i].cell);
        }
        movableResolved[i] = !movableIntent[i].has_value();
    }
    bool playerResolved = !playerIntent.has_value();

    // Moves one tile, resolves the fall, and updates slide momentum. Momentum
    // continues while the entity is icy (an ice block, or anything standing
    // on an ice floor), did not fall, and the next cell is not statically
    // blocked; entity-blocked slides are retried next step and cancelled if
    // still blocked.
    auto applyMovableMove = [&](size_t i, MoveDirection direction, GridPosition3 target) {
        after.movables[i].cell = target;
        const FallResult fall = movableFallTarget(level, after, i, target);
        const bool fell = fall.cell.z != target.z || fall.fallen;
        after.movables[i].cell = fall.cell;
        after.movables[i].fallen = fall.fallen;
        const bool slippery = after.movables[i].type == TileType::Ice ||
            isIceFloor(level, after, fall.cell);
        after.movables[i].sliding =
            (!fell && slippery && staticCellAllowsEntity(level, movementTarget(fall.cell, direction)))
                ? std::optional<MoveDirection>(direction)
                : std::nullopt;
        movableResolved[i] = true;
        movableMoved[i] = true;
    };

    auto applyPlayerMove = [&](MoveDirection direction, GridPosition3 target) {
        after.player = target;
        const FallResult fall = playerFallTarget(level, after, target);
        const bool fell = fall.cell.z != target.z || fall.fallen;
        after.player = fall.cell;
        after.playerDead = fall.fallen;
        after.playerSliding =
            (!fell && !after.playerDead &&
                isIceFloor(level, after, fall.cell) &&
                staticCellAllowsEntity(level, movementTarget(fall.cell, direction)))
                ? std::optional<MoveDirection>(direction)
                : std::nullopt;
        playerResolved = true;
    };

    // All entities move simultaneously: keep making passes so an entity
    // blocked only by another entity that vacates its cell this step still
    // advances. Each entity moves at most once per step.
    bool progressed = true;
    while (progressed) {
        progressed = false;

        for (size_t i = 0; i < movableCount; ++i) {
            if (movableResolved[i]) {
                continue;
            }
            const MoveDirection direction = *movableIntent[i];
            const GridPosition3 target = movementTarget(after.movables[i].cell, direction);
            if (!staticCellAllowsEntity(level, target)) {
                after.movables[i].sliding = std::nullopt;
                movableResolved[i] = true;
                progressed = true;
                continue;
            }
            if (movableBlocksAt(after, target, i) ||
                (!after.playerDead && after.player == target)) {
                continue; // the blocking entity may still move this step
            }
            applyMovableMove(i, direction, target);
            progressed = true;
        }

        if (!playerResolved) {
            const MoveDirection direction = *playerIntent;
            GridPosition3 target = movementTarget(after.player, direction);
            if (playerInputDriven) {
                target = playerLadderClimbTarget(level, after, direction).value_or(target);
            }

            if (!staticCellAllowsEntity(level, target)) {
                after.playerSliding = std::nullopt;
                playerResolved = true;
                progressed = true;
            } else if (const GameState::Movable* blocker = movableAt(after, target)) {
                const auto blockerIndex = static_cast<size_t>(blocker - after.movables.data());
                if (movableResolved[blockerIndex]) {
                    // The blocker has finished its own movement for this step.
                    // Direct input may push it, spending its one move.
                    const GridPosition3 pushTarget = movementTarget(target, direction);
                    if (playerInputDriven &&
                        !movableMoved[blockerIndex] &&
                        staticCellAllowsEntity(level, pushTarget) &&
                        !movableBlocksAt(after, pushTarget, blockerIndex)) {
                        applyMovableMove(blockerIndex, direction, pushTarget);
                        applyPlayerMove(direction, target);
                    } else {
                        after.playerSliding = std::nullopt;
                        playerResolved = true;
                    }
                    progressed = true;
                }
                // else: wait for the blocker to resolve first
            } else {
                applyPlayerMove(direction, target);
                progressed = true;
            }
        }
    }

    // Anything still unresolved is stuck in a mutual block this step; slide
    // momentum does not survive a blocked attempt.
    for (size_t i = 0; i < movableCount; ++i) {
        if (!movableResolved[i]) {
            after.movables[i].sliding = std::nullopt;
        }
    }
    if (!playerResolved) {
        after.playerSliding = std::nullopt;
    }

    return after;
}

} // namespace sokoban::rules
