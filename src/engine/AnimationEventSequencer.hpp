#pragma once

#include "engine/AnimationCatalog.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace sokoban {

// Evaluates authored animation-use markers for concrete runtime instances.
// Logical clocks are presentation seconds before catalog speed multipliers;
// event thresholds are compared in source-clip time so speed tuning and event
// poses cannot drift apart.
class AnimationEventSequencer {
public:
    struct FiredEvent {
        uint64_t instanceId = 0;
        AnimationUse use = AnimationUse::PlayerIdle;
        std::string eventId;
        float overshootSeconds = 0.0f;
    };

    void begin(
        uint64_t instanceId,
        AnimationUse use,
        float logicalTimeSeconds = 0.0f);
    [[nodiscard]] std::vector<FiredEvent> advance(
        uint64_t instanceId,
        float logicalTimeSeconds,
        const AnimationCatalog& catalog);
    void stop(uint64_t instanceId);
    void clear();

private:
    struct Instance {
        AnimationUse use = AnimationUse::PlayerIdle;
        float logicalTimeSeconds = 0.0f;
        std::vector<std::string> firedEvents;
    };

    std::unordered_map<uint64_t, Instance> instances_;
};

} // namespace sokoban
