#pragma once

#include "engine/FrameArena.hpp"

#include <cstddef>
#include <new>
#include <type_traits>

namespace sokoban {

// A fixed-capacity array carved out of a FrameArena in a single bump.
//
// Deliberately not a growing container. Growth inside a bump allocator cannot
// reuse the buffer it outgrew, because freeing one object is not an operation
// the arena has: a vector reaching N elements strands every earlier buffer
// and costs two to three times N, which is the per-frame allocation the arena
// exists to remove. Taking the frame's budget in one bump makes the frame one
// allocation, and the count is knowable for everything drawn per frame.
//
// Pushing past the capacity drops the value and counts it. The owner reports
// droppedCount() at the frame boundary: a budget that is genuinely too small
// is a number to raise, not a reason to lose the frame or the session.
template <class T>
class ArenaArray {
    static_assert(
        std::is_trivially_destructible_v<T>,
        "Arena storage is released by moving a pointer, so nothing placed in "
        "it may need a destructor to run.");

public:
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;

    // Empty and permanently full, for members that outlive a frame.
    ArenaArray() = default;

    ArenaArray(FrameArena& arena, std::size_t capacity)
        : data_(arena.allocateUninitialized<T>(capacity))
        // An exhausted arena yields no storage; the array is then empty
        // rather than a null pointer waiting to be written through.
        , capacity_(data_ != nullptr ? capacity : 0)
    {
    }

    // False when the array was full and the value was dropped.
    bool push_back(const T& value) noexcept
    {
        if (size_ == capacity_) {
            ++dropped_;
            return false;
        }
        ::new (static_cast<void*>(data_ + size_)) T(value);
        ++size_;
        return true;
    }

    void clear() noexcept
    {
        size_ = 0;
        dropped_ = 0;
    }

    iterator erase(const_iterator first, const_iterator last) noexcept
    {
        const std::size_t firstIndex =
            static_cast<std::size_t>(first - data_);
        const std::size_t lastIndex =
            static_cast<std::size_t>(last - data_);
        const std::size_t removed = lastIndex - firstIndex;
        for (std::size_t index = firstIndex;
             index + removed < size_;
             ++index) {
            data_[index] = data_[index + removed];
        }
        size_ -= removed;
        return data_ + firstIndex;
    }

    iterator erase(const_iterator position) noexcept
    {
        return erase(position, position + 1);
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] bool full() const noexcept { return size_ == capacity_; }

    // Values this array had no room for since the last clear.
    [[nodiscard]] std::size_t droppedCount() const noexcept { return dropped_; }

    [[nodiscard]] T* data() noexcept { return data_; }
    [[nodiscard]] const T* data() const noexcept { return data_; }

    [[nodiscard]] T& operator[](std::size_t index) noexcept
    {
        return data_[index];
    }
    [[nodiscard]] const T& operator[](std::size_t index) const noexcept
    {
        return data_[index];
    }

    [[nodiscard]] T& front() noexcept { return data_[0]; }
    [[nodiscard]] const T& front() const noexcept { return data_[0]; }
    [[nodiscard]] T& back() noexcept { return data_[size_ - 1]; }
    [[nodiscard]] const T& back() const noexcept { return data_[size_ - 1]; }

    [[nodiscard]] iterator begin() noexcept { return data_; }
    [[nodiscard]] iterator end() noexcept { return data_ + size_; }
    [[nodiscard]] const_iterator begin() const noexcept { return data_; }
    [[nodiscard]] const_iterator end() const noexcept { return data_ + size_; }
    [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

private:
    // Non-owning: the arena owns the storage and the frame boundary frees it.
    // That is what makes this copyable and assignable, so a frame's draw data
    // can simply be overwritten with a fresh one.
    T* data_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t size_ = 0;
    std::size_t dropped_ = 0;
};

} // namespace sokoban
