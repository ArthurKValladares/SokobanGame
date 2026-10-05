#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/EntityId.hpp"
#include "engine/Math.hpp"

#include <optional>
#include <vector>

namespace sokoban {

class Level;
class GameplayPresentation;
struct GameState;

// Pure proximity mixing, independent of the audio device. A sound set shares
// one voice across its tiles, using the nearest tile rather than adding gains.
// This bounds both voice count and volume on long or closely packed belts.
class AtmosphericAudio {
public:
    // Cache static emitters once when a screen/draft is loaded, including
    // plates authored beneath heroes or movable objects.
    void reset(const Level& level);
    [[nodiscard]] float gain(
        Vec3 listener, const AssetManifest::Atmosphere& settings) const;
    [[nodiscard]] static float distanceGain(
        float distanceTiles, const AssetManifest::Atmosphere& settings);
    // Follow the controlled original hero, or its surviving mirror copy.
    // Interpolated positions keep proximity changes continuous during motion.
    [[nodiscard]] static std::optional<Vec3> listenerPosition(
        const GameState& state, const GameplayPresentation& presentation,
        EntityId controller);

private:
    std::vector<Vec3> portals_;
    std::vector<Vec3> conveyors_;
};

} // namespace sokoban
