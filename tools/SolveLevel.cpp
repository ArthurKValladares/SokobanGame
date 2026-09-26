// Finds a solution for puzzle screens by breadth-first search over the real
// gameplay loop, and writes it as a recorded solution.
//
//   sokoban_solve_level <levels-root> <solutions-dir> [--level L [--screen S]]
//                       [--max-states N] [--best-first] [--overwrite]
//
// Each input is applied through solution::Driver, exactly as a replay applies
// it, so a solution this tool finds is one the solution_replay test accepts.
// Screens that already have a solution for their current content are skipped
// unless --overwrite is given.

#include "engine/Level.hpp"
#include "engine/LevelCatalog.hpp"
#include "engine/Rules.hpp"
#include "engine/Solution.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace sokoban;
using solution::Input;

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
    return std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z);
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

void appendCell(std::string& key, GridPosition3 cell)
{
    key += std::to_string(cell.x) + ',' + std::to_string(cell.y) + ',' +
        std::to_string(cell.z) + ';';
}

std::string stateKey(const GameState& state, EntityId controller)
{
    std::string key = std::to_string(controller) + '|';
    for (const GameState::Player& player : state.players) {
        key += 'p' + std::to_string(player.id);
        appendCell(key, player.cell);
        key += player.dead ? 'd' : '-';
    }
    for (const GameState::Movable& movable : state.movables) {
        key += 'm' + std::to_string(movable.id);
        appendCell(key, movable.cell);
        key += movable.fallen ? 'f' : '-';
        key += movable.dead ? 'd' : '-';
    }
    for (const GameState::Enemy& enemy : state.enemies) {
        key += 'e' + std::to_string(enemy.id);
        appendCell(key, enemy.cell);
        key += enemy.fallen ? 'f' : '-';
        key += enemy.dead ? 'd' : '-';
    }
    return key;
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

struct SearchResult {
    std::optional<std::vector<Input>> inputs;
    std::size_t statesVisited = 0;
    bool exhausted = false;
};

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

SearchResult search(const Level& level, std::size_t maxStates, bool bestFirst)
{
    constexpr std::array directions {
        Input::Up, Input::Down, Input::Left, Input::Right,
    };
    SearchResult result;
    solution::Driver driver(level);
    if (driver.solved()) {
        result.inputs = std::vector<Input> {};
        return result;
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
        if (!bestFirst) {
            return index;
        }
        return nodes[index].depth +
            3 * static_cast<std::size_t>(
                estimate(level, ends, nodes[index].state));
    };
    frontier.emplace(priority(0), 0);
    // Canonical keys of positions already expanded: the smallest walk-state
    // key in the reachable region.
    std::unordered_map<std::string, std::size_t> expanded;
    std::unordered_map<std::string, std::size_t> queued;
    queued.emplace(stateKey(nodes[0].state, nodes[0].controller), 0);

    const auto finish = [&](std::size_t index) {
        std::vector<Input> inputs;
        for (std::size_t at = index; at != 0; at = nodes[at].parent) {
            inputs.insert(
                inputs.begin(), nodes[at].inputs.begin(), nodes[at].inputs.end());
        }
        result.inputs = std::move(inputs);
    };

    while (!frontier.empty()) {
        const std::size_t index = frontier.top().second;
        frontier.pop();
        if (nodes.size() >= maxStates) {
            result.statesVisited = nodes.size();
            return result;
        }
        const EntityId controller = nodes[index].controller;

        // Flood the walkable region.
        std::vector<WalkState> walks { { .state = nodes[index].state } };
        std::unordered_map<std::string, std::size_t> walkSeen;
        std::string canonical = stateKey(walks[0].state, controller);
        walkSeen.emplace(canonical, 0);
        std::vector<std::pair<std::size_t, Input>> significant;
        for (std::size_t walk = 0; walk < walks.size(); ++walk) {
            for (const Input input : directions) {
                driver.resetTo(walks[walk].state, controller);
                if (!driver.apply(input) || driver.anyPlayerDead()) {
                    continue;
                }
                if (driver.state() == walks[walk].state) {
                    continue;
                }
                if (!onlyPlayersMoved(walks[walk].state, driver.state()) ||
                    driver.solved()) {
                    significant.emplace_back(walk, input);
                    continue;
                }
                std::string key = stateKey(driver.state(), controller);
                if (walkSeen.contains(key)) {
                    continue;
                }
                canonical = std::min(canonical, key);
                walkSeen.emplace(std::move(key), walks.size());
                walks.push_back({
                    .state = driver.state(),
                    .parent = walk,
                    .input = input,
                });
            }
            if (livingControllers(walks[walk].state) > 1) {
                significant.emplace_back(walk, Input::CycleHero);
            }
            if (rules::previewMirrorActivation(level, walks[walk].state)) {
                significant.emplace_back(walk, Input::Interact);
            }
        }
        if (!expanded.emplace(canonical, index).second) {
            continue;
        }

        for (const auto& [walk, input] : significant) {
            driver.resetTo(walks[walk].state, controller);
            if (!driver.apply(input) || driver.anyPlayerDead()) {
                continue;
            }
            std::string key =
                stateKey(driver.state(), driver.activeHeroController());
            if (queued.contains(key)) {
                continue;
            }
            queued.emplace(std::move(key), nodes.size());
            std::vector<Input> inputs = walkPath(walks, walk);
            inputs.push_back(input);
            const std::size_t depth = nodes[index].depth + inputs.size();
            nodes.push_back({
                .state = driver.state(),
                .controller = driver.activeHeroController(),
                .parent = index,
                .inputs = std::move(inputs),
                .depth = depth,
            });
            frontier.emplace(priority(nodes.size() - 1), nodes.size() - 1);
            if (driver.solved()) {
                finish(nodes.size() - 1);
                result.statesVisited = nodes.size();
                return result;
            }
        }
    }
    result.statesVisited = nodes.size();
    result.exhausted = true;
    return result;
}

std::map<std::uint64_t, std::filesystem::path> existingSolutions(
    const std::filesystem::path& directory)
{
    std::map<std::uint64_t, std::filesystem::path> found;
    if (!std::filesystem::is_directory(directory)) {
        return found;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".solution") {
            continue;
        }
        std::ifstream stream(entry.path(), std::ios::binary);
        std::stringstream text;
        text << stream.rdbuf();
        try {
            found.emplace(solution::parse(text.str()).levelDigest, entry.path());
        } catch (const std::exception&) {
            // A broken file is the replay test's to report.
            continue;
        }
    }
    return found;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "usage: sokoban_solve_level <levels-root> "
                     "<solutions-dir> [--level L [--screen S]] "
                     "[--max-states N] [--best-first] [--overwrite]\n";
        return 2;
    }
    const std::filesystem::path levelsRoot = argv[1];
    const std::filesystem::path solutionsDir = argv[2];
    std::optional<int> onlyLevel;
    std::optional<int> onlyScreen;
    std::size_t maxStates = 2'000'000;
    bool overwrite = false;
    bool bestFirst = false;
    try {
        for (int index = 3; index < argc; ++index) {
            const std::string_view argument = argv[index];
            const auto value = [&]() -> std::string {
                if (index + 1 >= argc) {
                    throw std::invalid_argument(
                        std::string(argument) + " needs a value");
                }
                return argv[++index];
            };
            if (argument == "--level") {
                onlyLevel = std::stoi(value());
            } else if (argument == "--screen") {
                onlyScreen = std::stoi(value());
            } else if (argument == "--max-states") {
                maxStates = static_cast<std::size_t>(std::stoull(value()));
            } else if (argument == "--overwrite") {
                overwrite = true;
            } else if (argument == "--best-first") {
                bestFirst = true;
            } else {
                std::cerr << "unknown argument " << argument << "\n";
                return 2;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 2;
    }

    std::filesystem::create_directories(solutionsDir);
    const auto existing = existingSolutions(solutionsDir);
    int failures = 0;
    for (int levelIndex = 0;; ++levelIndex) {
        const std::filesystem::path levelDirectory =
            levelDirectoryPath(levelsRoot, levelIndex);
        if (!std::filesystem::is_directory(levelDirectory)) {
            break;
        }
        if (onlyLevel && *onlyLevel != levelIndex) {
            continue;
        }
        for (int screenIndex = 0;; ++screenIndex) {
            const std::filesystem::path screenPath =
                screenFilePath(levelDirectory, screenIndex);
            if (!std::filesystem::is_regular_file(screenPath)) {
                break;
            }
            if (onlyScreen && *onlyScreen != screenIndex) {
                continue;
            }
            const std::string name = "level" + std::to_string(levelIndex) +
                "/screen" + std::to_string(screenIndex);
            const Level::Definition definition =
                Level::loadDefinitionFromFile(screenPath);
            const std::uint64_t digest = solution::levelDigest(definition);
            if (!overwrite && existing.contains(digest)) {
                std::cout << name << ": already solved by "
                          << existing.at(digest).filename().string() << "\n";
                continue;
            }
            const Level level =
                Level::loadFromDefinition(definition, screenPath.string());
            const auto started = std::chrono::steady_clock::now();
            const SearchResult found = search(level, maxStates, bestFirst);
            const double seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started).count();
            if (!found.inputs) {
                std::cout << name << ": no solution ("
                          << (found.exhausted ? "every reachable state tried"
                                              : "state limit reached")
                          << ", " << found.statesVisited << " states, "
                          << seconds << " s)\n"
                          << std::flush;
                ++failures;
                continue;
            }
            const solution::Recording recording = solution::record(
                level,
                definition,
                *found.inputs,
                name);
            if (!recording.solved) {
                std::cout << name << ": search result did not replay: "
                          << recording.error << "\n";
                ++failures;
                continue;
            }
            const std::filesystem::path output = solutionsDir /
                solution::fileNameFor({ levelIndex, screenIndex });
            std::ofstream(output, std::ios::binary | std::ios::trunc)
                << solution::serialize(recording.solution);
            std::cout << name << ": " << found.inputs->size() << " steps ("
                      << found.statesVisited << " states, " << seconds
                      << " s) -> " << output.filename().string() << "\n";
        }
    }
    return failures == 0 ? 0 : 1;
}
