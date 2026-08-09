#include "engine/EditorInteraction.hpp"

#include <cmath>
#include <iostream>

namespace {

int failures = 0;
int checks = 0;

void checkImpl(bool condition, const char* expression, int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)

bool near(float left, float right, float tolerance = 0.001f)
{
    return std::abs(left - right) <= tolerance;
}

void testBrushPreviewTopologyAndCoverage()
{
    sokoban::SplatCanvas::Brush brush;
    brush.radiusTiles = 3.0f;
    brush.hardness = 0.5f;
    brush.opacity = 0.8f;
    float projectedElevation = -1.0f;
    const auto preview = sokoban::EditorInteraction::brushPreview(
        brush,
        { 10.0f, 20.0f, 4.25f },
        [&](sokoban::Vec3 world) -> std::optional<sokoban::Vec2> {
            projectedElevation = world.z;
            return sokoban::Vec2 { world.x * 10.0f, world.y * 10.0f };
        });

    CHECK(preview.vertices.size() == 13 * 48);
    CHECK(preview.indices.size() == 12 * 48 * 6);
    CHECK(preview.rim.size() == 48);
    CHECK(near(projectedElevation, 4.25f));
    CHECK(near(preview.vertices.front().position.x, 100.0f));
    CHECK(near(preview.vertices.front().position.y, 200.0f));
    CHECK(preview.vertices.front().opacity >
        preview.vertices.back().opacity);

    for (std::uint32_t index : preview.indices) {
        CHECK(index < preview.vertices.size());
    }
}

void testEmptyBrushHasNoGeometry()
{
    sokoban::SplatCanvas::Brush brush;
    brush.radiusTiles = 0.0f;
    const auto preview = sokoban::EditorInteraction::brushPreview(
        brush,
        {},
        [](sokoban::Vec3) -> std::optional<sokoban::Vec2> {
            return sokoban::Vec2 {};
        });
    CHECK(preview.vertices.empty());
    CHECK(preview.indices.empty());
    CHECK(preview.rim.empty());
}

void testGizmoTargetsConstantPixelLength()
{
    sokoban::Level::Decoration decoration;
    decoration.position = { 2.0f, 3.0f, 4.0f };
    const auto geometry =
        sokoban::EditorInteraction::decorationGizmoGeometry(
            decoration,
            [](sokoban::Vec3 world) -> std::optional<sokoban::Vec2> {
                return sokoban::Vec2 {
                    world.x * 20.0f + world.z * 5.0f,
                    world.y * 10.0f - world.z * 5.0f,
                };
            });

    CHECK(geometry.has_value());
    for (const auto& axis : geometry->axes) {
        const float x = axis.end.x - axis.start.x;
        const float y = axis.end.y - axis.start.y;
        CHECK(near(std::sqrt(x * x + y * y), 92.0f));
    }
    for (const auto& ring : geometry->rings) {
        CHECK(ring.size() == 65);
        CHECK(near(ring.front().x, ring.back().x));
        CHECK(near(ring.front().y, ring.back().y));
    }
}

void testPointerPixelScaling()
{
    const sokoban::Vec2 scaled = sokoban::EditorInteraction::pointerPixels(
        { 320.0f, 180.0f }, { 1280.0f, 720.0f }, { 2560.0f, 1440.0f });
    CHECK(near(scaled.x, 640.0f));
    CHECK(near(scaled.y, 360.0f));
}

void testSelectorLabelsUseStableIdsAndWorldAnchors()
{
    const std::vector<sokoban::Level::ScreenSelector> selectors {
        { .id = 2, .cell = { 1, 3, 1 } },
        { .id = 9, .cell = { 5, 7, 2 } },
    };
    const auto labels = sokoban::EditorInteraction::selectorLabels(
        selectors,
        [](sokoban::Vec3 world) -> std::optional<sokoban::Vec2> {
            if (world.x > 5.0f) {
                return std::nullopt;
            }
            return sokoban::Vec2 { world.x * 10.0f, world.z * 20.0f };
        });
    CHECK(labels.size() == 1);
    CHECK(labels[0].id == 2);
    CHECK(labels[0].text == "Selector 2");
    CHECK(near(labels[0].anchor.x, 15.0f));
    CHECK(near(labels[0].anchor.y, 45.0f));
}

} // namespace

int main()
{
    testBrushPreviewTopologyAndCoverage();
    testEmptyBrushHasNoGeometry();
    testGizmoTargetsConstantPixelLength();
    testPointerPixelScaling();
    testSelectorLabelsUseStableIdsAndWorldAnchors();

    if (failures != 0) {
        std::cerr << "EditorInteractionTests: " << failures
                  << " failure(s) of " << checks << " checks\n";
        return 1;
    }
    std::cout << "EditorInteractionTests: " << checks << " checks passed\n";
    return 0;
}
