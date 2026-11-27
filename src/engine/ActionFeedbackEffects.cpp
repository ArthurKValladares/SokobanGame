#include "engine/ActionFeedbackEffects.hpp"

#include "engine/ActionFeedbackConfig.hpp"
#include "engine/AssetManifest.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sokoban {
namespace {

// How far above the ground a puff starts. A billboard leans back with the
// camera, and one that starts on the ground has its lower edge under it.
constexpr float dustLift = 0.1f;

// The box of velocities whose speed along `heading` lies in `along`, to its
// right in `across`, and upward in `up`. The heading is one of the four grid
// directions, so its right is one too, and a range of speeds along either is
// a range along one world axis.
struct VelocityBox {
    Vec3 minimum {};
    Vec3 maximum {};
};

VelocityBox velocityBox(Vec2 heading, Vec2 along, Vec2 across, Vec2 up)
{
    const Vec2 right { heading.y, -heading.x };
    const auto low = [](float axis, Vec2 range) {
        return std::min(axis * range.x, axis * range.y);
    };
    const auto high = [](float axis, Vec2 range) {
        return std::max(axis * range.x, axis * range.y);
    };
    return {
        .minimum = { low(heading.x, along) + low(right.x, across),
            low(heading.y, along) + low(right.y, across), up.x },
        .maximum = { high(heading.x, along) + high(right.x, across),
            high(heading.y, along) + high(right.y, across), up.y },
    };
}

} // namespace

ActionFeedbackEffects::ActionFeedbackEffects(const AssetManifest& manifest)
{
    // Dust is something the game can do without: a manifest draws it with
    // the sprites it declares, and with none of them draws none.
    textures_.reserve(config::actionFeedbackDustTextureNames.size());
    for (std::string_view name : config::actionFeedbackDustTextureNames) {
        if (const RenderTexture texture = manifest.findTextureIdByName(name);
            !texture.isNone()) {
            textures_.push_back(texture);
        }
    }
}

void ActionFeedbackEffects::puff(
    ParticleSystem& particles,
    Vec3 origin,
    uint32_t count,
    Vec3 minimumVelocity,
    Vec3 maximumVelocity,
    Vec2 initialSize,
    Vec2 finalSize,
    float lifetimeShare,
    float opacityShare) const
{
    const float size = config::actionFeedbackDustSize;
    const float seconds = config::actionFeedbackDustSeconds * lifetimeShare;
    particles.emit(origin, {
        .textures = textures_,
        .color = {
            config::actionFeedbackDustColor.x,
            config::actionFeedbackDustColor.y,
            config::actionFeedbackDustColor.z,
            std::clamp(config::actionFeedbackDustOpacity * opacityShare, 0.0f, 1.0f),
        },
        .particleCount = count,
        .lifetimeSeconds = { seconds * 0.65f, seconds },
        .initialSize = { initialSize.x * size, initialSize.y * size },
        .finalSize = { finalSize.x * size, finalSize.y * size },
        .spawnRadius = 0.05f,
        .minimumVelocity = minimumVelocity,
        .maximumVelocity = maximumVelocity,
        .minimumAngularVelocity = -1.2f,
        .maximumAngularVelocity = 1.2f,
    });
}

void ActionFeedbackEffects::emit(
    ParticleSystem& particles, const GameplayFeedbackCue& cue) const
{
    if (!config::actionFeedbackEnabled || textures_.empty()) {
        return;
    }
    const float strength = std::clamp(cue.strength, 0.0f, 1.0f);
    const Vec2 heading = cue.direction;
    const Vec2 right { heading.y, -heading.x };
    const Vec3 ground { cue.position.x, cue.position.y, cue.position.z + dustLift };
    switch (cue.kind) {
    case GameplayFeedback::PushOff:
        // From under the stone's two sides, toward its rear, back the way it
        // came and out to the side: the hero is right behind it and would
        // hide a puff there. Three along each side, the rearmost the
        // largest, so that the stone leaves a short trail as it pulls away
        // and not one blot.
        for (const float side : { -1.0f, 1.0f }) {
            const VelocityBox box = velocityBox(heading, { -0.55f, -0.2f },
                side < 0.0f ? Vec2 { -0.5f, -0.15f } : Vec2 { 0.15f, 0.5f },
                { 0.15f, 0.4f });
            for (const Vec2 puffAt : { Vec2 { 0.38f, 1.0f }, Vec2 { 0.2f, 0.84f },
                     Vec2 { 0.02f, 0.68f } }) {
                const float back = puffAt.x;
                const float grown = puffAt.y;
                puff(particles,
                    { ground.x + right.x * side * 0.44f - heading.x * back,
                      ground.y + right.y * side * 0.44f - heading.y * back, ground.z },
                    1, box.minimum, box.maximum,
                    { 0.2f * grown, 0.28f * grown }, { 0.44f * grown, 0.58f * grown },
                    0.8f, 1.0f);
            }
        }
        break;
    case GameplayFeedback::Landing: {
        // A ring that runs out from under the unit, more of it and faster
        // the harder it came down. Where around the ring the first puff
        // sits comes from where the unit landed, so that two landings side
        // by side are not copies of each other.
        const uint32_t count = 6U + static_cast<uint32_t>(std::lround(5.0f * strength));
        const float turn = cue.position.x * 2.399f + cue.position.y * 3.713f;
        const float phase = turn - std::floor(turn);
        const float speed = 0.4f + 0.45f * strength;
        const float grown = 0.6f + 0.4f * strength;
        // It starts at the unit's edge, not under it: a puff inside a stone
        // is a puff nobody sees. A hero is the lighter and the narrower.
        const float edge = 0.18f + 0.3f * strength;
        for (uint32_t index = 0; index < count; ++index) {
            const float angle = (static_cast<float>(index) + phase) /
                static_cast<float>(count) * 2.0f * std::numbers::pi_v<float>;
            const Vec2 out { std::cos(angle), std::sin(angle) };
            puff(particles,
                { ground.x + out.x * edge, ground.y + out.y * edge, ground.z },
                1,
                { std::min(out.x * speed * 0.7f, out.x * speed),
                  std::min(out.y * speed * 0.7f, out.y * speed), 0.1f },
                { std::max(out.x * speed * 0.7f, out.x * speed),
                  std::max(out.y * speed * 0.7f, out.y * speed), 0.3f },
                { 0.22f * grown, 0.3f * grown }, { 0.46f * grown, 0.64f * grown },
                1.0f, 1.0f);
        }
        break;
    }
    case GameplayFeedback::Bump:
        // Small, at the foot of whatever was in the way, and off to both
        // sides of it.
        for (const float side : { -1.0f, 1.0f }) {
            const VelocityBox box = velocityBox(heading, { -0.3f, -0.05f },
                side < 0.0f ? Vec2 { -0.4f, -0.12f } : Vec2 { 0.12f, 0.4f },
                { 0.2f, 0.45f });
            puff(particles,
                { ground.x + right.x * side * 0.12f - heading.x * 0.06f,
                  ground.y + right.y * side * 0.12f - heading.y * 0.06f, ground.z },
                2, box.minimum, box.maximum,
                { 0.1f, 0.15f }, { 0.22f, 0.3f }, 0.6f, 0.8f);
        }
        break;
    }
}

} // namespace sokoban
