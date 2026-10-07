#pragma once

#include "engine/render/GltfMesh.hpp"
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

struct GroundRimProfile {
    uint8_t exposedSides = groundAllSides;
    uint8_t concaveCorners = 0;
    float width = 0.12f;
    float depth = 0.10f;
    float bodyBand = 0.30f;
};

struct GroundRimSample {
    // The top's local height is 1-drop. Gradient is d(drop)/d(local x,y).
    float drop = 0.0f;
    Vec2 gradient {};
};

[[nodiscard]] bool groundRimProfileValid(const GroundRimProfile& profile) noexcept;

// Shared contract with shaders/include/GroundRim.glsl. The chamfer is the
// maximum of exposed-edge ramps and concave-corner ramps. Corner ramps are
// the minimum of their two adjacent edge ramps, so adjacent tile caps agree
// at L-shaped notches. Ties select the first candidate in cardinal/corner
// order. A non-positive width disables the rim; invalid profiles fail open.
[[nodiscard]] GroundRimSample sampleGroundRim(
    Vec2 localPosition,
    const GroundRimProfile& profile) noexcept;

// Compresses only the upper bodyBand in height, using exactly the same drop
// as the cap. depth<bodyBand keeps the map monotone and its Jacobian positive.
// Original horizontal positions, UVs, material IDs and tangent handedness
// remain intact. The lower body and disabled profiles are returned unchanged.
[[nodiscard]] Vec3 deformGroundRockPosition(
    Vec3 localPosition,
    const GroundRimProfile& profile) noexcept;
[[nodiscard]] MeshVertex deformGroundRockVertex(
    const MeshVertex& vertex,
    const GroundRimProfile& profile) noexcept;

} // namespace sokoban
