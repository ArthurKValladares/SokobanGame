#pragma once

#include "engine/Log.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace sokoban {

class LogDebugUi {
public:
    LogDebugUi();

    void draw();

private:
    void refreshEntries();

    std::array<char, 256> search_ {};
    std::array<bool, 4> enabledLevels_ {};
    std::array<bool, log::categoryCount> enabledCategories_ {};
    std::vector<log::Entry> entries_;
    uint64_t observedRevision_ = 0;
    bool hasObservedRevision_ = false;
    bool paused_ = false;
    bool autoScroll_ = true;
    bool scrollToBottom_ = true;
};

} // namespace sokoban
