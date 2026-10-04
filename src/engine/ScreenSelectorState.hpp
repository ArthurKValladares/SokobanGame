#pragma once

#include <cstdint>

namespace sokoban {

// Progress state of a screen as exposed by its overworld selector. Levels are
// independent; within one level, screen 0 is initially playable and each
// later screen requires the immediately preceding screen to be solved.
enum class ScreenSelectorStatus : uint8_t {
    Unavailable,
    Playable,
    Solved,
};

struct ScreenSelectorViewState {
    ScreenSelectorStatus status = ScreenSelectorStatus::Unavailable;
    bool lastScreenInLevel = false;
};

} // namespace sokoban
