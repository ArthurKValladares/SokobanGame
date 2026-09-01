// Covers the lane packing shared by the scene draws.
//
// This arithmetic used to live inside VulkanSceneRecorder's private draw
// methods, three and four copies deep, where nothing could reach it without a
// device and a command buffer. The values it produces are read by the shaders
// as raw floats in fixed slots, so a wrong one does not fail loudly - it
// renders. Hence the emphasis below on the exact channel each number lands in.

#include "TestHarness.hpp"

#include "engine/render/SceneDrawLanes.hpp"
#include "engine/render/VulkanRenderConstants.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {

using namespace sokoban;

bool sameFloat(float actual, float expected)
{
    // The packing is copies and multiplies, not accumulation: every value here
    // should be bit-for-bit what the arithmetic produces, so this is equality
    // with NaN handled, not a tolerance.
    if (std::isnan(expected)) {
        return std::isnan(actual);
    }
    return actual == expected;
}

bool sameVec4(Vec4 actual, Vec4 expected)
{
    return sameFloat(actual.x, expected.x) && sameFloat(actual.y, expected.y)
        && sameFloat(actual.z, expected.z) && sameFloat(actual.w, expected.w);
}

RenderFrameData::Lighting litScene()
{
    RenderFrameData::Lighting lighting {};
    lighting.sun.direction = Vec3 { 0.25f, -0.75f, 0.5f };
    lighting.sun.color = Vec3 { 1.0f, 0.5f, 0.25f };
    lighting.sun.intensity = 3.0f;
    lighting.ambient.color = Vec3 { 0.2f, 0.4f, 0.8f };
    lighting.ambient.intensity = 0.5f;
    lighting.shadows.enabled = true;
    lighting.shadows.opacity = 0.75f;
    lighting.shadows.bias = 0.002f;
    return lighting;
}

void testRadianceIsColourTimesIntensity()
{
    TEST("radianceIsColourTimesIntensity");
    const SunAmbientLanes lanes = sunAmbientLanes(litScene());

    // Sun radiance in xyz of its own lane.
    CHECK(sameFloat(lanes.sunRadianceAndAmbientBlue.x, 1.0f * 3.0f));
    CHECK(sameFloat(lanes.sunRadianceAndAmbientBlue.y, 0.5f * 3.0f));
    CHECK(sameFloat(lanes.sunRadianceAndAmbientBlue.z, 0.25f * 3.0f));

    // Ambient split one channel per lane, in this order. Getting the order
    // wrong tints the whole scene and nothing else complains.
    CHECK(sameFloat(lanes.ambientRed, 0.2f * 0.5f));
    CHECK(sameFloat(lanes.sunDirectionAndAmbientGreen.w, 0.4f * 0.5f));
    CHECK(sameFloat(lanes.sunRadianceAndAmbientBlue.w, 0.8f * 0.5f));
}

void testSunDirectionRidesInItsOwnLane()
{
    TEST("sunDirectionRidesInItsOwnLane");
    const SunAmbientLanes lanes = sunAmbientLanes(litScene());
    CHECK(sameFloat(lanes.sunDirectionAndAmbientGreen.x, 0.25f));
    CHECK(sameFloat(lanes.sunDirectionAndAmbientGreen.y, -0.75f));
    CHECK(sameFloat(lanes.sunDirectionAndAmbientGreen.z, 0.5f));
    // The direction is passed through, not normalised here. The shader
    // normalises it; doing it twice would be harmless but doing it here only
    // would hide an unnormalised frame from whoever reads the lane.
    RenderFrameData::Lighting unnormalised = litScene();
    unnormalised.sun.direction = Vec3 { 0.0f, -2.0f, 0.0f };
    const SunAmbientLanes raw = sunAmbientLanes(unnormalised);
    CHECK(sameFloat(raw.sunDirectionAndAmbientGreen.y, -2.0f));
}

void testZeroIntensityLeavesNoLight()
{
    TEST("zeroIntensityLeavesNoLight");
    RenderFrameData::Lighting dark = litScene();
    dark.sun.intensity = 0.0f;
    dark.ambient.intensity = 0.0f;
    const SunAmbientLanes lanes = sunAmbientLanes(dark);
    CHECK(sameFloat(lanes.ambientRed, 0.0f));
    CHECK(sameFloat(lanes.sunDirectionAndAmbientGreen.w, 0.0f));
    CHECK(sameFloat(lanes.sunRadianceAndAmbientBlue.w, 0.0f));
    CHECK(sameVec4(
        lanes.sunRadianceAndAmbientBlue, Vec4 { 0.0f, 0.0f, 0.0f, 0.0f }));
    // The direction survives an unlit frame; only the radiance goes to zero.
    CHECK(sameFloat(lanes.sunDirectionAndAmbientGreen.x, 0.25f));
}

