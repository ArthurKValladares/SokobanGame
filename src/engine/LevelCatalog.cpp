#include "engine/LevelCatalog.hpp"

#include <cstddef>

namespace sokoban {

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
