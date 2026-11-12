#include "engine/SolutionStore.hpp"

#include "engine/AnimationCatalog.hpp"
#include "engine/AtomicFile.hpp"
#include "engine/LevelCatalog.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace sokoban::solution {

namespace {

struct Stored {
    std::filesystem::path path;
    Solution solution;
};

struct Screen {
    LevelLocation location;
    Level level;
};

std::string locationName(LevelLocation location)
{
    return "level" + std::to_string(location.level) + "/screen" +
        std::to_string(location.screen);
}

// Current screens by content digest. Unreadable screens are skipped; the
// content pipeline reports those.
std::map<std::uint64_t, Screen> currentScreens(
    const std::filesystem::path& levelsRoot)
{
    std::map<std::uint64_t, Screen> screens;
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
                const Level::Definition definition = Level::loadDefinitionFromFile(path);
                const LevelLocation location { .level = levelIndex, .screen = screenIndex };
                screens.emplace(levelDigest(definition), Screen {
                    .location = location,
                    .level = Level::loadFromDefinition(definition, locationName(location)),
                });
            } catch (const std::exception&) {
                continue;
            }
        }
    }
    return screens;
}

std::string readBytes(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
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
            !std::filesystem::is_regular_file(entry.symlink_status(error))) {
            continue;
        }
        try {
            recordings.push_back({ entry.path(), parse(readBytes(entry.path())) });
        } catch (const std::exception&) {
            continue;
        }
    }
    std::ranges::sort(recordings, {}, &Stored::path);
    return recordings;
}

const Stored* bestRecording(
    const std::vector<Stored>& stored, std::uint64_t digest, const Level& level,
    const std::filesystem::path& preferredPath, const AnimationCatalog* animations)
{
    const Stored* best = nullptr;
    for (const Stored& entry : stored) {
        if (entry.solution.levelDigest != digest) {
            continue;
        }
        const std::size_t steps = entry.solution.stepCount();
        if ((best == nullptr || steps < best->solution.stepCount() ||
                (steps == best->solution.stepCount() &&
                    entry.path == preferredPath)) &&
            replay(level, entry.solution, animations).passed) {
            best = &entry;
        }
    }
    return best;
}

struct Placed {
    Solution solution;
    // Where the content came from; empty for the new recording.
    std::filesystem::path source;
};

struct FileChange {
    std::filesystem::path destination;
    // Null when removing an obsolete recording.
    const Solution* replacement = nullptr;
    std::filesystem::path staged;
    std::filesystem::path backup;
    bool attempted = false;
};

std::filesystem::path createStagingDirectory(const std::filesystem::path& draftsDir)
{
    std::filesystem::create_directories(draftsDir);
    if (!std::filesystem::is_directory(std::filesystem::symlink_status(draftsDir))) {
        throw std::runtime_error("draft recordings path is not a directory " + draftsDir.string());
    }
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto path = draftsDir / (".txn-" + std::to_string(attempt));
        if (std::filesystem::create_directory(path)) {
            return path;
        }
    }
    throw std::runtime_error("cannot allocate a solution staging directory in " + draftsDir.string());
}

void publishChanges(
    const std::filesystem::path& solutionsDir, const std::filesystem::path& draftsDir,
    std::vector<FileChange>& files)
{
    if (files.empty()) {
        return;
    }
    const auto transaction = createStagingDirectory(draftsDir);
    const auto cleanup = [&] {
        std::error_code ignored;
        std::filesystem::remove_all(transaction, ignored);
    };
    try {
        // Synchronize the new files and exact originals before touching the
        // store. Backups use the same relative paths as the live recordings.
        for (FileChange& change : files) {
            const auto relative = change.destination.lexically_relative(solutionsDir);
            const auto status = std::filesystem::symlink_status(change.destination);
            if (std::filesystem::exists(status)) {
                if (!std::filesystem::is_regular_file(status)) {
                    throw std::runtime_error("recording path is not a regular file " +
                        change.destination.string());
                }
                change.backup = transaction / "old" / relative;
                std::filesystem::create_directories(change.backup.parent_path());
                atomicFile::write(change.backup, readBytes(change.destination));
            }
            if (change.replacement != nullptr) {
                change.staged = transaction / "new" / relative;
                std::filesystem::create_directories(change.staged.parent_path());
                atomicFile::write(change.staged, serialize(*change.replacement));
            }
        }
        for (FileChange& change : files) {
            // Atomic replacement can report a final synchronization failure
            // after the new file has been installed.
            change.attempted = true;
            if (change.replacement != nullptr) {
                atomicFile::replace(change.destination, change.staged);
            } else {
                std::filesystem::remove(change.destination);
            }
        }
    } catch (const std::exception& failure) {
        std::string rollbackErrors;
        for (const auto& change : files | std::views::reverse) {
            if (!change.attempted) {
                continue;
            }
            try {
                // An install which left its source in place or a removal
                // which left its target alone has nothing to undo.
                const auto& remaining = change.replacement != nullptr
                    ? change.staged : change.destination;
                if (std::filesystem::exists(remaining)) {
                    continue;
                }
                if (change.backup.empty()) {
                    std::filesystem::remove(change.destination);
                } else {
                    auto restoration = change.backup;
                    restoration += ".r";
                    std::filesystem::copy_file(change.backup, restoration);
                    atomicFile::replace(change.destination, restoration);
                }
            } catch (const std::exception& rollbackFailure) {
                rollbackErrors += "\nCannot restore " + change.destination.string() +
                    " (" + rollbackFailure.what() + ')';
            }
        }
        if (!rollbackErrors.empty()) {
            throw std::runtime_error(std::string(failure.what()) +
                "\nOriginal backups are available in " + (transaction / "old").string() +
                rollbackErrors);
        }
        cleanup();
        throw;
    }
    cleanup();
}

} // namespace

