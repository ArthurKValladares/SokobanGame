#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>

namespace sokoban {

enum class SimulationSuspension : std::uint64_t {
    Minimized = 1U << 0,
    Backgrounded = 1U << 1,
};

enum class FramePacingSuspension : std::uint64_t {
    Unfocused = 1U << 0,
    Minimized = 1U << 1,
    Backgrounded = 1U << 2,
};

enum class FramePacingActivity {
    Focused,
    Unfocused,
    Minimized,
    Backgrounded,
};

// Keeps the foreground cap player-configurable while avoiding unnecessary
// rendering work in states where the game cannot be interacted with. Lifecycle
// updates are lock-free because SDL event watches may run off the main thread;
// beginFrame()/pace() remain main-thread-only.
class FramePacer {
public:
    static constexpr int unfocusedFrameRateLimit = 20;
    static constexpr int minimizedFrameRateLimit = 5;

    void setSuspended(
        FramePacingSuspension reason,
        bool suspended) noexcept
    {
        const std::uint64_t bit = static_cast<std::uint64_t>(reason);
        if (suspended) {
            suspensions_.fetch_or(bit, std::memory_order_release);
        } else {
            suspensions_.fetch_and(~bit, std::memory_order_release);
        }
    }

    void setFrameRateLimit(int framesPerSecond) noexcept
    {
        frameRateLimit_.store(
            std::clamp(framesPerSecond, 0, 1000),
            std::memory_order_release);
    }

    [[nodiscard]] int frameRateLimit() const noexcept
    {
        return frameRateLimit_.load(std::memory_order_acquire);
    }

    [[nodiscard]] FramePacingActivity activity() const noexcept
    {
        const std::uint64_t state = suspensions_.load(std::memory_order_acquire);
        if ((state & static_cast<std::uint64_t>(
                FramePacingSuspension::Backgrounded)) != 0) {
            return FramePacingActivity::Backgrounded;
        }
        if ((state & static_cast<std::uint64_t>(
                FramePacingSuspension::Minimized)) != 0) {
            return FramePacingActivity::Minimized;
        }
        if ((state & static_cast<std::uint64_t>(
                FramePacingSuspension::Unfocused)) != 0) {
            return FramePacingActivity::Unfocused;
        }
        return FramePacingActivity::Focused;
    }

    [[nodiscard]] static constexpr std::chrono::nanoseconds targetInterval(
        FramePacingActivity activity,
        int foregroundFrameRateLimit) noexcept
    {
        const int framesPerSecond =
            activity == FramePacingActivity::Backgrounded ||
                activity == FramePacingActivity::Minimized
            ? minimizedFrameRateLimit
            : activity == FramePacingActivity::Unfocused
            ? unfocusedFrameRateLimit
            : foregroundFrameRateLimit;
        return framesPerSecond <= 0
            ? std::chrono::nanoseconds::zero()
            : std::chrono::nanoseconds { 1'000'000'000LL / framesPerSecond };
    }

    void beginFrame() noexcept { frameStart_ = Clock::now(); }

    void pace() const
    {
        const std::chrono::nanoseconds interval = targetInterval(
            activity(), frameRateLimit());
        if (interval != std::chrono::nanoseconds::zero()) {
            std::this_thread::sleep_until(frameStart_ + interval);
        }
    }

private:
    using Clock = std::chrono::steady_clock;

    std::atomic<std::uint64_t> suspensions_ { 0 };
    std::atomic<int> frameRateLimit_ { 0 };
    Clock::time_point frameStart_ = Clock::now();
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
