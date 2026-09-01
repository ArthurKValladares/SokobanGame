#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace sokoban {

// Hands out contiguous runs of entries in the shared material buffer.
//
// A model claims a run when it publishes and gives it back when residency
// evicts it. Runs never move while live: the recorder reads a model's base off
// its slot at record time, so relocating one would change what a draw already
// in flight is pointing at. That rules out compaction and leaves a free list.
//
// The shape is a bump pointer plus first-fit reuse. Entries above the high-water
// mark have never been used; released runs below it go on a sorted free list and
// are coalesced with their neighbours, so a model that publishes and is evicted
// repeatedly does not saw the buffer into unusable slivers.
//
// Entry zero is never handed out. The buffer reserves it as the published-
// nothing fallback - an untextured white surface - so a draw recorded a frame
// early reads white rather than another model's material. Construct with
// reset(1) to express that.
//
// Vulkan-free on purpose: this is bookkeeping, and keeping it separate from the
// mapped buffer it describes is what lets it be tested.
class MaterialRangeAllocator {
public:
    struct FreeRange {
        uint32_t base = 0;
        uint32_t count = 0;

        friend constexpr bool operator==(const FreeRange&, const FreeRange&)
            = default;
    };

    // `reservedEntries` is where the high-water mark starts, and therefore how
    // many entries at the bottom are never allocated.
    void reset(uint32_t reservedEntries)
    {
        highWater_ = reservedEntries;
        free_.clear();
    }

    // The base of a run of `count` entries, or nothing when neither a free run
    // nor the space above the high-water mark can hold it.
    //
    // Failing is not fatal to the frame: it fails that one model's publication,
    // which is retried later, so this reports rather than throws.
    [[nodiscard]] std::optional<uint32_t> allocate(
        uint32_t count, uint32_t capacity)
    {
        if (count == 0) {
            return std::nullopt;
        }
        uint32_t base = highWater_;
        for (auto range = free_.begin(); range != free_.end(); ++range) {
            if (range->count < count) {
                continue;
            }
            // Taken from the front, leaving the tail free. First fit rather
            // than best fit: runs are small and alike, so the search order
            // matters less than keeping this cheap.
            base = range->base;
            range->base += count;
            range->count -= count;
            if (range->count == 0) {
                free_.erase(range);
            }
            break;
        }
        if (base == 0) {
            // Only reachable before reset() has reserved the fallback entry.
            return std::nullopt;
        }
        if (static_cast<uint64_t>(base) + count > capacity) {
            return std::nullopt;
        }
        highWater_ = std::max(highWater_, base + count);
        return base;
    }

    // Gives a run back. Adjacent and overlapping runs are merged, so the list
    // stays sorted and minimal.
    //
    // Releasing a run twice is absorbed rather than reported: the merge simply
    // folds the duplicate into its neighbour. Nothing in the renderer can do
    // that today - a slot's base is zeroed as it is released - so this is a
    // property worth knowing rather than a hole worth guarding, and
    // MaterialRangeAllocatorTests says so.
    void release(uint32_t base, uint32_t count)
    {
        if (base == 0 || count == 0) {
            return;
        }
        const auto position = std::lower_bound(
            free_.begin(),
            free_.end(),
            base,
            [](const FreeRange& range, uint32_t value) {
                return range.base < value;
            });
        free_.insert(position, FreeRange { .base = base, .count = count });

        std::vector<FreeRange> merged;
        merged.reserve(free_.size());
        for (const FreeRange& range : free_) {
            if (!merged.empty() &&
                merged.back().base + merged.back().count >= range.base) {
                const uint32_t end = std::max(
                    merged.back().base + merged.back().count,
                    range.base + range.count);
                merged.back().count = end - merged.back().base;
            } else {
                merged.push_back(range);
            }
        }
        free_ = std::move(merged);
    }

    // One past the highest entry ever handed out. The CPU mirror of the buffer
    // is grown to match, so the caller needs to see it.
    [[nodiscard]] uint32_t highWaterMark() const { return highWater_; }

    [[nodiscard]] const std::vector<FreeRange>& freeRanges() const
    {
        return free_;
    }

    [[nodiscard]] uint32_t freeEntryCount() const
    {
        uint32_t total = 0;
        for (const FreeRange& range : free_) {
            total += range.count;
        }
        return total;
    }

private:
    uint32_t highWater_ = 0;
    // Sorted by base and coalesced, which release() maintains as an invariant.
    std::vector<FreeRange> free_;
};

} // namespace sokoban
