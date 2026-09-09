#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

struct LevelLocation {
    int level = 0;
    int screen = 0;

    bool operator==(const LevelLocation&) const = default;
};

// Optional author-facing labels stored beside a level's screen files in
// metadata.json. Empty labels deliberately mean "use the numbered fallback",
// which keeps every existing level project valid without a migration.
struct LevelMetadata {
    std::string name;
    std::vector<std::string> screenNames;

    bool operator==(const LevelMetadata&) const = default;
};

inline constexpr std::string_view levelMetadataFilename = "metadata.json";

// Puzzle projects use zero-based `level<N>/screen<M>.scr` names. Parsing
// accepts any non-negative decimal spelling; path construction emits the
// canonical unpadded spelling used by the runtime and editor transactions.
[[nodiscard]] std::optional<int> levelIndexFromDirectoryName(
    std::string_view name) noexcept;

[[nodiscard]] std::optional<int> screenIndexFromFilename(
    std::string_view name) noexcept;

[[nodiscard]] std::string levelDirectoryName(int levelIndex);

[[nodiscard]] std::string screenFilename(int screenIndex);

[[nodiscard]] std::filesystem::path levelDirectoryPath(
    const std::filesystem::path& root,
    int levelIndex);

[[nodiscard]] std::filesystem::path screenFilePath(
    const std::filesystem::path& levelDirectory,
    int screenIndex);

// Interprets only the final two path components. Containment in a source or
// runtime root remains the caller's responsibility.
[[nodiscard]] std::optional<LevelLocation> levelLocationFromScreenPath(
    const std::filesystem::path& screenPath);

[[nodiscard]] LevelMetadata loadLevelMetadata(
    const std::filesystem::path& levelDirectory,
    std::size_t screenCount);

void writeLevelMetadata(
    const std::filesystem::path& levelDirectory,
    const LevelMetadata& metadata);

[[nodiscard]] bool levelLocationExists(
    std::span<const int> screenCounts,
    LevelLocation location) noexcept;

[[nodiscard]] LevelLocation resolveSavedLevelLocation(
    std::span<const int> screenCounts,
    LevelLocation savedLocation) noexcept;

} // namespace sokoban
