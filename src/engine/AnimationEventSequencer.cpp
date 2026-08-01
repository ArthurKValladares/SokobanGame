#include "engine/AnimationEventSequencer.hpp"

#include <algorithm>
#include <cmath>

namespace sokoban {

void AnimationEventSequencer::begin(
    uint64_t instanceId,
    AnimationUse use,
    float logicalTimeSeconds)
{
    instances_[instanceId] = {
        .use = use,
        .logicalTimeSeconds = std::max(logicalTimeSeconds, 0.0f),
    };
}

std::vector<AnimationEventSequencer::FiredEvent>
AnimationEventSequencer::advance(
    uint64_t instanceId,
    float logicalTimeSeconds,
    const AnimationCatalog& catalog)
{
    const auto found = instances_.find(instanceId);
    if (found == instances_.end()) {
        return {};
    }

    Instance& instance = found->second;
    logicalTimeSeconds = std::max(logicalTimeSeconds, 0.0f);
    const float speed = catalog.effectiveSpeed(instance.use);
    const float previousSourceTime = instance.logicalTimeSeconds * speed;
    const float currentSourceTime = logicalTimeSeconds * speed;
    instance.logicalTimeSeconds = logicalTimeSeconds;

    if (currentSourceTime < previousSourceTime) {
        instance.firedEvents.clear();
        return {};
    }

    constexpr float epsilon = 0.00001f;
    std::vector<FiredEvent> fired;
    for (const AnimationCatalog::TimelineEvent& event :
         catalog.events(instance.use)) {
        if (std::ranges::find(instance.firedEvents, event.id) !=
            instance.firedEvents.end()) {
            continue;
        }
        const float eventSourceTime =
            catalog.eventSourceTime(instance.use, event.id);
        if (eventSourceTime + epsilon < previousSourceTime ||
            eventSourceTime > currentSourceTime + epsilon) {
            continue;
        }
        instance.firedEvents.push_back(event.id);
        fired.push_back({
            .instanceId = instanceId,
            .use = instance.use,
            .eventId = event.id,
            .overshootSeconds = std::max(
                0.0f, (currentSourceTime - eventSourceTime) / speed),
        });
    }
    return fired;
}

void AnimationEventSequencer::stop(uint64_t instanceId)
{
    instances_.erase(instanceId);
}

void AnimationEventSequencer::clear()
{
    instances_.clear();
}

} // namespace sokoban
