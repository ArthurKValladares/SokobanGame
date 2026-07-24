#pragma once

#include <cstdint>
#include <vector>

namespace sokoban {

// Tracks which CPU frame slots still have submitted GPU work and which
// renderer resource generation each submission references.
class FrameResourceTracker {
public:
    explicit FrameResourceTracker(uint32_t frameCount);

    void markSubmitted(uint32_t frameIndex, uint64_t generation);
    [[nodiscard]] bool complete(uint32_t frameIndex);
    [[nodiscard]] uint32_t pendingMask() const;
    [[nodiscard]] uint32_t pendingMaskForGeneration(
        uint64_t generation) const;
    [[nodiscard]] bool pending(uint32_t frameIndex) const;

private:
    struct Frame {
        bool pending = false;
        uint64_t generation = 0;
    };

    [[nodiscard]] Frame& frame(uint32_t frameIndex);
    [[nodiscard]] const Frame& frame(uint32_t frameIndex) const;

    std::vector<Frame> frames_;
};

} // namespace sokoban
