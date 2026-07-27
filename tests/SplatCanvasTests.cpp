// Headless tests for the ground splat paint canvas: brush shape, falloff,
// opacity accumulation, stroke interpolation, and undo snapshots. No renderer,
// no window, no editor - this is where the painting maths is pinned down.

#include "engine/SplatCanvas.hpp"

#include <cmath>
#include <iostream>

namespace {

using namespace sokoban;

int failures = 0;
int checks = 0;
const char* currentTest = "";

void checkImpl(bool ok, const char* expression, int line)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << "FAIL [" << currentTest << "] line "
                  << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)
#define TEST(name) currentTest = name

constexpr uint32_t texelsPerTile = SplatCanvas::texelsPerTile;

void testBoardSizingMatchesTheShaderConvention()
{
    TEST("boardSizingMatchesTheShaderConvention");
    const SplatCanvas canvas = SplatCanvas::createForBoard(9, 7);
    CHECK(canvas.width() == 9 * texelsPerTile);
    CHECK(canvas.height() == 7 * texelsPerTile);
    CHECK(!canvas.empty());

    // The shader recovers board coverage by dividing the texture's own size by
    // the same constant. If these disagree every stroke lands offset.
    CHECK(std::abs(canvas.boardTiles().x - 9.0f) < 0.001f);
    CHECK(std::abs(canvas.boardTiles().y - 7.0f) < 0.001f);

    const SplatCanvas zero = SplatCanvas::createForBoard(0, 0);
    CHECK(zero.empty());
}

void testStampPaintsARoundSpotAtTheRightPlace()
{
    TEST("stampPaintsARoundSpotAtTheRightPlace");
    SplatCanvas canvas = SplatCanvas::createForBoard(8, 8, 0);
    const SplatCanvas::Brush brush {
        .radiusTiles = 1.0f,
        .hardness = 1.0f,
        .opacity = 1.0f,
        .color = SplatCanvas::BrushColor::White,
    };
    CHECK(canvas.stamp({ 4.0f, 4.0f }, brush));

    // Centre is fully painted.
    CHECK(canvas.weightAt(4 * texelsPerTile, 4 * texelsPerTile) == 255);
    // A point just inside the radius is painted, just outside is untouched.
    CHECK(canvas.weightAt(
              static_cast<uint32_t>(4.9f * texelsPerTile),
              4 * texelsPerTile) == 255);
    CHECK(canvas.weightAt(
              static_cast<uint32_t>(5.2f * texelsPerTile),
              4 * texelsPerTile) == 0);
    // Round, not square: the corner of the bounding box stays clean.
    CHECK(canvas.weightAt(
              static_cast<uint32_t>(4.9f * texelsPerTile),
              static_cast<uint32_t>(4.9f * texelsPerTile)) == 0);
    // Far side of the board is untouched.
    CHECK(canvas.weightAt(0, 0) == 0);
}

void testHardnessControlsTheEdgeFalloff()
{
    TEST("hardnessControlsTheEdgeFalloff");
    const auto weightAtRadiusFraction =
        [](float hardness, float fraction) {
            SplatCanvas canvas = SplatCanvas::createForBoard(8, 8, 0);
            canvas.stamp({ 4.0f, 4.0f }, {
                .radiusTiles = 2.0f,
                .hardness = hardness,
                .opacity = 1.0f,
                .color = SplatCanvas::BrushColor::White,
            });
            return canvas.weightAt(
                static_cast<uint32_t>((4.0f + 2.0f * fraction) * texelsPerTile),
                4 * texelsPerTile);
        };

    // A hard brush is flat to the rim; a soft one has already faded there.
    CHECK(weightAtRadiusFraction(1.0f, 0.9f) == 255);
    CHECK(weightAtRadiusFraction(0.0f, 0.9f) < 60);
    // Soft brushes are still solid at the very centre.
    CHECK(weightAtRadiusFraction(0.0f, 0.0f) == 255);
    // Falloff is monotonic outward.
    CHECK(weightAtRadiusFraction(0.5f, 0.6f) >= weightAtRadiusFraction(0.5f, 0.8f));
    CHECK(weightAtRadiusFraction(0.5f, 0.8f) >= weightAtRadiusFraction(0.5f, 0.95f));
    // Nothing lands outside the radius at any hardness.
    CHECK(weightAtRadiusFraction(1.0f, 1.1f) == 0);
    CHECK(weightAtRadiusFraction(0.0f, 1.1f) == 0);
}

