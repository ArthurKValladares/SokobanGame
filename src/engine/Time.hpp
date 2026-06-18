#pragma once

#include <chrono>

namespace sokoban {

class FrameTimer {
public:
    [[nodiscard]] float tick()
    {
        const auto now = Clock::now();
        const std::chrono::duration<float> elapsed = now - previous_;
        previous_ = now;
        return elapsed.count();
    }

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point previous_ = Clock::now();
};

} // namespace sokoban
