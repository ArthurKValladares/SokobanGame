#pragma once

#include "engine/TextureSource.hpp"
#include "engine/render/CompressedTextureArtifact.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

// What a texture upload decides before it touches the device.
//
// Choosing a VkFormat, deciding how many mip levels an image gets, and judging
// whether a source is usable at all are policy: they depend on the source, the
// authored sampler and one capability answer, and on nothing else. They were
// spread through two 80-line `beginTextureUpload` overloads behind a live
// VkDevice, which meant none of it could be reached from a test - and the two
// overloads disagreed about how to spell the same predicates.
//
// Two of those spellings were literally duplicated. `usesMipmaps` existed
// twice, as an `||` chain here and an exhaustive `switch` in
// CompressedTextureArtifact.cpp; only the switch would have complained when
// the filter enum grew. And the mip count existed twice under two names and
// two algorithms - `mipLevelCount` as `1 + floor(log2(max))` in double
// precision, sizing the image, and `mipCount` as a halving loop, building the
// chain the image has to hold. They agree: checked over 708,876 dimension
// pairs including every power-of-two boundary to 2^31. One of them is kept,
// and it is the integer one, because a size calculation has no business going
// through a double.
//
// Nothing here calls a vk* function. `vulkan.h` is included for the enums.

namespace sokoban::textureUploadPlan {

// -------------------------------------------------------------- mip levels

// How many levels a full chain has for an image this size: one, plus one per
// halving until both axes reach 1. A zero dimension yields 1, which is what
// both previous implementations did and what keeps a degenerate source from
// producing a zero-level image.
[[nodiscard]] constexpr uint32_t mipLevelCount(uint32_t width, uint32_t height)
{
    uint32_t levels = 1;
    while (width > 1 || height > 1) {
        width = std::max(width / 2U, 1U);
        height = std::max(height / 2U, 1U);
        ++levels;
    }
    return levels;
}

// Whether an authored minification filter asks for a mip chain. Written as an
// exhaustive switch on purpose: adding a filter without deciding this is a
// -Wswitch warning rather than a silent false.
[[nodiscard]] constexpr bool usesMipmaps(TextureMinificationFilter filter)
{
    switch (filter) {
    case TextureMinificationFilter::NearestMipmapNearest:
    case TextureMinificationFilter::LinearMipmapNearest:
    case TextureMinificationFilter::NearestMipmapLinear:
    case TextureMinificationFilter::LinearMipmapLinear:
        return true;
    case TextureMinificationFilter::Nearest:
    case TextureMinificationFilter::Linear:
        return false;
    }
    return false;
}

// ------------------------------------------------------------ format choice

// Colour textures decode sRGB on read; data textures - the splat weight map -
// must not, or a painted 0.5 arrives at the shader as 0.21.
[[nodiscard]] constexpr VkFormat uncompressedFormat(TextureColorSpace space)
{
    return space == TextureColorSpace::Linear
        ? VK_FORMAT_R8G8B8A8_UNORM
        : VK_FORMAT_R8G8B8A8_SRGB;
}

[[nodiscard]] constexpr VkFormat compressedFormat(
    CompressedTextureFormat format)
{
    return format == CompressedTextureFormat::Bc7Srgb
        ? VK_FORMAT_BC7_SRGB_BLOCK
        : VK_FORMAT_BC7_UNORM_BLOCK;
}

// A compressed artifact carries its own colour space, baked in when it was
// encoded. If that disagrees with what the material says the texture is, the
// artifact is stale rather than merely wrong, and uploading it would silently
// double or undo a gamma curve.
[[nodiscard]] constexpr bool colorSpaceAgrees(
    VkFormat format, TextureColorSpace expected)
{
    return (format == VK_FORMAT_BC7_SRGB_BLOCK)
        == (expected == TextureColorSpace::Srgb);
}

// ------------------------------------------------------ device capabilities

// Generating a mip chain means blitting level N-1 into level N with a linear
// filter, so the format has to support being both ends of a blit and linear
// sampling. A format that cannot gets a single level rather than a broken
// chain.
inline constexpr VkFormatFeatureFlags mipGenerationFeatures =
    VK_FORMAT_FEATURE_BLIT_SRC_BIT |
    VK_FORMAT_FEATURE_BLIT_DST_BIT |
    VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

// BC7 blocks are uploaded, never blitted, so the requirement is only that they
// can be transferred in and sampled.
inline constexpr VkFormatFeatureFlags compressedSamplingFeatures =
    VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
    VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
    VK_FORMAT_FEATURE_TRANSFER_DST_BIT;

[[nodiscard]] constexpr bool supports(
    VkFormatFeatureFlags optimalTilingFeatures, VkFormatFeatureFlags required)
{
    return (optimalTilingFeatures & required) == required;
}

// ------------------------------------------------------------- the decisions

// Whether an uncompressed upload will generate a chain at all: the authored
// filter has to ask for one and the format has to be able to produce it.
//
// Separate from the level count because it is also what decides anisotropy,
// and the two are not the same question. A 1x1 texture with a mipmap filter
// generates mipmaps by this predicate and still has exactly one level, so
// `mipLevels > 1` is a different test - which the compressed path happens to
// use for its own anisotropy decision. That divergence is pre-existing and is
// left alone here; naming this predicate is what makes it visible.
[[nodiscard]] constexpr bool generatesMipmaps(
    TextureMinificationFilter minFilter,
    VkFormatFeatureFlags optimalTilingFeatures)
{
    return usesMipmaps(minFilter)
        && supports(optimalTilingFeatures, mipGenerationFeatures);
}

// Levels for an uncompressed upload.
[[nodiscard]] constexpr uint32_t uncompressedMipLevels(
    uint32_t width,
    uint32_t height,
    TextureMinificationFilter minFilter,
    VkFormatFeatureFlags optimalTilingFeatures)
{
    return generatesMipmaps(minFilter, optimalTilingFeatures)
        ? mipLevelCount(width, height)
        : 1U;
}

// Levels a compressed upload publishes: everything from the resident base mip
// down. Dropping the top levels under memory pressure is how a texture gets
// smaller without being evicted, so the base is an input, not always zero.
[[nodiscard]] constexpr uint32_t compressedMipLevels(
    std::size_t sourceMipCount, uint32_t sourceBaseMip)
{
    return static_cast<uint32_t>(sourceMipCount) - sourceBaseMip;
}

// -------------------------------------------------------- source validity

[[nodiscard]] constexpr bool uncompressedSourceIsUsable(
    uint32_t width, uint32_t height, bool hasPixels)
{
    return width != 0 && height != 0 && hasPixels;
}

[[nodiscard]] constexpr bool compressedSourceIsUsable(
    uint32_t width,
    uint32_t height,
    std::size_t sourceMipCount,
    uint32_t sourceBaseMip)
{
    return width != 0 && height != 0 && sourceMipCount != 0
        && sourceBaseMip < sourceMipCount;
}

} // namespace sokoban::textureUploadPlan
