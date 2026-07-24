#include "engine/render/FrameResourceTracker.hpp"

#include <limits>
#include <stdexcept>

namespace sokoban {

FrameResourceTracker::FrameResourceTracker(uint32_t frameCount)
    : frames_(frameCount)
{
    if (frameCount == 0 ||
        frameCount > std::numeric_limits<uint32_t>::digits) {
        throw std::invalid_argument(
            "frame resource tracker count must fit a uint32 mask");
    }
}

void FrameResourceTracker::markSubmitted(
    uint32_t frameIndex,
    uint64_t generation)
{
    Frame& target = frame(frameIndex);
    if (target.pending) {
        throw std::logic_error(
            "frame slot submitted before its fence completed");
    }
    if (generation == 0) {
        throw std::invalid_argument(
            "resource generation must be nonzero");
    }
    target.pending = true;
    target.generation = generation;
}

bool FrameResourceTracker::complete(uint32_t frameIndex)
{
    Frame& target = frame(frameIndex);
    if (!target.pending) {
        return false;
    }
    target = {};
    return true;
}

uint32_t FrameResourceTracker::pendingMask() const
{
    uint32_t mask = 0;
    for (uint32_t index = 0; index < frames_.size(); ++index) {
        if (frames_[index].pending) {
            mask |= 1U << index;
        }
    }
    return mask;
}

uint32_t FrameResourceTracker::pendingMaskForGeneration(
    uint64_t generation) const
{
    uint32_t mask = 0;
    for (uint32_t index = 0; index < frames_.size(); ++index) {
        if (frames_[index].pending &&
            frames_[index].generation == generation) {
            mask |= 1U << index;
        }
    }
    return mask;
}

bool FrameResourceTracker::pending(uint32_t frameIndex) const
{
    return frame(frameIndex).pending;
}

FrameResourceTracker::Frame& FrameResourceTracker::frame(
    uint32_t frameIndex)
{
    if (frameIndex >= frames_.size()) {
        throw std::out_of_range("frame resource index");
    }
    return frames_[frameIndex];
}

const FrameResourceTracker::Frame& FrameResourceTracker::frame(
    uint32_t frameIndex) const
{
    if (frameIndex >= frames_.size()) {
        throw std::out_of_range("frame resource index");
    }
    return frames_[frameIndex];
}

} // namespace sokoban
