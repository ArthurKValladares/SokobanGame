#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace sokoban {

enum class SimulationSuspension : std::uint64_t {
    Minimized = 1U << 0,
    Backgrounded = 1U << 1,
};

// Main-thread simulation timing policy with thread-safe lifecycle updates.
// SDL can deliver application background events from an event-watch thread,
// so reasons and their transition generation share one atomic value. The
// first frame after any transition is discarded, resetting the wall-clock
// baseline without replaying time spent suspended.
class SimulationTiming {
public:
    static constexpr float maximumDeltaSeconds = 0.1f;

    void setSuspended(
        SimulationSuspension reason,
        bool suspended) noexcept
    {
        const std::uint64_t reasonBit = static_cast<std::uint64_t>(reason);
        std::uint64_t observed = lifecycleState_.load(
            std::memory_order_relaxed);
        for (;;) {
            const std::uint64_t reasons = observed & suspensionReasonMask_;
            const std::uint64_t updatedReasons = suspended
                ? reasons | reasonBit
                : reasons & ~reasonBit;
            if (updatedReasons == reasons) {
                return;
            }

            const std::uint64_t desired =
                ((observed & ~suspensionReasonMask_) + generationStep_) |
                updatedReasons;
            if (lifecycleState_.compare_exchange_weak(
                    observed,
                    desired,
                    std::memory_order_release,
                    std::memory_order_relaxed)) {
                return;
            }
        }
    }

    [[nodiscard]] bool suspended() const noexcept
    {
        return (lifecycleState_.load(std::memory_order_acquire) &
                   suspensionReasonMask_) != 0;
    }

    // Called once per main-loop frame with the elapsed wall-clock time. This
    // method itself is main-thread-only; lifecycle updates may be concurrent.
    [[nodiscard]] float frameDelta(float elapsedSeconds) noexcept
    {
        const std::uint64_t state = lifecycleState_.load(
            std::memory_order_acquire);
        const std::uint64_t generation = state & ~suspensionReasonMask_;
        if (generation != observedGeneration_) {
            observedGeneration_ = generation;
            return 0.0f;
        }
        if ((state & suspensionReasonMask_) != 0 ||
            !std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0f) {
            return 0.0f;
        }
        return std::min(elapsedSeconds, maximumDeltaSeconds);
    }

private:
    static constexpr std::uint64_t suspensionReasonMask_ =
        static_cast<std::uint64_t>(SimulationSuspension::Minimized) |
        static_cast<std::uint64_t>(SimulationSuspension::Backgrounded);
    static constexpr std::uint64_t generationStep_ =
        suspensionReasonMask_ + 1U;

    std::atomic<std::uint64_t> lifecycleState_ { 0 };
    std::uint64_t observedGeneration_ = 0;
};

class FrameTimer {
public:
    [[nodiscard]] float tick(SimulationTiming& timing)
    {
        const auto now = Clock::now();
        const std::chrono::duration<float> elapsed = now - previous_;
        previous_ = now;
        return timing.frameDelta(elapsed.count());
    }

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point previous_ = Clock::now();
};

} // namespace sokoban
