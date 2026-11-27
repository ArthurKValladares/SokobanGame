#pragma once

#include "engine/Math.hpp"

#include <cstdint>

namespace sokoban {

// What the world does back when something is done to it. None of it is a
// rule: a cue changes nothing a move can depend on, only what is drawn.
enum class GameplayFeedback : uint8_t {
    // A stone leaves its spot under a push or a pull, and grinds the ground.
    PushOff,
    // A unit that dropped comes down on ground.
    Landing,
    // A hero walked into something that will not move.
    Bump,
};

// Transient, action-local timing, exactly as GameplaySoundCue: neither cues
// nor their delivery state enter undo history or checkpoints, so loading and
// rewinding cannot replay an effect.
struct GameplayFeedbackCue {
    GameplayFeedback kind = GameplayFeedback::PushOff;
    // Where it happens, in world units, on the surface the unit stands on.
    Vec3 position {};
    // The unit's heading along the ground, of unit length. Zero for a unit
    // that came straight down.
    Vec2 direction {};
    // 0..1, how hard: a landing's grows with the drop and the weight.
    float strength = 1.0f;
    float triggerSeconds = 0.0f;
    bool emitted = false;

    bool operator==(const GameplayFeedbackCue&) const = default;
};

} // namespace sokoban
