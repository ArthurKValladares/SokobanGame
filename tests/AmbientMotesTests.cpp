// The motes in the air: a function of the time and of nothing else, kept to
// its box, faded at the faces of that box, and shown by the sun.

#include "TestHarness.hpp"

#include "engine/AmbientMotes.hpp"
#include "engine/AmbientMotesConfig.hpp"
#include "engine/render/SceneDrawLanes.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <vector>

using namespace sokoban;

namespace {

constexpr RenderTexture glow { 7 };

AmbientMoteField field()
{
    return {
        .minimum = { -4.0f, -3.0f, -1.0f },
        .maximum = { 12.0f, 10.0f, 4.0f },
        .density = 0.2f,
        .size = 0.08f,
        .color = { 1.0f, 0.9f, 0.8f },
        .opacity = 0.6f,
        .glow = 2.0f,
        .shadeVisibility = 0.1f,
        .driftVelocity = { 0.07f, 0.03f, 0.02f },
        .wanderRadius = 0.2f,
        .wanderSpeed = 0.5f,
        .edgeFade = 0.75f,
        .limit = 4000,
    };
}

// A cloud light with hard-edged clouds over half the sky, straight overhead.
CloudLightLanes clouds()
{
    RenderFrameData::Lighting lighting;
    lighting.sun.direction = { 0.0f, 0.0f, 1.0f };
    lighting.cloudLight = {
        .enabled = true,
        .surfaceStrength = 0.5f,
        .mediumStrength = 0.9f,
        .mediumGain = 1.0f,
        .scale = 0.2f,
        .coverage = 0.5f,
        .softness = 0.02f,
    };
    return cloudLightLanes(lighting, 0.0f, { 0.0f, 0.0f, -1.0f });
}

std::vector<RenderFrameData::Particle> motesAt(
    const AmbientMoteField& where, const CloudLightLanes& light, float time)
{
    RenderFrameData frame;
    appendAmbientMotes(frame, where, light, glow, time);
    return { frame.particles.begin(), frame.particles.end() };
}

void testTheSameMomentGivesTheSameMotes()
{
    const auto first = motesAt(field(), {}, 12.5f);
    const auto second = motesAt(field(), {}, 12.5f);
    CHECK(!first.empty());
    CHECK(first.size() == second.size());
    for (std::size_t i = 0; i < std::min(first.size(), second.size()); ++i) {
        CHECK(first[i].position.x == second[i].position.x);
        CHECK(first[i].position.y == second[i].position.y);
        CHECK(first[i].position.z == second[i].position.z);
        CHECK(first[i].color.w == second[i].color.w);
    }
}

void testMotesStayInTheirBoxAndFadeAtItsFaces()
{
    const AmbientMoteField where = field();
    for (const float time : { 0.0f, 37.0f, 1200.0f, 86400.0f }) {
        const auto motes = motesAt(where, {}, time);
        CHECK(!motes.empty());
        for (const auto& mote : motes) {
            CHECK(mote.position.x > where.minimum.x && mote.position.x < where.maximum.x);
            CHECK(mote.position.y > where.minimum.y && mote.position.y < where.maximum.y);
            CHECK(mote.position.z > where.minimum.z && mote.position.z < where.maximum.z);
            // Never brighter than the configured opacity, and within the
            // fade's reach of a face it is dimmer than the twinkle alone
            // could make it.
            CHECK(mote.color.w > 0.0f && mote.color.w <= where.opacity);
            const float toFace = std::min({
                mote.position.x - where.minimum.x, where.maximum.x - mote.position.x,
                mote.position.y - where.minimum.y, where.maximum.y - mote.position.y,
                mote.position.z - where.minimum.z, where.maximum.z - mote.position.z });
            if (toFace < where.edgeFade * 0.25f) {
                CHECK(mote.color.w < where.opacity * 0.2f);
            }
            CHECK(mote.size.x >= where.size * 0.5f && mote.size.x <= where.size);
            CHECK(mote.texture == glow);
            CHECK(!mote.drawOnTop);
        }
    }
}

void testTheNumberFollowsTheDensity()
{
    AmbientMoteField where = field();
    // 16 x 13 x 5 cells. The faces fade the outermost motes away, so count
    // against the share the hash should pick, with room for its scatter.
    const float cells = 16.0f * 13.0f * 5.0f;
    where.density = 0.2f;
    const float twenty = static_cast<float>(motesAt(where, {}, 3.0f).size());
    CHECK(twenty > cells * 0.2f * 0.6f && twenty < cells * 0.2f * 1.2f);
    where.density = 0.05f;
    const float five = static_cast<float>(motesAt(where, {}, 3.0f).size());
    CHECK(five > cells * 0.05f * 0.5f && five < cells * 0.05f * 1.3f);
    where.density = 0.0f;
    CHECK(motesAt(where, {}, 3.0f).empty());
}

void testNothingIsDrawnWithoutATextureAnOpacityOrABox()
{
    RenderFrameData frame;
    CHECK(appendAmbientMotes(frame, field(), {}, noTexture, 1.0f) == 0);
    AmbientMoteField clear = field();
    clear.opacity = 0.0f;
    CHECK(appendAmbientMotes(frame, clear, {}, glow, 1.0f) == 0);
    AmbientMoteField flat = field();
    flat.maximum.z = flat.minimum.z;
    CHECK(appendAmbientMotes(frame, flat, {}, glow, 1.0f) == 0);
    CHECK(frame.particles.empty());

    AmbientMoteField few = field();
    few.limit = 5;
    CHECK(appendAmbientMotes(frame, few, {}, glow, 1.0f) == 5);
    CHECK(frame.particles.size() == 5);
}

void testAMoteMovesSlowlyAndWithTheWind()
{
    // No wandering and no fade in reach: every mote of the middle moves by
    // exactly the drift.
    AmbientMoteField where = field();
    where.wanderRadius = 0.0f;
    where.minimum = { -40.0f, -40.0f, -40.0f };
    where.maximum = { 40.0f, 40.0f, 40.0f };
    where.density = 0.002f;
    const auto before = motesAt(where, {}, 100.0f);
    const auto after = motesAt(where, {}, 101.0f);
    CHECK(before.size() > 100);
    std::size_t matched = 0;
    for (const auto& mote : before) {
        const Vec3 expected {
            mote.position.x + where.driftVelocity.x,
            mote.position.y + where.driftVelocity.y,
            mote.position.z + where.driftVelocity.z,
        };
        matched += std::ranges::any_of(after, [&](const auto& candidate) {
            return std::abs(candidate.position.x - expected.x) < 1e-3f &&
                std::abs(candidate.position.y - expected.y) < 1e-3f &&
                std::abs(candidate.position.z - expected.z) < 1e-3f;
        }) ? 1 : 0;
    }
    // All but the few a face let in or out during that second.
    CHECK(matched * 100 >= before.size() * 97);
}

void testTheSunShowsTheMotes()
{
    // With no cloud light a mote is as bright as the box lets it be. With
    // clouds, the motes under them keep only the shade's share, and the
    // brightness of each agrees with the gap over its own head.
    const CloudLightLanes light = clouds();
    AmbientMoteField where = field();
    where.wanderRadius = 0.0f;
    const auto motes = motesAt(where, light, 5.0f);
    CHECK(!motes.empty());
    std::size_t lit = 0;
    std::size_t shaded = 0;
    for (const auto& mote : motes) {
        const float sun = cloudLightGap(light, cloudLightCoordinate(light, mote.position));
        const float expected = where.shadeVisibility + (1.0f - where.shadeVisibility) * sun;
        CHECK(std::abs(mote.emissiveStrength - where.glow * expected) < 1e-4f);
        lit += sun > 0.99f ? 1 : 0;
        shaded += sun < 0.01f ? 1 : 0;
    }
    CHECK(lit > motes.size() / 5);
    CHECK(shaded > motes.size() / 5);

    for (const auto& mote : motesAt(where, {}, 5.0f)) {
        CHECK(std::abs(mote.emissiveStrength - where.glow) < 1e-5f);
    }
}

void testTheCloudGapOnTheCpuIsAShareOfTheSky()
{
    // Off: open sky everywhere.
    CHECK(cloudLightGap({}, { 3.0f, -8.0f }) == 1.0f);

    // On: always a share between none and all, about half the sky open at
    // half coverage, and the same answer along one sun ray.
    const CloudLightLanes light = clouds();
    std::size_t open = 0;
    constexpr std::size_t samples = 4000;
    for (std::size_t i = 0; i < samples; ++i) {
        const Vec2 at {
            static_cast<float>(i % 80) * 1.37f - 50.0f,
            static_cast<float>(i / 80) * 1.91f - 40.0f,
        };
        const float gap = cloudLightGap(light, at);
        CHECK(gap >= 0.0f && gap <= 1.0f);
        open += gap > 0.5f ? 1 : 0;
    }
    CHECK(open > samples * 4 / 10 && open < samples * 6 / 10);

    RenderFrameData::Lighting slanted;
    slanted.sun.direction = { 0.4f, -0.3f, 0.8f };
    slanted.cloudLight = { .enabled = true, .surfaceStrength = 0.3f, .scale = 0.2f,
        .coverage = 0.5f, .softness = 0.2f };
    const CloudLightLanes lanes = cloudLightLanes(slanted, 9.0f, { 0.0f, 0.0f, -1.0f });
    const Vec3 low { 2.0f, 5.0f, 0.5f };
    const Vec3 high {
        low.x + 0.4f / 0.8f * 3.0f, low.y - 0.3f / 0.8f * 3.0f, low.z + 3.0f };
    CHECK(std::abs(
        cloudLightGap(lanes, cloudLightCoordinate(lanes, low)) -
        cloudLightGap(lanes, cloudLightCoordinate(lanes, high))) < 1e-3f);
}

void testTheTunablesMakeAFieldAroundTheBoard()
{
    RenderFrameData frame;
    frame.levelWidth = 9;
    frame.levelHeight = 7;
    frame.levelDepth = 3;
    const AmbientMoteField where = ambientMoteField(frame);
    CHECK(where.maximum.x - where.minimum.x >= 2.0f * config::ambientMotesReach);
    CHECK(where.maximum.y - where.minimum.y >= 2.0f * config::ambientMotesReach);
    CHECK(where.minimum.z == -config::ambientMotesDepth);
    CHECK(where.maximum.z == 3.0f + config::ambientMotesHeight);
    CHECK(where.density == config::ambientMotesDensity);
    CHECK(where.limit == config::ambientMotesLimit);
    // Slow: a mote takes many seconds to cross a tile.
    const float speed = std::sqrt(
        where.driftVelocity.x * where.driftVelocity.x +
        where.driftVelocity.y * where.driftVelocity.y);
    CHECK(std::abs(speed - config::ambientMotesDriftSpeed) < 1e-5f);
}

} // namespace

int main()
{
    testTheSameMomentGivesTheSameMotes();
    testMotesStayInTheirBoxAndFadeAtItsFaces();
    testTheNumberFollowsTheDensity();
    testNothingIsDrawnWithoutATextureAnOpacityOrABox();
    testAMoteMovesSlowlyAndWithTheWind();
    testTheSunShowsTheMotes();
    testTheCloudGapOnTheCpuIsAShareOfTheSky();
    testTheTunablesMakeAFieldAroundTheBoard();

    if (failures == 0) {
        std::cout << "AmbientMotesTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "AmbientMotesTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
