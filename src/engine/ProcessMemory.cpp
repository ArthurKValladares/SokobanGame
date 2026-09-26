#include "engine/ProcessMemory.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <unistd.h>

#include <cstdio>
#endif

namespace sokoban {

ProcessMemoryStatistics processMemoryStatistics() noexcept
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters {};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters))) {
        return {};
    }
    return {
        .available = true,
        .residentBytes = static_cast<uint64_t>(counters.WorkingSetSize),
        .peakResidentBytes = static_cast<uint64_t>(counters.PeakWorkingSetSize),
        .privateBytes = static_cast<uint64_t>(counters.PrivateUsage),
    };
#elif defined(__linux__)
    const long pageSize = sysconf(_SC_PAGESIZE);
    unsigned long virtualPages = 0;
    unsigned long residentPages = 0;
    FILE* statm = std::fopen("/proc/self/statm", "r");
    const bool read = statm &&
        std::fscanf(statm, "%lu %lu", &virtualPages, &residentPages) == 2;
    if (statm) {
        std::fclose(statm);
    }
    rusage usage {};
    const bool peakRead = getrusage(RUSAGE_SELF, &usage) == 0;
    if (!read || pageSize <= 0) {
        return {};
    }
    return {
        .available = true,
        .residentBytes = residentPages * static_cast<uint64_t>(pageSize),
        .peakResidentBytes = peakRead
            ? static_cast<uint64_t>(usage.ru_maxrss) * 1024U
            : 0,
        // Linux does not expose a Windows-like private-commit counter in
        // statm; virtual size is still useful and is clearly labelled by the
        // profiler panel on this platform.
        .privateBytes = virtualPages * static_cast<uint64_t>(pageSize),
    };
#else
    return {};
#endif
}

} // namespace sokoban