void testOpacityBuildsUpAcrossStrokesNotWithinOne()
{
    TEST("opacityBuildsUpAcrossStrokesNotWithinOne");
    SplatCanvas canvas = SplatCanvas::createForBoard(4, 4, 0);
    const SplatCanvas::Brush brush {
        .radiusTiles = 1.0f,
        .hardness = 1.0f,
        .opacity = 0.5f,
        .color = SplatCanvas::BrushColor::White,
    };
    const uint32_t x = 2 * texelsPerTile;
    const uint32_t y = 2 * texelsPerTile;

    // The pointer is sampled every frame, so holding the button re-stamps the
    // same spot many times for one click. Within a stroke that must not build
    // up, or opacity would creep to full and hardness would harden.
    canvas.beginStroke();
    CHECK(canvas.stamp({ 2.0f, 2.0f }, brush));
    const uint8_t first = canvas.weightAt(x, y);
    CHECK(first > 120 && first < 135);
    for (int i = 0; i < 30; ++i) {
        // No further change: nothing to re-upload, nothing to see.
        CHECK(!canvas.stamp({ 2.0f, 2.0f }, brush));
    }
    CHECK(canvas.weightAt(x, y) == first);
    canvas.endStroke();

    // A second stroke does build on the first, which is how repeated dabs are
    // expected to darken.
    canvas.beginStroke();
    CHECK(canvas.stamp({ 2.0f, 2.0f }, brush));
    canvas.endStroke();
    const uint8_t second = canvas.weightAt(x, y);
    CHECK(second > first);
    CHECK(second < 255);

    for (int i = 0; i < 20; ++i) {
        canvas.beginStroke();
        canvas.stamp({ 2.0f, 2.0f }, brush);
        canvas.endStroke();
    }
    CHECK(canvas.weightAt(x, y) == 255);
}

void testHardnessSurvivesAHeldClick()
{
    TEST("hardnessSurvivesAHeldClick");
    // Regression: a click held for a handful of frames used to composite the
    // brush onto its own output every frame, driving 1-(1-f)^N to full. Soft
    // brushes turned into hard discs and every hardness looked alike, while
    // opacity merely saturated - which is exactly how it looked on screen.
    const auto profileAfter = [](int frames, float hardness) {
        SplatCanvas canvas = SplatCanvas::createForBoard(8, 8, 0);
        canvas.beginStroke();
        for (int i = 0; i < frames; ++i) {
            // A held click with no movement: from == to, every frame.
            canvas.stampLine({ 4.0f, 4.0f }, { 4.0f, 4.0f }, {
                .radiusTiles = 2.0f,
                .hardness = hardness,
                .opacity = 1.0f,
                .color = SplatCanvas::BrushColor::White,
            });
        }
        canvas.endStroke();
        return canvas.weights();
    };

    for (const float hardness : { 1.0f, 0.5f, 0.25f, 0.125f, 0.05f }) {
        // Holding longer must change nothing at all.
        CHECK(profileAfter(1, hardness) == profileAfter(8, hardness));
        CHECK(profileAfter(1, hardness) == profileAfter(60, hardness));
    }

    // And the hardness settings must stay clearly distinct after a held click.
    const auto midRadius = [&](float hardness) {
        const std::vector<uint8_t> weights = profileAfter(8, hardness);
        const uint32_t x = static_cast<uint32_t>(5.0f * texelsPerTile);
        const uint32_t y = 4 * texelsPerTile;
        return weights[static_cast<std::size_t>(y) * 8 * texelsPerTile + x];
    };
    CHECK(midRadius(1.0f) == 255);
    CHECK(midRadius(0.25f) < 220);
    CHECK(midRadius(0.05f) < midRadius(0.25f));
    // A soft brush is still solid dead centre.
    const std::vector<uint8_t> soft = profileAfter(8, 0.05f);
    CHECK(soft[static_cast<std::size_t>(4 * texelsPerTile) * 8 * texelsPerTile +
              4 * texelsPerTile] == 255);
}

