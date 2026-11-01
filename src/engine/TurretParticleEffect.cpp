#include "engine/TurretParticleEffect.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/GameplayPresentation.hpp"
#include "engine/ParticleConfig.hpp"
#include "engine/TurretRayTrace.hpp"
#include "engine/render/IsoScenePreparer.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sokoban {
namespace {

Vec3 rayPoint(GridPosition3 cell, GridPosition edge, float elevation)
{
    return { static_cast<float>(cell.x) + 0.5f + static_cast<float>(edge.x) * 0.5f,
        static_cast<float>(cell.y) + 0.5f + static_cast<float>(edge.y) * 0.5f,
        static_cast<float>(cell.z) + elevation };
}

Vec3 muzzlePosition(const RenderFrameData& frame, EntityId turret,
    GridPosition3 origin, MoveDirection direction)
{
    const auto tile = std::ranges::find_if(frame.tiles, [&](const auto& visual) {
        return turret != invalidEntityId && visual.renderableId == turret;
    });
    if (tile != frame.tiles.end()) {
        // Midpoint of the two front barrel rims in Turret.glb, after the
        // loader's aspect-preserving normalization and authored half turn.
        constexpr Vec3 socket { 0.5001996f, 0.9979431f, 0.8496013f };
        const auto pose = IsoScenePreparer::modelTransformPoints(*tile);
        return pose.origin + (pose.xPoint - pose.origin) * socket.x +
            (pose.yPoint - pose.origin) * socket.y +
            (pose.zPoint - pose.origin) * socket.z;
    }
    // Synthetic fixtures and the final instant of a destroyed turret's
    // volley have no rendered model to attach to.
    const auto offset = rules::directionOffset(direction);
    Vec3 muzzle = rayPoint(origin, {}, config::turretMuzzleElevation);
    muzzle.x += static_cast<float>(offset.x) * config::turretMuzzleForwardOffset;
    muzzle.y += static_cast<float>(offset.y) * config::turretMuzzleForwardOffset;
    return muzzle;
}

std::pair<Vec3, Vec3> segmentPoints(const rules::TurretRaySegment& segment,
    Vec3 muzzle, float elevation, bool first, bool last)
{
    const Vec3 from = first ? muzzle : rayPoint(segment.from, segment.fromEdge, elevation);
    Vec3 to = rayPoint(segment.to, segment.toEdge, elevation);
    if (last && segment.toEdge == GridPosition {}) {
        to.z = static_cast<float>(segment.to.z) + config::turretTargetElevation;
    }
    return { from, to };
}

} // namespace

TurretParticleEffects makeTurretParticleEffects(const AssetManifest& manifest)
{
    TurretParticleEffects effects;
    effects.glowTexture = manifest.findTextureIdByName(config::turretGlowTextureName);
    return effects;
}

void TurretFlameSystem::update(const Level& level, const GameState& state, float dt,
    const GameplayPresentation* presentation)
{
    dt = std::max(dt, 0.0f);
    for (auto& stream : streams_) {
        stream.ageSeconds += dt;
        stream.shotHoldSeconds = std::max(stream.shotHoldSeconds - dt, 0.0f);
        stream.visible = false;
    }
    for (const auto& turret : state.movables) {
        const auto direction = rules::turretDirection(turret);
        if (!direction || turret.dead || turret.fallen) {
            continue;
        }
        // A pushed/carried barrel starts its new stream on arrival, matching
        // gameplay shot cues rather than firing from its old grid cell.
        if (presentation && std::ranges::any_of(presentation->movables(),
                [&](const auto& visual) {
                    return visual.target.id == turret.id && visual.moving;
                })) {
            continue;
        }
        bool targetFound = false;
        std::vector<rules::TurretRaySegment> segments;
        rules::traceTurretRay(turret.cell, *direction, std::nullopt,
            [&](GridPosition3 cell) {
                if (!rules::cellAllowsEntity(level, state, cell)) {
                    // Empty space beyond the board is not a target. Solid
                    // terrain and closed gates stop fire at their near face.
                    targetFound = level.inBounds(cell);
                    return rules::TurretRayCell::Blocked;
                }
                const bool player = std::ranges::any_of(state.players,
                    [&](const auto& actor) { return !actor.dead && !actor.drowned && actor.cell == cell; });
                if (rules::movableAt(state, cell) || rules::enemyAt(state, cell) || player) {
                    targetFound = true;
                    return rules::TurretRayCell::Occupied;
                }
                return rules::TurretRayCell::Open;
            },
            [&](GridPosition3 cell, MoveDirection ray) {
                return level.portalCrossing(cell, rules::directionOffset(ray));
            }, &segments);
        if (!targetFound) {
            continue;
        }
        auto found = std::ranges::find(streams_, turret.id, &Stream::turret);
        if (found == streams_.end()) {
            streams_.push_back({ .turret = turret.id, .origin = turret.cell,
                .direction = *direction, .segments = std::move(segments), .visible = true });
        } else {
            found->visible = true;
            if (found->shotHoldSeconds > 0.0f) {
                continue;
            }
            if (found->origin != turret.cell || found->direction != *direction ||
                found->segments != segments) {
                found->origin = turret.cell;
                found->direction = *direction;
                found->segments = std::move(segments);
                found->ageSeconds = 0.0f;
            }
        }
    }
    std::erase_if(streams_, [](const auto& stream) {
        return !stream.visible && stream.shotHoldSeconds <= 0.0f;
    });
}