void testDefaultLightingIsAWhiteSunAndNoAmbient()
{
    TEST("defaultLightingIsAWhiteSunAndNoAmbient");
    // A default-constructed Lighting is not an unlit frame: DirectionalLight
    // defaults to a full-intensity white sun along +Z, while AmbientLight
    // defaults to nothing. Worth pinning, because a caller that forgets to
    // fill this in gets a lit scene from the wrong direction rather than a
    // black one, and a black frame is far easier to notice.
    const SunAmbientLanes lanes = sunAmbientLanes(RenderFrameData::Lighting {});
    CHECK(sameFloat(lanes.ambientRed, 0.0f));
    CHECK(sameVec4(
        lanes.sunDirectionAndAmbientGreen, Vec4 { 0.0f, 0.0f, 1.0f, 0.0f }));
    CHECK(sameVec4(
        lanes.sunRadianceAndAmbientBlue, Vec4 { 1.0f, 1.0f, 1.0f, 0.0f }));
}

void testGridNeedsAllFourConditions()
{
    TEST("gridNeedsAllFourConditions");
    const Vec4 visible { 0.1f, 0.1f, 0.1f, 0.5f };
    const Vec2 cell { 1.0f, 1.0f };
    CHECK(sameFloat(gridLineWidthOrZero(visible, 0.02f, cell), 0.02f));

    // Each condition alone is enough to suppress the grid.
    CHECK(sameFloat(
        gridLineWidthOrZero(Vec4 { 0.1f, 0.1f, 0.1f, 0.0f }, 0.02f, cell),
        0.0f));
    CHECK(sameFloat(gridLineWidthOrZero(visible, 0.0f, cell), 0.0f));
    CHECK(sameFloat(
        gridLineWidthOrZero(visible, 0.02f, Vec2 { 0.0f, 1.0f }), 0.0f));
    CHECK(sameFloat(
        gridLineWidthOrZero(visible, 0.02f, Vec2 { 1.0f, 0.0f }), 0.0f));
}

void testGridRejectsNegatives()
{
    TEST("gridRejectsNegatives");
    // The conditions are strict greater-than, so a negative is refused rather
    // than passed through to become a shader artefact.
    const Vec2 cell { 1.0f, 1.0f };
    CHECK(sameFloat(
        gridLineWidthOrZero(Vec4 { 0.0f, 0.0f, 0.0f, -0.5f }, 0.02f, cell),
        0.0f));
    CHECK(sameFloat(
        gridLineWidthOrZero(Vec4 { 0.0f, 0.0f, 0.0f, 1.0f }, -0.02f, cell),
        0.0f));
    CHECK(sameFloat(
        gridLineWidthOrZero(
            Vec4 { 0.0f, 0.0f, 0.0f, 1.0f }, 0.02f, Vec2 { -1.0f, 1.0f }),
        0.0f));
}

void testFaceShadowOptionsPacksInOrder()
{
    TEST("faceShadowOptionsPacksInOrder");
    const Vec4 options = faceShadowOptions(
        litScene(), Vec4 { 0.0f, 0.0f, 0.0f, 1.0f }, 0.02f, Vec2 { 1.0f, 1.0f });
    CHECK(sameVec4(options, Vec4 { 1.0f, 0.75f, 0.002f, 0.02f }));
}

void testShadowsOffIsAFlagNotAZeroOpacity()
{
    TEST("shadowsOffIsAFlagNotAZeroOpacity");
    RenderFrameData::Lighting off = litScene();
    off.shadows.enabled = false;
    const Vec4 options
        = faceShadowOptions(off, Vec4 {}, 0.0f, Vec2 { 1.0f, 1.0f });
    CHECK(sameFloat(options.x, 0.0f));
    // Opacity and bias are still carried. The shader reads x as the switch, so
    // zeroing the others here would only hide what the frame asked for.
    CHECK(sameFloat(options.y, 0.75f));
    CHECK(sameFloat(options.z, 0.002f));
}

