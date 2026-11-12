#include "engine/LevelCatalog.hpp"
#include "engine/PathText.hpp"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cstddef>
#include <fstream>
#include <map>
#include <stdexcept>
#include <system_error>

namespace sokoban {
namespace {

std::optional<int> numberedNameIndex(
    std::string_view value,
    std::string_view prefix,
    std::string_view suffix = {}) noexcept
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

} // namespace

std::optional<int> levelIndexFromDirectoryName(std::string_view name) noexcept
{
    return numberedNameIndex(name, "level");
}

std::optional<int> screenIndexFromFilename(std::string_view name) noexcept
{
    return numberedNameIndex(name, "screen", ".scr");
}

std::string levelDirectoryName(int levelIndex)
{
    return "level" + std::to_string(levelIndex);
}

std::string screenFilename(int screenIndex)
{
    return "screen" + std::to_string(screenIndex) + ".scr";
}

std::filesystem::path levelDirectoryPath(
    const std::filesystem::path& root,
    int levelIndex)
{
    return root / levelDirectoryName(levelIndex);
}

std::filesystem::path screenFilePath(
    const std::filesystem::path& levelDirectory,
    int screenIndex)
{
    return levelDirectory / screenFilename(screenIndex);
}

std::optional<LevelLocation> levelLocationFromScreenPath(
    const std::filesystem::path& screenPath)
{
    if (!screenPath.has_parent_path()) {
        return std::nullopt;
    }
    const std::optional<int> screen =
        screenIndexFromFilename(pathText(screenPath.filename()));
    const std::optional<int> level = levelIndexFromDirectoryName(
        pathText(screenPath.parent_path().filename()));
    if (!screen || !level) {
        return std::nullopt;
    }
    return LevelLocation { .level = *level, .screen = *screen };
}

LevelMetadata loadLevelMetadata(
    const std::filesystem::path& levelDirectory,
    std::size_t screenCount)
{
    LevelMetadata metadata;
    metadata.screenNames.resize(screenCount);

    const std::filesystem::path path =
        levelDirectory / levelMetadataFilename;
    if (!std::filesystem::exists(path)) {
        return metadata;
    }

    try {
        std::ifstream file(path);
        if (!file) {
            throw std::runtime_error("cannot read " + pathText(path));
        }
        const nlohmann::json root = nlohmann::json::parse(file);
        if (!root.is_object()) {
            throw std::runtime_error("root must be an object");
        }
        if (root.value("format", 0) != 1) {
            throw std::runtime_error("unsupported or missing format");
        }
        if (const auto name = root.find("name"); name != root.end()) {
            if (!name->is_string()) {
                throw std::runtime_error("name must be a string");
            }
            metadata.name = name->get<std::string>();
        }
        if (const auto screens = root.find("screens"); screens != root.end()) {
            if (!screens->is_array()) {
                throw std::runtime_error("screens must be an array");
            }
            if (screens->size() > screenCount) {
                throw std::runtime_error(
                    "screens has more names than the level has screens");
            }
            for (std::size_t index = 0; index < screens->size(); ++index) {
                if (!(*screens)[index].is_string()) {
                    throw std::runtime_error(
                        "screens[" + std::to_string(index) +
                        "] must be a string");
                }
                metadata.screenNames[index] =
                    (*screens)[index].get<std::string>();
            }
        }
        return metadata;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(
            "invalid level metadata " + pathText(path) + ": " +
            error.what());
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(
            "invalid level metadata " + pathText(path) + ": " +
            error.what());
    }
}

void writeLevelMetadata(
    const std::filesystem::path& levelDirectory,
    const LevelMetadata& metadata)
{
    std::filesystem::create_directories(levelDirectory);
    const std::filesystem::path path =
        levelDirectory / levelMetadataFilename;
    nlohmann::ordered_json root = {
        { "format", 1 },
        { "name", metadata.name },
        { "screens", metadata.screenNames },
    };
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        throw std::runtime_error("cannot write " + pathText(path));
    }
    file << root.dump(2) << '\n';
    file.close();
    if (!file) {
        throw std::runtime_error("cannot write " + pathText(path));
    }
}

