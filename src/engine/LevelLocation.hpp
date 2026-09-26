#pragma once

namespace sokoban {

// A puzzle screen address. Split from LevelCatalog.hpp so the widely included
// render types can name a location without pulling in <filesystem>.
struct LevelLocation {
    int level = 0;
    int screen = 0;

    bool operator==(const LevelLocation&) const = default;
};

} // namespace sokoban
