// What a texture upload decides before it touches the device.
//
// None of this was reachable from a test before: it lived inside two
// `beginTextureUpload` overloads that need a live VkDevice to call. Getting any
// of it wrong is quiet - a texture uploaded as UNORM when it should be SRGB
// looks washed out, an image sized for the wrong number of mip levels blits
// past its own last level - so it is worth pinning now that it can be.

#include "engine/render/TextureUploadPlan.hpp"

#include "TestHarness.hpp"

#include <algorithm>
#include <cmath>

using namespace sokoban;
using namespace sokoban::textureUploadPlan;

namespace {

// The implementation this replaced, transcribed. It sized the image in
// VulkanModelResources while the halving form built the chain in
// CompressedTextureArtifact, so the two had to agree and nothing checked.
uint32_t byLog2(uint32_t width, uint32_t height)
{
    const uint32_t largestDimension = std::max(width, height);
    return largestDimension == 0
        ? 1U
        : 1U + static_cast<uint32_t>(
              std::floor(std::log2(static_cast<double>(largestDimension))));
}

void testMipLevelCountMatchesTheFormItReplaced()
{
    TEST("mip level count");
    // Every dimension pair a texture in this game could plausibly have, plus
    // the powers of two either side of where a double's log2 could round.
    int disagreements = 0;
    for (uint32_t width = 0; width <= 2048; ++width) {
        for (uint32_t height = 0; height <= 2048;
             height += (height < 32 ? 1 : 61)) {
            if (mipLevelCount(width, height) != byLog2(width, height)) {
                ++disagreements;
            }
        }
    }
    CHECK(disagreements == 0);

    for (int exponent = 0; exponent <= 30; ++exponent) {
        const uint32_t power = 1U << exponent;
        for (uint32_t dimension : { power - 1U, power, power + 1U }) {
            CHECK(mipLevelCount(dimension, 1) == byLog2(dimension, 1));
        }
    }

    // The values themselves, so a rewrite of both forms at once still fails.
    CHECK(mipLevelCount(1, 1) == 1);
    CHECK(mipLevelCount(2, 1) == 2);
    CHECK(mipLevelCount(1024, 1024) == 11);
    CHECK(mipLevelCount(1024, 1) == 11);
    CHECK(mipLevelCount(1, 1024) == 11);
    // Non-square and non-power-of-two: the longer axis decides.
    CHECK(mipLevelCount(640, 480) == 10);
    CHECK(mipLevelCount(3, 3) == 2);
    // A degenerate source still gets a level, rather than an image with none.
    CHECK(mipLevelCount(0, 0) == 1);
    CHECK(mipLevelCount(0, 512) == 10);
}

void testUsesMipmaps()
{
    TEST("filters that ask for a chain");
    CHECK(usesMipmaps(TextureMinificationFilter::NearestMipmapNearest));
    CHECK(usesMipmaps(TextureMinificationFilter::LinearMipmapNearest));
    CHECK(usesMipmaps(TextureMinificationFilter::NearestMipmapLinear));
    CHECK(usesMipmaps(TextureMinificationFilter::LinearMipmapLinear));
    CHECK(!usesMipmaps(TextureMinificationFilter::Nearest));
    CHECK(!usesMipmaps(TextureMinificationFilter::Linear));
}

void testFormatChoice()
{
    TEST("format choice");
    CHECK(uncompressedFormat(TextureColorSpace::Srgb)
        == VK_FORMAT_R8G8B8A8_SRGB);
    CHECK(uncompressedFormat(TextureColorSpace::Linear)
        == VK_FORMAT_R8G8B8A8_UNORM);
    CHECK(compressedFormat(CompressedTextureFormat::Bc7Srgb)
        == VK_FORMAT_BC7_SRGB_BLOCK);
    CHECK(compressedFormat(CompressedTextureFormat::Bc7Unorm)
        == VK_FORMAT_BC7_UNORM_BLOCK);

    // The agreement check both ways round, because a mismatch either way
    // means a gamma curve applied twice or not at all.
    CHECK(colorSpaceAgrees(VK_FORMAT_BC7_SRGB_BLOCK, TextureColorSpace::Srgb));
    CHECK(colorSpaceAgrees(
        VK_FORMAT_BC7_UNORM_BLOCK, TextureColorSpace::Linear));
    CHECK(!colorSpaceAgrees(
        VK_FORMAT_BC7_SRGB_BLOCK, TextureColorSpace::Linear));
    CHECK(!colorSpaceAgrees(
        VK_FORMAT_BC7_UNORM_BLOCK, TextureColorSpace::Srgb));
}

void testCapabilityGate()
{
    TEST("capability gate");
    CHECK(supports(mipGenerationFeatures, mipGenerationFeatures));
    CHECK(supports(
        mipGenerationFeatures | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT,
        mipGenerationFeatures));
    // Each required bit on its own is not enough.
    CHECK(!supports(VK_FORMAT_FEATURE_BLIT_SRC_BIT, mipGenerationFeatures));
    CHECK(!supports(VK_FORMAT_FEATURE_BLIT_DST_BIT, mipGenerationFeatures));
    CHECK(!supports(
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT,
        mipGenerationFeatures));
    CHECK(!supports(0, mipGenerationFeatures));
    // Dropping any one bit fails, which is what "== required" is for.
    CHECK(!supports(
        mipGenerationFeatures & ~VK_FORMAT_FEATURE_BLIT_DST_BIT,
        mipGenerationFeatures));
    CHECK(supports(compressedSamplingFeatures, compressedSamplingFeatures));
    CHECK(!supports(
        compressedSamplingFeatures & ~VK_FORMAT_FEATURE_TRANSFER_DST_BIT,
        compressedSamplingFeatures));
}

void testGeneratesMipmapsIsNotTheSameAsMoreThanOneLevel()
{
    TEST("generatesMipmaps vs mipLevels > 1");
    constexpr auto full = mipGenerationFeatures;
    constexpr auto filter = TextureMinificationFilter::LinearMipmapLinear;
    // The case that makes these two different questions, and the reason the
    // predicate is named rather than folded into the count: a 1x1 texture
    // generates mipmaps and has exactly one level.
    CHECK(generatesMipmaps(filter, full));
    CHECK(uncompressedMipLevels(1, 1, filter, full) == 1);
    CHECK(generatesMipmaps(filter, full)
        != (uncompressedMipLevels(1, 1, filter, full) > 1));
    // For anything larger they agree, which is why the difference hides.
    CHECK(generatesMipmaps(filter, full)
        == (uncompressedMipLevels(2, 1, filter, full) > 1));
    CHECK(!generatesMipmaps(TextureMinificationFilter::Linear, full));
    CHECK(!generatesMipmaps(filter, 0));
}

void testUncompressedMipLevels()
{
    TEST("uncompressed mip levels");
    constexpr auto full = mipGenerationFeatures;
    constexpr auto none = VkFormatFeatureFlags { 0 };
    // Both conditions must hold.
    CHECK(uncompressedMipLevels(
              256, 256, TextureMinificationFilter::LinearMipmapLinear, full)
        == 9);
    // The filter does not want a chain.
    CHECK(uncompressedMipLevels(
              256, 256, TextureMinificationFilter::Linear, full) == 1);
    // The format cannot blit, so one level rather than a broken chain.
    CHECK(uncompressedMipLevels(
              256, 256, TextureMinificationFilter::LinearMipmapLinear, none)
        == 1);
    CHECK(uncompressedMipLevels(
              256, 256, TextureMinificationFilter::Linear, none) == 1);
    // Partial support is not support.
    CHECK(uncompressedMipLevels(
              256,
              256,
              TextureMinificationFilter::LinearMipmapLinear,
              VK_FORMAT_FEATURE_BLIT_SRC_BIT)
        == 1);
}

void testCompressedMipLevels()
{
    TEST("compressed mip levels");
    CHECK(compressedMipLevels(11, 0) == 11);
    // Dropping the top levels under memory pressure: the base moves down and
    // the published count shrinks with it.
    CHECK(compressedMipLevels(11, 3) == 8);
    CHECK(compressedMipLevels(11, 10) == 1);
}

void testSourceValidity()
{
    TEST("source validity");
    CHECK(uncompressedSourceIsUsable(4, 4, true));
    CHECK(!uncompressedSourceIsUsable(0, 4, true));
    CHECK(!uncompressedSourceIsUsable(4, 0, true));
    CHECK(!uncompressedSourceIsUsable(4, 4, false));

    CHECK(compressedSourceIsUsable(4, 4, 3, 0));
    CHECK(compressedSourceIsUsable(4, 4, 3, 2));
    // A base at or past the end has no data to publish - the case that would
    // otherwise index off the end of the mip vector.
    CHECK(!compressedSourceIsUsable(4, 4, 3, 3));
    CHECK(!compressedSourceIsUsable(4, 4, 3, 99));
    CHECK(!compressedSourceIsUsable(4, 4, 0, 0));
    CHECK(!compressedSourceIsUsable(0, 4, 3, 0));
    CHECK(!compressedSourceIsUsable(4, 0, 3, 0));
}

} // namespace

int main()
{
    testMipLevelCountMatchesTheFormItReplaced();
    testUsesMipmaps();
    testFormatChoice();
    testCapabilityGate();
    testGeneratesMipmapsIsNotTheSameAsMoreThanOneLevel();
    testUncompressedMipLevels();
    testCompressedMipLevels();
    testSourceValidity();

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "texture_upload_plan: " << checks << " checks passed\n";
    return 0;
}
