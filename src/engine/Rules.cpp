#include "engine/Rules.hpp"

#include <algorithm>

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

    return { .cell = current, .fallen = false, .supported = false };
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

    return { .cell = current, .fallen = false, .supported = false };
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

GameState step(
    const Level& level,
    const GameState& state,
    std::optional<MoveDirection> playerInput,
    const StepRates& rates)
{
    GameState after = state;
    const size_t movableCount = after.movables.size();

    // Movement budget consumed so far this step, per entity. Budgets are
    // checked against the rate of the entity's *current* movement source, so
    // intents and rates are re-derived before every micro-step.
    int playerConsumed = 0;
    bool playerDone = after.playerDead;
    std::vector<int> movableConsumed(movableCount, 0);
    std::vector<bool> movableDone(movableCount, false);

    // Moves one tile, resolves the fall, and updates slide momentum. Momentum
    // continues while the entity is icy (an ice block, or anything standing
    // on an ice floor), did not fall, and the next cell is not statically
    // blocked; entity-blocked slides are retried and cancelled if still
    // blocked when their micro-step resolves.
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
        ++movableConsumed[i];
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
        ++playerConsumed;
    };

    bool microProgressed = true;
    while (microProgressed) {
        microProgressed = false;

        // Derive this micro-step's intents from the current state and the
        // remaining budget of each entity's movement source.
        std::optional<MoveDirection> playerIntent;
        bool playerInputDriven = false;
        if (!playerDone && !after.playerDead) {
            if (after.playerSliding) {
                if (playerConsumed < rates.slide) {
                    playerIntent = after.playerSliding;
                }
            } else if (playerInput) {
                if (playerConsumed < rates.playerMove) {
                    playerIntent = playerInput;
                    playerInputDriven = true;
                }
            } else if (const std::optional<MoveDirection> belt = conveyorDirectionAt(level, after.player)) {
                if (playerConsumed < rates.conveyor) {
                    playerIntent = belt;
                }
            }
        }

        std::vector<std::optional<MoveDirection>> movableIntent(movableCount);
        for (size_t i = 0; i < movableCount; ++i) {
            if (movableDone[i] || after.movables[i].fallen) {
                continue;
            }
            if (after.movables[i].sliding) {
                if (movableConsumed[i] < rates.slide) {
                    movableIntent[i] = after.movables[i].sliding;
                }
            } else if (const std::optional<MoveDirection> belt = conveyorDirectionAt(level, after.movables[i].cell)) {
                if (movableConsumed[i] < rates.conveyor) {
                    movableIntent[i] = belt;
                }
            }
        }

        std::vector<std::optional<GridPosition3>> movableTargets(movableCount);
        for (size_t i = 0; i < movableCount; ++i) {
            if (movableIntent[i]) {
                movableTargets[i] = movementTarget(after.movables[i].cell, *movableIntent[i]);
            }
        }
        std::optional<GridPosition3> playerTarget;
        if (playerIntent) {
            playerTarget = movementTarget(after.player, *playerIntent);
            if (playerInputDriven) {
                playerTarget = playerLadderClimbTarget(level, after, *playerIntent).value_or(*playerTarget);
            }
        }

        // Multiple simultaneous intents for one destination all lose. Without
        // this pre-pass, movable vector order would arbitrarily choose a
        // winner before the remaining intents were considered.
        std::vector<bool> movableTargetContested(movableCount, false);
        bool playerTargetContested = false;
        for (size_t i = 0; i < movableCount; ++i) {
            if (!movableTargets[i]) {
                continue;
            }
            for (size_t j = i + 1; j < movableCount; ++j) {
                if (movableTargets[j] && *movableTargets[i] == *movableTargets[j]) {
                    movableTargetContested[i] = true;
                    movableTargetContested[j] = true;
                }
            }
            if (playerTarget && *movableTargets[i] == *playerTarget) {
                movableTargetContested[i] = true;
                playerTargetContested = true;
            }
        }

        // Resolve this micro-step's simultaneous one-tile moves: keep making
        // passes so an entity blocked only by another entity that vacates its
        // cell this micro-step still advances. Each entity moves at most once
        // per micro-step.
        std::vector<bool> movableResolved(movableCount);
        std::vector<bool> movedThisMicro(movableCount, false);
        for (size_t i = 0; i < movableCount; ++i) {
            movableResolved[i] = !movableIntent[i].has_value();
        }
        bool playerResolved = !playerIntent.has_value();
        bool playerMovedThisMicro = false;

        bool progressed = true;
        while (progressed) {
            progressed = false;

            for (size_t i = 0; i < movableCount; ++i) {
                if (movableResolved[i]) {
                    continue;
                }
                const MoveDirection direction = *movableIntent[i];
                const GridPosition3 target = *movableTargets[i];
                if (movableTargetContested[i]) {
                    if (after.movables[i].sliding) {
                        after.movables[i].sliding = std::nullopt;
                        movableDone[i] = true;
                    }
                    movableResolved[i] = true;
                    progressed = true;
                    continue;
                }
                if (!staticCellAllowsEntity(level, target) ||
                    !movableFallTarget(level, after, i, target).supported) {
                    after.movables[i].sliding = std::nullopt;
                    movableDone[i] = true;
                    movableResolved[i] = true;
                    progressed = true;
                    continue;
                }
                if (movableBlocksAt(after, target, i) ||
                    (!after.playerDead && after.player == target)) {
                    continue; // the blocking entity may still move this micro-step
                }
                applyMovableMove(i, direction, target);
                movableResolved[i] = true;
                movedThisMicro[i] = true;
                progressed = true;
                microProgressed = true;
            }

            if (!playerResolved) {
                const MoveDirection direction = *playerIntent;
                const GridPosition3 target = *playerTarget;

                if (playerTargetContested) {
                    if (after.playerSliding) {
                        after.playerSliding = std::nullopt;
                        playerDone = true;
                    }
                    playerResolved = true;
                    progressed = true;
                } else if (!staticCellAllowsEntity(level, target) ||
                    !playerFallTarget(level, after, target).supported) {
                    after.playerSliding = std::nullopt;
                    playerDone = true;
                    playerResolved = true;
                    progressed = true;
                } else if (const GameState::Movable* blocker = movableAt(after, target)) {
                    const auto blockerIndex = static_cast<size_t>(blocker - after.movables.data());
                    if (movableResolved[blockerIndex]) {
                        // The blocker has finished its own movement for this
                        // micro-step. Direct input may push it.
                        const GridPosition3 pushTarget = movementTarget(target, direction);
                        if (playerInputDriven &&
                            !movedThisMicro[blockerIndex] &&
                            staticCellAllowsEntity(level, pushTarget) &&
                            !movableBlocksAt(after, pushTarget, blockerIndex) &&
                            movableFallTarget(level, after, blockerIndex, pushTarget)
                                .supported) {
                            applyMovableMove(blockerIndex, direction, pushTarget);
                            movedThisMicro[blockerIndex] = true;
                            movableDone[blockerIndex] = false;
                            applyPlayerMove(direction, target);
                            playerMovedThisMicro = true;
                            microProgressed = true;
                        } else if (after.playerSliding) {
                            // Blocked slides stop for good.
                            after.playerSliding = std::nullopt;
                            playerDone = true;
                        }
                        playerResolved = true;
                        progressed = true;
                    }
                    // else: wait for the blocker to resolve first
                } else {
                    applyPlayerMove(direction, target);
                    playerMovedThisMicro = true;
                    playerResolved = true;
                    progressed = true;
                    microProgressed = true;
                }
            }
        }

        // Anything with an intent that could not move this micro-step is in a
        // mutual block; blocked slide momentum does not survive.
        for (size_t i = 0; i < movableCount; ++i) {
            if (!movableIntent[i] || movedThisMicro[i]) {
                continue;
            }
            if (after.movables[i].sliding) {
                after.movables[i].sliding = std::nullopt;
                movableDone[i] = true;
            }
        }
        if (playerIntent && !playerMovedThisMicro && after.playerSliding) {
            after.playerSliding = std::nullopt;
            playerDone = true;
        }
    }

    return after;
}

} // namespace sokoban::rules
