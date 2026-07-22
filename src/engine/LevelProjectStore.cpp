#include "engine/LevelProjectStore.hpp"

#include "engine/Level.hpp"

#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace sokoban {
namespace {

struct IndexedPath {
    int index = 0;
    std::filesystem::path path;
};

std::optional<int> numberedName(
    std::string_view value,
    std::string_view prefix,
    std::string_view suffix = {})
{
    if (!value.starts_with(prefix) || !value.ends_with(suffix) ||
        value.size() <= prefix.size() + suffix.size()) {
        return std::nullopt;
    }

    const char* begin = value.data() + prefix.size();
    const char* end = value.data() + value.size() - suffix.size();
    int number = 0;
    const auto parsed = std::from_chars(begin, end, number);
    if (parsed.ec != std::errc {} || parsed.ptr != end || number < 0) {
        return std::nullopt;
    }
    return number;
}

std::filesystem::path workingPath(
    const std::filesystem::path& root,
    std::string_view suffix)
{
    return root.parent_path() /
        (root.filename().string() + std::string(suffix));
}

void removeTree(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::remove_all(path, error);
    if (error) {
        throw std::runtime_error(
            "cannot remove " + path.string() + ": " + error.message());
    }
}

void renamePath(
    const std::filesystem::path& source,
    const std::filesystem::path& destination)
{
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    if (error) {
        throw std::runtime_error(
            "cannot rename " + source.string() + " to " +
            destination.string() + ": " + error.message());
    }
}

void recoverWorkingTree(const std::filesystem::path& root)
{
    const std::filesystem::path backup = workingPath(root, ".editor-backup");
    const bool rootExists = std::filesystem::exists(root);
    const bool backupExists = std::filesystem::exists(backup);
    if (!rootExists && backupExists) {
        renamePath(backup, root);
    } else if (rootExists && backupExists) {
        removeTree(backup);
    }
    removeTree(workingPath(root, ".editor-stage"));
}

void copyDirectoryContents(
    const std::filesystem::path& source,
    const std::filesystem::path& destination)
{
    std::filesystem::create_directories(destination);
    if (!std::filesystem::exists(source)) {
        return;
    }
    if (!std::filesystem::is_directory(source)) {
        throw std::runtime_error(
            "level project root is not a directory: " + source.string());
    }

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(source)) {
        std::error_code error;
        std::filesystem::copy(
            entry.path(),
            destination / entry.path().filename(),
            std::filesystem::copy_options::recursive |
                std::filesystem::copy_options::copy_symlinks |
                std::filesystem::copy_options::overwrite_existing,
            error);
        if (error) {
            throw std::runtime_error(
                "cannot stage " + entry.path().string() + ": " +
                error.message());
        }
    }
}

std::vector<IndexedPath> indexedDirectories(
    const std::filesystem::path& root,
    std::string_view prefix)
{
    std::vector<IndexedPath> entries;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::optional<int> index =
            numberedName(entry.path().filename().string(), prefix);
        if (index) {
            entries.push_back({ *index, entry.path() });
        }
    }
    std::ranges::sort(entries, {}, &IndexedPath::index);
    return entries;
}

std::vector<IndexedPath> indexedScreens(const std::filesystem::path& levelRoot)
{
    std::vector<IndexedPath> screens;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(levelRoot)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::optional<int> index = numberedName(
            entry.path().filename().string(), "screen", ".scr");
        if (index) {
            screens.push_back({ *index, entry.path() });
        }
    }
    std::ranges::sort(screens, {}, &IndexedPath::index);
    return screens;
}

std::vector<IndexedPath> validateProject(const std::filesystem::path& root)
{
    const std::vector<IndexedPath> levels = indexedDirectories(root, "level");
    for (std::size_t levelIndex = 0; levelIndex < levels.size(); ++levelIndex) {
        if (levels[levelIndex].index != static_cast<int>(levelIndex)) {
            throw std::runtime_error(
                "level directories must be contiguous from level0");
        }

        const std::vector<IndexedPath> screens =
            indexedScreens(levels[levelIndex].path);
        if (screens.empty()) {
            throw std::runtime_error(
                levels[levelIndex].path.string() + " contains no screens");
        }
        for (std::size_t screenIndex = 0;
             screenIndex < screens.size();
             ++screenIndex) {
            if (screens[screenIndex].index != static_cast<int>(screenIndex)) {
                throw std::runtime_error(
                    "screen files must be contiguous from screen0 in " +
                    levels[levelIndex].path.string());
            }
            (void)Level::loadFromFile(screens[screenIndex].path);
        }
    }
    return levels;
}

