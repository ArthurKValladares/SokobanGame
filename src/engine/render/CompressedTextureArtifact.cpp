#include "engine/render/CompressedTextureArtifact.hpp"

#include "engine/render/TextureUploadPlan.hpp"

#include <bc7enc16.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>

namespace sokoban {
namespace {

constexpr std::array<std::byte, 12> ktx2Identifier {
    std::byte { 0xAB }, std::byte { 0x4B }, std::byte { 0x54 },
    std::byte { 0x58 }, std::byte { 0x20 }, std::byte { 0x32 },
    std::byte { 0x30 }, std::byte { 0xBB }, std::byte { 0x0D },
    std::byte { 0x0A }, std::byte { 0x1A }, std::byte { 0x0A },
};
constexpr uint64_t fnvOffset = 14695981039346656037ULL;
constexpr uint64_t fnvPrime = 1099511628211ULL;
constexpr uint32_t ktx2HeaderBytes = 80;
constexpr uint32_t dfdBytes = 44;

// usesMipmaps and mipCount lived here as well as in VulkanModelResources,
// under two names and, for the count, two algorithms. Both now come from
// TextureUploadPlan, which is also what the upload side reads - the chain
// built here and the image sized there cannot disagree if they ask the same
// function.
using textureUploadPlan::usesMipmaps;
constexpr auto mipCount = textureUploadPlan::mipLevelCount;

float srgbToLinear(float value)
{
    return value <= 0.04045f
        ? value / 12.92f
        : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float value)
{
    return value <= 0.0031308f
        ? value * 12.92f
        : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

uint8_t byteAt(const std::vector<std::byte>& bytes, std::size_t index)
{
    return std::to_integer<uint8_t>(bytes[index]);
}

std::byte quantize(float value)
{
    const float scaled = std::clamp(value, 0.0f, 1.0f) * 255.0f;
    return static_cast<std::byte>(static_cast<uint8_t>(std::lround(scaled)));
}

ImageData nextMip(const ImageData& source, TextureColorSpace colorSpace)
{
    ImageData result {
        .width = std::max(source.width / 2U, 1U),
        .height = std::max(source.height / 2U, 1U),
    };
    result.rgba.resize(
        static_cast<std::size_t>(result.width) * result.height * 4U);
    for (uint32_t y = 0; y < result.height; ++y) {
        for (uint32_t x = 0; x < result.width; ++x) {
            std::array<float, 4> sum {};
            for (uint32_t dy = 0; dy < 2; ++dy) {
                for (uint32_t dx = 0; dx < 2; ++dx) {
                    const uint32_t sourceX =
                        std::min(x * 2U + dx, source.width - 1U);
                    const uint32_t sourceY =
                        std::min(y * 2U + dy, source.height - 1U);
                    const std::size_t offset =
                        (static_cast<std::size_t>(sourceY) * source.width +
                            sourceX) * 4U;
                    for (std::size_t channel = 0; channel < 4; ++channel) {
                        float value = byteAt(source.rgba, offset + channel) /
                            255.0f;
                        if (colorSpace == TextureColorSpace::Srgb &&
                            channel < 3) {
                            value = srgbToLinear(value);
                        }
                        sum[channel] += value;
                    }
                }
            }
            const std::size_t destination =
                (static_cast<std::size_t>(y) * result.width + x) * 4U;
            for (std::size_t channel = 0; channel < 4; ++channel) {
                float value = sum[channel] * 0.25f;
                if (colorSpace == TextureColorSpace::Srgb && channel < 3) {
                    value = linearToSrgb(value);
                }
                result.rgba[destination + channel] = quantize(value);
            }
        }
    }
    return result;
}

std::vector<std::byte> compressBc7(
    const ImageData& image,
    TextureColorSpace colorSpace)
{
    static std::once_flag initialized;
    std::call_once(initialized, bc7enc16_compress_block_init);

    bc7enc16_compress_block_params parameters {};
    bc7enc16_compress_block_params_init(&parameters);
    // A bounded partition search makes clean builds practical while retaining
    // mode 1 for opaque content and mode 6 for alpha.
    parameters.m_max_partitions_mode1 = 16;
    if (colorSpace == TextureColorSpace::Linear) {
        bc7enc16_compress_block_params_init_linear_weights(&parameters);
    }

    const uint32_t blocksWide = (image.width + 3U) / 4U;
    const uint32_t blocksHigh = (image.height + 3U) / 4U;
    std::vector<std::byte> result(
        static_cast<std::size_t>(blocksWide) * blocksHigh *
        BC7ENC16_BLOCK_SIZE);
    std::array<uint8_t, 4U * 4U * 4U> block {};
    for (uint32_t blockY = 0; blockY < blocksHigh; ++blockY) {
        for (uint32_t blockX = 0; blockX < blocksWide; ++blockX) {
            for (uint32_t y = 0; y < 4; ++y) {
                for (uint32_t x = 0; x < 4; ++x) {
                    const uint32_t sourceX =
                        std::min(blockX * 4U + x, image.width - 1U);
                    const uint32_t sourceY =
                        std::min(blockY * 4U + y, image.height - 1U);
                    const std::size_t sourceOffset =
                        (static_cast<std::size_t>(sourceY) * image.width +
                            sourceX) * 4U;
                    const std::size_t blockOffset =
                        (static_cast<std::size_t>(y) * 4U + x) * 4U;
                    for (std::size_t channel = 0; channel < 4; ++channel) {
                        block[blockOffset + channel] =
                            byteAt(image.rgba, sourceOffset + channel);
                    }
                }
            }
            const std::size_t destination =
                (static_cast<std::size_t>(blockY) * blocksWide + blockX) *
                BC7ENC16_BLOCK_SIZE;
            (void)bc7enc16_compress_block(
                result.data() + destination, block.data(), &parameters);
        }
    }
    return result;
}

void appendU32(std::vector<std::byte>& bytes, uint32_t value)
{
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::byte>(value >> shift));
    }
}

void appendU64(std::vector<std::byte>& bytes, uint64_t value)
{
    for (uint32_t shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<std::byte>(value >> shift));
    }
}

