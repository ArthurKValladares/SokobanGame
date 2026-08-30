#pragma once

#include "engine/render/CompressedTextureArtifact.hpp"

#include <cstdint>
#include <optional>

namespace sokoban {

struct TextureMipResidencyPlan {
    uint32_t sourceBaseMip = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 0;
    uint64_t residentBytes = 0;
    uint64_t fullQualityBytes = 0;

    [[nodiscard]] bool degraded() const { return sourceBaseMip != 0; }
    [[nodiscard]] uint64_t omittedBytes() const
    {
        return fullQualityBytes - residentBytes;
    }
};

// Selects the finest complete mip tail that fits the available residency
// capacity. Re-basing a tail as a smaller Vulkan image makes source mip N the
// new image's level zero, so implicit shader LOD remains correct for its new
// dimensions without shader or descriptor ABI changes.
[[nodiscard]] std::optional<TextureMipResidencyPlan>
chooseTextureMipResidency(
    const CompressedTextureArtifact& texture,
    uint64_t availableBytes);

} // namespace sokoban
