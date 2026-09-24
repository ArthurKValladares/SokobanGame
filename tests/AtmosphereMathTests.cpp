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

} // namespace

int main()
{
    testTransmittanceUsesWorldDistance();
    testPhaseUsesForwardScatteringConvention();
    testHeightFalloffKeepsLowFogDense();

    if (failures == 0) {
        std::cout << "AtmosphereMathTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "AtmosphereMathTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
