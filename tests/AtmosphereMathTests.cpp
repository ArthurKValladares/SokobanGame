#include "TestHarness.hpp"

#include "engine/render/AtmosphereMath.hpp"

#include <cmath>
#include <iostream>

namespace {

using namespace sokoban;

bool near(float left, float right, float epsilon = 0.0001f)
{
    return std::abs(left - right) <= epsilon;
}

void testTransmittanceUsesWorldDistance()
{
    TEST("atmosphereTransmittanceUsesWorldDistance");
    CHECK(near(atmosphereTransmittance(0.0f, 50.0f), 1.0f));
    CHECK(near(atmosphereTransmittance(0.02f, 50.0f), std::exp(-1.0f)));
    CHECK(near(atmosphereTransmittance(-1.0f, 10.0f), 1.0f));
    CHECK(near(atmosphereTransmittance(1.0f, -10.0f), 1.0f));
}

void testPhaseUsesForwardScatteringConvention()
{
    TEST("atmospherePhaseUsesForwardScatteringConvention");
    CHECK(near(atmospherePhase(-0.7f, 0.0f), 1.0f));
    CHECK(near(atmospherePhase(0.7f, 0.0f), 1.0f));
    CHECK(atmospherePhase(1.0f, 0.5f) > atmospherePhase(0.0f, 0.5f));
    CHECK(atmospherePhase(0.0f, 0.5f) > atmospherePhase(-1.0f, 0.5f));
}

void testHeightFalloffKeepsLowFogDense()
{
    TEST("atmosphereHeightFalloffKeepsLowFogDense");
    CHECK(near(atmosphereDensityAtHeight(0.1f, 0.5f, 2.0f, 1.0f), 0.1f));
    CHECK(near(atmosphereDensityAtHeight(0.1f, 0.5f, 2.0f, 4.0f),
        0.1f * std::exp(-1.0f)));
    CHECK(near(atmosphereDensityAtHeight(-1.0f, 0.5f, 0.0f, 10.0f), 0.0f));
}

void testDepthFalloffThinsAMediumBelowItsBase()
{
    TEST("atmosphereDepthFalloffThinsAMediumBelowItsBase");
    // Without a depth falloff the medium keeps its base density all the way
    // down, which is what the atmosphere and the fog of war rely on.
    CHECK(near(atmosphereDensityAtHeight(0.1f, 0.5f, 2.0f, -6.0f), 0.1f));
    CHECK(near(atmosphereDensityAtHeight(0.1f, 0.5f, 2.0f, -6.0f, 0.0f), 0.1f));
    // The island mist hangs from its base and thins downward.
    CHECK(near(atmosphereDensityAtHeight(0.1f, 0.0f, 2.0f, 2.0f, 0.25f), 0.1f));
    CHECK(near(atmosphereDensityAtHeight(0.1f, 0.0f, 2.0f, 0.0f, 0.25f),
        0.1f * std::exp(-0.5f)));
    // Each falloff applies on its own side of the base height only.
    CHECK(near(atmosphereDensityAtHeight(0.1f, 0.5f, 2.0f, 4.0f, 9.0f),
        0.1f * std::exp(-1.0f)));
    CHECK(near(atmosphereDensityAtHeight(0.1f, 9.0f, 2.0f, 0.0f, 0.25f),
        0.1f * std::exp(-0.5f)));
    CHECK(near(atmosphereDensityAtHeight(0.1f, 0.0f, 2.0f, 0.0f, -3.0f), 0.1f));
}

void testIslandMistHangsBelowAndAroundTheLevel()
{
    TEST("islandMistHangsBelowAndAroundTheLevel");
    const AtmosphereVolumeBounds bounds = islandMistBounds(
        { 9.0f, -7.0f }, { 17.0f, -1.0f }, 0.5f, 9.0f, 10.0f);
    // Full density under the framed rectangle, fading out over `reach`
    // beyond it: the box's sides are where the density reaches zero.
    CHECK(near(bounds.minimum.x, -1.0f));
    CHECK(near(bounds.minimum.y, -17.0f));
    CHECK(near(bounds.maximum.x, 27.0f));
    CHECK(near(bounds.maximum.y, 9.0f));
    // Nothing above the top height, so nothing on the board is inside it.
    CHECK(near(bounds.maximum.z, 0.5f));
    CHECK(near(bounds.minimum.z, -8.5f));

    // Corners given the wrong way round, and negative sizes, stay a box.
    const AtmosphereVolumeBounds degenerate = islandMistBounds(
        { 4.0f, 3.0f }, { 1.0f, 3.0f }, 0.0f, -1.0f, -1.0f);
    CHECK(near(degenerate.minimum.x, 1.0f));
    CHECK(near(degenerate.maximum.x, 4.0f));
    CHECK(near(degenerate.minimum.y, 3.0f));
    CHECK(near(degenerate.maximum.y, 3.0f));
    CHECK(near(degenerate.minimum.z, 0.0f));
    CHECK(near(degenerate.maximum.z, 0.0f));
}

void testReconstructionWeightsCancelTheIntegrationDither()
{
    TEST("atmosphereReconstructionWeightsCancelTheIntegrationDither");
    for (int step = 0; step <= 16; ++step) {
        const float fraction = static_cast<float>(step) / 16.0f;
        const AtmosphereReconstructionWeights weights =
            atmosphereReconstructionWeights(fraction);
        CHECK(weights.first >= 0.0f && weights.second >= 0.0f &&
            weights.third >= 0.0f);
        CHECK(near(weights.first + weights.second + weights.third, 1.0f));
        // The first and third texels share a parity and the second has the
        // other. Half the weight on each is what cancels a dither that
        // alternates from texel to texel, wherever the output pixel falls.
        CHECK(near(weights.first + weights.third, weights.second));
        // The weighted position follows the pixel, so the filter does not
        // shift the medium: half a texel before the middle texel's centre
        // at fraction zero, half a texel after it at fraction one.
        CHECK(near(weights.third - weights.first, fraction - 0.5f));
    }
    // A texel-to-texel alternation of any size reconstructs to its mean.
    const AtmosphereReconstructionWeights weights =
        atmosphereReconstructionWeights(0.3f);
    CHECK(near(
        weights.first * 7.0f + weights.second * 1.0f + weights.third * 7.0f,
        4.0f));
    CHECK(near(atmosphereReconstructionWeights(-2.0f).first, 0.5f));
    CHECK(near(atmosphereReconstructionWeights(3.0f).third, 0.5f));
}

void testVolumeProjectionProducesConservativePixelBounds()
{
    TEST("volumeProjectionProducesConservativePixelBounds");
    const Mat4 identity = mat4Identity;
    const auto center = projectAtmosphereVolumeToScreen(
        identity,
        { -0.5f, -0.5f, 0.5f },
        { 0.5f, 0.5f, 0.8f },
        100,
        80,
        2);
    CHECK(center.has_value());
    if (!center) {
        return;
    }
    CHECK(center->x == 23);
    CHECK(center->y == 18);
    CHECK(center->width == 54);
    CHECK(center->height == 44);

    const auto outside = projectAtmosphereVolumeToScreen(
        identity,
        { 2.0f, -0.5f, 0.5f },
        { 3.0f, 0.5f, 0.8f },
        100,
        80,
        0);
    CHECK(!outside.has_value());
}

void testVolumeProjectionFallsBackAtNearPlane()
{
    TEST("volumeProjectionFallsBackAtNearPlane");
    const auto bounds = projectAtmosphereVolumeToScreen(
        mat4Identity,
        { -0.2f, -0.2f, -0.1f },
        { 0.2f, 0.2f, 0.5f },
        1920,
        1080);
    CHECK(bounds.has_value());
    if (!bounds) {
        return;
    }
    CHECK(bounds->x == 0);
    CHECK(bounds->y == 0);
    CHECK(bounds->width == 1920);
    CHECK(bounds->height == 1080);
}

} // namespace

int main()
{
    testTransmittanceUsesWorldDistance();
    testPhaseUsesForwardScatteringConvention();
    testHeightFalloffKeepsLowFogDense();
    testDepthFalloffThinsAMediumBelowItsBase();
    testIslandMistHangsBelowAndAroundTheLevel();
    testReconstructionWeightsCancelTheIntegrationDither();
    testVolumeProjectionProducesConservativePixelBounds();
    testVolumeProjectionFallsBackAtNearPlane();

    if (failures == 0) {
        std::cout << "AtmosphereMathTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "AtmosphereMathTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
