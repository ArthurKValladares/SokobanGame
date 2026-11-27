#include "engine/AmbientMotes.hpp"

#include "engine/AmbientMotesConfig.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/ParticleConfig.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/render/CloudLightConfig.hpp"
#include "engine/render/IsoScenePreparer.hpp"

#include <algorithm>
#include <cmath>

namespace sokoban {

namespace {

// The lattice cell is hashed as integers, so a cell names the same mote on
// every machine and at every time.
uint32_t moteHash(int32_t x, int32_t y, int32_t z, uint32_t salt)
{
    uint32_t h = static_cast<uint32_t>(x) * 0x8da6b343u;
    h ^= static_cast<uint32_t>(y) * 0xd8163841u;
    h ^= static_cast<uint32_t>(z) * 0xcb1ab31fu;
    h ^= salt * 0x9e3779b9u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

// Zero up to, not including, one.
float unitFloat(uint32_t bits)
{
    return static_cast<float>(bits >> 8) * (1.0f / 16777216.0f);
}

float smoothStep(float edge0, float edge1, float value)
{
    if (edge1 <= edge0) {
        return value < edge0 ? 0.0f : 1.0f;
    }
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// One at the middle of the box, falling to zero at each face.
float insideFade(float position, float minimum, float maximum, float fade)
{
    return smoothStep(0.0f, fade, std::min(position - minimum, maximum - position));
}

} // namespace

AmbientMoteField ambientMoteField(const RenderFrameData& frame)
{
    const FramedBoardArea board = framedBoardArea(frame);
    const float reach = std::max(config::ambientMotesReach, 0.0f);
    const float windRadians = degreesToRadians(config::cloudLightDriftDirectionDegrees);
    const float drift = std::max(config::ambientMotesDriftSpeed, 0.0f);
    return {
        .minimum = {
            board.minimum.x - reach,
            board.minimum.y - reach,
            -std::max(config::ambientMotesDepth, 0.0f),
        },
        .maximum = {
            board.maximum.x + reach,
            board.maximum.y + reach,
            static_cast<float>(frame.levelDepth) +
                std::max(config::ambientMotesHeight, 0.0f),
        },
        .density = std::clamp(config::ambientMotesDensity, 0.0f, 1.0f),
        .size = std::max(config::ambientMotesSize, 0.0f),
        .color = config::ambientMotesColor,
        .opacity = std::clamp(config::ambientMotesOpacity, 0.0f, 1.0f),
        .glow = std::max(config::ambientMotesGlow, 0.0f),
        .shadeVisibility = std::clamp(config::ambientMotesShadeVisibility, 0.0f, 1.0f),
        .driftVelocity = {
            std::cos(windRadians) * drift,
            std::sin(windRadians) * drift,
            config::ambientMotesRiseSpeed,
        },
        .wanderRadius = std::max(config::ambientMotesWanderRadius, 0.0f),
        .wanderSpeed = std::max(config::ambientMotesWanderSpeed, 0.0f),
        .edgeFade = config::ambientMotesEdgeFade,
        .limit = config::ambientMotesLimit,
    };
}

uint32_t appendAmbientMotes(
    RenderFrameData& frame,
    const AmbientMoteField& field,
    const CloudLightLanes& clouds,
    RenderTexture texture,
    float timeSeconds)
{
    if (texture.isNone() || field.density <= 0.0f || field.opacity <= 0.0f ||
        field.size <= 0.0f || field.limit == 0 ||
        field.maximum.x <= field.minimum.x || field.maximum.y <= field.minimum.y ||
        field.maximum.z <= field.minimum.z) {
        return 0;
    }

    // The lattice rides the wind. A cell that the drift has carried into the
    // box brings its mote with it, and the fade at the faces hides the moment
    // it crosses.
    const Vec3 carried {
        field.driftVelocity.x * timeSeconds,
        field.driftVelocity.y * timeSeconds,
        field.driftVelocity.z * timeSeconds,
    };
    const auto firstCell = [](float minimum, float offset) {
        return static_cast<int32_t>(std::floor(minimum - offset)) - 1;
    };
    const auto lastCell = [](float maximum, float offset) {
        return static_cast<int32_t>(std::floor(maximum - offset)) + 1;
    };
    constexpr float twoPi = 6.28318530718f;
    uint32_t appended = 0;
    for (int32_t z = firstCell(field.minimum.z, carried.z);
         z <= lastCell(field.maximum.z, carried.z); ++z) {
        for (int32_t y = firstCell(field.minimum.y, carried.y);
             y <= lastCell(field.maximum.y, carried.y); ++y) {
            for (int32_t x = firstCell(field.minimum.x, carried.x);
                 x <= lastCell(field.maximum.x, carried.x); ++x) {
                if (unitFloat(moteHash(x, y, z, 0)) >= field.density) {
                    continue;
                }
                const auto random = [&](uint32_t salt) {
                    return unitFloat(moteHash(x, y, z, salt));
                };
                // Each axis wanders at its own pace, so no two motes trace
                // the same loop.
                const auto wander = [&](uint32_t salt) {
                    const float pace = 0.6f + 0.8f * random(salt);
                    return field.wanderRadius * std::sin(
                        timeSeconds * field.wanderSpeed * pace +
                        twoPi * random(salt + 1));
                };
                const Vec3 position {
                    static_cast<float>(x) + random(1) + carried.x + wander(10),
                    static_cast<float>(y) + random(2) + carried.y + wander(12),
                    static_cast<float>(z) + random(3) + carried.z + wander(14),
                };
                const float inside =
                    insideFade(position.x, field.minimum.x, field.maximum.x, field.edgeFade) *
                    insideFade(position.y, field.minimum.y, field.maximum.y, field.edgeFade) *
                    insideFade(position.z, field.minimum.z, field.maximum.z, field.edgeFade);
                if (inside <= 0.0f) {
                    continue;
                }
                // Dust shows where the light catches it.
                const float sun = cloudLightGap(
                    clouds, cloudLightCoordinate(clouds, position));
                const float light =
                    field.shadeVisibility + (1.0f - field.shadeVisibility) * sun;
                const float twinkle = 0.7f + 0.3f * std::sin(
                    timeSeconds * (0.7f + 1.1f * random(4)) + twoPi * random(5));
                const float alpha = field.opacity * inside * light * twinkle;
                if (alpha < 0.01f) {
                    continue;
                }
                const float size = field.size * (0.5f + 0.5f * random(6));
                frame.particles.push_back({
                    .position = position,
                    .size = { size, size },
                    .color = { field.color.x, field.color.y, field.color.z, alpha },
                    .emissiveStrength = field.glow * light,
                    .texture = texture,
                    // A mote is its soft disc: as a white square it is
                    // plainly a fault, so it waits.
                    .waitsForTexture = true,
                });
                if (++appended >= field.limit) {
                    return appended;
                }
            }
        }
    }
    return appended;
}

uint32_t appendAmbientLife(
    RenderFrameData& frame,
    const AssetManifest& manifest,
    const PresentationSettings& settings)
{
    if (!settings.lighting.ambientMotesEnabled) {
        return 0;
    }
    return appendAmbientMotes(
        frame,
        ambientMoteField(frame),
        cloudLightLanes(
            frame.lighting, frame.effectAnimationTimeSeconds, { 0.0f, 0.0f, -1.0f }),
        manifest.findTextureIdByName(config::turretGlowTextureName),
        frame.effectAnimationTimeSeconds);
}

} // namespace sokoban
