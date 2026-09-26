#pragma once

#include <cstdint>

namespace sokoban {

struct ProcessMemoryStatistics {
    bool available = false;
    uint64_t residentBytes = 0;
    uint64_t peakResidentBytes = 0;
    uint64_t privateBytes = 0;
};

// Read-only process counters sampled by the profiler UI. Unsupported
// platforms return an unavailable, zero-initialized result.
[[nodiscard]] ProcessMemoryStatistics processMemoryStatistics() noexcept;

} // namespace sokoban
