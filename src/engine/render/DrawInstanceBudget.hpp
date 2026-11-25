#pragma once

#include "engine/render/RenderTypes.hpp"

#include <cstdint>

namespace sokoban {

struct PreparedRenderScene;

// Shared by the rim-cohort guard and point-shadow batching. Each scene
// reserves its sun faces plus a conservative two copies of visible scene
// draws, particles, water, debug labels, and top-down overlays. Main and
// preview budgets are combined with the UI and 64 fixed composition draws.
// Point-shadow batches reuse one instance range across six faces and may
// borrow only capacity left above this reserve; otherwise they use pushes.
[[nodiscard]] uint64_t ordinarySceneDrawInstanceReserve(
    const RenderFrameData& frame,
    const PreparedRenderScene& scene) noexcept;
[[nodiscard]] uint64_t ordinaryFrameDrawInstanceReserve(
    uint64_t mainSceneReserve,
    uint64_t previewSceneReserve,
    uint64_t uiInstanceCount) noexcept;
[[nodiscard]] bool groundRimDrawInstanceBudgetFits(
    uint64_t mainSceneReserve,
    uint64_t previewSceneReserve,
    uint64_t uiInstanceCount,
    uint64_t capacity) noexcept;

} // namespace sokoban
