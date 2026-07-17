#pragma once

#include "engine/render/RenderTypes.hpp"

#include <array>
#include <cstddef>

namespace sokoban {

class Level;
enum class TileType;

class RenderAssetRequirements {
public:
    void requireModel(RenderModel model);
    void requireAnimation(RenderAnimation animation);
    void merge(const RenderAssetRequirements& other);

    [[nodiscard]] bool contains(RenderModel model) const;
    [[nodiscard]] bool contains(RenderAnimation animation) const;
    [[nodiscard]] std::size_t modelCount() const;
    [[nodiscard]] std::size_t animationCount() const;
    [[nodiscard]] bool empty() const;

private:
    std::array<bool, static_cast<std::size_t>(RenderModel::Count)> models_ {};
    std::array<bool, static_cast<std::size_t>(RenderAnimation::Count)> animations_ {};
};

[[nodiscard]] RenderModel renderModelForTile(TileType tile);
[[nodiscard]] RenderAssetRequirements renderAssetRequirementsForLevel(const Level& level);
[[nodiscard]] RenderAssetRequirements renderAssetRequirementsForFrame(const RenderFrameData& frame);

} // namespace sokoban
