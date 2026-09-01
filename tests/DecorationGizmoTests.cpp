#include "TestHarness.hpp"

#include "engine/DecorationGizmo.hpp"

#include <cmath>
#include <iostream>

namespace {

sokoban::DecorationGizmo::Geometry geometry()
{
    sokoban::DecorationGizmo::Geometry result;
    result.origin = { 100.0f, 100.0f };
    result.axes = {
        sokoban::DecorationGizmo::AxisHandle {
            .start = result.origin, .end = { 200.0f, 100.0f },
            .worldLength = 2.0f },
        sokoban::DecorationGizmo::AxisHandle {
            .start = result.origin, .end = { 100.0f, 200.0f },
            .worldLength = 4.0f },
        sokoban::DecorationGizmo::AxisHandle {
            .start = result.origin, .end = { 30.0f, 30.0f },
            .worldLength = 1.0f },
    };
    result.rings[2] = {
        { 150.0f, 100.0f },
        { 145.0f, 120.0f },
        { 130.0f, 140.0f },
    };
    return result;
}

void testTranslateAndScaleUseProjectedAxis()
{
    sokoban::DecorationGizmo gizmo;
    sokoban::Level::Decoration decoration {
        .model = "Stone",
        .position = { 1.0f, 2.0f, 3.0f },
    };
    const auto projected = geometry();

    CHECK(gizmo.beginDrag(projected, { 170.0f, 102.0f }, decoration));
    const auto translated = gizmo.updateDrag({ 195.0f, 102.0f });
    CHECK(translated.has_value());
    CHECK(std::abs(translated->position.x - 1.5f) < 0.0001f);
    CHECK(translated->position.y == 2.0f);
    gizmo.endDrag();

    gizmo.setMode(sokoban::DecorationGizmo::Mode::Scale);
    CHECK(gizmo.beginDrag(projected, { 100.0f, 160.0f }, decoration));
    const auto scaled = gizmo.updateDrag({ 100.0f, 210.0f });
    CHECK(scaled.has_value());
    CHECK(std::abs(scaled->scale.y - 1.5f) < 0.0001f);
    gizmo.endDrag();
}

void testRotationUsesRingTangentAndMissesEmptySpace()
{
    sokoban::DecorationGizmo gizmo;
    gizmo.setMode(sokoban::DecorationGizmo::Mode::Rotate);
    sokoban::Level::Decoration decoration { .model = "Stone" };
    const auto projected = geometry();

    CHECK(!gizmo.beginDrag(projected, { 400.0f, 400.0f }, decoration));
    CHECK(gizmo.beginDrag(projected, { 147.0f, 111.0f }, decoration));
    const auto rotated = gizmo.updateDrag({ 137.0f, 131.0f });
    CHECK(rotated.has_value());
    CHECK(std::abs(rotated->rotationDegrees.z) > 1.0f);
    CHECK(rotated->rotationDegrees.x == 0.0f);
    CHECK(rotated->rotationDegrees.y == 0.0f);
}

} // namespace

int main()
{
    testTranslateAndScaleUseProjectedAxis();
    testRotationUsesRingTangentAndMissesEmptySpace();

    if (failures == 0) {
        std::cout << "DecorationGizmoTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "DecorationGizmoTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
