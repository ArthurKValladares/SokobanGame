#include "engine/FrameArena.hpp"

#include "engine/Log.hpp"

namespace sokoban {

FrameArena::FrameArena(const char* name, std::size_t capacityBytes)
    : name_(name)
    , storage_(std::make_unique<std::byte[]>(capacityBytes))
    , head_(storage_.get())
    , end_(storage_.get() + capacityBytes)
    , capacityBytes_(capacityBytes)
{
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
                << bytes << " bytes with " << (available - padding)
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
