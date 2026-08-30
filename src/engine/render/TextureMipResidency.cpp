#include "engine/render/TextureMipResidency.hpp"

#include <limits>
#include <stdexcept>
#include <vector>

namespace sokoban {

std::optional<TextureMipResidencyPlan> chooseTextureMipResidency(
    const CompressedTextureArtifact& texture,
    uint64_t availableBytes)
{
    if (texture.mips.empty()) {
        throw std::invalid_argument(
            "Cannot select residency for a texture without mip levels");
    }

    std::vector<uint64_t> tailBytes(texture.mips.size());
    uint64_t total = 0;
    for (std::size_t index = texture.mips.size(); index-- > 0;) {
        const uint64_t levelBytes = texture.mips[index].bytes.size();
        if (levelBytes > std::numeric_limits<uint64_t>::max() - total) {
            throw std::overflow_error("Texture mip residency size overflow");
        }
        total += levelBytes;
        tailBytes[index] = total;
    }

    const uint64_t fullQualityBytes = tailBytes.front();
    for (uint32_t baseMip = 0; baseMip < texture.mips.size(); ++baseMip) {
        if (tailBytes[baseMip] > availableBytes) {
            continue;
        }
        const CompressedTextureMip& base = texture.mips[baseMip];
        return TextureMipResidencyPlan {
            .sourceBaseMip = baseMip,
            .width = base.width,
            .height = base.height,
            .mipLevels = static_cast<uint32_t>(texture.mips.size()) - baseMip,
            .residentBytes = tailBytes[baseMip],
            .fullQualityBytes = fullQualityBytes,
        };
    }
    return std::nullopt;
}

} // namespace sokoban
