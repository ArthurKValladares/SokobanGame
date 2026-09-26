#include "engine/Profiler.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
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
#include <unordered_map>
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

void buildHotPaths(CpuProfileFrame& frame)
{
    struct Totals {
        uint64_t calls = 0;
        double inclusive = 0.0;
        double exclusive = 0.0;
        double maximum = 0.0;
    };
    std::map<std::string, Totals, std::less<>> totals;

    std::vector<std::size_t> order(frame.events.size());
    for (std::size_t index = 0; index < order.size(); ++index) {
        order[index] = index;
    }
    std::ranges::sort(order, {}, [&](std::size_t index) {
        const CpuProfileEvent& event = frame.events[index];
        return std::tuple {
            event.threadIndex, event.startMilliseconds, event.depth };
    });

    std::vector<double> childMilliseconds(frame.events.size(), 0.0);
    std::vector<std::size_t> stack;
    uint32_t activeThread = std::numeric_limits<uint32_t>::max();
    for (std::size_t eventIndex : order) {
        const CpuProfileEvent& event = frame.events[eventIndex];
        if (event.threadIndex != activeThread) {
            stack.clear();
            activeThread = event.threadIndex;
        }
        while (stack.size() > event.depth) {
            stack.pop_back();
        }
        if (!stack.empty()) {
            childMilliseconds[stack.back()] += event.durationMilliseconds;
        }
        stack.push_back(eventIndex);
    }

    for (std::size_t index = 0; index < frame.events.size(); ++index) {
        const CpuProfileEvent& event = frame.events[index];
        Totals& value = totals[event.name];
        ++value.calls;
        value.inclusive += event.durationMilliseconds;
        value.exclusive += std::max(
            0.0, event.durationMilliseconds - childMilliseconds[index]);
        value.maximum = std::max(value.maximum, event.durationMilliseconds);
    }
    frame.hotPaths.reserve(totals.size());
    for (const auto& [name, value] : totals) {
        frame.hotPaths.push_back({
            .name = name,
            .calls = value.calls,
            .inclusiveMilliseconds = value.inclusive,
            .exclusiveMilliseconds = value.exclusive,
            .maximumMilliseconds = value.maximum,
        });
    }
    std::ranges::sort(
        frame.hotPaths,
        std::greater {},
        &CpuHotPath::exclusiveMilliseconds);
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
    std::deque<CpuProfileFrame> captured;

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
    CpuProfileFrame frame {
        .frameIndex = frameIndex,
        .durationMilliseconds = nanosecondsToMilliseconds(frameEnd - frameStart),
        .traceStartMicroseconds = frameStart / 1'000,
    };

    {
        const std::scoped_lock registryLock(impl_->registryMutex);
        frame.threads.reserve(impl_->threadBuffers.size());
        for (const auto& ownedBuffer : impl_->threadBuffers) {
            CpuThreadBuffer& buffer = *ownedBuffer;
            frame.threads.push_back({ buffer.index, buffer.name });
            const std::scoped_lock bufferLock(buffer.mutex);
            frame.droppedEvents += buffer.droppedEvents;
            buffer.droppedEvents = 0;
            auto event = buffer.events.begin();
            while (event != buffer.events.end()) {
                // Long-running worker tasks can cross a frame boundary. They
                // become visible in the first snapshot taken after they
                // finish instead of being silently discarded.
                if (event->frameIndex <= frameIndex) {
                    frame.events.push_back({
                        .name = event->name ? event->name : "(unnamed)",
                        .threadIndex = buffer.index,
                        .depth = event->depth,
                        .startMilliseconds = nanosecondsToMilliseconds(
                            event->startNanoseconds - frameStart),
                        .durationMilliseconds = nanosecondsToMilliseconds(
                            event->endNanoseconds - event->startNanoseconds),
                    });
                }
                if (event->frameIndex <= frameIndex) {
                    event = buffer.events.erase(event);
                } else {
                    ++event;
                }
            }
        }
    }
    buildHotPaths(frame);
    const std::scoped_lock captureLock(impl_->captureMutex);
    impl_->captured.push_back(std::move(frame));
    while (impl_->captured.size() > capturedFrameCapacity) {
        impl_->captured.pop_front();
    }
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
    return impl_->captured.empty()
        ? CpuProfileFrame {}
        : impl_->captured.back();
}

std::vector<CpuProfileFrame> CpuProfiler::capturedFrames() const
{
    const std::scoped_lock lock(impl_->captureMutex);
    return { impl_->captured.begin(), impl_->captured.end() };
}

void CpuProfiler::clearCapture()
{
    const std::scoped_lock lock(impl_->captureMutex);
    impl_->captured.clear();
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
                       << frame.traceStartMicroseconds +
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
