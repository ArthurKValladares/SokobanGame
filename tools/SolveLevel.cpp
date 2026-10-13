// Finds solutions for puzzle screens through the reusable engine solver and
// writes them as recorded solutions.
//
//   sokoban_solve_level <levels-root> <solutions-dir> [--level L [--screen S]]
//                       [--max-states N] [--max-walking-cache N]
//                       [--best-first]
//                       [--progress-interval N] [--overwrite] [--refresh-existing]
//
// The solver plans ordinary transitions through the production rules and uses
// solution::Driver for automatic consequences. Every result is still recorded
// and replayed through Driver before it is written.
// Screens with a passing solution for their current content are skipped unless
// --overwrite is given. --refresh-existing first re-records the canonical
// file's inputs against the current screen, even when its old digest is stale;
// missing, malformed or unsuccessful recordings fall back to bounded search.

#include "engine/AtomicFile.hpp"
#include "engine/Level.hpp"
#include "engine/LevelCatalog.hpp"
#include "engine/Solution.hpp"
#include "engine/solver/Solver.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace sokoban;

solution::Solution readRecording(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot read " + path.string());
    }
    std::stringstream text;
    text << stream.rdbuf();
    if (stream.bad()) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return solution::parse(text.str());
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
        try {
            found.emplace(readRecording(entry.path()).levelDigest, entry.path());
        } catch (const std::exception&) {
            // A broken file is the replay test's to report.
            continue;
        }
    }
    return found;
}

std::size_t duplicateCount(const solver::Statistics& statistics)
{
    return statistics.canonicalDuplicates;
}

void appendStatistics(
    std::ostream& stream,
    const solver::Statistics& statistics,
    double seconds)
{
    stream << statistics.generatedStates << " generated, "
           << statistics.expandedPositions << " expanded, "
           << duplicateCount(statistics) << " duplicate ("
           << statistics.canonicalizationCacheHits << " cache, "
           << statistics.canonicalDuplicates -
                  statistics.canonicalizationCacheHits
           << " after flood), "
           << statistics.canonicalizationFloods << " canonical floods/"
           << statistics.canonicalizationWalkStates << " walk states, "
           << statistics.canonicalizationCachedStates << "/"
           << statistics.peakCanonicalizationCachedStates
           << " cached now/peak";
    if (statistics.canonicalizationCacheRotations != 0) {
        stream << ", " << statistics.canonicalizationCacheEvictions
               << " cache evictions/"
               << statistics.canonicalizationCacheRotations
               << " rotations";
    }
    if (statistics.deadPositionChecks != 0) {
        stream << ", " << statistics.deadPositionPrunes
               << " dead-position prunes/"
               << statistics.deadPositionChecks << " checks ["
               << statistics.deadPositionUnitCountPrunes << " unit, "
               << statistics.deadPositionStaticMatchingPrunes
               << " matching, "
               << statistics.deadPositionFrozenClusterPrunes
               << " frozen-cluster]";
        if (statistics.staticDeadPositionAnalysisEnabled) {
            stream << " (" << statistics.staticDeadCells
                   << " static dead cells";
            if (statistics.multiRockFreezeAnalysisEnabled) {
                stream << ", 2x2 freeze enabled";
            }
            stream << ")";
        }
    }
    stream << ", " << statistics.precomputedSuccessorsReused
           << " precomputed successors/"
           << statistics.drivenSuccessors << " driven";
    if (statistics.localSuccessorDuplicates != 0) {
        stream << ", " << statistics.localSuccessorDuplicates
               << " local successor duplicates";
    }
    stream << ", peak frontier " << statistics.peakFrontier;
    if (statistics.bestHeuristic) {
        stream << ", best estimate " << *statistics.bestHeuristic;
        stream << " (" << statistics.heuristicGraphCells << " cells/"
               << statistics.heuristicGraphEdges << " edges/"
               << statistics.heuristicMirrorEdges << " mirror)";
    }
    if (statistics.solutionSignificantMoves) {
        stream << ", " << *statistics.solutionSignificantMoves
               << " significant moves";
    }
    stream << ", " << seconds << " s";
}

} // namespace

