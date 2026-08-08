#pragma once

#include <filesystem>
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
