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
