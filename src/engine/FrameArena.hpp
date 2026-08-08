#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace sokoban {

// Bytes an arena needs to hold `count` objects of type T, including the worst
// case padding spent aligning the head in front of them.
template <class T>
[[nodiscard]] constexpr std::size_t arenaBytesFor(std::size_t count) noexcept
{
    return count * sizeof(T) + alignof(T) - 1;
}

// A bump allocator for data whose lifetime ends at the next frame boundary.
//
// The whole design is a start, a head and an end. allocate() rounds the head
// up to the requested alignment, returns it, and advances it past the object;
// reset() puts the head back at the start. There is no free list, no chunk
// chain and no upstream allocator, because individual objects are never
// released - the frame boundary releases all of them at once.
//
// The arena hands out raw storage and never runs a destructor, so everything
// placed in it must be trivially destructible. allocateUninitialized()
// enforces that at compile time.
//
// Not thread safe, deliberately: the head is one unsynchronized pointer. An
// arena belongs to one thread, and work spread across the task system needs
// one arena per worker rather than one shared arena.
class FrameArena {
public:
    // `name` appears in the exhaustion warning, so it should say which arena
    // this is ("UI", "scene", ...). The storage is a single heap allocation
    // made at startup: a frame-sized arena does not belong on a Windows
    // thread's 1 MB stack, and reset() never allocates.
    FrameArena(const char* name, std::size_t capacityBytes);

    FrameArena(const FrameArena&) = delete;
    FrameArena& operator=(const FrameArena&) = delete;

    // Returns nullptr when the request does not fit, having logged once.
    //
    // There is deliberately no heap fallback. An arena that quietly reaches
    // past itself still pays the per-frame allocation it exists to remove,
    // and does it invisibly; a null return makes the caller decide, and the
    // caller knows what it can drop.
    [[nodiscard]] void* allocate(
        std::size_t bytes,
        std::size_t alignment) noexcept;

    // Raw storage for `count` objects. Nothing is constructed - the arena
    // does not touch the bytes it hands out - so the caller places objects
    // into them.
    template <class T>
    [[nodiscard]] T* allocateUninitialized(std::size_t count) noexcept
    {
        static_assert(
            std::is_trivially_destructible_v<T>,
            "Arena storage is released by moving a pointer, so nothing placed "
            "in it may need a destructor to run.");
        if (count > SIZE_MAX / sizeof(T)) {
            return nullptr;
        }
        return static_cast<T*>(allocate(count * sizeof(T), alignof(T)));
    }

    // The frame boundary. Every pointer handed out before this call is dead.
    void reset() noexcept
    {
        head_ = storage_.get();
        exhausted_ = false;
    }

    [[nodiscard]] std::size_t bytesUsed() const noexcept
    {
        return static_cast<std::size_t>(head_ - storage_.get());
    }

    [[nodiscard]] std::size_t bytesRemaining() const noexcept
    {
        return static_cast<std::size_t>(end_ - head_);
    }

    [[nodiscard]] std::size_t capacityBytes() const noexcept
    {
        return capacityBytes_;
    }

    // The largest bytesUsed() any single frame has reached, which is the
    // number to size the arena from.
    [[nodiscard]] std::size_t highWaterBytes() const noexcept
    {
        return highWaterBytes_;
    }

    // True when this frame asked for more than was left.
    [[nodiscard]] bool exhausted() const noexcept { return exhausted_; }

    [[nodiscard]] const char* name() const noexcept { return name_; }

private:
    const char* name_;
    std::unique_ptr<std::byte[]> storage_;
    std::byte* head_ = nullptr;
    std::byte* end_ = nullptr;
    std::size_t capacityBytes_ = 0;
    std::size_t highWaterBytes_ = 0;
    bool exhausted_ = false;
    // One warning per arena. A frame that overruns usually overruns again on
    // the next one, and a per-frame warning would bury the rest of the log.
    bool reported_ = false;
};

} // namespace sokoban
