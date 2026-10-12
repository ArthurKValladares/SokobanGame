#include "TestHarness.hpp"
#include "ScopedTestDirectory.hpp"

#include "engine/render/CompressedTextureArtifact.hpp"
#include "engine/render/TextureMipResidency.hpp"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

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

void writeArtifact(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes)
{
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw std::runtime_error("Cannot write test KTX2 artifact");
    }
}

void writeU32(std::vector<std::byte>& bytes, std::size_t offset, uint32_t value)
{
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        bytes[offset++] = static_cast<std::byte>(value >> shift);
    }
}

void writeU64(std::vector<std::byte>& bytes, std::size_t offset, uint64_t value)
{
    for (uint32_t shift = 0; shift < 64; shift += 8) {
        bytes[offset++] = static_cast<std::byte>(value >> shift);
    }
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

    CHECK_MESSAGE(artifact.format == sokoban::CompressedTextureFormat::Bc7Srgb,
        "sRGB interpretation selects BC7 SRGB");
    CHECK_MESSAGE(artifact.width == 5 && artifact.height == 3,
        "base dimensions survive KTX2 round trip");
    CHECK_MESSAGE(artifact.mips.size() == 3,
        "mipmapped filter emits a complete odd-sized pyramid");
    CHECK_MESSAGE(artifact.mips[0].width == 5 && artifact.mips[0].height == 3,
        "level zero dimensions preserved");
    CHECK_MESSAGE(artifact.mips[1].width == 2 && artifact.mips[1].height == 1,
        "odd dimensions halve using Vulkan mip rules");
    CHECK_MESSAGE(artifact.mips[2].width == 1 && artifact.mips[2].height == 1,
        "pyramid terminates at one texel");
    CHECK_MESSAGE(artifact.mips[0].bytes.size() == 32,
        "base BC7 level has one block per partial 4x4 region");
    CHECK_MESSAGE(artifact.mips[1].bytes.size() == 16 &&
            artifact.mips[2].bytes.size() == 16,
        "small BC7 levels remain one whole block");
    CHECK_MESSAGE(artifact.residentBytes() == 64,
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
    CHECK_MESSAGE(artifact.format == sokoban::CompressedTextureFormat::Bc7Unorm,
        "linear interpretation selects BC7 UNORM");
    CHECK_MESSAGE(artifact.mips.size() == 1,
        "non-mipmapped sampler stores only the base level");

    const sokoban::TextureSourceIdentity linearIdentity {
        .source = sokoban::ExternalTextureSource { "textures/a.png" },
        .interpretation = linear,
    };
    sokoban::TextureSourceIdentity srgbIdentity = linearIdentity;
    srgbIdentity.interpretation.colorSpace = sokoban::TextureColorSpace::Srgb;
    CHECK_MESSAGE(
        sokoban::compressedTextureArtifactPath(linearIdentity) ==
            sokoban::compressedTextureArtifactPath(linearIdentity),
        "artifact path is deterministic");
    CHECK_MESSAGE(
        sokoban::compressedTextureArtifactPath(linearIdentity) !=
            sokoban::compressedTextureArtifactPath(srgbIdentity),
        "colour interpretation participates in artifact identity");
}

void testMalformedArtifactsAreRejected()
{
    const ScopedTestDirectory temp("sokoban-ktx2-inspection");
    const std::filesystem::path path = temp.path() / "malformed.ktx2";
    const auto reject = [&](const std::vector<std::byte>& malformed,
                            const char* label) {
        writeArtifact(path, malformed);
        checkThrows([&] { (void)sokoban::parseBc7Ktx2(malformed); }, label);
        checkThrows([&] { (void)sokoban::loadBc7Ktx2(path); }, label);
        checkThrows([&] { (void)sokoban::inspectBc7Ktx2(path); }, label);
    };
    sokoban::TextureInterpretation interpretation {};
    const std::vector<std::byte> bytes =
        sokoban::buildBc7Ktx2(oddImage(), interpretation);

    std::vector<std::byte> badIdentifier = bytes;
    badIdentifier[0] = std::byte { 0 };
    reject(badIdentifier, "bad KTX2 identifier rejected");

    reject(std::vector<std::byte>(bytes.begin(), bytes.begin() + 20),
        "truncated header rejected");
    reject(std::vector<std::byte>(bytes.begin(), bytes.begin() + 90),
        "truncated level index rejected");
    reject(std::vector<std::byte>(bytes.begin(), bytes.begin() + 160),
        "truncated data format descriptor rejected");
    reject(std::vector<std::byte>(bytes.begin(), bytes.end() - 1),
        "truncated mip payload rejected even during metadata inspection");

    std::vector<std::byte> wrongTransfer = bytes;
    // DFD begins at 80 + 3 * 24 = 152. Transfer is byte 2 of model word.
    wrongTransfer[152 + 12 + 2] = std::byte { 1 };
    reject(wrongTransfer, "sRGB Vulkan format with linear DFD rejected");

    std::vector<std::byte> wrongDescriptor = bytes;
    writeU32(wrongDescriptor, 48, 0xFFFFFFF0U);
    reject(wrongDescriptor, "out-of-range data format descriptor rejected");

    std::vector<std::byte> wrongLevelCount = bytes;
    writeU32(wrongLevelCount, 40, 0xFFFFFFFFU);
    reject(wrongLevelCount, "unbounded mip count rejected before reading metadata");

    std::vector<std::byte> wrongOffset = bytes;
    writeU64(wrongOffset, 80, static_cast<uint64_t>(bytes.size()) + 16U);
    reject(wrongOffset, "out-of-range mip offset rejected");
    writeU64(wrongOffset, 80, 193);
    reject(wrongOffset, "unaligned mip offset rejected");

    std::vector<std::byte> wrongLength = bytes;
    writeU64(wrongLength, 88, 16);
    reject(wrongLength, "wrong mip payload length rejected");
    writeU64(wrongLength, 88, 32);
    writeU64(wrongLength, 96, 16);
    reject(wrongLength, "mismatched uncompressed length rejected");

    std::vector<std::byte> oversizedDimensions = bytes;
    writeU32(oversizedDimensions, 20, 0xFFFFFFFFU);
    writeU32(oversizedDimensions, 24, 0xFFFFFFFFU);
    writeU64(oversizedDimensions, 88, 0);
    writeU64(oversizedDimensions, 96, 0);
    reject(oversizedDimensions, "overflowing mip dimensions rejected");
}

void testMetadataInspectionMatchesLoadedPayload()
{
    const ScopedTestDirectory temp("sokoban-ktx2-metadata");
    const std::filesystem::path path = temp.path() / "texture.ktx2";
    for (const auto colorSpace : {
             sokoban::TextureColorSpace::Srgb,
             sokoban::TextureColorSpace::Linear,
         }) {
        for (const auto minFilter : {
                 sokoban::TextureMinificationFilter::Linear,
                 sokoban::TextureMinificationFilter::LinearMipmapLinear,
             }) {
            const std::vector<std::byte> bytes = sokoban::buildBc7Ktx2(
                oddImage(), { .colorSpace = colorSpace, .minFilter = minFilter });
            writeArtifact(path, bytes);
            const sokoban::CompressedTextureArtifact loaded = sokoban::loadBc7Ktx2(path);
            const sokoban::CompressedTextureArtifactMetadata metadata =
                sokoban::inspectBc7Ktx2(path);
            CHECK(metadata.format == loaded.format);
            CHECK(metadata.width == loaded.width);
            CHECK(metadata.height == loaded.height);
            CHECK(metadata.mipLevels == loaded.mips.size());
            CHECK(metadata.residentBytes == loaded.residentBytes());
            CHECK(metadata.preparedBytes() == loaded.residentBytes() +
                loaded.mips.size() * sizeof(sokoban::CompressedTextureMip));
        }
    }
    checkThrows([&] {
        (void)sokoban::inspectBc7Ktx2(temp.path() / "missing.ktx2");
    }, "missing artifact inspection is reported");
}

void testMipResidencyChoosesTheFinestCompleteTailThatFits()
{
    const sokoban::CompressedTextureArtifact artifact =
        sokoban::parseBc7Ktx2(sokoban::buildBc7Ktx2(
            oddImage(), sokoban::TextureInterpretation {}));

    const auto full = sokoban::chooseTextureMipResidency(artifact, 64);
    CHECK_MESSAGE(full && full->sourceBaseMip == 0 && !full->degraded(),
        "full mip chain wins when it fits");
    CHECK_MESSAGE(full && full->residentBytes == 64 &&
            full->fullQualityBytes == 64 && full->omittedBytes() == 0,
        "full-quality residency accounting is exact");

    const auto middle = sokoban::chooseTextureMipResidency(artifact, 32);
    CHECK_MESSAGE(middle && middle->sourceBaseMip == 1,
        "first source mip is omitted under moderate pressure");
    CHECK_MESSAGE(middle && middle->width == 2 && middle->height == 1 &&
            middle->mipLevels == 2,
        "selected source mip becomes the resident base level");
    CHECK_MESSAGE(middle && middle->residentBytes == 32 &&
            middle->omittedBytes() == 32,
        "reduced residency reports saved bytes");

    const auto smallest = sokoban::chooseTextureMipResidency(artifact, 16);
    CHECK_MESSAGE(smallest && smallest->sourceBaseMip == 2 &&
            smallest->mipLevels == 1,
        "smallest complete tail remains a valid last resort");
    CHECK_MESSAGE(!sokoban::chooseTextureMipResidency(artifact, 15),
        "selection fails when even the smallest compressed block cannot fit");

    sokoban::CompressedTextureArtifact empty;
    checkThrows(
        [&] { (void)sokoban::chooseTextureMipResidency(empty, 64); },
        "empty texture cannot produce a residency plan");
}

void testInvalidationCoversEveryInterpretationOfOneSource()
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("sokoban_artifact_invalidation_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::filesystem::path source = "textures/authored.png";
    const std::filesystem::path otherSource = "textures/other.png";
    sokoban::TextureSourceIdentity first {
        .source = sokoban::ExternalTextureSource { source },
    };
    sokoban::TextureSourceIdentity second = first;
    second.interpretation.colorSpace = sokoban::TextureColorSpace::Linear;
    second.interpretation.wrapU =
        sokoban::TextureAddressMode::ClampToEdge;
    sokoban::TextureSourceIdentity unrelated = first;
    unrelated.source = sokoban::ExternalTextureSource { otherSource };

    const auto createArtifact = [&](const sokoban::TextureSourceIdentity& id) {
        const std::filesystem::path path =
            root / sokoban::compressedTextureArtifactPath(id);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << "prepared";
        return path;
    };
    const std::filesystem::path firstPath = createArtifact(first);
    const std::filesystem::path secondPath = createArtifact(second);
    const std::filesystem::path unrelatedPath = createArtifact(unrelated);

    sokoban::invalidateCompressedTextureArtifactsForSource(root, source);
    CHECK_MESSAGE(!std::filesystem::exists(firstPath),
        "default interpretation is invalidated");
    CHECK_MESSAGE(!std::filesystem::exists(secondPath),
        "alternate interpretation is invalidated");
    CHECK_MESSAGE(std::filesystem::exists(unrelatedPath),
        "an unrelated authored texture keeps its prepared artifact");
    checkThrows([&] {
        sokoban::invalidateCompressedTextureArtifactsForSource(
            root, "../outside.png");
    }, "source traversal is rejected");

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

} // namespace

int main()
{
    testRoundTripPreservesFormatDimensionsAndMipBytes();
    testLinearNonMipmappedArtifactAndStableIdentityPath();
    testMalformedArtifactsAreRejected();
    testMetadataInspectionMatchesLoadedPayload();
    testMipResidencyChoosesTheFinestCompleteTailThatFits();
    testInvalidationCoversEveryInterpretationOfOneSource();

    if (failures == 0) {
        std::cout << "CompressedTextureArtifactTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "CompressedTextureArtifactTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
