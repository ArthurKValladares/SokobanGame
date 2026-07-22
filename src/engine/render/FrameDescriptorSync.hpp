#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sokoban {

// Tracks which frame-local descriptor sets contain the latest resource views.
// The renderer updates a set only after waiting for that frame's fence.
class FrameDescriptorSync {
public:
    explicit FrameDescriptorSync(std::size_t frameCount = 0)
    {
        reset(frameCount);
    }

    void reset(std::size_t frameCount)
    {
        generation_ = 1;
        frameGenerations_.assign(frameCount, 0);
    }

    void resourcesChanged()
    {
        ++generation_;
        if (generation_ == 0) {
            generation_ = 1;
            std::fill(frameGenerations_.begin(), frameGenerations_.end(), 0);
        }
    }

    [[nodiscard]] bool needsUpdate(std::size_t frameIndex) const
    {
        return frameGenerations_.at(frameIndex) != generation_;
    }

    void markUpdated(std::size_t frameIndex)
    {
        frameGenerations_.at(frameIndex) = generation_;
    }

    void markAllUpdated()
    {
        std::fill(
            frameGenerations_.begin(), frameGenerations_.end(), generation_);
    }

    [[nodiscard]] uint64_t generation() const { return generation_; }

private:
    uint64_t generation_ = 1;
    std::vector<uint64_t> frameGenerations_;
};

} // namespace sokoban
