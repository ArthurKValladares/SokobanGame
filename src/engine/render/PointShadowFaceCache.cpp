#include "engine/render/PointShadowFaceCache.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>

namespace sokoban {

bool PointShadowFaceCache::reusable(
    std::size_t lightIndex,
    const RenderFrameData::PointLight& light,
    std::span<const std::array<Vec3, 4>> allFaces,
    std::span<const std::size_t> faceIndices,
    std::span<const PointShadowModelState> modelStates) const
{
    const Entry& entry = entries_.at(lightIndex);
    if (!entry.valid ||
        entry.lightPosition != light.position ||
        entry.lightRange != light.range ||
        entry.faces.size() != faceIndices.size() ||
        entry.models.size() != modelStates.size()) {
        return false;
    }
    for (std::size_t index = 0; index < faceIndices.size(); ++index) {
        if (faceIndices[index] >= allFaces.size() ||
            entry.faces[index] != allFaces[faceIndices[index]]) {
            return false;
        }
    }
    return std::ranges::equal(entry.models, modelStates);
}

void PointShadowFaceCache::markRendered(
    std::size_t lightIndex,
    const RenderFrameData::PointLight& light,
    std::span<const std::array<Vec3, 4>> allFaces,
    std::span<const std::size_t> faceIndices,
    std::span<const PointShadowModelState> modelStates)
{
    Entry& entry = entries_.at(lightIndex);
    entry.faces.clear();
    entry.faces.reserve(faceIndices.size());
    for (std::size_t faceIndex : faceIndices) {
        if (faceIndex >= allFaces.size()) {
            throw std::out_of_range(
                "Point-shadow face index is out of range");
        }
        entry.faces.push_back(allFaces[faceIndex]);
    }
    entry.lightPosition = light.position;
    entry.lightRange = light.range;
    entry.models.assign(modelStates.begin(), modelStates.end());
    entry.valid = true;
}

void PointShadowFaceCache::invalidate(std::size_t lightIndex)
{
    entries_.at(lightIndex).valid = false;
}

} // namespace sokoban
