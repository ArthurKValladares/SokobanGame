#include "engine/Profiler.hpp"
#include "engine/ArenaArray.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <array>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <tuple>
#include <utility>

namespace sokoban {
namespace {

using Clock = std::chrono::steady_clock;

int64_t nowNanoseconds() noexcept
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now().time_since_epoch()).count();
}

double nanosecondsToMilliseconds(int64_t nanoseconds) noexcept
{
    return static_cast<double>(nanoseconds) / 1'000'000.0;
}

std::string jsonEscaped(const std::string& input)
{
    std::ostringstream output;
    for (const unsigned char value : input) {
        switch (value) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (value < 0x20) {
                output << "\\u" << std::hex << std::setw(4)
                       << std::setfill('0') << static_cast<unsigned>(value)
                       << std::dec;
            } else {
                output << static_cast<char>(value);
            }
            break;
        }
    }
    return output.str();
}

struct RawCpuEvent {
    const char* name = nullptr;
    uint64_t frameIndex = 0;
    int64_t startNanoseconds = 0;
    int64_t endNanoseconds = 0;
    uint16_t depth = 0;
};

struct CpuThreadBuffer {
    uint32_t index = 0;
    std::thread::id id;
    std::string name;
    std::mutex mutex;
    std::vector<RawCpuEvent> events;
    uint64_t droppedEvents = 0;
};

struct ThreadLocalProfileState {
    CpuThreadBuffer* buffer = nullptr;
    uint16_t depth = 0;
};

thread_local ThreadLocalProfileState localState;

void buildHotPaths(CpuProfileFrame& frame, FrameArena& arena)
{
    arena.reset();
    ArenaArray<std::size_t> order(arena, frame.events.size());
    double* childMilliseconds = arena.allocateUninitialized<double>(frame.events.size());
    std::size_t* stack = arena.allocateUninitialized<std::size_t>(frame.events.size());
    if (order.capacity() != frame.events.size() || !childMilliseconds || !stack) {
        frame.hotPaths.clear();
        return;
    }
    std::fill_n(childMilliseconds, frame.events.size(), 0.0);
    for (std::size_t index = 0; index < frame.events.size(); ++index) {
        order.push_back(index);
    }
    std::ranges::sort(order, {}, [&](std::size_t index) {
        const CpuProfileEvent& event = frame.events[index];
        return std::tuple { event.threadIndex, event.startMilliseconds, event.depth };
    });

    std::size_t stackSize = 0;
    uint32_t activeThread = std::numeric_limits<uint32_t>::max();
    for (std::size_t eventIndex : order) {
        const CpuProfileEvent& event = frame.events[eventIndex];
        if (event.threadIndex != activeThread) {
            stackSize = 0;
            activeThread = event.threadIndex;
        }
        stackSize = std::min(stackSize, static_cast<std::size_t>(event.depth));
        if (stackSize != 0) {
            childMilliseconds[stack[stackSize - 1]] += event.durationMilliseconds;
        }
        stack[stackSize++] = eventIndex;
    }

    // Group equal names using the same arena indices, without map nodes.
    std::size_t maximumNameBytes = 0;
    for (const auto& event : frame.events) {
        maximumNameBytes = std::max(maximumNameBytes, event.name.size());
    }
    std::ranges::sort(order, {}, [&](std::size_t index) -> std::string_view {
        return frame.events[index].name;
    });
    std::size_t hotPathCount = 0;
    for (std::size_t first = 0; first < order.size();) {
        const std::string& name = frame.events[order[first]].name;
        if (hotPathCount == frame.hotPaths.size()) {
            frame.hotPaths.emplace_back();
        }
        CpuHotPath& value = frame.hotPaths[hotPathCount++];
        // Sorting moves names between slots. Give each slot room for any name
        // in this frame so a change in timing/order does not force a new string.
        value.name.reserve(maximumNameBytes);
        value.name = name;
        value.calls = 0;
        value.inclusiveMilliseconds = 0.0;
        value.exclusiveMilliseconds = 0.0;
        value.maximumMilliseconds = 0.0;
        do {
            const std::size_t index = order[first++];
            const CpuProfileEvent& event = frame.events[index];
            ++value.calls;
            value.inclusiveMilliseconds += event.durationMilliseconds;
            value.exclusiveMilliseconds += std::max(
                0.0, event.durationMilliseconds - childMilliseconds[index]);
            value.maximumMilliseconds = std::max(
                value.maximumMilliseconds, event.durationMilliseconds);
        } while (first < order.size() && frame.events[order[first]].name == name);
    }
    frame.hotPaths.resize(hotPathCount);
    std::ranges::sort(frame.hotPaths, std::greater {}, &CpuHotPath::exclusiveMilliseconds);
}

} // namespace

