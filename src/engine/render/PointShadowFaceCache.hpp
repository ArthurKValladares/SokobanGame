#pragma once

#include "engine/render/IsoScenePreparer.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace sokoban {

struct PointShadowModelState {
    std::size_t tileIndex = 0;
    RenderFrameData::Tile tile;
    bool ready = false;

    friend constexpr bool operator==(
        const PointShadowModelState&,
        const PointShadowModelState&) = default;
};

// Exact CPU-side cache key for one point light's six depth faces. The cache
// deliberately stores geometry rather than a hash: a collision here would
// preserve stale shadow depth and become a visual correctness bug.
class PointShadowFaceCache {
public:
    [[nodiscard]] bool reusable(
        std::size_t lightIndex,
        const RenderFrameData::PointLight& light,
        std::span<const std::array<Vec3, 4>> allFaces,
        std::span<const std::size_t> faceIndices,
        std::span<const PointShadowModelState> modelStates) const;

    void markRendered(
        std::size_t lightIndex,
        const RenderFrameData::PointLight& light,
        std::span<const std::array<Vec3, 4>> allFaces,
        std::span<const std::size_t> faceIndices,
        std::span<const PointShadowModelState> modelStates);

    void invalidate(std::size_t lightIndex);

private:
    struct Entry {
        Vec3 lightPosition {};
        float lightRange = 0.0f;
        std::vector<std::array<Vec3, 4>> faces;
        std::vector<PointShadowModelState> models;
        bool valid = false;
    };

    std::array<Entry, RenderFrameData::pointLightCapacity> entries_;
};

} // namespace sokoban