void writeU64(std::vector<std::byte>& bytes, std::size_t offset, uint64_t value)
{
    for (uint32_t shift = 0; shift < 64; shift += 8) {
        bytes.at(offset++) = static_cast<std::byte>(value >> shift);
    }
}

uint32_t readU32(std::span<const std::byte> bytes, std::size_t offset)
{
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        throw std::runtime_error("truncated KTX2 integer");
    }
    uint32_t result = 0;
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        result |= static_cast<uint32_t>(
            std::to_integer<uint8_t>(bytes[offset++])) << shift;
    }
    return result;
}

uint64_t readU64(std::span<const std::byte> bytes, std::size_t offset)
{
    if (offset > bytes.size() || bytes.size() - offset < 8) {
        throw std::runtime_error("truncated KTX2 integer");
    }
    uint64_t result = 0;
    for (uint32_t shift = 0; shift < 64; shift += 8) {
        result |= static_cast<uint64_t>(
            std::to_integer<uint8_t>(bytes[offset++])) << shift;
    }
    return result;
}

uint64_t alignUp(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1U) / alignment * alignment;
}

std::string diagnosticLabel(const std::filesystem::path& path)
{
    return path.empty() ? "KTX2 texture" : path.string();
}

[[noreturn]] void invalidKtx(
    const std::filesystem::path& path,
    std::string_view reason)
{
    throw std::runtime_error(
        "Invalid BC7 KTX2 artifact " + diagnosticLabel(path) + ": " +
        std::string(reason));
}

} // namespace

uint64_t CompressedTextureArtifact::residentBytes() const
{
    return std::accumulate(
        mips.begin(), mips.end(), uint64_t { 0 },
        [](uint64_t total, const CompressedTextureMip& mip) {
            return total + mip.bytes.size();
        });
}

std::filesystem::path compressedTextureArtifactPath(
    const TextureSourceIdentity& identity)
{
    uint64_t hash = fnvOffset;
    for (unsigned char byte : textureSourceIdentityKey(identity)) {
        hash ^= byte;
        hash *= fnvPrime;
    }
    std::ostringstream name;
    name << std::hex << std::setfill('0') << std::setw(16) << hash << ".ktx2";
    return std::filesystem::path("compiled-textures") / name.str();
}