struct CpuProfiler::Impl {
    std::atomic<bool> enabled { SOKOBAN_ENABLE_DEBUG_UI != 0 };
    std::atomic<bool> paused { false };
    std::atomic<uint64_t> activeFrame { 0 };
    std::atomic<int64_t> frameStartNanoseconds { 0 };
    mutable std::mutex registryMutex;
    std::vector<std::unique_ptr<CpuThreadBuffer>> threadBuffers;
    mutable std::mutex captureMutex;
    // History owns these buffers across frames. Overwriting a ring slot retains
    // its event/thread strings and vector capacity instead of freeing a deque node.
    std::array<CpuProfileFrame, capturedFrameCapacity> captured;
    std::size_t captureCount = 0;
    std::size_t captureHead = 0;
    FrameArena analysisArena { "CPU profiler analysis", 4 * 1024 * 1024 };

    CpuThreadBuffer* bufferForCurrentThread()
    {
        if (localState.buffer) {
            return localState.buffer;
        }
        const std::scoped_lock lock(registryMutex);
        const std::thread::id id = std::this_thread::get_id();
        for (const auto& buffer : threadBuffers) {
            if (buffer->id == id) {
                localState.buffer = buffer.get();
                return localState.buffer;
            }
        }
        auto buffer = std::make_unique<CpuThreadBuffer>();
        buffer->index = static_cast<uint32_t>(threadBuffers.size());
        buffer->id = id;
        buffer->name = buffer->index == 0
            ? "Main"
            : "Worker " + std::to_string(buffer->index);
        buffer->events.reserve(512);
        localState.buffer = buffer.get();
        threadBuffers.push_back(std::move(buffer));
        return localState.buffer;
    }
};

CpuProfiler& CpuProfiler::instance()
{
    static CpuProfiler profiler;
    return profiler;
}

CpuProfiler::CpuProfiler()
    : impl_(new Impl)
{
}

CpuProfiler::~CpuProfiler()
{
    delete impl_;
}

void CpuProfiler::setEnabled(bool enabled) noexcept
{
    impl_->enabled.store(enabled, std::memory_order_release);
    if (!enabled) {
        impl_->activeFrame.store(0, std::memory_order_release);
    }
}

bool CpuProfiler::enabled() const noexcept
{
    return impl_->enabled.load(std::memory_order_acquire);
}

void CpuProfiler::setPaused(bool paused) noexcept
{
    impl_->paused.store(paused, std::memory_order_release);
    if (paused) {
        impl_->activeFrame.store(0, std::memory_order_release);
    }
}

bool CpuProfiler::paused() const noexcept
{
    return impl_->paused.load(std::memory_order_acquire);
}

void CpuProfiler::beginFrame(uint64_t frameIndex)
{
    if (!enabled() || paused() || frameIndex == 0) {
        impl_->activeFrame.store(0, std::memory_order_release);
        return;
    }
    const int64_t start = nowNanoseconds();
    impl_->frameStartNanoseconds.store(start, std::memory_order_release);
    impl_->activeFrame.store(frameIndex, std::memory_order_release);
}

