#pragma once

#include "engine/TextureSource.hpp"

#include <algorithm>
#include <cstdint>

// The mip arithmetic, with no Vulkan in it.
//
// Split out of TextureUploadPlan.hpp for a reason worth stating: this is
// needed by CompressedTextureArtifact, which lives in sokoban_core, and
// sokoban_core deliberately does not link Vulkan::Vulkan. A header that
// includes <vulkan/vulkan.h> cannot be reached from there, and one that
// tries breaks the build only on a real CMake configure - not on a compile
// that happens to have the include path.
//
// That is exactly how this file came to exist: the arithmetic was put in the
// Vulkan header first and the layering violation was found by a Visual Studio
// build, not by anything here. `tools/check_core_is_vulkan_free.sh` now checks
// it, and the split keeps the shared half honestly Vulkan-free.

namespace sokoban::textureMipChain {

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

} // namespace sokoban::textureMipChain
