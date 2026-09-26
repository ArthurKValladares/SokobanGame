#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#ifndef SOKOBAN_ENABLE_DEBUG_UI
#error "SOKOBAN_ENABLE_DEBUG_UI must be defined by the build (see CMakeLists.txt)"
#endif

namespace sokoban {

struct CpuProfileEvent {
    std::string name;
    uint32_t threadIndex = 0;
    uint16_t depth = 0;
    double startMilliseconds = 0.0;
    double durationMilliseconds = 0.0;
};

struct CpuProfileThread {
    uint32_t index = 0;
    std::string name;
};

struct CpuHotPath {
    std::string name;
    uint64_t calls = 0;
    double inclusiveMilliseconds = 0.0;
    double exclusiveMilliseconds = 0.0;
    double maximumMilliseconds = 0.0;
};

struct CpuProfileFrame {
    uint64_t frameIndex = 0;
    double durationMilliseconds = 0.0;
    uint64_t droppedEvents = 0;
    int64_t traceStartMicroseconds = 0;
    std::vector<CpuProfileThread> threads;
    std::vector<CpuProfileEvent> events;
    std::vector<CpuHotPath> hotPaths;
};

// Thread-aware, bounded CPU timeline recorder. Scope completion is the only
// hot-path synchronization: each thread writes to its own small buffer, while
// endFrame drains those buffers after the application has joined its per-frame
// work. Captured history is bounded so leaving profiling enabled cannot grow
// memory without limit.
class CpuProfiler {
public:
    static constexpr std::size_t capturedFrameCapacity = 240;
    static constexpr std::size_t eventsPerThreadCapacity = 16'384;

    [[nodiscard]] static CpuProfiler& instance();

    void setEnabled(bool enabled) noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    void setPaused(bool paused) noexcept;
    [[nodiscard]] bool paused() const noexcept;

    void beginFrame(uint64_t frameIndex);
    void endFrame();
    void setCurrentThreadName(const char* name);

    [[nodiscard]] CpuProfileFrame latestFrame() const;
    [[nodiscard]] std::vector<CpuProfileFrame> capturedFrames() const;
    void clearCapture();
    // Writes Chrome/Perfetto trace-event JSON. Returns false and optionally
    // describes the filesystem failure instead of throwing through the UI.
    [[nodiscard]] bool exportChromeTrace(
        const std::filesystem::path& path,
        std::string* error = nullptr) const noexcept;

private:
    friend class CpuProfileScope;
    struct Impl;

    CpuProfiler();
    ~CpuProfiler();
    CpuProfiler(const CpuProfiler&) = delete;
    CpuProfiler& operator=(const CpuProfiler&) = delete;

    void* beginEvent(
        const char* name,
        uint64_t& frameIndex,
        int64_t& startNanoseconds,
        uint16_t& depth) noexcept;
    void endEvent(
        void* buffer,
        const char* name,
        uint64_t frameIndex,
        int64_t startNanoseconds,
        uint16_t depth) noexcept;

    Impl* impl_ = nullptr;
};

class CpuProfileScope {
public:
    explicit CpuProfileScope(const char* name) noexcept;
    ~CpuProfileScope();

    CpuProfileScope(const CpuProfileScope&) = delete;
    CpuProfileScope& operator=(const CpuProfileScope&) = delete;

private:
    const char* name_ = nullptr;
    void* buffer_ = nullptr;
    uint64_t frameIndex_ = 0;
    int64_t startNanoseconds_ = 0;
    uint16_t depth_ = 0;
};

class CpuProfileFrameScope {
public:
    explicit CpuProfileFrameScope(uint64_t frameIndex);
    ~CpuProfileFrameScope();

    CpuProfileFrameScope(const CpuProfileFrameScope&) = delete;
    CpuProfileFrameScope& operator=(const CpuProfileFrameScope&) = delete;
};

#define SOKOBAN_PROFILE_CONCAT_INNER(left, right) left##right
#define SOKOBAN_PROFILE_CONCAT(left, right) \
    SOKOBAN_PROFILE_CONCAT_INNER(left, right)
#if SOKOBAN_ENABLE_DEBUG_UI
#define SOKOBAN_PROFILE_SCOPE(name) \
    ::sokoban::CpuProfileScope SOKOBAN_PROFILE_CONCAT( \
        sokobanProfileScope_, __LINE__)(name)
#define SOKOBAN_PROFILE_FUNCTION() SOKOBAN_PROFILE_SCOPE(__func__)
#else
#define SOKOBAN_PROFILE_SCOPE(name) ((void)0)
#define SOKOBAN_PROFILE_FUNCTION() ((void)0)
#endif

} // namespace sokoban
