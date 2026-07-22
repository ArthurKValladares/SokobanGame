#include "engine/Rules.hpp"

#include <algorithm>
#include <cstddef>
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

FallResult playerFallTarget(const Level& level, const GameState& state, GridPosition3 position)
{
    return fallTarget(level, state, position, [&](GridPosition3 below) {
        return movableAt(state, below) != nullptr ||
            fallenMovableAt(state, below) != nullptr;
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
        return !state.playerDead && state.player == below;
    });
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
        , status_(movableCount_ + 1)
    {
        status_[playerIndex()].done = after_.playerDead;
    }

    void run()
    {
        bool anyMovement = true;
        while (anyMovement) {
            deriveIntents();
            markContested();
            anyMovement = resolveMoves();
            settleBlocked();
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

    [[nodiscard]] std::size_t playerIndex() const { return movableCount_; }
    [[nodiscard]] bool isPlayer(std::size_t index) const
    {
        return index == playerIndex();
    }

    [[nodiscard]] std::optional<MoveDirection>& slidingOf(std::size_t index)
    {
        return isPlayer(index)
            ? after_.playerSliding
            : after_.movables[index].sliding;
    }

    [[nodiscard]] GridPosition3 cellOf(std::size_t index) const
    {
        return isPlayer(index) ? after_.player : after_.movables[index].cell;
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
                if (after_.playerDead) {
                    continue;
                }
                if (after_.playerSliding) {
                    if (status.consumed < rates_.slide) {
                        status.intent = after_.playerSliding;
                    }
                } else if (playerInput_) {
                    if (status.consumed < rates_.playerMove) {
                        status.intent = playerInput_;
                        status.inputDriven = true;
                    }
                } else if (const std::optional<MoveDirection> belt =
                               conveyorDirectionAt(level_, after_.player)) {
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
                        playerLadderClimbTarget(level_, after_, *status.intent)
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
                    ? resolvePlayer(anyMovement)
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
            (!after_.playerDead && after_.player == target)) {
            return false; // the blocking entity may still move this micro-step
        }
        applyMovableMove(index, direction, target);
        status.resolved = true;
        status.movedThisMicro = true;
        anyMovement = true;
        return true;
    }

    [[nodiscard]] bool resolvePlayer(bool& anyMovement)
    {
        Status& status = status_[playerIndex()];
        const MoveDirection direction = *status.intent;
        const GridPosition3 target = *status.target;

        if (status.contested) {
            cancelAndFinish(playerIndex(), true);
            status.resolved = true;
            return true;
        }
        if (!staticCellAllowsEntity(level_, target) ||
            !playerFallTarget(level_, after_, target).supported) {
            after_.playerSliding = std::nullopt;
            status.done = true;
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
            if (status.inputDriven &&
                !status_[blockerIndex].movedThisMicro &&
                staticCellAllowsEntity(level_, pushTarget) &&
                !movableBlocksAt(after_, pushTarget, blockerIndex) &&
                movableFallTarget(level_, after_, blockerIndex, pushTarget)
                    .supported) {
                applyMovableMove(blockerIndex, direction, pushTarget);
                status_[blockerIndex].movedThisMicro = true;
                status_[blockerIndex].done = false;
                applyPlayerMove(direction, target);
                status.movedThisMicro = true;
                anyMovement = true;
            } else if (after_.playerSliding) {
                // Blocked slides stop for good.
                after_.playerSliding = std::nullopt;
                status.done = true;
            }
            status.resolved = true;
            return true;
        }
        applyPlayerMove(direction, target);
        status.resolved = true;
        status.movedThisMicro = true;
        anyMovement = true;
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

    void applyPlayerMove(MoveDirection direction, GridPosition3 target)
    {
        after_.player = target;
        const FallResult fall = playerFallTarget(level_, after_, target);
        const bool fell = fall.cell.z != target.z || fall.fallen;
        after_.player = fall.cell;
        after_.playerDead = fall.fallen;
        after_.playerSliding =
            (!fell && !after_.playerDead &&
                isIceFloor(level_, after_, fall.cell) &&
                staticCellAllowsEntity(level_, movementTarget(fall.cell, direction)))
                ? std::optional<MoveDirection>(direction)
                : std::nullopt;
        ++status_[playerIndex()].consumed;
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
