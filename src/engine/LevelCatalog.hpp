#pragma once

#include <span>

namespace sokoban {

struct LevelLocation {
    int level = 0;
    int screen = 0;

    bool operator==(const LevelLocation&) const = default;
};

[[nodiscard]] bool levelLocationExists(
    std::span<const int> screenCounts,
    LevelLocation location) noexcept;

[[nodiscard]] LevelLocation resolveSavedLevelLocation(
    std::span<const int> screenCounts,
    LevelLocation savedLocation) noexcept;

} // namespace sokoban
