#include "engine/solver/Solver.hpp"

#include "engine/ActionPlan.hpp"
#include "engine/GameplayConfig.hpp"
#include "engine/Rules.hpp"
#include "engine/solver/DeadPosition.hpp"
#include "engine/solver/StateKey.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <unordered_set>
#include <utility>

namespace sokoban::solver {
namespace {

using solution::Input;
using detail::PackedStateKey;
using detail::PackedStateKeyHash;

constexpr std::array directions {
    Input::Up, Input::Down, Input::Left, Input::Right,
};

// A search node is a "position" in the classic Sokoban sense: everything
// that is not a walking hero is fixed, and walking between cells the heroes
// can reach without changing anything else is free. Collapsing walks keeps
// the search to the moves that matter (pushes, deaths avoided, mirror
// activations, hero switches), which is what makes the larger rooms tractable.
struct Node {
    GameState state;
    EntityId controller = invalidEntityId;
    std::size_t parent = 0;
    // Inputs from the parent's state to this one: a walk, then the move.
    std::vector<Input> inputs;
    // Inputs from the start to here.
    std::size_t depth = 0;
};

int distance(GridPosition3 a, GridPosition3 b)
{
    return std::abs(a.x - b.x) + std::abs(a.y - b.y) +
        std::abs(a.z - b.z);
}

// Estimated remaining work for best-first search: every uncovered pressure
// plate wants the nearest free movable, then every End wants a hero. Not
// admissible - it is for finding a solution in a large room, not the
// shortest one.
int estimate(
    const Level& level,
    const std::vector<GridPosition3>& ends,
    const GameState& state)
{
    int cost = 0;
    const auto onPlate = [&](GridPosition3 cell) {
        return std::ranges::find(level.pressurePlates(), cell) !=
            level.pressurePlates().end();
    };
    for (const GridPosition3 plate : level.pressurePlates()) {
        if (rules::movableAt(state, plate) != nullptr) {
            continue;
        }
        int nearest = 64;
        for (const GameState::Movable& movable : state.movables) {
            if (!movable.fallen && !movable.dead && !onPlate(movable.cell)) {
                nearest = std::min(nearest, distance(movable.cell, plate));
            }
        }
        cost += 8 + nearest;
    }
    if (cost == 0) {
        // Every End needs its own hero. Missing heroes must come from
        // mirrors, which is expensive.
        int living = 0;
        for (const GameState::Player& player : state.players) {
            living += player.dead ? 0 : 1;
        }
        const int missing = static_cast<int>(ends.size()) - living;
        cost += 8 * std::max(missing, 0);
        for (const GridPosition3 end : ends) {
            int nearest = 64;
            for (const GameState::Player& player : state.players) {
                if (!player.dead) {
                    nearest = std::min(nearest, distance(player.cell, end));
                }
            }
            cost += nearest;
        }
    }
    return cost;
}

std::size_t livingControllers(const GameState& state)
{
    std::vector<EntityId> controllers;
    for (std::size_t i = 0; i < state.players.size(); ++i) {
        if (state.players[i].dead) {
            continue;
        }
        const EntityId controller = rules::playerControllerId(state, i);
        if (std::ranges::find(controllers, controller) == controllers.end()) {
            controllers.push_back(controller);
        }
    }
    return controllers.size();
}

// Only heroes changed: an ordinary walk (or a slide the walk set off).
bool onlyPlayersMoved(const GameState& before, const GameState& after)
{
    if (rules::anyPlayerDead(after) ||
        before.players.size() != after.players.size()) {
        return false;
    }
    return before.movables == after.movables &&
        before.enemies == after.enemies;
}

struct DirectionTransition {
    GameState state;
    bool solved = false;
};

std::optional<DirectionTransition> applyDirection(
    const Level& level,
    const GameState& before,
    EntityId controller,
    Input input,
    solution::Driver& fallbackDriver)
{
    MoveDirection direction = MoveDirection::Up;
    switch (input) {
    case Input::Up: direction = MoveDirection::Up; break;
    case Input::Down: direction = MoveDirection::Down; break;
    case Input::Left: direction = MoveDirection::Left; break;
    case Input::Right: direction = MoveDirection::Right; break;
    case Input::CycleHero:
    case Input::Interact:
    case Input::Undo:
        return std::nullopt;
    }

    // Most solver probes are settled walks or pushes with no automatic
    // consequence. Plan those directly through the same production rules the
    // GameplaySession uses, avoiding scheduler and presentation setup. Ice,
    // conveyors, and turret reactions still take the full Driver path.
    const std::optional<plans::PlannedAction> planned =
        plans::planPlayerStep(
            level,
            before,
            direction,
            {},
            config::stepDurationSeconds,
            controller);
    if (!planned) {
        return DirectionTransition {
            .state = before,
            .solved = rules::isAtUnlockedEnd(level, before),
        };
    }
    if (!rules::hasPendingMotion(level, planned->action.after)) {
        return DirectionTransition {
            .state = planned->action.after,
            .solved = rules::isAtUnlockedEnd(level, planned->action.after),
        };
    }

    fallbackDriver.resetTo(before, controller);
    if (!fallbackDriver.apply(input)) {
        return std::nullopt;
    }
    return DirectionTransition {
        .state = fallbackDriver.state(),
        .solved = fallbackDriver.solved(),
    };
}

PackedStateKey canonicalWalkingKey(
    const Level& level,
    const GameState& state,
    EntityId controller,
    solution::Driver& driver,
    Statistics& statistics)
{
    ++statistics.canonicalizationFloods;
    std::vector<GameState> walks { state };
    PackedStateKey canonical =
        detail::makePackedStateKey(state, controller);
    std::unordered_set<PackedStateKey, PackedStateKeyHash> seen;
    seen.emplace(canonical);

    for (std::size_t walk = 0; walk < walks.size(); ++walk) {
        ++statistics.canonicalizationWalkStates;
        for (const Input input : directions) {
            const std::optional<DirectionTransition> transition =
                applyDirection(
                    level, walks[walk], controller, input, driver);
            if (!transition || rules::anyPlayerDead(transition->state) ||
                transition->state == walks[walk] ||
                !onlyPlayersMoved(walks[walk], transition->state) ||
                transition->solved) {
                continue;
            }
            PackedStateKey key =
                detail::makePackedStateKey(transition->state, controller);
            if (!seen.emplace(key).second) {
                continue;
            }
            if (key < canonical) {
                canonical = key;
            }
            walks.push_back(transition->state);
        }
    }
    statistics.peakCanonicalWalkRegion = std::max(
        statistics.peakCanonicalWalkRegion, walks.size());
    return canonical;
}

struct WalkState {
    GameState state;
    std::size_t parent = 0;
    Input input = Input::Up;
};

std::vector<Input> walkPath(
    const std::vector<WalkState>& walks, std::size_t index)
{
    std::vector<Input> inputs;
    for (; index != 0; index = walks[index].parent) {
        inputs.push_back(walks[index].input);
    }
    std::ranges::reverse(inputs);
    return inputs;
}

} // namespace

Result solve(const Level& level, const Options& options)
{
    Result result;
    if (options.maxStates == 0) {
        result.status = Status::StateLimitReached;
        return result;
    }
    result.statistics.generatedStates = 1;
    solution::Driver driver(level);
    if (driver.solved()) {
        result.status = Status::Solved;
        return result;
    }
    const detail::DeadPositionIndex deadPositions(level);
    result.statistics.staticDeadPositionAnalysisEnabled =
        deadPositions.staticAnalysisEnabled();
    result.statistics.staticDeadCells = deadPositions.deadCellCount();
    if (deadPositions.applicable()) {
        ++result.statistics.deadPositionChecks;
        if (deadPositions.rejects(driver.state())) {
            ++result.statistics.deadPositionPrunes;
            result.status = Status::Exhausted;
            return result;
        }
    }

    std::vector<Node> nodes;
    nodes.push_back({
        .state = driver.state(),
        .controller = driver.activeHeroController(),
    });
    const std::vector<GridPosition3>& ends = level.ends();
    // Breadth-first by default (fewest significant moves). Best-first orders
    // by depth plus a weighted estimate instead.
    using Entry = std::pair<std::size_t, std::size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> frontier;
    const auto priority = [&](std::size_t index) -> std::size_t {
        if (options.strategy != Strategy::BestFirst) {
            return index;
        }
        const int remaining = estimate(level, ends, nodes[index].state);
        ++result.statistics.heuristicEvaluations;
        if (!result.statistics.bestHeuristic ||
            remaining < *result.statistics.bestHeuristic) {
            result.statistics.bestHeuristic = remaining;
        }
        return nodes[index].depth +
            3 * static_cast<std::size_t>(remaining);
    };
    frontier.emplace(priority(0), 0);
    result.statistics.peakFrontier = 1;

    // Keep an inexpensive exact-state filter in front of walk-region
    // canonicalization. The canonical set then prevents equivalent walking
    // variants from ever consuming frontier or retained-node capacity.
    std::unordered_set<PackedStateKey, PackedStateKeyHash> rawQueued;
    std::unordered_set<PackedStateKey, PackedStateKeyHash> canonicalQueued;
    rawQueued.emplace(detail::makePackedStateKey(
        nodes[0].state, nodes[0].controller));
    canonicalQueued.emplace(canonicalWalkingKey(
        level,
        nodes[0].state,
        nodes[0].controller,
        driver,
        result.statistics));

    const auto finish = [&](std::size_t index) {
        std::vector<Input> inputs;
        for (std::size_t at = index; at != 0; at = nodes[at].parent) {
            inputs.insert(
                inputs.begin(), nodes[at].inputs.begin(), nodes[at].inputs.end());
        }
        result.inputs = std::move(inputs);
        result.status = Status::Solved;
    };

    std::size_t nextProgress = options.progressInterval;
    const auto reportProgress = [&]() {
        if (!options.progress || options.progressInterval == 0 ||
            result.statistics.generatedStates < nextProgress) {
            return true;
        }
        const bool keepGoing = options.progress({
            .statistics = result.statistics,
            .frontierSize = frontier.size(),
        });
        nextProgress = result.statistics.generatedStates +
            options.progressInterval;
        return keepGoing;
    };

    while (!frontier.empty()) {
        const std::size_t index = frontier.top().second;
        frontier.pop();
        ++result.statistics.frontierPops;
        if (nodes.size() >= options.maxStates) {
            result.status = Status::StateLimitReached;
            return result;
        }
        const EntityId controller = nodes[index].controller;

        // Flood the walkable region.
        std::vector<WalkState> walks { { .state = nodes[index].state } };
        std::unordered_set<PackedStateKey, PackedStateKeyHash> walkSeen;
        walkSeen.emplace(
            detail::makePackedStateKey(walks[0].state, controller));
        std::vector<std::pair<std::size_t, Input>> significant;
        for (std::size_t walk = 0; walk < walks.size(); ++walk) {
            ++result.statistics.walkStates;
            for (const Input input : directions) {
                const std::optional<DirectionTransition> transition =
                    applyDirection(
                        level,
                        walks[walk].state,
                        controller,
                        input,
                        driver);
                if (!transition) {
                    ++result.statistics.settleFailures;
                    continue;
                }
                if (rules::anyPlayerDead(transition->state)) {
                    ++result.statistics.playerDeathPrunes;
                    continue;
                }
                if (transition->state == walks[walk].state) {
                    ++result.statistics.unchangedInputs;
                    continue;
                }
                if (!onlyPlayersMoved(
                        walks[walk].state, transition->state) ||
                    transition->solved) {
                    significant.emplace_back(walk, input);
                    ++result.statistics.significantMovesDiscovered;
                    continue;
                }
                PackedStateKey key =
                    detail::makePackedStateKey(transition->state, controller);
                if (!walkSeen.emplace(std::move(key)).second) {
                    continue;
                }
                walks.push_back({
                    .state = transition->state,
                    .parent = walk,
                    .input = input,
                });
            }
            if (livingControllers(walks[walk].state) > 1) {
                significant.emplace_back(walk, Input::CycleHero);
                ++result.statistics.significantMovesDiscovered;
            }
            if (rules::previewMirrorActivation(level, walks[walk].state)) {
                significant.emplace_back(walk, Input::Interact);
                ++result.statistics.significantMovesDiscovered;
            }
        }
        result.statistics.peakWalkRegion = std::max(
            result.statistics.peakWalkRegion, walks.size());
        ++result.statistics.expandedPositions;

        for (const auto& [walk, input] : significant) {
            ++result.statistics.significantMovesTried;
            driver.resetTo(walks[walk].state, controller);
            if (!driver.apply(input)) {
                ++result.statistics.settleFailures;
                continue;
            }
            if (driver.anyPlayerDead()) {
                ++result.statistics.playerDeathPrunes;
                continue;
            }
            const GameState successorState = driver.state();
            const bool successorSolved = driver.solved();
            if (deadPositions.applicable()) {
                ++result.statistics.deadPositionChecks;
                if (!successorSolved &&
                    deadPositions.rejects(successorState)) {
                    ++result.statistics.deadPositionPrunes;
                    continue;
                }
            }
            const EntityId successorController =
                driver.activeHeroController();
            PackedStateKey rawKey = detail::makePackedStateKey(
                successorState, successorController);
            if (!rawQueued.emplace(rawKey).second) {
                ++result.statistics.queuedDuplicates;
                continue;
            }
            PackedStateKey canonicalKey = successorSolved
                ? rawKey
                : canonicalWalkingKey(
                    level,
                    successorState,
                    successorController,
                    driver,
                    result.statistics);
            if (!canonicalQueued.emplace(std::move(canonicalKey)).second) {
                ++result.statistics.canonicalDuplicates;
                continue;
            }
            // Keep maxStates an exact retained-node budget. The former
            // tool-local search observed the limit only on the next frontier
            // pop, so one high-branching expansion could overshoot it.
            if (nodes.size() >= options.maxStates) {
                result.status = Status::StateLimitReached;
                return result;
            }
            std::vector<Input> inputs = walkPath(walks, walk);
            inputs.push_back(input);
            const std::size_t depth = nodes[index].depth + inputs.size();
            nodes.push_back({
                .state = successorState,
                .controller = successorController,
                .parent = index,
                .inputs = std::move(inputs),
                .depth = depth,
            });
            ++result.statistics.generatedStates;
            frontier.emplace(priority(nodes.size() - 1), nodes.size() - 1);
            result.statistics.peakFrontier = std::max(
                result.statistics.peakFrontier, frontier.size());
            if (successorSolved) {
                finish(nodes.size() - 1);
                return result;
            }
        }
        if (!reportProgress()) {
            result.status = Status::Cancelled;
            return result;
        }
    }

    result.status = Status::Exhausted;
    return result;
}

std::string_view statusName(Status status)
{
    switch (status) {
    case Status::Solved: return "solved";
    case Status::Exhausted: return "exhausted";
    case Status::StateLimitReached: return "state limit reached";
    case Status::Cancelled: return "cancelled";
    }
    return "unknown";
}

} // namespace sokoban::solver
