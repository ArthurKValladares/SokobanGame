#include "engine/AtmosphericAudio.hpp"

#include "engine/GameplayPresentation.hpp"
#include "engine/Level.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sokoban {
namespace {

Vec3 toVec3(GridPosition3 cell)
{
    return { static_cast<float>(cell.x), static_cast<float>(cell.y),
        static_cast<float>(cell.z) };
}

} // namespace

void AtmosphericAudio::reset(const Level& level)
{
    portals_.clear();
    conveyors_.clear();
    for (uint32_t z = 0; z < level.depth(); ++z) {
        for (uint32_t y = 0; y < level.height(); ++y) {
            for (uint32_t x = 0; x < level.width(); ++x) {
                const GridPosition3 cell {
                    static_cast<int>(x), static_cast<int>(y), static_cast<int>(z),
                };
                const auto plate = level.plateAt(cell);
                if (plate && tileTypeIsPortal(*plate)) {
                    portals_.push_back(toVec3(cell));
                }
                // Conveyors are terrain, not overlay plates. Riders occupy
                // the belt's grid cell while its authored tile stays static.
                if (tileTypeIsConveyor(level.tileAt(x, y, z))) {
                    conveyors_.push_back(toVec3(cell));
                }
            }
        }
    }
}

float AtmosphericAudio::distanceGain(
    float distanceTiles, const AssetManifest::Atmosphere& settings)
{
    if (!std::isfinite(distanceTiles) ||
        !std::isfinite(settings.audibleDistanceTiles) ||
        !std::isfinite(settings.fullVolumeDistanceTiles) ||
        !std::isfinite(settings.falloffExponent) ||
        !(settings.audibleDistanceTiles > settings.fullVolumeDistanceTiles) ||
        !(settings.fullVolumeDistanceTiles >= 0.0f) ||
        !(settings.falloffExponent > 0.0f)) {
        return 0.0f;
    }
    const float linear = std::clamp(
        (settings.audibleDistanceTiles - distanceTiles) /
            (settings.audibleDistanceTiles - settings.fullVolumeDistanceTiles),
        0.0f, 1.0f);
    return std::pow(linear, settings.falloffExponent);
}

float AtmosphericAudio::gain(
    Vec3 listener, const AssetManifest::Atmosphere& settings) const
{
    const auto& sources = settings.source == AssetManifest::AtmosphericSource::Portal
        ? portals_ : conveyors_;
    float nearestSquared = std::numeric_limits<float>::infinity();
    for (const auto source : sources) {
        const Vec3 offset = source - listener;
        nearestSquared = std::min(nearestSquared, dot(offset, offset));
    }
    return distanceGain(std::sqrt(nearestSquared), settings);
}

std::optional<Vec3> AtmosphericAudio::listenerPosition(
    const GameState& state, const GameplayPresentation& presentation,
    EntityId controller)
{
    // Prefer the original's id; choosing a different copy every frame would
    // make ambience jump between widely separated mirror destinations.
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t index = 0; index < state.players.size(); ++index) {
            const auto& player = state.players[index];
            const EntityId id = resolvedEntityId(EntityKind::Player, player.id, index);
            if (player.dead || (controller != invalidEntityId &&
                (pass == 0 ? id != controller : player.controller != controller))) {
                continue;
            }
            const auto visual = std::ranges::find_if(presentation.players(),
                [id](const auto& candidate) { return candidate.motion.target.id == id; });
            return visual == presentation.players().end()
                ? toVec3(player.cell) : visual->motion.renderPosition;
        }
    }
    return std::nullopt;
}

} // namespace sokoban