void TurretFlameSystem::triggerShot(const rules::TurretShot& shot, float impactDelaySeconds)
{
    auto segments = shot.beamSegments;
    if (segments.empty()) {
        segments.push_back({ .from = shot.turretCell, .to = shot.targetCell });
    }
    auto found = std::ranges::find(streams_, shot.turret.id, &Stream::turret);
    if (found == streams_.end()) {
        streams_.push_back({ .turret = shot.turret.id });
        found = streams_.end() - 1;
    }
    if (found->origin != shot.turretCell || found->direction != shot.direction ||
        found->segments != segments) {
        found->ageSeconds = 0.0f;
    }
    found->origin = shot.turretCell;
    found->direction = shot.direction;
    found->segments = std::move(segments);
    found->shotHoldSeconds = std::max(impactDelaySeconds, 0.0f) +
        config::turretFlameGrowthSeconds + config::turretFlameAfterImpactSeconds;
}

void TurretFlameSystem::appendRenderData(RenderFrameData& frame,
    const TurretParticleEffects& effects) const
{
    for (const auto& stream : streams_) {
        const Vec3 muzzle = muzzlePosition(frame, stream.turret, stream.origin, stream.direction);
        const float elevation = muzzle.z - static_cast<float>(stream.origin.z);
        float totalLength = 0.0f;
        for (std::size_t index = 0; index < stream.segments.size(); ++index) {
            const auto [from, to] = segmentPoints(stream.segments[index], muzzle, elevation,
                index == 0, index + 1 == stream.segments.size());
            totalLength += length(to - from);
        }
        if (totalLength < 0.001f) {
            continue;
        }
        // One front advances over the whole path, including portal segments.
        // The original laser grew in 0.08 s, independent of distance.
        const float reach = totalLength * std::clamp(
            stream.ageSeconds / config::turretFlameGrowthSeconds, 0.0f, 1.0f);
        const float seed = static_cast<float>(stream.turret % 997U) * 0.73f;
        float traveled = 0.0f;
        for (std::size_t index = 0; index < stream.segments.size(); ++index) {
            const auto [from, to] = segmentPoints(stream.segments[index], muzzle, elevation,
                index == 0, index + 1 == stream.segments.size());
            const float distance = length(to - from);
            if (distance < 0.001f) {
                continue;
            }
            const Vec3 direction = (to - from) * (1.0f / distance);
            const float visibleLength = std::clamp(reach - traveled, 0.0f, distance);
            if (visibleLength > 0.0001f) {
                // One connected surface per segment. Its shader carries broad
                // fire folds downstream instead of flickering point sprites.
                frame.particles.push_back({
                    .position = from + direction * (visibleLength * 0.5f),
                    .size = { config::turretFlameWidth, visibleLength },
                    .billboardAlignment = direction,
                    .billboardAlignmentUsesY = true,
                    .color = { 1.0f, 1.0f, 1.0f, 1.0f },
                    .emissiveStrength = config::turretFlameEmissiveStrength,
                    .drawOnTop = config::turretParticlesDrawOnTop,
                    .flameStream = RenderFrameData::FlameStream {
                        .timeSeconds = stream.ageSeconds,
                        .pathOffset = traveled,
                        .pathLength = visibleLength,
                        .seed = seed,
                    },
                });
            }
            traveled += distance;
        }
        // Anchor the jet at the nozzle. The endpoint bloom starts on arrival.
        if (reach > 0.0f && !effects.glowTexture.isNone()) {
            const Vec3 target = segmentPoints(stream.segments.back(), muzzle, elevation,
                stream.segments.size() == 1, true).second;
            frame.particles.push_back({ .position = muzzle, .size = { 0.38f, 0.38f },
                .color = config::turretFlameCoreColor, .emissiveStrength = 2.0f,
                .texture = effects.glowTexture, .drawOnTop = config::turretParticlesDrawOnTop,
                .drawOrder = 2 });
            if (reach >= totalLength) {
                const float size = 0.48f + 0.08f * std::sin(stream.ageSeconds * 43.0f + seed);
                frame.particles.push_back({ .position = target, .size = { size, size },
                    .color = config::turretFlameCoreColor, .emissiveStrength = 2.4f,
                    .texture = effects.glowTexture, .drawOnTop = config::turretParticlesDrawOnTop,
                    .drawOrder = 2 });
            }
        }
    }
}

void TurretFlameSystem::reset()
{
    streams_.clear();
}

} // namespace sokoban