void testShadowOpacityAndBiasAreClamped()
{
    TEST("shadowOpacityAndBiasAreClamped");
    RenderFrameData::Lighting wild = litScene();
    wild.shadows.opacity = 4.0f;
    wild.shadows.bias = -1.0f;
    const Vec4 high
        = faceShadowOptions(wild, Vec4 {}, 0.0f, Vec2 { 1.0f, 1.0f });
    CHECK(sameFloat(high.y, 1.0f));
    CHECK(sameFloat(high.z, 0.0f));

    wild.shadows.opacity = -2.0f;
    wild.shadows.bias = 0.5f;
    const Vec4 low
        = faceShadowOptions(wild, Vec4 {}, 0.0f, Vec2 { 1.0f, 1.0f });
    CHECK(sameFloat(low.y, 0.0f));
    // Bias is floored, not clamped above: a large bias is a tuning choice.
    CHECK(sameFloat(low.z, 0.5f));
}

void testFaceShadowOptionsCarriesTheGridDecision()
{
    TEST("faceShadowOptionsCarriesTheGridDecision");
    // w is the grid width, which is why the grid predicate lives next to the
    // shadow lane at all: they share a vec4.
    const RenderFrameData::Lighting lighting = litScene();
    CHECK(sameFloat(
        faceShadowOptions(lighting, Vec4 { 0.0f, 0.0f, 0.0f, 1.0f }, 0.02f,
            Vec2 { 1.0f, 1.0f })
            .w,
        0.02f));
    CHECK(sameFloat(
        faceShadowOptions(
            lighting, Vec4 {}, 0.02f, Vec2 { 1.0f, 1.0f })
            .w,
        0.0f));
}

void testQuadVerticesCarryTheSpaceFlag()
{
    TEST("quadVerticesCarryTheSpaceFlag");
    const std::array<Vec3, 4> corners {
        Vec3 { 0.0f, 1.0f, 2.0f },
        Vec3 { 3.0f, 4.0f, 5.0f },
        Vec3 { 6.0f, 7.0f, 8.0f },
        Vec3 { 9.0f, 10.0f, 11.0f },
    };

    const std::array<Vec4, 4> world = quadVertices(corners, worldSpaceQuad);
    // Corner order is the winding the shader's fixed index buffer expects, so
    // it has to survive the copy unchanged.
    for (std::size_t index = 0; index < corners.size(); ++index) {
        CHECK(sameFloat(world[index].x, corners[index].x));
        CHECK(sameFloat(world[index].y, corners[index].y));
        CHECK(sameFloat(world[index].z, corners[index].z));
        CHECK(sameFloat(world[index].w, worldSpaceQuad));
    }

    const std::array<Vec4, 4> clip = quadVertices(corners, clipSpaceQuad);
    for (const Vec4& vertex : clip) {
        CHECK(sameFloat(vertex.w, clipSpaceQuad));
    }
    // The two spaces are distinguishable, which is the whole point of the lane.
    CHECK(!sameFloat(worldSpaceQuad, clipSpaceQuad));
}

void testDegenerateValuesArePassedThroughUntouched()
{
    TEST("degenerateValuesArePassedThroughUntouched");
    // Nothing here sanitises geometry. If a NaN corner reaches the recorder the
    // bug is upstream, and silently substituting zero would bury it.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array<Vec3, 4> corners {
        Vec3 { nan, 0.0f, 0.0f },
        Vec3 {},
        Vec3 {},
        Vec3 {},
    };
    const std::array<Vec4, 4> packed = quadVertices(corners, worldSpaceQuad);
    CHECK(std::isnan(packed[0].x));
    CHECK(sameFloat(packed[0].w, worldSpaceQuad));
}

} // namespace

int main()
{
    testRadianceIsColourTimesIntensity();
    testSunDirectionRidesInItsOwnLane();
    testZeroIntensityLeavesNoLight();
    testDefaultLightingIsAWhiteSunAndNoAmbient();

    testGridNeedsAllFourConditions();
    testGridRejectsNegatives();

    testFaceShadowOptionsPacksInOrder();
    testShadowsOffIsAFlagNotAZeroOpacity();
    testShadowOpacityAndBiasAreClamped();
    testFaceShadowOptionsCarriesTheGridDecision();

    testQuadVerticesCarryTheSpaceFlag();
    testDegenerateValuesArePassedThroughUntouched();

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "scene_draw_lanes: " << checks << " checks passed\n";
    return 0;
}
