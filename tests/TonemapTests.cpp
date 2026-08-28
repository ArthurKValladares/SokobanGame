#include "engine/render/Tonemap.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {

int failures = 0;
int checks = 0;

void checkImpl(bool ok, const char* expression, int line)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)

bool near(float left, float right)
{
    return std::abs(left - right) < 0.0001f;
}

void testExposureUsesPhotographicStops()
{
    CHECK(near(sokoban::exposureMultiplier(0.0f), 1.0f));
    CHECK(near(sokoban::exposureMultiplier(1.0f), 2.0f));
    CHECK(near(sokoban::exposureMultiplier(-1.0f), 0.5f));
    CHECK(sokoban::normalizedExposureEv(-99.0f) ==
        sokoban::minimumExposureEv);
    CHECK(sokoban::normalizedExposureEv(99.0f) ==
        sokoban::maximumExposureEv);
    CHECK(sokoban::normalizedExposureEv(
        std::numeric_limits<float>::quiet_NaN()) ==
        sokoban::defaultExposureEv);
}

void testClampComparisonMatchesLegacyOutput()
{
    const sokoban::Vec3 color = sokoban::applyOutputTransform(
        { 2.0f, 0.5f, -1.0f },
        0.0f,
        sokoban::TonemapCurve::Clamp);
    CHECK(near(color.x, 1.0f));
    CHECK(near(color.y, 0.5f));
    CHECK(near(color.z, 0.0f));
}

void testPbrNeutralPreservesBaseColorAndCompressesHighlights()
{
    // Below the highlight-compression threshold the reference curve removes
    // the common 0.04 dielectric reflection offset without shifting hue.
    const sokoban::Vec3 base = sokoban::applyOutputTransform(
        { 0.54f, 0.24f, 0.14f },
        0.0f,
        sokoban::TonemapCurve::PbrNeutral);
    CHECK(near(base.x, 0.50f));
    CHECK(near(base.y, 0.20f));
    CHECK(near(base.z, 0.10f));

    const sokoban::Vec3 highlight = sokoban::applyOutputTransform(
        { 8.0f, 3.0f, 1.0f },
        0.0f,
        sokoban::TonemapCurve::PbrNeutral);
    CHECK(highlight.x <= 1.0f && highlight.x > highlight.y);
    CHECK(highlight.y > highlight.z && highlight.z >= 0.0f);

    const sokoban::Vec3 neutral = sokoban::applyOutputTransform(
        { 4.0f, 4.0f, 4.0f },
        0.0f,
        sokoban::TonemapCurve::PbrNeutral);
    CHECK(near(neutral.x, neutral.y));
    CHECK(near(neutral.y, neutral.z));
    CHECK(neutral.x < 1.0f);
}

} // namespace

int main()
{
    testExposureUsesPhotographicStops();
    testClampComparisonMatchesLegacyOutput();
    testPbrNeutralPreservesBaseColorAndCompressesHighlights();

    if (failures == 0) {
        std::cout << "TonemapTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "TonemapTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
