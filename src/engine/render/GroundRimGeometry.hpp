#pragma once

#include "engine/render/GltfMesh.hpp"
#include "engine/render/RenderTypes.hpp"

#include <array>
#include <cstddef>
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
    // World tile origin anchors the fracture pattern across tile/model seams.
    Vec2 origin {};
};

// Local cap topology. A triangle repeats its final vertex, matching the
// existing instanced face path. The CPU sampler and model shaders interpolate
// these same broad facets, rather than sampling noise between mesh vertices.
struct GroundRimGeometry {
    static constexpr std::size_t capacity = 38;
    std::array<std::array<Vec3, 4>, capacity> patches {};
    std::size_t count = 0;
};

[[nodiscard]] GroundRimGeometry buildGroundRimGeometry(
    const GroundRimProfile& profile) noexcept;

struct GroundRimSample {
    // The top's local height is 1-drop. Gradient is d(drop)/d(local x,y).
    float drop = 0.0f;
    Vec2 gradient {};
};

[[nodiscard]] bool groundRimProfileValid(const GroundRimProfile& profile) noexcept;

// Shared contract with shaders/include/GroundRim.glsl. World grid corners set
// irregular widths/heights; exposed body borders remain affine from corner
// to corner so all authored wall segments seal exactly. Hidden borders use
// matching corner ramps. The jagged inner outline forms broad planar facets.
// A non-positive width disables the rim; invalid profiles fail open.
[[nodiscard]] GroundRimSample sampleGroundRim(
    Vec2 localPosition,
    const GroundRimProfile& profile) noexcept;

// Compresses only the upper bodyBand in height, using exactly the same drop
// as the cap. The maximum corner drop stays below bodyBand, keeping the map
// monotone and its Jacobian positive.
// Original horizontal positions, UVs, material IDs and tangent handedness
// remain intact. The lower body and disabled profiles are returned unchanged.
[[nodiscard]] Vec3 deformGroundRockPosition(
    Vec3 localPosition,
    const GroundRimProfile& profile) noexcept;
[[nodiscard]] MeshVertex deformGroundRockVertex(
    const MeshVertex& vertex,
    const GroundRimProfile& profile) noexcept;

} // namespace sokoban
