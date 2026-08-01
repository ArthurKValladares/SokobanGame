#pragma once

#include "engine/AnimationCatalog.hpp"

#include <cstddef>
#include <vector>

namespace sokoban {

enum class ActionActorKind {
    Player,
    Enemy,
};

// A semantic animation span on an action-local timeline. Times are logical
// presentation seconds, before per-use animation speed is applied.
struct ActionAnimationSpan {
    ActionActorKind actorKind = ActionActorKind::Player;
    std::size_t actorIndex = 0;
    AnimationUse use = AnimationUse::PlayerIdle;
    float startSeconds = 0.0f;
    float durationSeconds = 0.0f;
    float clipStartSeconds = 0.0f;

    bool operator==(const ActionAnimationSpan&) const = default;
};

// Recorded with a gameplay action so undo can traverse the exact authored
// motion/animation ordering in reverse instead of inferring it from snapshots.
struct ActionPresentationTimeline {
    float durationSeconds = 0.0f;
    float motionStartSeconds = 0.0f;
    float motionDurationSeconds = 0.0f;
    std::vector<ActionAnimationSpan> animations;

    [[nodiscard]] bool empty() const
    {
        return durationSeconds <= 0.0f && animations.empty();
    }

    bool operator==(const ActionPresentationTimeline&) const = default;
};

} // namespace sokoban