std::vector<int> PuzzleCatalog::screenCounts() const
{
    std::vector<int> counts;
    counts.reserve(levels.size());
    for (const auto& level : levels) {
        counts.push_back(static_cast<int>(level.screens.size()));
    }
    return counts;
}

const std::filesystem::path* PuzzleCatalog::findScreen(LevelLocation location) const noexcept
{
    if (location.level < 0 || static_cast<std::size_t>(location.level) >= levels.size()) {
        return nullptr;
    }
    const auto& screens = levels[static_cast<std::size_t>(location.level)].screens;
    if (location.screen < 0 || static_cast<std::size_t>(location.screen) >= screens.size()) {
        return nullptr;
    }
    return &screens[static_cast<std::size_t>(location.screen)];
}

PuzzleCatalog loadPuzzleCatalog(
    const std::filesystem::path& root, PuzzleCatalogEntries entries)
{
    const bool strict = entries == PuzzleCatalogEntries::RejectUnrelated;
    std::map<int, std::filesystem::path> directories;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory()) continue;
        const auto& path = entry.path();
        const std::string name = pathText(path.filename());
        if (name == "Deleted" || (name == "overworld" &&
                std::filesystem::is_regular_file(path / "layout.json"))) {
            continue;
        }
        const auto index = levelIndexFromDirectoryName(name);
        if (!index) {
            if (strict) throw std::runtime_error("unexpected level directory: " + pathText(path));
            continue;
        }
        const auto canonical = levelDirectoryName(*index);
        if (name != canonical) {
            throw std::runtime_error("puzzle level directory must be named '" + canonical +
                "': " + pathText(path));
        }
        directories.emplace(*index, path);
    }

    PuzzleCatalog catalog;
    catalog.levels.reserve(directories.size());
    for (const auto& [index, directory] : directories) {
        if (static_cast<std::size_t>(index) != catalog.levels.size()) {
            throw std::runtime_error("level indices must be contiguous starting at level0: " +
                pathText(directory));
        }
        std::map<int, std::filesystem::path> paths;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            const auto& path = entry.path();
            if (!entry.is_regular_file()) {
                if (strict) throw std::runtime_error("unexpected entry in level directory: " + pathText(path));
                continue;
            }
            const std::string name = pathText(path.filename());
            if (name == levelMetadataFilename) continue;
            const auto screen = screenIndexFromFilename(name);
            if (!screen) {
                if (strict) throw std::runtime_error("unexpected level file: " + pathText(path));
                continue;
            }
            const auto canonical = screenFilename(*screen);
            if (name != canonical) {
                throw std::runtime_error("puzzle screen file must be named '" + canonical +
                    "': " + pathText(path));
            }
            paths.emplace(*screen, path);
        }
        if (paths.empty()) {
            throw std::runtime_error(pathText(directory) + " contains no playable screens");
        }
        auto& level = catalog.levels.emplace_back(PuzzleCatalog::LevelEntry { .directory = directory });
        level.screens.reserve(paths.size());
        for (const auto& [screen, path] : paths) {
            if (static_cast<std::size_t>(screen) != level.screens.size()) {
                throw std::runtime_error("screen indices must be contiguous starting at screen0: " +
                    pathText(path));
            }
            level.screens.push_back(path);
        }
        level.metadata = loadLevelMetadata(directory, level.screens.size());
    }
    return catalog;
}

bool levelLocationExists(
    std::span<const int> screenCounts,
    LevelLocation location) noexcept
{
    return location.level >= 0 &&
        location.level < static_cast<int>(screenCounts.size()) &&
        location.screen >= 0 &&
        location.screen < screenCounts[static_cast<std::size_t>(location.level)];
}

LevelLocation resolveSavedLevelLocation(
    std::span<const int> screenCounts,
    LevelLocation savedLocation) noexcept
{
    return levelLocationExists(screenCounts, savedLocation)
        ? savedLocation
        : LevelLocation {};
}

} // namespace sokoban
