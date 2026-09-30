#include "TestHarness.hpp"

#include "engine/render/NineSlice.hpp"

#include <iostream>

namespace {

using namespace sokoban;

void testDisabledByDefault()
{
    TEST("disabledByDefault");
    const NineSlice slice;
    CHECK(!slice.enabled());

    const NineSliceDrawData draw = nineSliceDrawData(
        slice, { 320.0f, 80.0f });
    CHECK(draw.sourceBorders == Vec4 {});
    CHECK(draw.targetExtentAndSourcePixelScale.x == 320.0f);
    CHECK(draw.targetExtentAndSourcePixelScale.y == 80.0f);
}

void testFitWidthFactoryCreatesSymmetricBorders()
{
    TEST("fitWidthFactoryCreatesSymmetricBorders");
    const NineSlice slice =
        NineSlice::symmetricFitTargetWidth({ 0.2f, 0.25f });
    CHECK(slice.enabled());
    CHECK(slice.sourceBorders.x == 0.2f);
    CHECK(slice.sourceBorders.y == 0.25f);
    CHECK(slice.sourceBorders.z == 0.2f);
    CHECK(slice.sourceBorders.w == 0.25f);
    CHECK(slice.scaleMode == NineSliceScaleMode::FitTargetWidth);

    const NineSliceDrawData draw = nineSliceDrawData(
        slice, { 0.9f, 8.0f });
    CHECK(draw.sourceBorders == slice.sourceBorders);
    CHECK(draw.targetExtentAndSourcePixelScale.x == 0.9f);
    CHECK(draw.targetExtentAndSourcePixelScale.y == 8.0f);
    CHECK(draw.targetExtentAndSourcePixelScale.z == 0.0f);
}

void testUiScaleAndAsymmetricBordersArePreserved()
{
    TEST("uiScaleAndAsymmetricBordersArePreserved");
    const NineSlice slice = NineSlice::preserveSourcePixels(
        { 0.05f, 0.1f, 0.15f, 0.2f }, 2.0f);
    const NineSliceDrawData draw = nineSliceDrawData(
        slice, { 400.0f, 160.0f });
    CHECK(draw.sourceBorders.x == 0.05f);
    CHECK(draw.sourceBorders.y == 0.1f);
    CHECK(draw.sourceBorders.z == 0.15f);
    CHECK(draw.sourceBorders.w == 0.2f);
    CHECK(draw.targetExtentAndSourcePixelScale.z == 2.0f);
}

void testInvalidInputsAreSanitizedAtTheComponentBoundary()
{
    TEST("invalidInputsAreSanitizedAtTheComponentBoundary");
    const NineSlice sanitized = sanitizedNineSlice(
        NineSlice::preserveSourcePixels(
            { -1.0f, 0.25f, 2.0f, 0.75f }, -4.0f));
    CHECK(sanitized.sourceBorders.x == 0.0f);
    CHECK(sanitized.sourceBorders.y == 0.25f);
    CHECK(sanitized.sourceBorders.z == 0.499f);
    CHECK(sanitized.sourceBorders.w == 0.499f);
    CHECK(sanitized.sourcePixelScale == 0.0001f);

    const NineSliceDrawData draw = nineSliceDrawData(
        sanitized, { -20.0f, 0.0f });
    CHECK(draw.targetExtentAndSourcePixelScale.x == 0.0001f);
    CHECK(draw.targetExtentAndSourcePixelScale.y == 0.0001f);
}

} // namespace

int main()
{
    testDisabledByDefault();
    testFitWidthFactoryCreatesSymmetricBorders();
    testUiScaleAndAsymmetricBordersArePreserved();
    testInvalidInputsAreSanitizedAtTheComponentBoundary();

    if (failures == 0) {
        std::cout << "NineSliceTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "NineSliceTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
