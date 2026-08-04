#include "engine/PresentationTransactionBuilder.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>

namespace sokoban {
namespace {

ActionAnimationTrack* findTrack(
    std::vector<ActionAnimationTrack>& tracks,
    EntityTarget target)
{
    const auto found = std::ranges::find(tracks, target, &ActionAnimationTrack::target);
    return found == tracks.end() ? nullptr : &*found;
}

} // namespace

void PresentationTransactionBuilder::addMotion(ActionMotionTrack motion)
{
    motion.startSeconds = std::max(motion.startSeconds, 0.0f);
    motion.durationSeconds = std::max(motion.durationSeconds, 0.0f);
    motions_.push_back(std::move(motion));
}

void PresentationTransactionBuilder::setInitialAnimation(
    EntityTarget target,
    AnimationUse use,
    float clipTimeSeconds)
{
    if (ActionAnimationTrack* track = findTrack(initialTracks_, target)) {
        track->initialUse = use;
        track->initialClipTimeSeconds = std::max(clipTimeSeconds, 0.0f);
        return;
    }
    initialTracks_.push_back({
        .target = target,
        .initialUse = use,
        .initialClipTimeSeconds = std::max(clipTimeSeconds, 0.0f),
    });
}

PresentationTransactionBuilder::AnimationIntentId
PresentationTransactionBuilder::addAnimation(AnimationIntent intent)
{
    intent.earliestStartSeconds = std::max(intent.earliestStartSeconds, 0.0f);
    intent.clipStartSeconds = std::max(intent.clipStartSeconds, 0.0f);
    animationIntents_.push_back(std::move(intent));
    return animationIntents_.size() - 1;
}

bool PresentationTransactionBuilder::startAfterCatalogEvent(
    AnimationIntentId dependent,
    AnimationIntentId source)
{
    if (catalog_ == nullptr || dependent >= animationIntents_.size() ||
        source >= animationIntents_.size()) {
        return false;
    }
    const auto& gate = catalog_->startGate(animationIntents_[dependent].use);
    if (!gate || gate->sourceUse != animationIntents_[source].use) {
        return false;
    }
    return startAfterEvent(dependent, source, gate->eventId);
}

bool PresentationTransactionBuilder::startAfterEvent(
    AnimationIntentId dependent,
    AnimationIntentId source,
    std::string eventId)
{
    if (dependent >= animationIntents_.size() ||
        source >= animationIntents_.size() || eventId.empty()) {
        return false;
    }
    animationIntents_[dependent].startsAfter = EventDependency {
        .source = source,
        .eventId = std::move(eventId),
    };
    return true;
}

ActionPresentationTimeline PresentationTransactionBuilder::build() const
{
    ActionPresentationTimeline result {
        .motions = motions_,
        .animations = initialTracks_,
    };
    for (const ActionMotionTrack& motion : result.motions) {
        result.durationSeconds = std::max(
            result.durationSeconds,
            motion.startSeconds + motion.durationSeconds);
    }

    std::vector<float> starts(animationIntents_.size(), 0.0f);
    std::vector<uint8_t> visits(animationIntents_.size(), 0);
    std::function<float(AnimationIntentId)> resolveStart =
        [&](AnimationIntentId id) -> float {
        if (id >= animationIntents_.size()) {
            throw std::runtime_error("presentation dependency references an unknown intent");
        }
        if (visits[id] == 2) {
            return starts[id];
        }
        if (visits[id] == 1) {
            throw std::runtime_error("presentation animation dependencies contain a cycle");
        }
        visits[id] = 1;
        const AnimationIntent& intent = animationIntents_[id];
        float start = intent.earliestStartSeconds;
        if (intent.startsAfter) {
            const float sourceStart = resolveStart(intent.startsAfter->source);
            const AnimationIntent& source =
                animationIntents_.at(intent.startsAfter->source);
            float eventTime = 0.0f;
            if (catalog_ != nullptr) {
                const float speed = catalog_->effectiveSpeed(source.use);
                if (speed > 0.0f) {
                    eventTime = catalog_->eventSourceTime(
                        source.use,
                        intent.startsAfter->eventId) / speed;
                }
            }
            start = std::max(
                start,
                sourceStart + eventTime);
        }
        starts[id] = start;
        visits[id] = 2;
        return start;
    };

    for (AnimationIntentId id = 0; id < animationIntents_.size(); ++id) {
        const AnimationIntent& intent = animationIntents_[id];
        float duration = intent.durationSeconds.value_or(0.0f);
        if (!intent.durationSeconds && catalog_ != nullptr) {
            const float speed = catalog_->effectiveSpeed(intent.use);
            if (speed > 0.0f) {
                duration = catalog_->clipDuration(catalog_->animation(intent.use)) /
                    speed;
            }
        }
        if (duration <= 0.0f) {
            continue;
        }
        ActionAnimationTrack* track = findTrack(result.animations, intent.target);
        if (track == nullptr) {
            result.animations.push_back({
                .target = intent.target,
                .initialUse = intent.completionUse,
            });
            track = &result.animations.back();
        }
        const float start = resolveStart(id);
        track->segments.push_back({
            .use = intent.use,
            .completionUse = intent.completionUse,
            .fallbackUse = intent.fallbackUse,
            .startSeconds = start,
            .durationSeconds = duration,
            .clipStartSeconds = intent.clipStartSeconds,
            .loops = intent.loops,
        });
        result.durationSeconds = std::max(result.durationSeconds, start + duration);
    }
    for (ActionAnimationTrack& track : result.animations) {
        std::ranges::stable_sort(
            track.segments,
            {},
            &ActionAnimationSegment::startSeconds);
    }
    return result;
}

ActionPresentationTimeline concatenateTimelines(
    ActionPresentationTimeline earlier,
    const ActionPresentationTimeline& later,
    float offsetSeconds)
{
    const float offset = std::max(offsetSeconds, 0.0f);
    earlier.durationSeconds =
        std::max(earlier.durationSeconds, offset + later.durationSeconds);

    for (const ActionMotionTrack& motion : later.motions) {
        ActionMotionTrack shifted = motion;
        shifted.startSeconds += offset;
        earlier.motions.push_back(shifted);
    }

    for (const ActionAnimationTrack& track : later.animations) {
        const auto existing = std::ranges::find(
            earlier.animations, track.target, &ActionAnimationTrack::target);
        // A target the earlier half never mentioned brings its own initial
        // pose; one it did keeps the pose it started from, or reversing the
        // joined timeline would begin from the wrong frame.
        ActionAnimationTrack& into = existing == earlier.animations.end()
            ? earlier.animations.emplace_back(ActionAnimationTrack {
                  .target = track.target,
                  .initialUse = track.initialUse,
                  .initialClipTimeSeconds = track.initialClipTimeSeconds,
                  .segments = {},
              })
            : *existing;
        for (const ActionAnimationSegment& segment : track.segments) {
            ActionAnimationSegment shifted = segment;
            shifted.startSeconds += offset;
            into.segments.push_back(shifted);
        }
    }
    return earlier;
}

} // namespace sokoban
