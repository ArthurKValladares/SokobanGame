#include "engine/render/SsaoMath.hpp"
#include "engine/render/IsoScenePreparer.hpp"

#include <array>
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
        std::cerr << "FAIL [" << currentTest << "] line " << line
                  << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)
#define TEST(name) currentTest = name

bool near(float left, float right, float epsilon = 0.0001f)
{
    return std::abs(left - right) <= epsilon;
}

bool near(Vec2 left, Vec2 right, float epsilon = 0.0001f)
{
    return near(left.x, right.x, epsilon) &&
        near(left.y, right.y, epsilon);
}

bool near(Vec3 left, Vec3 right, float epsilon = 0.0001f)
{
    return near(left.x, right.x, epsilon) &&
        near(left.y, right.y, epsilon) &&
        near(left.z, right.z, epsilon);
}

float projectedDepth(const Mat4& clipFromView, Vec3 position)
{
    const Vec4 clip = transform(
        clipFromView, { position.x, position.y, position.z, 1.0f });
    return clip.z / clip.w;
}

Mat4 testProjection()
{
    return mat4PerspectiveOffCenter(
        2.4f,
        16.0f / 9.0f,
        1.0f,
        30.0f,
        { 0.12f, -0.08f },
        0.85f);
}

void testProjectionRoundTripUsesVulkanDepthAndFramebufferY()
{
    TEST("projectionRoundTripUsesVulkanDepthAndFramebufferY");
    const Mat4 clipFromView = testProjection();
    const Mat4 viewFromClip = inverse(clipFromView);
    for (Vec3 point : {
             Vec3 { 0.0f, 0.0f, 2.0f },
             Vec3 { 0.8f, 0.4f, 5.0f },
             Vec3 { -1.2f, -0.7f, 12.0f },
         }) {
        const Vec2 uv = projectSsaoViewPosition(clipFromView, point);
        const Vec3 reconstructed = reconstructSsaoViewPosition(
            viewFromClip, uv, projectedDepth(clipFromView, point));
        CHECK(near(reconstructed, point, 0.0005f));
    }

    const Vec2 upper = projectSsaoViewPosition(
        clipFromView, { 0.0f, 1.0f, 5.0f });
    const Vec2 lower = projectSsaoViewPosition(
        clipFromView, { 0.0f, -1.0f, 5.0f });
    CHECK(upper.y < lower.y);
}

void testAoExtentIsHalfResolutionAndCoversOddEdges()
{
    TEST("aoExtentIsHalfResolutionAndCoversOddEdges");
    const PixelExtent even = ssaoBufferExtent({ 1920, 1080 });
    CHECK(even == (PixelExtent { 960, 540 }));
    const PixelExtent odd = ssaoBufferExtent({ 1919, 1079 });
    CHECK(odd == (PixelExtent { 960, 540 }));
    const PixelExtent single = ssaoBufferExtent({ 1, 1 });
    CHECK(single == (PixelExtent { 1, 1 }));
    const PixelExtent empty = ssaoBufferExtent({ 0, 0 });
    CHECK(empty == (PixelExtent { 0, 0 }));
}

void testRotationNoiseUsesBothAxesWithoutRowBias()
{
    TEST("rotationNoiseUsesBothAxesWithoutRowBias");
    CHECK(ssaoRotationNoise(13, 7) == ssaoRotationNoise(13, 7));
    CHECK(ssaoRotationNoise(13, 7) != ssaoRotationNoise(14, 7));
    CHECK(ssaoRotationNoise(13, 7) != ssaoRotationNoise(13, 8));

    std::array<float, 16> rowMeans {};
    float total = 0.0f;
    for (uint32_t y = 0; y < rowMeans.size(); ++y) {
        for (uint32_t x = 0; x < 64; ++x) {
            const float value = ssaoRotationNoise(x, y);
            CHECK(value >= 0.0f);
            CHECK(value < 1.0f);
            rowMeans[y] += value;
            total += value;
        }
        rowMeans[y] /= 64.0f;
        CHECK(rowMeans[y] > 0.35f);
        CHECK(rowMeans[y] < 0.65f);
    }
    CHECK(total / (64.0f * static_cast<float>(rowMeans.size())) > 0.47f);
    CHECK(total / (64.0f * static_cast<float>(rowMeans.size())) < 0.53f);
}