std::vector<std::byte> buildBc7Ktx2(
    const ImageData& source,
    const TextureInterpretation& interpretation)
{
    const uint64_t expectedBytes = static_cast<uint64_t>(source.width) *
        source.height * 4U;
    if (source.width == 0 || source.height == 0 ||
        source.rgba.size() != expectedBytes) {
        throw std::runtime_error("Cannot compress an invalid RGBA texture");
    }

    const uint32_t levels = usesMipmaps(interpretation.minFilter)
        ? mipCount(source.width, source.height)
        : 1U;
    std::vector<CompressedTextureMip> mips;
    mips.reserve(levels);
    ImageData image = source;
    for (uint32_t level = 0; level < levels; ++level) {
        mips.push_back({
            .width = image.width,
            .height = image.height,
            .bytes = compressBc7(image, interpretation.colorSpace),
        });
        if (level + 1U < levels) {
            image = nextMip(image, interpretation.colorSpace);
        }
    }

    const uint32_t dfdOffset = ktx2HeaderBytes + levels * 24U;
    const uint64_t imageDataStart = alignUp(dfdOffset + dfdBytes, 16U);
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(imageDataStart) +
        static_cast<std::size_t>(std::accumulate(
            mips.begin(), mips.end(), uint64_t { 0 },
            [](uint64_t total, const CompressedTextureMip& mip) {
                return total + mip.bytes.size();
            })));
    bytes.insert(bytes.end(), ktx2Identifier.begin(), ktx2Identifier.end());
    appendU32(bytes, static_cast<uint32_t>(
        interpretation.colorSpace == TextureColorSpace::Srgb
            ? CompressedTextureFormat::Bc7Srgb
            : CompressedTextureFormat::Bc7Unorm));
    appendU32(bytes, 1); // typeSize for Vulkan block formats
    appendU32(bytes, source.width);
    appendU32(bytes, source.height);
    appendU32(bytes, 0); // pixelDepth
    appendU32(bytes, 0); // layerCount: not an array
    appendU32(bytes, 1); // faceCount
    appendU32(bytes, levels);
    appendU32(bytes, 0); // no supercompression
    appendU32(bytes, dfdOffset);
    appendU32(bytes, dfdBytes);
    appendU32(bytes, 0); // no key/value data
    appendU32(bytes, 0);
    appendU64(bytes, 0); // no supercompression global data
    appendU64(bytes, 0);

    const std::size_t levelIndexOffset = bytes.size();
    bytes.resize(bytes.size() + static_cast<std::size_t>(levels) * 24U);
    bytes.resize(static_cast<std::size_t>(imageDataStart));

    // Khronos requires physical mip data from smallest to largest while the
    // level index itself remains base-level first.
    for (std::size_t reverse = mips.size(); reverse-- > 0;) {
        bytes.resize(static_cast<std::size_t>(alignUp(bytes.size(), 16U)));
        const uint64_t offset = bytes.size();
        const CompressedTextureMip& mip = mips[reverse];
        bytes.insert(bytes.end(), mip.bytes.begin(), mip.bytes.end());
        const std::size_t index = levelIndexOffset + reverse * 24U;
        writeU64(bytes, index, offset);
        writeU64(bytes, index + 8U, mip.bytes.size());
        writeU64(bytes, index + 16U, mip.bytes.size());
    }

    // Fill the reserved DFD region between the level index and mip data.
    std::vector<std::byte> dfd;
    appendU32(dfd, dfdBytes);
    appendU32(dfd, 0); // Khronos vendor, basic descriptor
    appendU32(dfd, 2U | (40U << 16U));
    const uint32_t transfer = interpretation.colorSpace == TextureColorSpace::Srgb
        ? 2U
        : 1U;
    appendU32(dfd, 134U | (1U << 8U) | (transfer << 16U));
    appendU32(dfd, 3U | (3U << 8U)); // 4x4x1x1 block dimensions minus one
    appendU32(dfd, 16U); // bytesPlane0
    appendU32(dfd, 0);
    appendU32(dfd, 127U << 16U); // one 128-bit BC7 colour sample
    appendU32(dfd, 0);
    appendU32(dfd, 0);
    appendU32(dfd, 0xFFFFFFFFU);
    std::copy(dfd.begin(), dfd.end(), bytes.begin() + dfdOffset);
    return bytes;
}

