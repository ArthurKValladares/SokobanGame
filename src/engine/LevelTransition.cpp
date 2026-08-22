#include "engine/LevelTransition.hpp"

#include <algorithm>

namespace sokoban {
namespace {

float smoothStep(float value)
{
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

bool LevelTransition::start()
{
    if (active()) {
        return false;
    }
    phase_ = Phase::Closing;
    elapsedSeconds_ = 0.0f;
    return true;
}

LevelTransition::UpdateResult LevelTransition::update(float dt)
{
    UpdateResult result;
    if (!active()) {
        return result;
    }

    elapsedSeconds_ += std::max(dt, 0.0f);
    if (phase_ == Phase::Closing &&
        elapsedSeconds_ >= closingDurationSeconds) {
        // Do not carry excess time into Opening: the destination must render
        // at amount 1 for at least one frame after the world swap.
        phase_ = Phase::Opening;
        elapsedSeconds_ = 0.0f;
        result.midpointReached = true;
    } else if (phase_ == Phase::Opening &&
        elapsedSeconds_ >= openingDurationSeconds) {
        phase_ = Phase::Idle;
        elapsedSeconds_ = 0.0f;
        result.finished = true;
    }
    return result;
}

float LevelTransition::amount() const
{
    switch (phase_) {
    case Phase::Closing:
        return smoothStep(elapsedSeconds_ / closingDurationSeconds);
    case Phase::Opening:
        return 1.0f - smoothStep(elapsedSeconds_ / openingDurationSeconds);
    case Phase::Idle:
        return 0.0f;
    }
    return 0.0f;
}

} // namespace sokoban
