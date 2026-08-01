#pragma once

#include "engine/AnimationCatalog.hpp"
#include "engine/EntityId.hpp"
#include "engine/Math.hpp"

#include <optional>
#include <vector>

namespace sokoban {

struct ActionMotionTrack {
    EntityTarget target;
    Vec3 from {};
    Vec3 to {};
    float startSeconds = 0.0f;
    float durationSeconds = 0.0f;

    bool operator==(const ActionMotionTrack& other) const
    {
        return target == other.target &&
            from.x == other.from.x && from.y == other.from.y && from.z == other.from.z &&
            to.x == other.to.x && to.y == other.to.y && to.z == other.to.z &&
            startSeconds == other.startSeconds &&
            durationSeconds == other.durationSeconds;
    }
};

// One resolved clip on an actor's action-local animation track. Dependencies
// and authored timeline events have already been reduced to startSeconds, so
// playback and undo only sample immutable data.
struct ActionAnimationSegment {
    AnimationUse use = AnimationUse::PlayerIdle;
    AnimationUse completionUse = AnimationUse::PlayerIdle;
    std::optional<AnimationUse> fallbackUse;
    float startSeconds = 0.0f;
    float durationSeconds = 0.0f;
    float clipStartSeconds = 0.0f;
    bool loops = false;

    bool operator==(const ActionAnimationSegment&) const = default;
};

struct ActionAnimationTrack {
    EntityTarget target;
    AnimationUse initialUse = AnimationUse::PlayerIdle;
    float initialClipTimeSeconds = 0.0f;
    std::vector<ActionAnimationSegment> segments;

    bool operator==(const ActionAnimationTrack&) const = default;
};

// Recorded with a gameplay action. Forward playback and undo both seek this
// exact transaction; reversing an action never reconstructs mechanic-specific
// animation ordering from state snapshots.
struct ActionPresentationTimeline {
    float durationSeconds = 0.0f;
    std::vector<ActionMotionTrack> motions;
    std::vector<ActionAnimationTrack> animations;

    [[nodiscard]] bool empty() const
    {
        return durationSeconds <= 0.0f && motions.empty() && animations.empty();
    }

    bool operator==(const ActionPresentationTimeline&) const = default;
};

} // namespace sokoban