int main(int argc, char** argv)
try
{
    if (argc < 3) {
        std::cerr << "usage: sokoban_solve_level <levels-root> "
                     "<solutions-dir> [--level L [--screen S]] "
                     "[--max-states N] [--max-walking-cache N] "
                     "[--best-first] "
                     "[--progress-interval N] [--overwrite] [--refresh-existing]\n";
        return 2;
    }
    const std::filesystem::path levelsRoot = argv[1];
    const std::filesystem::path solutionsDir = argv[2];
    std::optional<int> onlyLevel;
    std::optional<int> onlyScreen;
    std::size_t maxStates = 2'000'000;
    std::size_t maxCachedWalkingStates =
        solver::Options {}.maxCachedWalkingStates;
    std::size_t progressInterval = 0;
    bool overwrite = false;
    bool refreshExisting = false;
    solver::Strategy strategy = solver::Strategy::BreadthFirst;
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
            } else if (argument == "--max-walking-cache") {
                maxCachedWalkingStates =
                    static_cast<std::size_t>(std::stoull(value()));
            } else if (argument == "--progress-interval") {
                progressInterval =
                    static_cast<std::size_t>(std::stoull(value()));
            } else if (argument == "--overwrite") {
                overwrite = true;
            } else if (argument == "--refresh-existing") {
                refreshExisting = true;
            } else if (argument == "--best-first") {
                strategy = solver::Strategy::BestFirst;
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
    auto existing = existingSolutions(solutionsDir);
    const auto publish = [&](const std::filesystem::path& output,
                             const solution::Solution& recording) {
        atomicFile::write(output, solution::serialize(recording));
        std::erase_if(existing, [&](const auto& entry) {
            return entry.second == output;
        });
        existing[recording.levelDigest] = output;
    };
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
            const Level level =
                Level::loadFromDefinition(definition, screenPath.string());
            const std::filesystem::path output = solutionsDir /
                solution::fileNameFor({ levelIndex, screenIndex });
            if (refreshExisting) {
                std::optional<solution::Solution> refreshed;
                try {
                    const auto previous = readRecording(output);
                    std::vector<solution::Input> inputs;
                    inputs.reserve(previous.steps.size());
                    for (const auto& step : previous.steps) inputs.push_back(step.input);
                    auto recording = solution::record(level, definition, inputs, name);
                    if (!recording.solved) {
                        std::cout << name << ": existing inputs cannot be refreshed: "
                                  << recording.error << "; searching instead\n" << std::flush;
                    } else {
                        const auto verified = solution::replay(level, recording.solution);
                        if (verified.passed) {
                            refreshed = std::move(recording.solution);
                        } else {
                            std::cout << name << ": refreshed recording failed verification: "
                                      << verified.message << "; searching instead\n" << std::flush;
                        }
                    }
                } catch (const std::exception& error) {
                    std::cout << name << ": cannot refresh " << output.filename().string()
                              << ": " << error.what() << "; searching instead\n" << std::flush;
                }
                if (refreshed) {
                    publish(output, *refreshed);
                    std::cout << name << ": refreshed " << refreshed->steps.size()
                              << " existing input steps -> " << output.filename().string()
                              << "\n" << std::flush;
                    continue;
                }
            } else if (!overwrite && existing.contains(digest)) {
                const auto file = existing.at(digest);
                try {
                    const auto recording = readRecording(file);
                    const auto verified = recording.levelDigest == digest
                        ? solution::replay(level, recording)
                        : solution::ReplayReport { .message = "recording digest changed since inventory" };
                    if (verified.passed) {
                        std::cout << name << ": already solved by "
                                  << file.filename().string() << "\n";
                        continue;
                    }
                    std::cout << name << ": current recording " << file.filename().string()
                              << " failed verification: " << verified.message
                              << "; searching instead\n" << std::flush;
                } catch (const std::exception& error) {
                    std::cout << name << ": cannot replay " << file.filename().string()
                              << ": " << error.what() << "; searching instead\n" << std::flush;
                }
            }
            const auto started = std::chrono::steady_clock::now();
            solver::Options options {
                .maxStates = maxStates,
                .maxCachedWalkingStates = maxCachedWalkingStates,
                .strategy = strategy,
                .progressInterval = progressInterval,
            };
            if (progressInterval != 0) {
                options.progress = [&](const solver::Progress& progress) {
                    const double seconds = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - started).count();
                    std::clog << name << ": progress ";
                    appendStatistics(
                        std::clog, progress.statistics, seconds);
                    std::clog << ", current frontier "
                              << progress.frontierSize << "\n"
                              << std::flush;
                    return true;
                };
            }
            const solver::Result found = solver::solve(level, options);
            const double seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started).count();
            if (!found.solved()) {
                std::cout << name << ": no solution (";
                switch (found.status) {
                case solver::Status::Exhausted:
                    std::cout << "every reachable position tried";
                    break;
                case solver::Status::StateLimitReached:
                    std::cout << "state limit reached";
                    break;
                case solver::Status::Cancelled:
                    std::cout << "search cancelled";
                    break;
                case solver::Status::Solved:
                    break;
                }
                std::cout << ", ";
                appendStatistics(std::cout, found.statistics, seconds);
                std::cout << ")\n" << std::flush;
                ++failures;
                continue;
            }
            const solution::Recording recording = solution::record(
                level,
                definition,
                found.inputs,
                name);
            if (!recording.solved) {
                std::cout << name << ": search result did not replay: "
                          << recording.error << "\n";
                ++failures;
                continue;
            }
            const auto verified = solution::replay(level, recording.solution);
            if (!verified.passed) {
                std::cout << name << ": search recording failed verification: "
                          << verified.message << "\n";
                ++failures;
                continue;
            }
            publish(output, recording.solution);
            std::cout << name << ": " << found.inputs.size() << " steps (";
            appendStatistics(std::cout, found.statistics, seconds);
            std::cout << ") -> " << output.filename().string() << "\n";
        }
    }
    return failures == 0 ? 0 : 1;
}
catch (const std::exception& error)
{
    std::cerr << "sokoban_solve_level: " << error.what() << "\n";
    return 1;
}