void CpuProfiler::endFrame()
{
    const uint64_t frameIndex =
        impl_->activeFrame.exchange(0, std::memory_order_acq_rel);
    if (frameIndex == 0) {
        return;
    }
    const int64_t frameStart =
        impl_->frameStartNanoseconds.load(std::memory_order_acquire);
    const int64_t frameEnd = nowNanoseconds();
    const std::scoped_lock captureLock(impl_->captureMutex);
    CpuProfileFrame& frame = impl_->captured[impl_->captureHead];
    frame.frameIndex = frameIndex;
    frame.durationMilliseconds = nanosecondsToMilliseconds(frameEnd - frameStart);
    frame.traceStartMicroseconds = frameStart / 1'000;
    frame.droppedEvents = 0;
    std::size_t eventCount = 0;

    {
        const std::scoped_lock registryLock(impl_->registryMutex);
        frame.threads.resize(impl_->threadBuffers.size());
        for (const auto& ownedBuffer : impl_->threadBuffers) {
            CpuThreadBuffer& buffer = *ownedBuffer;
            const std::scoped_lock bufferLock(buffer.mutex);
            frame.threads[buffer.index].index = buffer.index;
            frame.threads[buffer.index].name = buffer.name;
            frame.droppedEvents += buffer.droppedEvents;
            buffer.droppedEvents = 0;
            auto event = buffer.events.begin();
            while (event != buffer.events.end()) {
                // Long-running worker tasks can cross a frame boundary. They
                // become visible in the first snapshot taken after they
                // finish instead of being silently discarded.
                if (event->frameIndex <= frameIndex) {
                    if (eventCount == frame.events.size()) {
                        frame.events.emplace_back();
                    }
                    CpuProfileEvent& output = frame.events[eventCount++];
                    output.name = event->name ? event->name : "(unnamed)";
                    output.threadIndex = buffer.index;
                    output.depth = event->depth;
                    output.startMilliseconds = nanosecondsToMilliseconds(
                        event->startNanoseconds - frameStart);
                    output.durationMilliseconds = nanosecondsToMilliseconds(
                        event->endNanoseconds - event->startNanoseconds);
                }
                if (event->frameIndex <= frameIndex) {
                    event = buffer.events.erase(event);
                } else {
                    ++event;
                }
            }
        }
    }
    frame.events.resize(eventCount);
    buildHotPaths(frame, impl_->analysisArena);
    impl_->captureHead = (impl_->captureHead + 1) % capturedFrameCapacity;
    impl_->captureCount = std::min(impl_->captureCount + 1, capturedFrameCapacity);
}

void CpuProfiler::setCurrentThreadName(const char* name)
{
    CpuThreadBuffer* buffer = impl_->bufferForCurrentThread();
    const std::scoped_lock lock(buffer->mutex);
    buffer->name = name && *name ? name : "Unnamed";
}

CpuProfileFrame CpuProfiler::latestFrame() const
{
    const std::scoped_lock lock(impl_->captureMutex);
    return impl_->captureCount == 0
        ? CpuProfileFrame {}
        : impl_->captured[(impl_->captureHead + capturedFrameCapacity - 1) % capturedFrameCapacity];
}

std::vector<CpuProfileFrame> CpuProfiler::capturedFrames() const
{
    const std::scoped_lock lock(impl_->captureMutex);
    std::vector<CpuProfileFrame> frames;
    frames.reserve(impl_->captureCount);
    const std::size_t first =
        (impl_->captureHead + capturedFrameCapacity - impl_->captureCount) % capturedFrameCapacity;
    for (std::size_t index = 0; index < impl_->captureCount; ++index) {
        frames.push_back(impl_->captured[(first + index) % capturedFrameCapacity]);
    }
    return frames;
}

void CpuProfiler::clearCapture()
{
    const std::scoped_lock lock(impl_->captureMutex);
    impl_->captureCount = 0;
    impl_->captureHead = 0;
}

