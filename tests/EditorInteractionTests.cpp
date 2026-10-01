#include "TestHarness.hpp"

#include "engine/EditorCursorArt.hpp"
#include "engine/EditorInteraction.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>
#include <iostream>

namespace {

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
        },
        [](const sokoban::Level::ScreenSelector& selector) {
            return "Selector " + std::to_string(selector.id) +
                ": Easy Plains / Screen 1";
        });
    CHECK(labels.size() == 1);
    CHECK(labels[0].id == 2);
    CHECK(labels[0].text == "Selector 2: Easy Plains / Screen 1");
    CHECK(near(labels[0].anchor.x, 15.0f));
    CHECK(near(labels[0].anchor.y, 45.0f));
}

void testGridLineIsContinuousAndInclusive()
{
    using sokoban::EditorInteraction;
    using sokoban::GridPosition;
    const auto single = EditorInteraction::gridLine({ 3, 4 }, { 3, 4 });
    CHECK(single.size() == 1);

    const auto horizontal = EditorInteraction::gridLine({ 5, 1 }, { 1, 1 });
    CHECK(horizontal.size() == 5);
    CHECK((horizontal.front() == GridPosition { 5, 1 }));
    CHECK((horizontal.back() == GridPosition { 1, 1 }));

    for (const auto& [from, to] : std::array {
             std::pair { GridPosition { 0, 0 }, GridPosition { 7, 3 } },
             std::pair { GridPosition { 2, 9 }, GridPosition { -3, 0 } },
             std::pair { GridPosition { 0, 0 }, GridPosition { 4, -4 } },
         }) {
        const auto line = EditorInteraction::gridLine(from, to);
        CHECK((line.front() == from));
        CHECK((line.back() == to));
        CHECK(line.size() ==
            static_cast<std::size_t>(std::max(
                std::abs(to.x - from.x), std::abs(to.y - from.y))) + 1U);
        for (std::size_t index = 1; index < line.size(); ++index) {
            CHECK(std::abs(line[index].x - line[index - 1].x) <= 1);
            CHECK(std::abs(line[index].y - line[index - 1].y) <= 1);
        }
    }
}

void testAxisConstraint()
{
    using sokoban::EditorInteraction;
    using sokoban::GridPosition;
    CHECK((EditorInteraction::constrainToAxis({ 2, 2 }, { 7, 4 }) ==
        GridPosition { 7, 2 }));
    CHECK((EditorInteraction::constrainToAxis({ 2, 2 }, { 1, -5 }) ==
        GridPosition { 2, -5 }));
}

} // namespace

void testEditorToolCursors()
{
    TEST("editorToolCursors");
    namespace art = sokoban::editorCursorArt;
    const auto check = [](const art::Image& image) {
        CHECK(image.rgba.size() ==
            static_cast<std::size_t>(art::size * art::size * 4));
        // The tip under the hotspot is solid; the opposite corners are
        // clear, so the cursor is a drawing and not a square.
        CHECK(image.alphaAt(image.hotspotX, image.hotspotY) == 255);
        CHECK(image.alphaAt(art::size - 1, art::size - 1) == 0);
        CHECK(image.alphaAt(0, 0) == 0);
        // Mostly transparent, with something drawn on it.
        std::size_t opaque = 0;
        for (int y = 0; y < art::size; ++y) {
            for (int x = 0; x < art::size; ++x) {
                opaque += image.alphaAt(x, y) > 128 ? 1U : 0U;
            }
        }
        CHECK(opaque > 100);
        CHECK(opaque < static_cast<std::size_t>(art::size * art::size / 2));
    };
    check(art::eyedropper());
    const art::Image brush = art::brush({ 0.2f, 0.4f, 1.0f });
    check(brush);
    // The bristles carry the active link color.
    const std::size_t bristle =
        static_cast<std::size_t>((25 * art::size + 7) * 4);
    CHECK(brush.rgba[bristle + 2] > 200);
    CHECK(brush.rgba[bristle + 0] < 80);
    const art::Image orange = art::brush({ 1.0f, 0.72f, 0.12f });
    CHECK(orange.rgba[bristle + 0] > 200);
    CHECK(orange.rgba[bristle + 2] < 80);
}

int main()
{
    testEditorToolCursors();
    testBrushPreviewTopologyAndCoverage();
    testEmptyBrushHasNoGeometry();
    testGizmoTargetsConstantPixelLength();
    testPointerPixelScaling();
    testSelectorLabelsUseStableIdsAndWorldAnchors();
    testGridLineIsContinuousAndInclusive();
    testAxisConstraint();

    if (failures != 0) {
        std::cerr << "EditorInteractionTests: " << failures
                  << " failure(s) of " << checks << " checks\n";
        return 1;
    }
    std::cout << "EditorInteractionTests: " << checks << " checks passed\n";
    return 0;
}
