#include "engine/render/SsaoMath.hpp"
#include "engine/render/IsoScenePreparer.hpp"

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

} // namespace

int main()
{
    testProjectionRoundTripUsesVulkanDepthAndFramebufferY();
    testPhysicalRadiusIsIndependentOfRenderResolution();
    testReconstructedNormalFacesTheCamera();
    testSampleComparisonUsesViewUnitsAndRejectsHalos();

    if (failures == 0) {
        std::cout << "SsaoMathTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "SsaoMathTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
