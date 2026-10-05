#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sokoban {

// Holds resources until every frame that could still reference them has
// completed. New submissions are deliberately not added to an existing
// entry: after retirement, newly recorded frames use the replacement or
// fallback resource instead.
template <typename Resource>
class FrameRetirementQueue {
public:
    struct Entry {
        Resource resource;
        uint32_t pendingFrameMask = 0;
    };

    void retire(Resource resource, uint32_t pendingFrameMask)
    {
        entries_.push_back({
            .resource = std::move(resource),
            .pendingFrameMask = pendingFrameMask,
        });
    }

    void completeFrame(uint32_t frameIndex)
    {
        if (frameIndex >= 32) {
            throw std::out_of_range(
                "Frame retirement index exceeds the mask width");
        }
        const uint32_t completedBit = ~(1U << frameIndex);
        for (Entry& entry : entries_) {
            entry.pendingFrameMask &= completedBit;
        }
    }

    template <typename Destroy>
    void drainCompleted(Destroy destroy)
    {
        for (std::size_t index = 0; index < entries_.size();) {
            if (entries_[index].pendingFrameMask != 0) {
                ++index;
                continue;
            }
            destroy(entries_[index].resource);
            // Keep relocation bounds in element indices. GCC 13 can
            // misdiagnose vector::erase's bulk move after inlining this
            // method into a caller that grows and drains a small queue.
            for (std::size_t next = index + 1; next < entries_.size(); ++next) {
                entries_[next - 1].resource =
                    std::move(entries_[next].resource);
                entries_[next - 1].pendingFrameMask =
                    entries_[next].pendingFrameMask;
            }
            entries_.pop_back();
        }
    }

    template <typename Destroy>
    void drainAll(Destroy destroy)
    {
        for (Entry& entry : entries_) {
            destroy(entry.resource);
        }
        entries_.clear();
    }

    [[nodiscard]] std::size_t size() const { return entries_.size(); }
    [[nodiscard]] bool empty() const { return entries_.empty(); }

private:
    std::vector<Entry> entries_;
};

} // namespace sokoban
