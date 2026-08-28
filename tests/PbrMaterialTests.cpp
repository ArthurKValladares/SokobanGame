#include "engine/render/PbrMaterial.hpp"

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

bool near(float a, float b)
{
    return std::abs(a - b) < 0.00001f;
}

bool near(Vec3 a, Vec3 b)
{
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

void testMissingMapPreservesFactors()
{
    TEST("missingMapPreservesFactors");
    const MetallicRoughness result = resolveMetallicRoughness(0.6f, 0.7f);
    CHECK(near(result.metallic, 0.6f));
    CHECK(near(result.roughness, 0.7f));
}

void testPackedChannelsMultiplyFactors()
{
    TEST("packedChannelsMultiplyFactors");
    const MetallicRoughness result = resolveMetallicRoughness(
        0.8f,
        0.5f,
        { 0.11f, 0.25f, 0.75f, 0.93f });
    CHECK(near(result.metallic, 0.6f));
    CHECK(near(result.roughness, 0.125f));

    const MetallicRoughness differentUnusedChannels =
        resolveMetallicRoughness(
            0.8f,
            0.5f,
            { 0.99f, 0.25f, 0.75f, 0.01f });
    CHECK(near(differentUnusedChannels.metallic, result.metallic));
    CHECK(near(differentUnusedChannels.roughness, result.roughness));
}

void testPhysicalRangeIsAppliedAfterSampling()
{
    TEST("physicalRangeIsAppliedAfterSampling");
    const MetallicRoughness result = resolveMetallicRoughness(
        2.0f,
        0.5f,
        { 1.0f, 0.0f, 0.75f, 1.0f });
    CHECK(near(result.metallic, 1.0f));
    CHECK(near(result.roughness, minimumPbrRoughness));
}

void testNeutralNormalPreservesTheGeometricNormal()
{
    TEST("neutralNormalPreservesTheGeometricNormal");
    const Vec3 result = resolveNormalMap(
        { 0.0f, 0.0f, 2.0f },
        { 2.0f, 0.0f, 0.4f, 1.0f });
    CHECK(near(result, { 0.0f, 0.0f, 1.0f }));
}

void testNormalScaleAffectsOnlyTheTangentPlane()
{
    TEST("normalScaleAffectsOnlyTheTangentPlane");
    const Vec4 sample { 1.0f, 0.5f, 1.0f, 1.0f };
    const Vec3 unscaled = resolveNormalMap(
        { 0.0f, 0.0f, 1.0f },
        { 1.0f, 0.0f, 0.0f, 1.0f },
        sample,
        0.0f);
    CHECK(near(unscaled, { 0.0f, 0.0f, 1.0f }));

    const Vec3 scaled = resolveNormalMap(
        { 0.0f, 0.0f, 1.0f },
        { 1.0f, 0.0f, 0.0f, 1.0f },
        sample,
        1.0f);
    const float inverseSqrtTwo = std::sqrt(0.5f);
    CHECK(near(scaled, { inverseSqrtTwo, 0.0f, inverseSqrtTwo }));
}

void testTangentHandednessControlsTheBitangent()
{
    TEST("tangentHandednessControlsTheBitangent");
    const Vec4 bitangentSample { 0.5f, 1.0f, 1.0f, 1.0f };
    const Vec3 positive = resolveNormalMap(
        { 0.0f, 0.0f, 1.0f },
        { 1.0f, 0.0f, 0.0f, 1.0f },
        bitangentSample);
    const Vec3 negative = resolveNormalMap(
        { 0.0f, 0.0f, 1.0f },
        { 1.0f, 0.0f, 0.0f, -1.0f },
        bitangentSample);
    CHECK(positive.y > 0.0f);
    CHECK(negative.y < 0.0f);
    CHECK(near(positive.x, negative.x));
    CHECK(near(positive.z, negative.z));
}

void testDegenerateTangentGetsAnOrthonormalFallback()
{
    TEST("degenerateTangentGetsAnOrthonormalFallback");
    const Vec3 result = resolveNormalMap(
        { 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, 2.0f, 1.0f },
        { 1.0f, 0.5f, 1.0f, 1.0f });
    const float inverseSqrtTwo = std::sqrt(0.5f);
    CHECK(near(result, { 0.0f, -inverseSqrtTwo, inverseSqrtTwo }));
}

void testDoubleSidedBackFaceFlipsTheFinalMappedNormal()
{
    TEST("doubleSidedBackFaceFlipsTheFinalMappedNormal");
    const Vec3 front = resolveNormalMap(
        { 0.0f, 0.0f, 1.0f },
        { 1.0f, 0.0f, 0.0f, 1.0f },
        { 0.8f, 0.3f, 0.9f, 1.0f });
    const Vec3 back = resolveNormalMap(
        { 0.0f, 0.0f, 1.0f },
        { 1.0f, 0.0f, 0.0f, 1.0f },
        { 0.8f, 0.3f, 0.9f, 1.0f },
        1.0f,
        true);
    CHECK(near(back, -front));
}

void testMissingEmissiveMapPreservesTheFactor()
{
    TEST("missingEmissiveMapPreservesTheFactor");
    CHECK(near(
        resolveEmissive({ 0.25f, 0.5f, 0.75f }),
        { 0.25f, 0.5f, 0.75f }));
}

void testEmissiveMapMultipliesLinearRgbAndIgnoresAlpha()
{
    TEST("emissiveMapMultipliesLinearRgbAndIgnoresAlpha");
    const Vec3 result = resolveEmissive(
        { 0.8f, 0.6f, 0.4f },
        { 0.25f, 0.5f, 0.75f, 0.0f });
    CHECK(near(result, { 0.2f, 0.3f, 0.3f }));

    const Vec3 differentAlpha = resolveEmissive(
        { 0.8f, 0.6f, 0.4f },
        { 0.25f, 0.5f, 0.75f, 1.0f });
    CHECK(near(differentAlpha, result));
}

void testEmissiveResolutionPreservesHdrValues()
{
    TEST("emissiveResolutionPreservesHdrValues");
    const Vec3 result = resolveEmissive(
        { 4.0f, 3.0f, 2.0f },
        { 0.5f, 0.5f, 0.75f, 1.0f });
    CHECK(near(result, { 2.0f, 1.5f, 1.5f }));
    CHECK(result.x > 1.0f);
}

void testEmissiveIsExcludedFromTheAmbientNumerator()
{
    TEST("emissiveIsExcludedFromTheAmbientNumerator");
    const Vec3 ambient { 1.0f, 1.0f, 1.0f };
    const Vec3 direct { 1.0f, 1.0f, 1.0f };
    const Vec3 emissive { 2.0f, 2.0f, 2.0f };

    CHECK(near(ambientLightRatio(ambient, ambient + direct), 0.5f));
    CHECK(near(
        ambientLightRatio(ambient, ambient + direct + emissive),
        0.25f));
    CHECK(near(ambientLightRatio({}, emissive), 0.0f));
}

} // namespace

int main()
{
    testMissingMapPreservesFactors();
    testPackedChannelsMultiplyFactors();
    testPhysicalRangeIsAppliedAfterSampling();
    testNeutralNormalPreservesTheGeometricNormal();
    testNormalScaleAffectsOnlyTheTangentPlane();
    testTangentHandednessControlsTheBitangent();
    testDegenerateTangentGetsAnOrthonormalFallback();
    testDoubleSidedBackFaceFlipsTheFinalMappedNormal();
    testMissingEmissiveMapPreservesTheFactor();
    testEmissiveMapMultipliesLinearRgbAndIgnoresAlpha();
    testEmissiveResolutionPreservesHdrValues();
    testEmissiveIsExcludedFromTheAmbientNumerator();

    if (failures == 0) {
        std::cout << "PbrMaterialTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "PbrMaterialTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
