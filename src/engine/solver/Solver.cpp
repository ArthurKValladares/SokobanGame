#include "engine/solver/Solver.hpp"

#include "engine/ActionPlan.hpp"
#include "engine/GameplayConfig.hpp"
#include "engine/Rules.hpp"
#include "engine/solver/DeadPosition.hpp"
#include "engine/solver/Heuristic.hpp"
#include "engine/solver/StateKey.hpp"

#include <algorithm>
#include <array>
#include <queue>
#include <tuple>
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
    // Search cost counts state-changing macro actions. The raw input count is
    // only a tie-breaker; otherwise a long harmless walk can outweigh a push.
    std::size_t significantDepth = 0;
    std::size_t inputDepth = 0;
};

std::vector<EntityId> livingControllerIds(const GameState& state)
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
    return controllers;
}

EntityId nextLivingController(
    const std::vector<EntityId>& controllers, EntityId current)
{
    const auto currentIt = std::ranges::find(controllers, current);
    if (currentIt == controllers.end() || currentIt + 1 == controllers.end()) {
        return controllers.front();
    }
    return *(currentIt + 1);
}

// Only heroes changed: an ordinary walk (or a slide the walk set off). A walk
// that presses a plate and so turns a mirror or moves an elevator changed the
// board, and is a significant action like a push.
bool onlyPlayersMoved(const GameState& before, const GameState& after)
{
    if (rules::anyPlayerDead(after) ||
        before.players.size() != after.players.size()) {
        return false;
    }
    return before.movables == after.movables &&
        before.enemies == after.enemies &&
        before.turnedMirrors == after.turnedMirrors &&
        before.elevators == after.elevators &&
        before.minecarts == after.minecarts;
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

struct CanonicalWalkingRegion {
    PackedStateKey canonical;
    std::unordered_set<PackedStateKey, PackedStateKeyHash> members;
};

class WalkingRegionCache {
public:
    explicit WalkingRegionCache(std::size_t maxStates)
        : capacity_(maxStates)
    {
    }

    [[nodiscard]] bool contains(const PackedStateKey& key) const
    {
        return states_.contains(key);
    }

    void insert(
        std::unordered_set<PackedStateKey, PackedStateKeyHash>& members,
        Statistics& statistics)
    {
        if (capacity_ == 0) {
            return;
        }
        if (members.size() > capacity_) {
            if (!states_.empty()) {
                rotate(statistics, capacity_);
            }
            while (states_.size() < capacity_) {
                states_.insert(members.extract(members.begin()));
            }
            updateStatistics(statistics);
            return;
        }
        if (states_.size() > capacity_ - members.size()) {
            rotate(statistics, members.size());
        }
        // Walking regions retained by different canonical positions are
        // normally disjoint. Node-wise merge preserves that fast path and
        // leaves any unexpected overlaps behind for destruction.
        states_.merge(members);
        updateStatistics(statistics);
    }

private:
    using StateSet =
        std::unordered_set<PackedStateKey, PackedStateKeyHash>;

    void rotate(Statistics& statistics, std::size_t incoming)
    {
        const std::size_t retained = std::min(
            capacity_ / 2, capacity_ - incoming);
        const std::size_t before = states_.size();
        while (states_.size() > retained) {
            states_.erase(states_.begin());
        }
        statistics.canonicalizationCacheEvictions +=
            before - states_.size();
        ++statistics.canonicalizationCacheRotations;
        updateStatistics(statistics);
    }

    void updateStatistics(Statistics& statistics) const
    {
        statistics.canonicalizationCachedStates =
            states_.size();
        statistics.peakCanonicalizationCachedStates = std::max(
            statistics.peakCanonicalizationCachedStates,
            statistics.canonicalizationCachedStates);
    }

    std::size_t capacity_ = 0;
    StateSet states_;
};

CanonicalWalkingRegion canonicalWalkingRegion(
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
    return {
        .canonical = std::move(canonical),
        .members = std::move(seen),
    };
}

struct WalkState {
    GameState state;
    std::size_t parent = 0;
    Input input = Input::Up;
};

struct SignificantTransition {
    std::size_t walk = 0;
    Input input = Input::Up;
    GameState successor;
    EntityId controller = invalidEntityId;
    bool solved = false;
    bool precomputed = false;
    // Points into the expansion-local set. References and pointers to
    // unordered-set elements remain valid when the table rehashes.
    const PackedStateKey* rawKey = nullptr;
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
    result.statistics.multiRockFreezeAnalysisEnabled =
        deadPositions.multiRockFreezeAnalysisEnabled();
    result.statistics.staticDeadCells = deadPositions.deadCellCount();
    const auto rejectedByDeadPosition = [&](const GameState& state) {
        if (!deadPositions.applicable()) {
            return false;
        }
        ++result.statistics.deadPositionChecks;
        const detail::DeadPositionReason reason =
            deadPositions.rejectionReason(state);
        if (reason == detail::DeadPositionReason::None) {
            return false;
        }
        ++result.statistics.deadPositionPrunes;
        switch (reason) {
        case detail::DeadPositionReason::None: break;
        case detail::DeadPositionReason::UnitCount:
            ++result.statistics.deadPositionUnitCountPrunes;
            break;
        case detail::DeadPositionReason::StaticMatching:
            ++result.statistics.deadPositionStaticMatchingPrunes;
            break;
        case detail::DeadPositionReason::FrozenCluster:
            ++result.statistics.deadPositionFrozenClusterPrunes;
            break;
        }
        return true;
    };
    if (rejectedByDeadPosition(driver.state())) {
        result.status = Status::Exhausted;
        return result;
    }
    const detail::RelaxedHeuristic heuristic(level);
    result.statistics.heuristicGraphCells =
        heuristic.traversableCellCount();
    result.statistics.heuristicGraphEdges = heuristic.edgeCount();
    result.statistics.heuristicMirrorEdges = heuristic.mirrorEdgeCount();

    std::vector<Node> nodes;
    nodes.push_back({
        .state = driver.state(),
        .controller = driver.activeHeroController(),
    });
    // Breadth-first by default (fewest significant moves). Best-first orders
    // by significant actions plus a weighted estimate. Raw input count only
    // breaks ties, so walking distance cannot bury a strategically useful
    // push or mirror activation.
    using Entry = std::tuple<std::size_t, std::size_t, std::size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> frontier;
    const auto priority = [&](std::size_t index) -> Entry {
        std::size_t score = nodes[index].significantDepth;
        if (options.strategy == Strategy::BestFirst) {
            const int remaining = heuristic.estimate(nodes[index].state);
            ++result.statistics.heuristicEvaluations;
            if (!result.statistics.bestHeuristic ||
                remaining < *result.statistics.bestHeuristic) {
                result.statistics.bestHeuristic = remaining;
            }
            score += 3 * static_cast<std::size_t>(remaining);
        }
        return { score, nodes[index].inputDepth, index };
    };
    frontier.push(priority(0));
    result.statistics.peakFrontier = 1;

    // The bounded membership cache handles the common duplicate path without
    // retaining every raw successor forever. The canonical set is the
    // lossless, unbounded identity of positions that consume frontier space.
    std::unordered_set<PackedStateKey, PackedStateKeyHash> canonicalQueued;
    WalkingRegionCache walkingRegionCache(options.maxCachedWalkingStates);
    CanonicalWalkingRegion initialRegion = canonicalWalkingRegion(
        level,
        nodes[0].state,
        nodes[0].controller,
        driver,
        result.statistics);
    canonicalQueued.emplace(std::move(initialRegion.canonical));
    walkingRegionCache.insert(initialRegion.members, result.statistics);

    const auto finish = [&](std::size_t index) {
        std::vector<Input> inputs;
        for (std::size_t at = index; at != 0; at = nodes[at].parent) {
            inputs.insert(
                inputs.begin(), nodes[at].inputs.begin(), nodes[at].inputs.end());
        }
        result.inputs = std::move(inputs);
        result.statistics.solutionSignificantMoves =
            nodes[index].significantDepth;
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
        const std::size_t index = std::get<2>(frontier.top());
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
        std::vector<SignificantTransition> significant;
        std::unordered_set<PackedStateKey, PackedStateKeyHash>
            localSuccessors;
        const auto addPrecomputedSuccessor = [
                &result, &significant, &localSuccessors](
                std::size_t walk,
                Input input,
                GameState successor,
                EntityId successorController,
                bool solved) {
            ++result.statistics.significantMovesDiscovered;
            PackedStateKey key = detail::makePackedStateKey(
                successor, successorController);
            const auto [keyPosition, inserted] =
                localSuccessors.emplace(std::move(key));
            if (!inserted) {
                ++result.statistics.localSuccessorDuplicates;
                return;
            }
            significant.push_back({
                .walk = walk,
                .input = input,
                .successor = std::move(successor),
                .controller = successorController,
                .solved = solved,
                .precomputed = true,
                .rawKey = &*keyPosition,
            });
        };
        const auto addDrivenSuccessor = [&result, &significant](
                std::size_t walk, Input input) {
            ++result.statistics.significantMovesDiscovered;
            significant.push_back({
                .walk = walk,
                .input = input,
            });
        };
        for (std::size_t walk = 0; walk < walks.size(); ++walk) {
            ++result.statistics.walkStates;
            for (const Input input : directions) {
                std::optional<DirectionTransition> transition =
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
                    addPrecomputedSuccessor(
                        walk,
                        input,
                        std::move(transition->state),
                        controller,
                        transition->solved);
                    continue;
                }
                PackedStateKey key =
                    detail::makePackedStateKey(transition->state, controller);
                if (!walkSeen.emplace(std::move(key)).second) {
                    continue;
                }
                walks.push_back({
                    .state = std::move(transition->state),
                    .parent = walk,
                    .input = input,
                });
            }
            const std::vector<EntityId> controllers =
                livingControllerIds(walks[walk].state);
            if (controllers.size() > 1) {
                addPrecomputedSuccessor(
                    walk,
                    Input::CycleHero,
                    walks[walk].state,
                    nextLivingController(controllers, controller),
                    false);
            }
            std::optional<rules::MirrorActivationPreview> mirror =
                rules::previewMirrorActivation(level, walks[walk].state);
            if (mirror) {
                if (rules::anyPlayerDead(mirror->after)) {
                    ++result.statistics.playerDeathPrunes;
                } else if (!rules::hasPendingMotion(level, mirror->after)) {
                    const bool solved =
                        rules::isAtUnlockedEnd(level, mirror->after);
                    addPrecomputedSuccessor(
                        walk,
                        Input::Interact,
                        std::move(mirror->after),
                        controller,
                        solved);
                } else {
                    // Mirror previews describe the transaction itself. Let the
                    // Driver resolve any ice, conveyor, or turret consequence.
                    addDrivenSuccessor(walk, Input::Interact);
                }
            }
        }
        result.statistics.peakWalkRegion = std::max(
            result.statistics.peakWalkRegion, walks.size());
        ++result.statistics.expandedPositions;

        for (SignificantTransition& transition : significant) {
            ++result.statistics.significantMovesTried;
            GameState successorState;
            bool successorSolved = false;
            EntityId successorController = invalidEntityId;
            if (transition.precomputed) {
                ++result.statistics.precomputedSuccessorsReused;
                successorState = std::move(transition.successor);
                successorSolved = transition.solved;
                successorController = transition.controller;
            } else {
                ++result.statistics.drivenSuccessors;
                driver.resetTo(
                    walks[transition.walk].state, controller);
                if (!driver.apply(transition.input)) {
                    ++result.statistics.settleFailures;
                    continue;
                }
                if (driver.anyPlayerDead()) {
                    ++result.statistics.playerDeathPrunes;
                    continue;
                }
                successorState = driver.state();
                successorSolved = driver.solved();
                successorController = driver.activeHeroController();
                PackedStateKey localKey = detail::makePackedStateKey(
                    successorState, successorController);
                const auto [keyPosition, inserted] =
                    localSuccessors.emplace(std::move(localKey));
                if (!inserted) {
                    ++result.statistics.localSuccessorDuplicates;
                    continue;
                }
                transition.rawKey = &*keyPosition;
            }
            if (!successorSolved &&
                rejectedByDeadPosition(successorState)) {
                continue;
            }
            const PackedStateKey& rawKey = *transition.rawKey;
            // Cache members are added only after their canonical position is
            // retained. Expanding that position will visit this exact walking
            // state, so the successor cannot expose a new significant move.
            if (!successorSolved && walkingRegionCache.contains(rawKey)) {
                ++result.statistics.canonicalizationCacheHits;
                ++result.statistics.canonicalDuplicates;
                continue;
            }
            std::optional<CanonicalWalkingRegion> region;
            if (!successorSolved) {
                region = canonicalWalkingRegion(
                    level,
                    successorState,
                    successorController,
                    driver,
                    result.statistics);
            }
            PackedStateKey canonicalKey = successorSolved
                ? rawKey
                : std::move(region->canonical);
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
            if (region) {
                walkingRegionCache.insert(
                    region->members, result.statistics);
            }
            std::vector<Input> inputs =
                walkPath(walks, transition.walk);
            inputs.push_back(transition.input);
            const std::size_t inputDepth =
                nodes[index].inputDepth + inputs.size();
            nodes.push_back({
                .state = successorState,
                .controller = successorController,
                .parent = index,
                .inputs = std::move(inputs),
                .significantDepth = nodes[index].significantDepth + 1,
                .inputDepth = inputDepth,
            });
            ++result.statistics.generatedStates;
            frontier.push(priority(nodes.size() - 1));
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
