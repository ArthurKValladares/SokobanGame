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

} // namespace

int main()
{
    testMissingMapPreservesFactors();
    testPackedChannelsMultiplyFactors();
    testPhysicalRangeIsAppliedAfterSampling();

    if (failures == 0) {
        std::cout << "PbrMaterialTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "PbrMaterialTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