std::vector<StoreChange> reconcileStore(
    const std::filesystem::path& levelsRoot,
    const std::filesystem::path& solutionsDir,
    const std::optional<SolveToStore>& solve)
{
    static std::mutex storeMutex;
    const std::scoped_lock lock(storeMutex);
    const std::filesystem::path draftsDir = solutionsDir / "drafts";
    const auto screens = currentScreens(levelsRoot);
    std::vector<Stored> stored = readRecordings(solutionsDir);
    for (Stored& draft : readRecordings(draftsDir)) {
        stored.push_back(std::move(draft));
    }
    const bool needsAnimations = (solve && !solve->frames.empty()) || std::ranges::any_of(stored, [](const Stored& entry) {
        return entry.solution.timed.has_value();
    });
    const auto animations = needsAnimations
        ? loadReplayAnimationCatalog(levelsRoot.parent_path() / "assets") : std::nullopt;

    std::vector<StoreChange> changes;
    std::optional<Solution> candidate;
    std::optional<Level> draftLevel;
    if (solve) {
        const std::uint64_t digest = levelDigest(solve->definition);
        const auto screen = screens.find(digest);
        if (screen == screens.end()) {
            draftLevel.emplace(Level::loadFromDefinition(solve->definition, solve->name));
        }
        const Level& level = draftLevel ? *draftLevel : screen->second.level;
        const std::string recordedFor = draftLevel
            ? solve->name : locationName(screen->second.location);
        Recording recording = recordTimed(level, solve->definition, solve->frames,
            recordedFor, animations ? &*animations : nullptr, solve->playingDraft);
        if (recording.solved) {
            candidate = std::move(recording.solution);
        } else {
            changes.push_back({
                .kind = StoreChange::Kind::NotRecorded,
                .message = "Timed replay failed (" + recording.error + ").",
            });
        }
    }

    const auto draftPath = [&](std::uint64_t digest) {
        return draftsDir / (digestText(digest) + ".solution");
    };
    std::map<std::filesystem::path, Placed> layout;
    std::set<std::uint64_t> verifiedDigests;

    // Ties keep the shortest verified recording already in place.
    const auto placeVerified = [&](std::uint64_t digest, const Stored* best,
                                   const std::filesystem::path& target,
                                   std::string_view recordedFor) {
        const bool hasCandidate = candidate && candidate->levelDigest == digest;
        if (best == nullptr && !hasCandidate) {
            return;
        }
        verifiedDigests.insert(digest);
        if (hasCandidate && (best == nullptr ||
                candidate->stepCount() < best->solution.stepCount())) {
            layout[target] = { .solution = *candidate, .source = {} };
            return;
        }

        Placed placed { .solution = best->solution, .source = best->path };
        if (best->path != target && !recordedFor.empty()) {
            placed.solution.recordedFor = recordedFor;
        }
        layout[target] = std::move(placed);
        if (hasCandidate) {
            changes.push_back({
                .kind = StoreChange::Kind::KeptExisting,
                .file = target,
                .steps = best->solution.stepCount(),
            });
        }
    };
    const AnimationCatalog* replayAnimations = animations ? &*animations : nullptr;
    for (const auto& [digest, screen] : screens) {
        const auto target = solutionsDir / fileNameFor(screen.location);
        const Stored* best = bestRecording(
            stored, digest, screen.level, target, replayAnimations);
        placeVerified(digest, best, target, locationName(screen.location));
    }
    if (candidate && draftLevel) {
        const auto digest = candidate->levelDigest;
        const auto preferred = draftPath(digest);
        const Stored* best = bestRecording(
            stored, digest, *draftLevel, preferred, replayAnimations);
        const auto& target = best && !layout.contains(best->path) ? best->path : preferred;
        placeVerified(digest, best, target, {});
    }

    // Keep recordings without a verified replacement, including those for
    // unavailable layouts. If displaced, park them without replacing an
    // existing draft whose content cannot be verified here.
    std::vector<const Stored*> displaced;
    for (const Stored& entry : stored) {
        if (verifiedDigests.contains(entry.solution.levelDigest)) {
            continue;
        }
        if (layout.contains(entry.path)) {
            displaced.push_back(&entry);
        } else {
            layout[entry.path] = { .solution = entry.solution, .source = entry.path };
        }
    }
    for (const Stored* entry : displaced) {
        const auto digest = entry->solution.levelDigest;
        auto path = draftPath(digest);
        for (std::size_t suffix = 1; layout.contains(path) ||
             std::filesystem::exists(std::filesystem::symlink_status(path)); ++suffix) {
            path = draftsDir / (digestText(digest) + '-' + std::to_string(suffix) + ".solution");
        }
        layout[path] = { .solution = entry->solution, .source = entry->path };
    }

    std::vector<FileChange> files;
    for (const auto& [path, placed] : layout) {
        if (placed.source == path) {
            const auto original = std::ranges::find(stored, path, &Stored::path);
            if (original != stored.end() &&
                original->solution == placed.solution) {
                continue;
            }
        }
        files.push_back({ .destination = path, .replacement = &placed.solution });
        changes.push_back({
            .kind = placed.source.empty() ? StoreChange::Kind::Saved
                                          : StoreChange::Kind::Moved,
            .file = path,
            .steps = placed.solution.stepCount(),
            .message = placed.source.empty()
                ? std::string {}
                : "from " + placed.source.filename().string(),
        });
    }
    for (const Stored& entry : stored) {
        if (!layout.contains(entry.path)) {
            files.push_back({ .destination = entry.path });
        }
    }
    publishChanges(solutionsDir, draftsDir, files);
    return changes;
}

} // namespace sokoban::solution
