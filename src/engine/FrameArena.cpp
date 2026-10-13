#include "engine/FrameArena.hpp"

#include "engine/Log.hpp"

#include <algorithm>
#include <mutex>

namespace sokoban {
namespace {

struct FrameArenaMemoryLedger {
    std::mutex mutex;
    FrameArenaMemoryStats stats;
};

FrameArenaMemoryLedger& memoryLedger()
{
    // Initialized before the first arena finishes construction, so static
    // owners such as the CPU profiler destroy their arenas before the ledger.
    static FrameArenaMemoryLedger ledger;
    return ledger;
}

} // namespace

FrameArenaMemoryStats frameArenaMemoryStats() noexcept
{
    auto& ledger = memoryLedger();
    const std::scoped_lock lock(ledger.mutex);
    return ledger.stats;
}

FrameArena::FrameArena(const char* name, std::size_t capacityBytes)
    : name_(name)
    , storage_(std::make_unique<std::byte[]>(capacityBytes))
    , head_(storage_.get())
    , end_(storage_.get() + capacityBytes)
    , capacityBytes_(capacityBytes)
{
    auto& ledger = memoryLedger();
    const std::scoped_lock lock(ledger.mutex);
    ++ledger.stats.arenaCount;
    ledger.stats.reservedBytes += capacityBytes_;
    ledger.stats.peakReservedBytes = std::max(
        ledger.stats.peakReservedBytes, ledger.stats.reservedBytes);
}

FrameArena::~FrameArena()
{
    auto& ledger = memoryLedger();
    const std::scoped_lock lock(ledger.mutex);
    --ledger.stats.arenaCount;
    ledger.stats.reservedBytes -= capacityBytes_;
}

void* FrameArena::allocate(std::size_t bytes, std::size_t alignment) noexcept
{
    // Round the head up to the requested alignment. Every alignof() is a
    // power of two, so clearing the low bits of head + (alignment - 1) is the
    // next aligned address at or after the head.
    const auto current = reinterpret_cast<std::uintptr_t>(head_);
    const auto mask = static_cast<std::uintptr_t>(alignment) - 1;
    const std::size_t padding =
        static_cast<std::size_t>(((current + mask) & ~mask) - current);

    // Compared against what is left rather than by forming the end pointer,
    // which would run past the buffer before the check could reject it.
    const std::size_t available = bytesRemaining();
    if (padding > available || bytes > available - padding) {
        exhausted_ = true;
        if (!reported_) {
            reported_ = true;
            log::warning(log::Category::Rendering)
                << "Frame arena '" << name_ << "' is out of space: needed "
                << bytes << " bytes with " << (padding > available ? 0 : available - padding)
                << " usable of " << capacityBytes_
                << ". Raise its capacity.";
        }
        return nullptr;
    }

    std::byte* const result = head_ + padding;
    head_ = result + bytes;
    if (const std::size_t used = bytesUsed(); used > highWaterBytes_) {
        highWaterBytes_ = used;
    }
    return result;
}

} // namespace sokoban