CompressedTextureArtifact parseBc7Ktx2(
    std::span<const std::byte> bytes,
    const std::filesystem::path& diagnosticPath)
{
    try {
        if (bytes.size() < ktx2HeaderBytes ||
            !std::equal(ktx2Identifier.begin(), ktx2Identifier.end(),
                bytes.begin())) {
            invalidKtx(diagnosticPath, "bad identifier or truncated header");
        }
        const uint32_t formatValue = readU32(bytes, 12);
        if (formatValue != static_cast<uint32_t>(
                CompressedTextureFormat::Bc7Unorm) &&
            formatValue != static_cast<uint32_t>(
                CompressedTextureFormat::Bc7Srgb)) {
            invalidKtx(diagnosticPath, "format is not BC7 UNORM or BC7 SRGB");
        }
        const uint32_t width = readU32(bytes, 20);
        const uint32_t height = readU32(bytes, 24);
        const uint32_t levelCount = readU32(bytes, 40);
        if (readU32(bytes, 16) != 1 || width == 0 || height == 0 ||
            readU32(bytes, 28) != 0 || readU32(bytes, 32) != 0 ||
            readU32(bytes, 36) != 1 || levelCount == 0 ||
            levelCount > mipCount(width, height) ||
            readU32(bytes, 44) != 0) {
            invalidKtx(diagnosticPath, "unsupported texture shape or header fields");
        }
        const uint32_t dfdOffset = readU32(bytes, 48);
        const uint32_t dfdLength = readU32(bytes, 52);
        if (dfdOffset != ktx2HeaderBytes + levelCount * 24U ||
            dfdLength != dfdBytes ||
            static_cast<uint64_t>(dfdOffset) + dfdLength > bytes.size() ||
            readU32(bytes, dfdOffset) != dfdBytes ||
            (readU32(bytes, dfdOffset + 12U) & 0xFFU) != 134U ||
            readU32(bytes, dfdOffset + 16U) != 0x00000303U ||
            (readU32(bytes, dfdOffset + 20U) & 0xFFU) != 16U) {
            invalidKtx(diagnosticPath, "invalid BC7 data format descriptor");
        }
        const uint32_t expectedTransfer =
            formatValue == static_cast<uint32_t>(
                CompressedTextureFormat::Bc7Srgb)
            ? 2U
            : 1U;
        if (((readU32(bytes, dfdOffset + 12U) >> 16U) & 0xFFU) !=
            expectedTransfer) {
            invalidKtx(diagnosticPath, "format and transfer function disagree");
        }

        CompressedTextureArtifact result {
            .format = static_cast<CompressedTextureFormat>(formatValue),
            .width = width,
            .height = height,
        };
        result.mips.reserve(levelCount);
        for (uint32_t level = 0; level < levelCount; ++level) {
            const std::size_t index = ktx2HeaderBytes + level * 24U;
            const uint64_t offset = readU64(bytes, index);
            const uint64_t length = readU64(bytes, index + 8U);
            const uint64_t uncompressedLength = readU64(bytes, index + 16U);
            const uint32_t mipWidth = std::max(width >> level, 1U);
            const uint32_t mipHeight = std::max(height >> level, 1U);
            const uint64_t expectedLength =
                static_cast<uint64_t>((mipWidth + 3U) / 4U) *
                ((mipHeight + 3U) / 4U) * 16U;
            if (length != expectedLength || uncompressedLength != length ||
                offset % 16U != 0 || offset > bytes.size() ||
                length > bytes.size() - offset) {
                invalidKtx(diagnosticPath, "invalid mip level offset or size");
            }
            result.mips.push_back({
                .width = mipWidth,
                .height = mipHeight,
                .bytes = std::vector<std::byte>(
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset + length)),
            });
        }
        return result;
    } catch (const std::out_of_range&) {
        invalidKtx(diagnosticPath, "truncated level index");
    }
}

CompressedTextureArtifact loadBc7Ktx2(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("Cannot open BC7 KTX2 artifact: " + path.string());
    }
    const std::streampos end = stream.tellg();
    if (end < 0 || static_cast<uint64_t>(end) >
        std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("BC7 KTX2 artifact is too large: " + path.string());
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw std::runtime_error("Cannot read BC7 KTX2 artifact: " + path.string());
    }
    return parseBc7Ktx2(bytes, path);
}

} // namespace sokoban
