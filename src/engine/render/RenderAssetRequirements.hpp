#pragma once

#include "engine/render/RenderTypes.hpp"

#include <cstddef>
#include <vector>

namespace sokoban {

class AssetManifest;
class Level;

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
    // Indexed by id index (value - 1); grown on demand.
    std::vector<bool> models_;
    std::vector<bool> animations_;
};

[[nodiscard]] RenderAssetRequirements renderAssetRequirementsForLevel(
    const Level& level,
    const AssetManifest& manifest);
[[nodiscard]] RenderAssetRequirements renderAssetRequirementsForFrame(
    const RenderFrameData& frame);

} // namespace sokoban
