#pragma once

#include "engine/ArenaArray.hpp"

#include <cassert>
#include <algorithm>
#include <initializer_list>
#include <utility>
#include <variant>
#include <vector>

namespace sokoban {

// Vector-compatible frame storage. Ordinary frames acquire an owning vector
// on their first write, so empty optional collections require no heap storage
// or debug-iterator bookkeeping. Live render frames select the ArenaArray
// alternative and take their whole capacity in one arena bump.
template <class T>
class FrameArray {
public:
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;

    FrameArray() = default;

    FrameArray(FrameArena& arena, std::size_t capacity)
        : storage_(std::in_place_type<ArenaArray<T>>, arena, capacity)
    {
    }

    // Copying an arena-backed array **moves it to the heap**. That is a
    // performance cliff on a per-frame structure, and it is silent - the type
    // does not change, only where its bytes live.
    //
    // It is deliberate rather than an oversight: the heap copy is what makes
    // a RenderFrameData safe to outlive the arena it was built in, which is
    // the other half of the frame-arena lifetime story (see
    // Application::beginRenderFrameArena). Copy when you mean to escape the
    // arena; move, or take a reference, when you do not. `clone()` says so at
    // the call site.
    FrameArray(const FrameArray& other)
    {
        if (!other.empty()) {
            std::vector<T> values(other.begin(), other.end());
            storage_ = std::move(values);
        }
    }

    // An explicit copy, for the places that want the heap copy on purpose.
    [[nodiscard]] FrameArray clone() const { return FrameArray(*this); }

    // Same cliff as the copy constructor above.
    FrameArray& operator=(const FrameArray& other)
    {
        if (this == &other) {
            return *this;
        }
        // Build the owning copy before replacing live storage. If allocation
        // or an element copy throws, the destination stays valid; committing
        // the variant through its non-throwing move cannot make it valueless.
        FrameArray replacement(other);
        storage_ = std::move(replacement.storage_);
        return *this;
    }

    FrameArray(FrameArray&&) noexcept = default;
    FrameArray& operator=(FrameArray&&) noexcept = default;

    FrameArray& operator=(std::initializer_list<T> values)
    {
        clear();
        reserve(values.size());
        for (const T& value : values) {
            (void)push_back(value);
        }
        return *this;
    }

    bool push_back(const T& value)
    {
        if (auto* arena = std::get_if<ArenaArray<T>>(&storage_)) {
            return arena->push_back(value);
        }
        owningStorage().push_back(value);
        return true;
    }

    bool push_back(T&& value)
    {
        if (auto* arena = std::get_if<ArenaArray<T>>(&storage_)) {
            return arena->push_back(std::move(value));
        }
        owningStorage().push_back(std::move(value));
        return true;
    }

    void reserve(std::size_t capacity)
    {
        if (!arenaBacked() && capacity != 0) {
            owningStorage().reserve(capacity);
        }
    }

    void clear() noexcept
    {
        if (auto* values = std::get_if<std::vector<T>>(&storage_)) {
            values->clear();
        } else if (auto* arena = std::get_if<ArenaArray<T>>(&storage_)) {
            arena->clear();
        }
    }

    iterator erase(const_iterator first, const_iterator last)
    {
        if (first == last) {
            return const_cast<iterator>(first);
        }
        if (auto* vector = std::get_if<std::vector<T>>(&storage_)) {
            const auto firstOffset = first - vector->data();
            const auto lastOffset = last - vector->data();
            vector->erase(
                vector->begin() + firstOffset,
                vector->begin() + lastOffset);
            return vector->data() + firstOffset;
        }
        return arenaStorage().erase(first, last);
    }

    iterator erase(const_iterator position)
    {
        return erase(position, position + 1);
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        if (const auto* arena = std::get_if<ArenaArray<T>>(&storage_)) {
            return arena->size();
        }
        if (const auto* values = std::get_if<std::vector<T>>(&storage_)) {
            return values->size();
        }
        return 0;
    }

    [[nodiscard]] std::size_t capacity() const noexcept
    {
        if (const auto* arena = std::get_if<ArenaArray<T>>(&storage_)) {
            return arena->capacity();
        }
        if (const auto* values = std::get_if<std::vector<T>>(&storage_)) {
            return values->capacity();
        }
        return 0;
    }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] bool arenaBacked() const noexcept
    {
        return std::holds_alternative<ArenaArray<T>>(storage_);
    }
    [[nodiscard]] std::size_t droppedCount() const noexcept
    {
        if (const auto* arena = std::get_if<ArenaArray<T>>(&storage_)) {
            return arena->droppedCount();
        }
        return 0;
    }

    [[nodiscard]] T* data() noexcept
    {
        if (auto* arena = std::get_if<ArenaArray<T>>(&storage_)) {
            return arena->data();
        }
        if (auto* values = std::get_if<std::vector<T>>(&storage_)) {
            return values->data();
        }
        return nullptr;
    }
    [[nodiscard]] const T* data() const noexcept
    {
        if (const auto* arena = std::get_if<ArenaArray<T>>(&storage_)) {
            return arena->data();
        }
        if (const auto* values = std::get_if<std::vector<T>>(&storage_)) {
            return values->data();
        }
        return nullptr;
    }
    [[nodiscard]] iterator begin() noexcept { return data(); }
    [[nodiscard]] iterator end() noexcept { return empty() ? data() : data() + size(); }
    [[nodiscard]] const_iterator begin() const noexcept { return data(); }
    [[nodiscard]] const_iterator end() const noexcept { return empty() ? data() : data() + size(); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

    [[nodiscard]] T& operator[](std::size_t index) noexcept
    {
        return data()[index];
    }
    [[nodiscard]] const T& operator[](std::size_t index) const noexcept
    {
        return data()[index];
    }
    [[nodiscard]] T& front() noexcept { return *begin(); }
    [[nodiscard]] const T& front() const noexcept { return *begin(); }
    [[nodiscard]] T& back() noexcept { return *(end() - 1); }
    [[nodiscard]] const T& back() const noexcept { return *(end() - 1); }

    [[nodiscard]] bool operator==(const std::vector<T>& other) const
    {
        return std::ranges::equal(*this, other);
    }

private:
    [[nodiscard]] std::vector<T>& owningStorage()
    {
        if (auto* values = std::get_if<std::vector<T>>(&storage_)) {
            return *values;
        }
        assert(std::holds_alternative<std::monostate>(storage_));
        // Finish construction before replacing the lazy alternative. Later
        // reserve/push failures leave an ordinary valid, empty vector, and no
        // throwing element construction occurs inside variant::emplace.
        std::vector<T> values;
        storage_ = std::move(values);
        return *std::get_if<std::vector<T>>(&storage_);
    }

    // Constructors and moves preserve an active alternative, and copy
    // assignment commits only after its potentially throwing work succeeds.
    // Unlike std::visit/std::get, this access cannot throw bad_variant_access.
    [[nodiscard]] ArenaArray<T>& arenaStorage() noexcept
    {
        auto* values = std::get_if<ArenaArray<T>>(&storage_);
        assert(values != nullptr);
        return *values;
    }

    [[nodiscard]] const ArenaArray<T>& arenaStorage() const noexcept
    {
        const auto* values = std::get_if<ArenaArray<T>>(&storage_);
        assert(values != nullptr);
        return *values;
    }

    std::variant<std::monostate, std::vector<T>, ArenaArray<T>> storage_;
};

} // namespace sokoban
