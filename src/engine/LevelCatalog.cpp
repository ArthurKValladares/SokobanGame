#include "engine/LevelCatalog.hpp"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cstddef>
#include <fstream>
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
        screenIndexFromFilename(screenPath.filename().string());
    const std::optional<int> level = levelIndexFromDirectoryName(
        screenPath.parent_path().filename().string());
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
            throw std::runtime_error("cannot read " + path.string());
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
            "invalid level metadata " + path.string() + ": " +
            error.what());
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(
            "invalid level metadata " + path.string() + ": " +
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
        throw std::runtime_error("cannot write " + path.string());
    }
    file << root.dump(2) << '\n';
    file.close();
    if (!file) {
        throw std::runtime_error("cannot write " + path.string());
    }
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
