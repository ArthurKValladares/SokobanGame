#include "engine/render/GeometrySuballocator.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sokoban {
namespace {

uint64_t alignUp(uint64_t value, uint64_t alignment)
{
    if (alignment == 0) {
        throw std::invalid_argument("Geometry allocation alignment must be non-zero");
    }
    const uint64_t remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }
    if (value > std::numeric_limits<uint64_t>::max() - (alignment - remainder)) {
        throw std::overflow_error("Geometry allocation alignment overflow");
    }
    return value + alignment - remainder;
}

} // namespace

GeometrySuballocator::GeometrySuballocator(uint64_t capacity)
    : capacity_(capacity)
{
    if (capacity != 0) {
        freeRanges_.push_back({ .offset = 0, .size = capacity });
    }
}

std::optional<GeometrySuballocator::Allocation> GeometrySuballocator::allocate(
    uint64_t size,
    uint64_t alignment)
{
    if (size == 0) {
        return std::nullopt;
    }
    for (auto iterator = freeRanges_.begin(); iterator != freeRanges_.end(); ++iterator) {
        const uint64_t alignedOffset = alignUp(iterator->offset, alignment);
        if (alignedOffset < iterator->offset ||
            alignedOffset - iterator->offset > iterator->size) {
            continue;
        }
        const uint64_t padding = alignedOffset - iterator->offset;
        if (size > iterator->size - padding) {
            continue;
        }

        const uint64_t suffixOffset = alignedOffset + size;
        const uint64_t suffixSize = iterator->offset + iterator->size - suffixOffset;
        const Range prefix { iterator->offset, padding };
        if (prefix.size != 0 && suffixSize != 0) {
            iterator->size = prefix.size;
            freeRanges_.insert(iterator + 1, { suffixOffset, suffixSize });
        } else if (prefix.size != 0) {
            iterator->size = prefix.size;
        } else if (suffixSize != 0) {
            iterator->offset = suffixOffset;
            iterator->size = suffixSize;
        } else {
            freeRanges_.erase(iterator);
        }
        usedBytes_ += size;
        return Allocation { alignedOffset, size };
    }
    return std::nullopt;
}

void GeometrySuballocator::release(Allocation allocation)
{
    if (!allocation.valid() || allocation.offset > capacity_ ||
        allocation.size > capacity_ - allocation.offset ||
        allocation.size > usedBytes_) {
        throw std::invalid_argument("Invalid geometry allocation release");
    }

    const auto insertion = std::lower_bound(
        freeRanges_.begin(),
        freeRanges_.end(),
        allocation.offset,
        [](const Range& range, uint64_t offset) { return range.offset < offset; });
    if (insertion != freeRanges_.begin()) {
        const Range& previous = *(insertion - 1);
        if (previous.offset + previous.size > allocation.offset) {
            throw std::invalid_argument("Overlapping geometry allocation release");
        }
    }
    if (insertion != freeRanges_.end() &&
        allocation.offset + allocation.size > insertion->offset) {
        throw std::invalid_argument("Overlapping geometry allocation release");
    }

    auto merged = freeRanges_.insert(
        insertion,
        { allocation.offset, allocation.size });
    if (merged != freeRanges_.begin()) {
        auto previous = merged - 1;
        if (previous->offset + previous->size == merged->offset) {
            previous->size += merged->size;
            merged = freeRanges_.erase(merged);
            merged = previous;
        }
    }
    if (merged + 1 != freeRanges_.end() &&
        merged->offset + merged->size == (merged + 1)->offset) {
        merged->size += (merged + 1)->size;
        freeRanges_.erase(merged + 1);
    }
    usedBytes_ -= allocation.size;
}

uint64_t GeometrySuballocator::largestFreeRange() const
{
    uint64_t result = 0;
    for (const Range& range : freeRanges_) {
        result = std::max(result, range.size);
    }
    return result;
}

} // namespace sokoban