bool CpuProfiler::exportChromeTrace(
    const std::filesystem::path& path,
    std::string* error) const noexcept
{
    try {
        const std::vector<CpuProfileFrame> frames = capturedFrames();
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            if (error) {
                *error = "Could not open " + path.string();
            }
            return false;
        }
        output << "{\n\"displayTimeUnit\":\"ms\",\n\"traceEvents\":[\n";
        bool first = true;
        std::map<uint32_t, std::string> threadNames;
        for (const CpuProfileFrame& frame : frames) {
            for (const CpuProfileThread& thread : frame.threads) {
                threadNames[thread.index] = thread.name;
            }
        }
        for (const auto& [index, name] : threadNames) {
            if (!first) output << ",\n";
            first = false;
            output << "{\"name\":\"thread_name\",\"ph\":\"M\","
                   << "\"pid\":1,\"tid\":" << index
                   << ",\"args\":{\"name\":\""
                   << jsonEscaped(name) << "\"}}";
        }
        for (const CpuProfileFrame& frame : frames) {
            if (!first) output << ",\n";
            first = false;
            output << "{\"name\":\"Frame " << frame.frameIndex
                   << "\",\"cat\":\"frame\",\"ph\":\"X\","
                   << "\"pid\":1,\"tid\":0,\"ts\":"
                   << frame.traceStartMicroseconds << ",\"dur\":"
                   << frame.durationMilliseconds * 1000.0 << '}';
            for (const CpuProfileEvent& event : frame.events) {
                output << ",\n{\"name\":\"" << jsonEscaped(event.name)
                       << "\",\"cat\":\"cpu\",\"ph\":\"X\","
                       << "\"pid\":1,\"tid\":" << event.threadIndex
                       << ",\"ts\":"
                       << static_cast<double>(frame.traceStartMicroseconds) +
                              event.startMilliseconds * 1000.0
                       << ",\"dur\":"
                       << event.durationMilliseconds * 1000.0 << '}';
            }
        }
        output << "\n]}\n";
        if (!output) {
            if (error) {
                *error = "Failed while writing " + path.string();
            }
            return false;
        }
        if (error) {
            error->clear();
        }
        return true;
    } catch (const std::exception& exception) {
        if (error) {
            *error = exception.what();
        }
        return false;
    }
}

void* CpuProfiler::beginEvent(
    const char*,
    uint64_t& frameIndex,
    int64_t& startNanoseconds,
    uint16_t& depth) noexcept
{
    if (!enabled() || paused()) {
        return nullptr;
    }
    frameIndex = impl_->activeFrame.load(std::memory_order_acquire);
    if (frameIndex == 0) {
        return nullptr;
    }
    CpuThreadBuffer* buffer = impl_->bufferForCurrentThread();
    depth = localState.depth++;
    startNanoseconds = nowNanoseconds();
    return buffer;
}

void CpuProfiler::endEvent(
    void* opaqueBuffer,
    const char* name,
    uint64_t frameIndex,
    int64_t startNanoseconds,
    uint16_t depth) noexcept
{
    if (!opaqueBuffer) {
        return;
    }
    if (localState.depth != 0) {
        --localState.depth;
    }
    auto& buffer = *static_cast<CpuThreadBuffer*>(opaqueBuffer);
    const RawCpuEvent event {
        .name = name,
        .frameIndex = frameIndex,
        .startNanoseconds = startNanoseconds,
        .endNanoseconds = nowNanoseconds(),
        .depth = depth,
    };
    try {
        const std::scoped_lock lock(buffer.mutex);
        if (buffer.events.size() < eventsPerThreadCapacity) {
            buffer.events.push_back(event);
        } else {
            ++buffer.droppedEvents;
        }
    } catch (...) {
        const std::scoped_lock lock(buffer.mutex);
        ++buffer.droppedEvents;
    }
}

CpuProfileScope::CpuProfileScope(const char* name) noexcept
    : name_(name)
{
    buffer_ = CpuProfiler::instance().beginEvent(
        name_, frameIndex_, startNanoseconds_, depth_);
}

CpuProfileScope::~CpuProfileScope()
{
    CpuProfiler::instance().endEvent(
        buffer_, name_, frameIndex_, startNanoseconds_, depth_);
}

CpuProfileFrameScope::CpuProfileFrameScope(uint64_t frameIndex)
{
    CpuProfiler::instance().beginFrame(frameIndex);
}

CpuProfileFrameScope::~CpuProfileFrameScope()
{
    CpuProfiler::instance().endFrame();
}

} // namespace sokoban