void testOverlappingStampsInOneStrokeDoNotDarken()
{
    TEST("overlappingStampsInOneStrokeDoNotDarken");
    // Dragging back and forth over the same ground, or the heavy overlap
    // between consecutive stamps along a line, must paint at brush strength
    // rather than compounding into a darker streak.
    SplatCanvas canvas = SplatCanvas::createForBoard(8, 4, 0);
    const SplatCanvas::Brush brush {
        .radiusTiles = 1.0f,
        .hardness = 1.0f,
        .opacity = 0.4f,
        .color = SplatCanvas::BrushColor::White,
    };
    canvas.beginStroke();
    canvas.stampLine({ 1.0f, 2.0f }, { 7.0f, 2.0f }, brush);
    const uint8_t single = canvas.weightAt(4 * texelsPerTile, 2 * texelsPerTile);
    canvas.stampLine({ 7.0f, 2.0f }, { 1.0f, 2.0f }, brush);
    canvas.stampLine({ 1.0f, 2.0f }, { 7.0f, 2.0f }, brush);
    canvas.endStroke();
    CHECK(canvas.weightAt(4 * texelsPerTile, 2 * texelsPerTile) == single);

    // The line is also even along its length rather than darker where stamps
    // piled up.
    for (uint32_t tile = 2; tile <= 6; ++tile) {
        CHECK(canvas.weightAt(tile * texelsPerTile, 2 * texelsPerTile) == single);
    }
}

void testBlackAndWhitePaintInOppositeDirections()
{
    TEST("blackAndWhitePaintInOppositeDirections");
    SplatCanvas canvas = SplatCanvas::createForBoard(4, 4, 255);
    const uint32_t x = 2 * texelsPerTile;
    const uint32_t y = 2 * texelsPerTile;
    CHECK(canvas.weightAt(x, y) == 255);

    CHECK(canvas.stamp({ 2.0f, 2.0f }, {
        .radiusTiles = 1.0f,
        .hardness = 1.0f,
        .opacity = 1.0f,
        .color = SplatCanvas::BrushColor::Black,
    }));
    CHECK(canvas.weightAt(x, y) == 0);

    CHECK(canvas.stamp({ 2.0f, 2.0f }, {
        .radiusTiles = 1.0f,
        .hardness = 1.0f,
        .opacity = 1.0f,
        .color = SplatCanvas::BrushColor::White,
    }));
    CHECK(canvas.weightAt(x, y) == 255);

    // Painting white over an already-white spot leaves the covered interior
    // untouched. The anti-aliased rim is only partially covered, so it keeps
    // converging for a few strokes before settling - at which point the whole
    // stamp is a genuine no-op and the editor can skip the re-upload and the
    // undo entry.
    const SplatCanvas::Brush white {
        .radiusTiles = 1.0f,
        .hardness = 1.0f,
        .opacity = 1.0f,
        .color = SplatCanvas::BrushColor::White,
    };
    bool settled = false;
    for (int i = 0; i < 20 && !settled; ++i) {
        settled = !canvas.stamp({ 2.0f, 2.0f }, white);
        CHECK(canvas.weightAt(x, y) == 255);
    }
    CHECK(settled);
}

void testStampLineIsContinuous()
{
    TEST("stampLineIsContinuous");
    SplatCanvas canvas = SplatCanvas::createForBoard(16, 4, 0);
    const SplatCanvas::Brush brush {
        .radiusTiles = 0.5f,
        .hardness = 1.0f,
        .opacity = 1.0f,
        .color = SplatCanvas::BrushColor::White,
    };
    // A fast drag jumps many tiles between frames; the gap must still be
    // filled, or strokes come out as a row of dots.
    CHECK(canvas.stampLine({ 1.0f, 2.0f }, { 15.0f, 2.0f }, brush));

    for (uint32_t tile = 1; tile <= 15; ++tile) {
        CHECK(canvas.weightAt(tile * texelsPerTile, 2 * texelsPerTile) == 255);
    }
    // Both endpoints land.
    CHECK(canvas.weightAt(1 * texelsPerTile, 2 * texelsPerTile) == 255);
    CHECK(canvas.weightAt(15 * texelsPerTile, 2 * texelsPerTile) == 255);
    // The line has thickness but does not bleed a whole tile away.
    CHECK(canvas.weightAt(8 * texelsPerTile, 0) == 0);
}

void testZeroLengthLineStillPaints()
{
    TEST("zeroLengthLineStillPaints");
    SplatCanvas canvas = SplatCanvas::createForBoard(4, 4, 0);
    // A click with no drag: from == to must not divide by zero or no-op.
    CHECK(canvas.stampLine({ 2.0f, 2.0f }, { 2.0f, 2.0f }, {
        .radiusTiles = 1.0f,
        .hardness = 1.0f,
        .opacity = 1.0f,
        .color = SplatCanvas::BrushColor::White,
    }));
    CHECK(canvas.weightAt(2 * texelsPerTile, 2 * texelsPerTile) == 255);
}

