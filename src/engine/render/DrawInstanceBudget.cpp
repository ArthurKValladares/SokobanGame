#include "engine/render/DrawInstanceBudget.hpp"

#include "engine/render/IsoScenePreparer.hpp"

#include <algorithm>
#include <limits>

namespace sokoban {
namespace {

uint64_t saturatedAdd(uint64_t a, uint64_t b)
{
    return b > std::numeric_limits<uint64_t>::max() - a
        ? std::numeric_limits<uint64_t>::max() : a + b;
}

uint64_t saturatedMultiply(uint64_t count, uint64_t multiplier)
{
    return count > std::numeric_limits<uint64_t>::max() / multiplier
        ? std::numeric_limits<uint64_t>::max() : count * multiplier;
}

} // namespace

uint64_t ordinarySceneDrawInstanceReserve(
    const RenderFrameData& frame,
    const PreparedRenderScene& scene) noexcept
{
    const uint64_t sceneDraws = saturatedAdd(
        saturatedAdd(scene.opaqueFaceIndices.size(), scene.translucentFaceIndices.size()),
        saturatedAdd(scene.opaqueModelIndices.size(), scene.translucentModelIndices.size()));
    uint64_t reserve = saturatedAdd(scene.shadowFaces.size(), saturatedMultiply(sceneDraws, 2));
    for (const auto& chunk : scene.groundChunkDraws) {
        reserve = saturatedAdd(reserve, saturatedMultiply(chunk.tileCount, 2));
    }
    reserve = saturatedAdd(reserve, scene.particles.size());
    reserve = saturatedAdd(reserve, frame.waterSurfaces.size());
#if SOKOBAN_ENABLE_DEBUG_UI
    if (frame.showOccludedEntities && frame.viewMode == RenderViewMode::Isometric3D) {
        reserve = saturatedAdd(reserve, static_cast<uint64_t>(
            std::ranges::count(frame.tiles, true, &RenderFrameData::Tile::interactionOutline)));
    }
    reserve = saturatedAdd(reserve, frame.debugItemOutlines.size());
    reserve = saturatedAdd(reserve, frame.debugItemLinks.size());
    reserve = saturatedAdd(reserve, saturatedMultiply(frame.debugItemLabels.size(),
        RenderFrameData::DebugItemLabel::textCapacity + 1ULL));
#endif
    if (frame.viewMode == RenderViewMode::TopDown2D) {
        reserve = saturatedAdd(reserve, frame.tiles.size());
        reserve = saturatedAdd(reserve,
            2ULL * (uint64_t { frame.levelWidth } + frame.levelHeight + 2ULL));
    }
    return reserve;
}

uint64_t ordinaryFrameDrawInstanceReserve(
    uint64_t mainSceneReserve,
    uint64_t previewSceneReserve,
    uint64_t uiInstanceCount) noexcept
{
    return saturatedAdd(saturatedAdd(
        saturatedAdd(mainSceneReserve, previewSceneReserve), uiInstanceCount), 64);
}

bool groundRimDrawInstanceBudgetFits(
    uint64_t mainSceneReserve,
    uint64_t previewSceneReserve,
    uint64_t uiInstanceCount,
    uint64_t capacity) noexcept
{
    // Never let adversarial counts wrap into an apparently small reserve.
    if (mainSceneReserve > capacity) return false;
    capacity -= mainSceneReserve;
    if (previewSceneReserve > capacity) return false;
    capacity -= previewSceneReserve;
    if (uiInstanceCount > capacity) return false;
    return 64 <= capacity - uiInstanceCount;
}

} // namespace sokoban
