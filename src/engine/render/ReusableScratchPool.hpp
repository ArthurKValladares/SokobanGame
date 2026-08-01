#pragma once

#include <array>
#include <cstddef>
#include <memory>

namespace sokoban {

// Reuses scratch allocations while leases are short-lived, but never mutates
// storage that a retained prepared result still references.
template <typename Scratch, std::size_t RetainedSlots>
class ReusableScratchPool {
    static_assert(RetainedSlots > 0);

public:
    [[nodiscard]] std::shared_ptr<Scratch> acquire()
    {
        for (std::size_t offset = 0; offset < RetainedSlots; ++offset) {
            const std::size_t index = (nextSlot_ + offset) % RetainedSlots;
            std::shared_ptr<Scratch>& slot = slots_[index];
            if (!slot) {
                slot = std::make_shared<Scratch>();
            }
            if (slot.use_count() == 1) {
                nextSlot_ = (index + 1) % RetainedSlots;
                return slot;
            }
        }

        // Nested/offscreen rendering can temporarily retain every normal
        // slot. Overflow storage is deliberately not retained by the pool;
        // it is reclaimed when the returned lease is released.
        return std::make_shared<Scratch>();
    }

private:
    std::array<std::shared_ptr<Scratch>, RetainedSlots> slots_ {};
    std::size_t nextSlot_ = 0;
};

} // namespace sokoban