void testStrokesClipAtTheBoardEdge()
{
    TEST("strokesClipAtTheBoardEdge");
    SplatCanvas canvas = SplatCanvas::createForBoard(4, 4, 0);
    const SplatCanvas::Brush brush {
        .radiusTiles = 2.0f,
        .hardness = 1.0f,
        .opacity = 1.0f,
        .color = SplatCanvas::BrushColor::White,
    };
    // Centred on the corner: half the brush is off-canvas. Must paint the
    // in-bounds part and not wrap to the far edge or read out of bounds.
    CHECK(canvas.stamp({ 0.0f, 0.0f }, brush));
    CHECK(canvas.weightAt(0, 0) == 255);
    CHECK(canvas.weightAt(canvas.width() - 1, canvas.height() - 1) == 0);
    CHECK(canvas.weightAt(canvas.width() - 1, 0) == 0);

    // Entirely outside: nothing changes, and nothing crashes.
    SplatCanvas untouched = SplatCanvas::createForBoard(4, 4, 0);
    CHECK(!untouched.stamp({ -50.0f, -50.0f }, brush));
    CHECK(!untouched.stamp({ 500.0f, 500.0f }, brush));
}

void testDegenerateBrushesAreRejected()
{
    TEST("degenerateBrushesAreRejected");
    SplatCanvas canvas = SplatCanvas::createForBoard(4, 4, 0);
    CHECK(!canvas.stamp({ 2.0f, 2.0f }, { .radiusTiles = 0.0f }));
    CHECK(!canvas.stamp({ 2.0f, 2.0f }, { .radiusTiles = -1.0f }));
    CHECK(!canvas.stamp({ 2.0f, 2.0f }, {
        .radiusTiles = 1.0f, .hardness = 1.0f, .opacity = 0.0f }));
    CHECK(canvas.weightAt(2 * texelsPerTile, 2 * texelsPerTile) == 0);
}

void testSnapshotRestoreRoundTrips()
{
    TEST("snapshotRestoreRoundTrips");
    SplatCanvas canvas = SplatCanvas::createForBoard(4, 4, 0);
    const std::vector<uint8_t> before = canvas.snapshot();

    canvas.stamp({ 2.0f, 2.0f }, {
        .radiusTiles = 1.0f,
        .hardness = 1.0f,
        .opacity = 1.0f,
        .color = SplatCanvas::BrushColor::White,
    });
    CHECK(canvas.weightAt(2 * texelsPerTile, 2 * texelsPerTile) == 255);

    CHECK(canvas.restore(before));
    CHECK(canvas.weightAt(2 * texelsPerTile, 2 * texelsPerTile) == 0);
    CHECK(canvas.snapshot() == before);

    // A snapshot from a differently sized canvas must be refused rather than
    // resizing or partially applying.
    const SplatCanvas other = SplatCanvas::createForBoard(8, 8, 0);
    CHECK(!canvas.restore(other.snapshot()));
    CHECK(canvas.width() == 4 * texelsPerTile);
}

void testResizeToBoardKeepsPaintAnchoredAtTheOrigin()
{
    TEST("resizeToBoardKeepsPaintAnchoredAtTheOrigin");
    SplatCanvas canvas = SplatCanvas::createForBoard(4, 4, 0);
    const SplatCanvas::Brush brush {
        .radiusTiles = 0.4f,
        .hardness = 1.0f,
        .opacity = 1.0f,
        .color = SplatCanvas::BrushColor::White,
    };
    canvas.stamp({ 0.5f, 0.5f }, brush);
    canvas.stamp({ 3.5f, 3.5f }, brush);

    // Growing keeps everything and fills the new strip with base material.
    CHECK(canvas.resizeToBoard(6, 5));
    CHECK(canvas.width() == 6 * texelsPerTile);
    CHECK(canvas.height() == 5 * texelsPerTile);
    CHECK(std::abs(canvas.boardTiles().x - 6.0f) < 0.001f);
    // Both original marks are still where they were, in the same tiles.
    CHECK(canvas.weightAt(
        static_cast<uint32_t>(0.5f * texelsPerTile),
        static_cast<uint32_t>(0.5f * texelsPerTile)) == 255);
    CHECK(canvas.weightAt(
        static_cast<uint32_t>(3.5f * texelsPerTile),
        static_cast<uint32_t>(3.5f * texelsPerTile)) == 255);
    // New area is blank rather than a smear of the old edge.
    CHECK(canvas.weightAt(5 * texelsPerTile, 4 * texelsPerTile) == 0);

    // Shrinking crops; the surviving corner is untouched.
    CHECK(canvas.resizeToBoard(2, 2));
    CHECK(canvas.width() == 2 * texelsPerTile);
    CHECK(canvas.height() == 2 * texelsPerTile);
    CHECK(canvas.weightAt(
        static_cast<uint32_t>(0.5f * texelsPerTile),
        static_cast<uint32_t>(0.5f * texelsPerTile)) == 255);
    CHECK(canvas.weights().size() ==
        static_cast<std::size_t>(canvas.width()) * canvas.height());

    // Same size is not a change, and a degenerate board is refused.
    CHECK(!canvas.resizeToBoard(2, 2));
    CHECK(!canvas.resizeToBoard(0, 4));
    CHECK(!canvas.resizeToBoard(4, 0));
    CHECK(canvas.width() == 2 * texelsPerTile);

    // Painting still works, and lands correctly, after a resize.
    CHECK(canvas.stamp({ 1.5f, 1.5f }, brush));
    CHECK(canvas.weightAt(
        static_cast<uint32_t>(1.5f * texelsPerTile),
        static_cast<uint32_t>(1.5f * texelsPerTile)) == 255);
}

