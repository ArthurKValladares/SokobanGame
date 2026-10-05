#include "TestHarness.hpp"

#include "engine/render/WaterCellCachePlan.hpp"

#include <iostream>
#include <limits>

namespace {
using namespace sokoban;

RenderFrameData waterFrame()
{
    RenderFrameData frame;
    frame.waterSurfaces.push_back({ });
    frame.waterGridBounds = { -7, -5, 14, 10 };
    frame.waterRendering.rippleSpatialFrequency = 1.5f;
    frame.waterRendering.rippleSpeed = 1.15f;
    return frame;
}

void testWindowsFollowBothDomains()
{
    TEST("windowsFollowBothDomains");
    auto frame = waterFrame();
    frame.waterGridBounds = { -30, -20, 14, 10 };
    frame.waterAnimationTimeSeconds = 300.0f;
    const auto plan = waterCellCachePlan(frame);
    CHECK(plan.dimensions[3] == 1);
    const float time = 300.0f * 1.15f;
    const Vec2 primary { -23.0f * 1.5f + time * 0.10f,
                         -15.0f * 1.5f - time * 0.075f };
    const Vec2 secondary { -primary.y + 2.31f, primary.x - 1.73f };
    const std::array<Vec2, 2> centers { primary, secondary };
    for (std::size_t pattern = 0; pattern < centers.size(); ++pattern) {
        const float row = centers[pattern].y * 1.1547005f;
        const std::array<float, 2> lattice { centers[pattern].x - row * 0.5f,
                                             row };
        for (std::size_t axis = 0; axis < lattice.size(); ++axis) {
            const float local = lattice[axis] -
                static_cast<float>(plan.origins[pattern * 2 + axis]);
            CHECK(local >= 32.0f);
            CHECK(local < 96.0f);
            CHECK(plan.origins[pattern * 2 + axis] % 32 == 0);
        }
    }
}

void testRebuildsAreAmortized()
{
    TEST("rebuildsAreAmortized");
    auto frame = waterFrame();
    frame.waterAnimationTimeSeconds = 100.0f;
    const auto initial = waterCellCachePlan(frame);
    frame.waterAnimationTimeSeconds += 1.0f / 60.0f;
    CHECK(waterCellCachePlan(frame) == initial);
    frame.waterAnimationTimeSeconds += 1000.0f;
    CHECK(waterCellCachePlan(frame) != initial);
    frame = waterFrame();
    frame.waterGridBounds.originX = 100;
    const auto moved = waterCellCachePlan(frame);
    frame.waterRendering.rippleSpatialFrequency = 6.0f;
    CHECK(waterCellCachePlan(frame) != moved);
}

void testFallbackForDisabledOrExtremeData()
{
    TEST("fallbackForDisabledOrExtremeData");
    auto frame = waterFrame();
    CHECK(waterCellCachePlan(frame, false).dimensions[3] == 0);
    frame.waterSurfaces.clear();
    CHECK(waterCellCachePlan(frame).dimensions[3] == 0);
    frame = waterFrame();
    frame.waterAnimationTimeSeconds = std::numeric_limits<float>::infinity();
    CHECK(waterCellCachePlan(frame).dimensions[3] == 0);
    frame.waterAnimationTimeSeconds = std::numeric_limits<float>::quiet_NaN();
    CHECK(waterCellCachePlan(frame).dimensions[3] == 0);
    frame = waterFrame();
    frame.waterGridBounds.originX = std::numeric_limits<int32_t>::min();
    CHECK(waterCellCachePlan(frame).dimensions[3] == 0);
    frame = waterFrame();
    frame.waterRendering.rippleSpatialFrequency = -1.0f;
    CHECK(waterCellCachePlan(frame).dimensions[3] == 1);
}
} // namespace

int main()
{
    testWindowsFollowBothDomains();
    testRebuildsAreAmortized();
    testFallbackForDisabledOrExtremeData();
    std::cout << "water_cell_cache: " << checks << " checks, " << failures
              << " failures\n";
    return failures == 0 ? 0 : 1;
}
