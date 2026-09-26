#include "engine/SolutionStore.hpp"

#include "engine/AtomicFile.hpp"
#include "engine/LevelCatalog.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>
#include <utility>

namespace sokoban::solution {

namespace {

struct Stored {
    std::filesystem::path path;
    Solution solution;
};

std::string locationName(LevelLocation location)
{
    return "level" + std::to_string(location.level) + "/screen" +
        std::to_string(location.screen);
}

// Current screens by content digest. Unreadable screens are skipped; the
// content pipeline reports those.
std::map<std::uint64_t, LevelLocation> currentScreens(
    const std::filesystem::path& levelsRoot)
{
    std::map<std::uint64_t, LevelLocation> screens;
    for (int levelIndex = 0;; ++levelIndex) {
        const std::filesystem::path directory =
            levelDirectoryPath(levelsRoot, levelIndex);
        if (!std::filesystem::is_directory(directory)) {
            break;
        }
        for (int screenIndex = 0;; ++screenIndex) {
            const std::filesystem::path path =
                screenFilePath(directory, screenIndex);
            if (!std::filesystem::is_regular_file(path)) {
                break;
            }
            try {
                screens.emplace(
                    levelDigest(Level::loadDefinitionFromFile(path)),
                    LevelLocation { .level = levelIndex, .screen = screenIndex });
            } catch (const std::exception&) {
                continue;
            }
        }
    }
    return screens;
}

// Parsable recordings directly inside `directory`. Files that do not parse
// are left alone.
std::vector<Stored> readRecordings(const std::filesystem::path& directory)
{
    std::vector<Stored> recordings;
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        return recordings;
    }
    for (const auto& entry :
         std::filesystem::directory_iterator(directory, error)) {
        if (entry.path().extension() != ".solution" ||
            !entry.is_regular_file(error)) {
            continue;
        }
        std::ifstream stream(entry.path(), std::ios::binary);
        std::stringstream text;
        text << stream.rdbuf();
        try {
            recordings.push_back({ entry.path(), parse(text.str()) });
        } catch (const std::exception&) {
            continue;
        }
    }
    std::ranges::sort(recordings, {}, &Stored::path);
    return recordings;
}

struct Placed {
    Solution solution;
    // Where the content came from; empty for the new recording.
    std::filesystem::path source;
};

} // namespace

std::vector<StoreChange> reconcileStore(
    const std::filesystem::path& levelsRoot,
    const std::filesystem::path& solutionsDir,
    const std::optional<SolveToStore>& solve)
{
    const std::filesystem::path draftsDir = solutionsDir / "drafts";
    const std::map<std::uint64_t, LevelLocation> screens =
        currentScreens(levelsRoot);
    std::vector<Stored> stored = readRecordings(solutionsDir);
    for (Stored& draft : readRecordings(draftsDir)) {
        stored.push_back(std::move(draft));
    }

    std::vector<StoreChange> changes;
    std::optional<Solution> candidate;
    if (solve) {
        const std::uint64_t digest = levelDigest(solve->definition);
        const auto screen = screens.find(digest);
        const Level level =
            Level::loadFromDefinition(solve->definition, solve->name);
        Recording recording = record(
            level,
            solve->definition,
            solve->inputs,
            screen != screens.end() ? locationName(screen->second)
                                    : solve->name);
        if (recording.solved) {
            candidate = std::move(recording.solution);
        } else {
            changes.push_back({
                .kind = StoreChange::Kind::NotRecorded,
                .message = recording.error,
            });
        }
    }

    const auto draftPath = [&](std::uint64_t digest) {
        return draftsDir / (digestText(digest) + ".solution");
    };
    std::map<std::filesystem::path, Placed> layout;

    // One file per current screen that has any recording: the shortest one.
    // Ties keep what is already in place, so the store does not churn.
    for (const auto& [digest, location] : screens) {
        const std::filesystem::path target = solutionsDir / fileNameFor(location);
        const Stored* best = nullptr;
        for (const Stored& entry : stored) {
            if (entry.solution.levelDigest != digest) {
                continue;
            }
            if (best == nullptr ||
                entry.solution.steps.size() < best->solution.steps.size() ||
                (entry.solution.steps.size() == best->solution.steps.size() &&
                    entry.path == target)) {
                best = &entry;
            }
        }
        const bool candidateWins = candidate &&
            candidate->levelDigest == digest &&
            (best == nullptr ||
                candidate->steps.size() < best->solution.steps.size());
        if (candidateWins) {
            layout[target] = { .solution = *candidate, .source = {} };
        } else if (best != nullptr) {
            Placed placed { .solution = best->solution, .source = best->path };
            if (best->path != target) {
                placed.solution.recordedFor = locationName(location);
            }
            layout[target] = std::move(placed);
            if (candidate && candidate->levelDigest == digest) {
                changes.push_back({
                    .kind = StoreChange::Kind::KeptExisting,
                    .file = target,
                    .steps = best->solution.steps.size(),
                });
            }
        }
    }

    // Recordings of content that is not on disk stay where they are, unless
    // a current screen's recording now needs their file name.
    const std::map<std::filesystem::path, Placed> screenFiles = layout;
    for (const Stored& entry : stored) {
        if (screens.contains(entry.solution.levelDigest)) {
            continue;
        }
        std::filesystem::path path = entry.path;
        if (screenFiles.contains(path)) {
            path = draftPath(entry.solution.levelDigest);
        }
        const auto existing = layout.find(path);
        if (existing != layout.end() &&
            existing->second.solution.steps.size() <=
                entry.solution.steps.size()) {
            continue;
        }
        layout[path] = { .solution = entry.solution, .source = entry.path };
    }

    if (candidate && !screens.contains(candidate->levelDigest)) {
        std::filesystem::path path = draftPath(candidate->levelDigest);
        for (const auto& [placedPath, placed] : layout) {
            if (placed.solution.levelDigest == candidate->levelDigest) {
                path = placedPath;
                break;
            }
        }
        const auto existing = layout.find(path);
        if (existing != layout.end() &&
            existing->second.solution.steps.size() <=
                candidate->steps.size()) {
            changes.push_back({
                .kind = StoreChange::Kind::KeptExisting,
                .file = path,
                .steps = existing->second.solution.steps.size(),
            });
        } else {
            layout[path] = { .solution = *candidate, .source = {} };
        }
    }

    // Write what changed, then remove recordings that were superseded or
    // moved. Everything was read into memory above, so order is safe.
    for (const auto& [path, placed] : layout) {
        if (placed.source == path) {
            const auto original = std::ranges::find(stored, path, &Stored::path);
            if (original != stored.end() &&
                original->solution == placed.solution) {
                continue;
            }
        }
        std::filesystem::create_directories(path.parent_path());
        atomicFile::write(path, serialize(placed.solution));
        changes.push_back({
            .kind = placed.source.empty() ? StoreChange::Kind::Saved
                                          : StoreChange::Kind::Moved,
            .file = path,
            .steps = placed.solution.steps.size(),
            .message = placed.source.empty()
                ? std::string {}
                : "from " + placed.source.filename().string(),
        });
    }
    for (const Stored& entry : stored) {
        if (!layout.contains(entry.path)) {
            std::filesystem::remove(entry.path);
        }
    }
    return changes;
}

} // namespace sokoban::solution
