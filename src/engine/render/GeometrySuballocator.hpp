#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace sokoban {

// Free-list suballocator for one geometry buffer block. It is deliberately
// Vulkan-independent so fragmentation and alignment rules remain testable
// without a GPU. Allocations are byte ranges; callers own the backing buffer.
class GeometrySuballocator {
public:
    struct Allocation {
        uint64_t offset = 0;
        uint64_t size = 0;

        [[nodiscard]] bool valid() const { return size != 0; }
        [[nodiscard]] bool operator==(const Allocation&) const = default;
    };

    explicit GeometrySuballocator(uint64_t capacity = 0);

    [[nodiscard]] std::optional<Allocation> allocate(
        uint64_t size,
        uint64_t alignment);
    void release(Allocation allocation);

    [[nodiscard]] uint64_t capacity() const { return capacity_; }
    [[nodiscard]] uint64_t usedBytes() const { return usedBytes_; }
    [[nodiscard]] uint64_t freeBytes() const { return capacity_ - usedBytes_; }
    [[nodiscard]] uint64_t largestFreeRange() const;

private:
    struct Range {
        uint64_t offset = 0;
        uint64_t size = 0;
    };

    uint64_t capacity_ = 0;
    uint64_t usedBytes_ = 0;
    std::vector<Range> freeRanges_;
};

} // namespace sokoban