void testImageRoundTrip()
{
    TEST("imageRoundTrip");
    SplatCanvas canvas = SplatCanvas::createForBoard(3, 2, 0);
    canvas.stamp({ 1.5f, 1.0f }, {
        .radiusTiles = 0.75f,
        .hardness = 0.4f,
        .opacity = 1.0f,
        .color = SplatCanvas::BrushColor::White,
    });

    const ImageData image = canvas.toImage();
    CHECK(image.width == canvas.width());
    CHECK(image.height == canvas.height());
    CHECK(image.rgba.size() ==
        static_cast<std::size_t>(image.width) * image.height * 4);

    // Grey, so the map reads correctly in an image viewer, and opaque.
    const std::size_t sample =
        (static_cast<std::size_t>(1 * texelsPerTile) * image.width +
            static_cast<std::size_t>(1.5f * texelsPerTile)) * 4;
    CHECK(image.rgba[sample + 0] == image.rgba[sample + 1]);
    CHECK(image.rgba[sample + 1] == image.rgba[sample + 2]);
    CHECK(image.rgba[sample + 3] == static_cast<std::byte>(255));

    const std::optional<SplatCanvas> reloaded = SplatCanvas::fromImage(image);
    CHECK(reloaded.has_value());
    CHECK(reloaded->weights() == canvas.weights());

    // A map saved and reloaded must be paint-identical, or every editing
    // session would drift the ground slightly.
    CHECK(reloaded->width() == canvas.width());
    CHECK(reloaded->height() == canvas.height());
}

void testFromImageRejectsUnusableInput()
{
    TEST("fromImageRejectsUnusableInput");
    CHECK(!SplatCanvas::fromImage(ImageData {}).has_value());

    ImageData truncated;
    truncated.width = 4;
    truncated.height = 4;
    truncated.rgba.resize(8); // far short of 4*4*4
    CHECK(!SplatCanvas::fromImage(truncated).has_value());

    // A map authored at a different density is still usable; it just covers a
    // different number of tiles, which the shader derives the same way.
    ImageData odd;
    odd.width = 3;
    odd.height = 5;
    odd.rgba.assign(3 * 5 * 4, static_cast<std::byte>(200));
    const std::optional<SplatCanvas> loaded = SplatCanvas::fromImage(odd);
    CHECK(loaded.has_value());
    CHECK(loaded->weightAt(0, 0) == 200);
}

} // namespace

int main()
{
    testBoardSizingMatchesTheShaderConvention();
    testStampPaintsARoundSpotAtTheRightPlace();
    testHardnessControlsTheEdgeFalloff();
    testOpacityBuildsUpAcrossStrokesNotWithinOne();
    testHardnessSurvivesAHeldClick();
    testOverlappingStampsInOneStrokeDoNotDarken();
    testBlackAndWhitePaintInOppositeDirections();
    testStampLineIsContinuous();
    testZeroLengthLineStillPaints();
    testStrokesClipAtTheBoardEdge();
    testDegenerateBrushesAreRejected();
    testSnapshotRestoreRoundTrips();
    testResizeToBoardKeepsPaintAnchoredAtTheOrigin();
    testImageRoundTrip();
    testFromImageRejectsUnusableInput();

    if (failures == 0) {
        std::cout << "SplatCanvasTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "SplatCanvasTests: "
              << failures << " of " << checks << " checks failed\n";
    return 1;
}
