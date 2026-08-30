#include "engine/render/CompressedTextureArtifact.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* label)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

template <typename Function>
void checkThrows(Function&& function, const char* label)
{
    try {
        function();
        check(false, label);
    } catch (const std::exception&) {
        check(true, label);
    }
}

sokoban::ImageData oddImage()
{
    sokoban::ImageData image { .width = 5, .height = 3 };
    image.rgba.resize(5U * 3U * 4U);
    for (std::size_t pixel = 0; pixel < 15; ++pixel) {
        image.rgba[pixel * 4U] = static_cast<std::byte>(pixel * 13U);
        image.rgba[pixel * 4U + 1U] =
            static_cast<std::byte>(255U - pixel * 11U);
        image.rgba[pixel * 4U + 2U] =
            static_cast<std::byte>(pixel * 7U);
        image.rgba[pixel * 4U + 3U] =
            static_cast<std::byte>(pixel == 7 ? 96U : 255U);
    }
    return image;
}

void testRoundTripPreservesFormatDimensionsAndMipBytes()
{
    sokoban::TextureInterpretation interpretation {
        .colorSpace = sokoban::TextureColorSpace::Srgb,
    };
    const std::vector<std::byte> bytes =
        sokoban::buildBc7Ktx2(oddImage(), interpretation);
    const sokoban::CompressedTextureArtifact artifact =
        sokoban::parseBc7Ktx2(bytes);

    check(artifact.format == sokoban::CompressedTextureFormat::Bc7Srgb,
        "sRGB interpretation selects BC7 SRGB");
    check(artifact.width == 5 && artifact.height == 3,
        "base dimensions survive KTX2 round trip");
    check(artifact.mips.size() == 3,
        "mipmapped filter emits a complete odd-sized pyramid");
    check(artifact.mips[0].width == 5 && artifact.mips[0].height == 3,
        "level zero dimensions preserved");
    check(artifact.mips[1].width == 2 && artifact.mips[1].height == 1,
        "odd dimensions halve using Vulkan mip rules");
    check(artifact.mips[2].width == 1 && artifact.mips[2].height == 1,
        "pyramid terminates at one texel");
    check(artifact.mips[0].bytes.size() == 32,
        "base BC7 level has one block per partial 4x4 region");
    check(artifact.mips[1].bytes.size() == 16 &&
            artifact.mips[2].bytes.size() == 16,
        "small BC7 levels remain one whole block");
    check(artifact.residentBytes() == 64,
        "resident byte count is the exact sum of compressed mips");
}

void testLinearNonMipmappedArtifactAndStableIdentityPath()
{
    sokoban::TextureInterpretation linear {
        .colorSpace = sokoban::TextureColorSpace::Linear,
        .minFilter = sokoban::TextureMinificationFilter::Linear,
    };
    const sokoban::CompressedTextureArtifact artifact =
        sokoban::parseBc7Ktx2(sokoban::buildBc7Ktx2(oddImage(), linear));
    check(artifact.format == sokoban::CompressedTextureFormat::Bc7Unorm,
        "linear interpretation selects BC7 UNORM");
    check(artifact.mips.size() == 1,
        "non-mipmapped sampler stores only the base level");

    const sokoban::TextureSourceIdentity linearIdentity {
        .source = sokoban::ExternalTextureSource { "textures/a.png" },
        .interpretation = linear,
    };
    sokoban::TextureSourceIdentity srgbIdentity = linearIdentity;
    srgbIdentity.interpretation.colorSpace = sokoban::TextureColorSpace::Srgb;
    check(
        sokoban::compressedTextureArtifactPath(linearIdentity) ==
            sokoban::compressedTextureArtifactPath(linearIdentity),
        "artifact path is deterministic");
    check(
        sokoban::compressedTextureArtifactPath(linearIdentity) !=
            sokoban::compressedTextureArtifactPath(srgbIdentity),
        "colour interpretation participates in artifact identity");
}

void testMalformedArtifactsAreRejected()
{
    sokoban::TextureInterpretation interpretation {};
    std::vector<std::byte> bytes =
        sokoban::buildBc7Ktx2(oddImage(), interpretation);

    std::vector<std::byte> badIdentifier = bytes;
    badIdentifier[0] = std::byte { 0 };
    checkThrows(
        [&] { (void)sokoban::parseBc7Ktx2(badIdentifier); },
        "bad KTX2 identifier rejected");

    std::vector<std::byte> truncated(bytes.begin(), bytes.begin() + 90);
    checkThrows(
        [&] { (void)sokoban::parseBc7Ktx2(truncated); },
        "truncated level payload rejected");

    std::vector<std::byte> wrongTransfer = bytes;
    // DFD begins at 80 + 3 * 24 = 152. Transfer is byte 2 of model word.
    wrongTransfer[152 + 12 + 2] = std::byte { 1 };
    checkThrows(
        [&] { (void)sokoban::parseBc7Ktx2(wrongTransfer); },
        "sRGB Vulkan format with linear DFD rejected");
}

} // namespace

int main()
{
    testRoundTripPreservesFormatDimensionsAndMipBytes();
    testLinearNonMipmappedArtifactAndStableIdentityPath();
    testMalformedArtifactsAreRejected();

    if (failures == 0) {
        std::cout << "CompressedTextureArtifactTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "CompressedTextureArtifactTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
