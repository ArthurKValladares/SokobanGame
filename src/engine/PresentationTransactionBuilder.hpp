#pragma once

#include "engine/ActionPresentation.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace sokoban {

class PresentationTransactionBuilder {
public:
    using AnimationIntentId = std::size_t;

    struct EventDependency {
        AnimationIntentId source = 0;
        std::string eventId;
    };

    struct AnimationIntent {
        EntityTarget target;
        AnimationUse use = AnimationUse::PlayerIdle;
        AnimationUse completionUse = AnimationUse::PlayerIdle;
        std::optional<AnimationUse> fallbackUse;
        float earliestStartSeconds = 0.0f;
        float clipStartSeconds = 0.0f;
        std::optional<float> durationSeconds;
        bool loops = false;
        std::optional<EventDependency> startsAfter;
    };

    explicit PresentationTransactionBuilder(const AnimationCatalog* catalog)
        : catalog_(catalog)
    {
    }

    void addMotion(ActionMotionTrack motion);
    void setInitialAnimation(
        EntityTarget target,
        AnimationUse use,
        float clipTimeSeconds = 0.0f);
    [[nodiscard]] AnimationIntentId addAnimation(AnimationIntent intent);
    [[nodiscard]] bool startAfterEvent(
        AnimationIntentId dependent,
        AnimationIntentId source,
        std::string eventId);
    [[nodiscard]] bool startAfterCatalogEvent(
        AnimationIntentId dependent,
        AnimationIntentId source);
    [[nodiscard]] ActionPresentationTimeline build() const;

private:
    const AnimationCatalog* catalog_ = nullptr;
    std::vector<ActionMotionTrack> motions_;
    std::vector<ActionAnimationTrack> initialTracks_;
    std::vector<AnimationIntent> animationIntents_;
};

// Appends `later` to `earlier`, shifted so it begins `offsetSeconds` in.
//
// Every track already carries an absolute `startSeconds`, so joining two
// resolved timelines is just an offset. Tracks for the same actor merge into
// one, so that playback - forward or reversed - still sees a single timeline
// per entity.
//
// Two callers with the same need: the legs of a chained slide, each resolved as
// its own transaction and laid end to end, and an undo entry that a consequence
// folded into.
[[nodiscard]] ActionPresentationTimeline concatenateTimelines(
    ActionPresentationTimeline earlier,
    const ActionPresentationTimeline& later,
    float offsetSeconds);

} // namespace sokoban