void prepareRuntimeMirror(
    const std::vector<IndexedPath>& levels,
    const std::filesystem::path& runtimeStage)
{
    std::filesystem::create_directories(runtimeStage);
    for (const IndexedPath& level : levels) {
        std::error_code error;
        std::filesystem::copy(
            level.path,
            runtimeStage / level.path.filename(),
            std::filesystem::copy_options::recursive |
                std::filesystem::copy_options::copy_symlinks |
                std::filesystem::copy_options::overwrite_existing,
            error);
        if (error) {
            throw std::runtime_error(
                "cannot prepare runtime level mirror: " + error.message());
        }
    }
}

std::string restoreBackup(
    const std::filesystem::path& root,
    const std::filesystem::path& backup,
    bool hadOriginal) noexcept
{
    std::error_code error;
    std::filesystem::remove_all(root, error);
    if (error) {
        return "cannot remove incomplete tree " + root.string() +
            ": " + error.message();
    }
    if (!hadOriginal) {
        return {};
    }

    if (!std::filesystem::exists(backup, error) || error) {
        return "original backup is unavailable at " + backup.string();
    }
    std::filesystem::rename(backup, root, error);
    if (error) {
        return "cannot restore " + backup.string() + " to " +
            root.string() + ": " + error.message();
    }
    return {};
}

} // namespace

LevelProjectStore::Result LevelProjectStore::transact(
    const std::filesystem::path& projectRoot,
    const std::optional<std::filesystem::path>& runtimeRoot,
    const Mutation& mutation)
{
    const std::filesystem::path projectStage =
        workingPath(projectRoot, ".editor-stage");
    const std::filesystem::path projectBackup =
        workingPath(projectRoot, ".editor-backup");
    std::filesystem::path runtimeStage;
    std::filesystem::path runtimeBackup;
    bool projectInstalled = false;
    bool projectBackedUp = false;
    bool runtimeInstalled = false;
    bool runtimeBackedUp = false;
    bool projectHadOriginal = false;
    bool runtimeHadOriginal = false;

    try {
        recoverWorkingTree(projectRoot);
        if (runtimeRoot) {
            recoverWorkingTree(*runtimeRoot);
            runtimeStage = workingPath(*runtimeRoot, ".editor-stage");
            runtimeBackup = workingPath(*runtimeRoot, ".editor-backup");
        }

        copyDirectoryContents(projectRoot, projectStage);
        mutation(projectStage);
        const std::vector<IndexedPath> levels = validateProject(projectStage);
        if (runtimeRoot) {
            prepareRuntimeMirror(levels, runtimeStage);
        }

        projectHadOriginal = std::filesystem::exists(projectRoot);
        if (projectHadOriginal) {
            renamePath(projectRoot, projectBackup);
            projectBackedUp = true;
        }
        renamePath(projectStage, projectRoot);
        projectInstalled = true;

        if (runtimeRoot) {
            runtimeHadOriginal = std::filesystem::exists(*runtimeRoot);
            if (runtimeHadOriginal) {
                renamePath(*runtimeRoot, runtimeBackup);
                runtimeBackedUp = true;
            }
            renamePath(runtimeStage, *runtimeRoot);
            runtimeInstalled = true;
        }

        std::error_code ignored;
        std::filesystem::remove_all(projectBackup, ignored);
        if (runtimeRoot) {
            ignored.clear();
            std::filesystem::remove_all(runtimeBackup, ignored);
        }
        return {
            .succeeded = true,
            .originalsPreserved = true,
        };
    } catch (const std::exception& error) {
        std::vector<std::string> rollbackFailures;
        if (runtimeRoot && (runtimeInstalled || runtimeBackedUp)) {
            std::string failure =
                restoreBackup(*runtimeRoot, runtimeBackup, runtimeHadOriginal);
            if (!failure.empty()) {
                rollbackFailures.push_back(std::move(failure));
            }
        }
        if (projectInstalled || projectBackedUp) {
            std::string failure =
                restoreBackup(projectRoot, projectBackup, projectHadOriginal);
            if (!failure.empty()) {
                rollbackFailures.push_back(std::move(failure));
            }
        }
        std::error_code ignored;
        std::filesystem::remove_all(projectStage, ignored);
        if (runtimeRoot) {
            ignored.clear();
            std::filesystem::remove_all(runtimeStage, ignored);
        }
        std::string message = error.what();
        for (const std::string& rollbackFailure : rollbackFailures) {
            message += "; rollback incomplete: " + rollbackFailure;
        }
        return {
            .succeeded = false,
            .originalsPreserved = rollbackFailures.empty(),
            .message = std::move(message),
        };
    }
}

} // namespace sokoban