void testPhysicalRadiusIsIndependentOfRenderResolution()
{
    TEST("physicalRadiusIsIndependentOfRenderResolution");
    const IsoRenderLayout layout {
        .focalLength = 2.4f,
        .fitScale = 0.85f,
        .nearestDepth = 1.0f,
        .farthestDepth = 30.0f,
    };
    const Mat4 clipFromView = isoClipFromView(
        layout, { 1280.0f, 720.0f });
    const Mat4 highResolutionProjection = isoClipFromView(
        layout, { 2560.0f, 1440.0f });
    CHECK(clipFromView == highResolutionProjection);
    const Mat4 viewFromClip = inverse(clipFromView);
    const Vec3 center { 0.0f, 0.0f, 6.0f };
    const Vec3 neighbor { 0.45f, 0.0f, 6.0f };
    const Vec2 centerUv = projectSsaoViewPosition(clipFromView, center);
    const Vec2 neighborUv = projectSsaoViewPosition(clipFromView, neighbor);
    const float lowResolutionPixels =
        (neighborUv.x - centerUv.x) * 1280.0f;
    const float highResolutionPixels =
        (neighborUv.x - centerUv.x) * 2560.0f;
    CHECK(near(highResolutionPixels, lowResolutionPixels * 2.0f));

    const Vec3 lowReconstructed = reconstructSsaoViewPosition(
        viewFromClip,
        neighborUv,
        projectedDepth(clipFromView, neighbor));
    const Vec3 highReconstructed = reconstructSsaoViewPosition(
        viewFromClip,
        neighborUv,
        projectedDepth(clipFromView, neighbor));
    CHECK(near(length(subtract(lowReconstructed, center)), 0.45f));
    CHECK(near(highReconstructed, lowReconstructed));
}

void testReconstructedNormalFacesTheCamera()
{
    TEST("reconstructedNormalFacesTheCamera");
    const Vec3 center { 0.0f, 0.0f, 5.0f };
    const Vec3 right { 1.0f, 0.0f, 5.0f };
    const Vec3 down { 0.0f, -1.0f, 5.0f };
    const Vec3 normal = resolveSsaoViewNormal(center, right, down);
    CHECK(near(normal, { 0.0f, 0.0f, -1.0f }));

    const Vec3 reversed = resolveSsaoViewNormal(center, down, right);
    CHECK(near(reversed, normal));
}

void testSampleComparisonUsesViewUnitsAndRejectsHalos()
{
    TEST("sampleComparisonUsesViewUnitsAndRejectsHalos");
    const Vec3 center { 0.0f, 0.0f, 5.0f };
    const Vec3 proposed { 0.0f, 0.0f, 4.8f };
    CHECK(near(ssaoSampleOcclusion(
        center, proposed, { 0.0f, 0.0f, 4.7f }, 0.5f, 0.05f), 1.0f));
    CHECK(near(ssaoSampleOcclusion(
        center, proposed, { 0.0f, 0.0f, 4.78f }, 0.5f, 0.05f), 0.0f));
    CHECK(near(ssaoSampleOcclusion(
        center, proposed, { 0.0f, 0.0f, 4.0f }, 0.5f, 0.05f), 0.0f));

    const float faded = ssaoSampleOcclusion(
        center, proposed, { 0.0f, 0.0f, 4.3f }, 0.5f, 0.05f);
    CHECK(faded > 0.0f);
    CHECK(faded < 1.0f);
}

void testBilateralWeightRejectsDepthAndNormalDiscontinuities()
{
    TEST("bilateralWeightRejectsDepthAndNormalDiscontinuities");
    const Vec3 center { 0.0f, 0.0f, 5.0f };
    const Vec3 normal { 0.0f, 0.0f, -1.0f };
    CHECK(near(ssaoBilateralWeight(
        center,
        normal,
        { 1.0f, 0.0f, 5.0f },
        normal,
        0.1f,
        0.8f,
        0.5f), 0.5f));

    const float acrossDepthEdge = ssaoBilateralWeight(
        center,
        normal,
        { 0.0f, 0.0f, 5.5f },
        normal,
        0.1f,
        0.8f,
        0.5f);
    CHECK(acrossDepthEdge < 0.00001f);

    CHECK(near(ssaoBilateralWeight(
        center,
        normal,
        { 0.0f, 0.0f, 5.0f },
        { 1.0f, 0.0f, 0.0f },
        0.1f,
        0.8f,
        0.5f), 0.0f));
    const float similarNormal = ssaoBilateralWeight(
        center,
        normal,
        { 0.0f, 0.0f, 5.0f },
        normalize(Vec3 { 0.4f, 0.0f, -1.0f }),
        0.1f,
        0.8f,
        0.5f);
    CHECK(similarNormal > 0.0f);
    CHECK(similarNormal < 0.5f);
    CHECK(near(ssaoBilateralWeight(
        center,
        normal,
        center,
        normal,
        0.1f,
        0.8f,
        -1.0f), 0.0f));
}

} // namespace

int main()
{
    testProjectionRoundTripUsesVulkanDepthAndFramebufferY();
    testAoExtentIsHalfResolutionAndCoversOddEdges();
    testRotationNoiseUsesBothAxesWithoutRowBias();
    testPhysicalRadiusIsIndependentOfRenderResolution();
    testReconstructedNormalFacesTheCamera();
    testSampleComparisonUsesViewUnitsAndRejectsHalos();
    testBilateralWeightRejectsDepthAndNormalDiscontinuities();

    if (failures == 0) {
        std::cout << "SsaoMathTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "SsaoMathTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
