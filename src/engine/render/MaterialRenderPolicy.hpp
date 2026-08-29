#pragma once

#include "engine/Math.hpp"
#include "engine/render/GltfMesh.hpp"

#include <cstdint>
#include <span>

namespace sokoban {

// A mixed-material mesh can contribute to both scene passes without creating
// another pipeline permutation. The recorder selects the relevant subset and
// the fragment shader rejects primitives that belong to the other pass.
enum class MaterialAlphaSelection : uint32_t {
    All = 0,
    OpaqueAndMask = 1,
    BlendOnly = 2,
};

struct ModelMaterialPolicy {
    // The empty/fallback material is opaque, so this defaults true.
    bool hasOpaqueOrMask = true;
    bool hasBlend = false;
    bool hasDoubleSided = false;
};

[[nodiscard]] ModelMaterialPolicy modelMaterialPolicy(
    std::span<const MeshMaterial> materials);
[[nodiscard]] bool materialSelected(
    MaterialAlphaMode mode,
    MaterialAlphaSelection selection);

// CPU reference for the base-colour/coverage contract mirrored by
// triangle.frag.glsl. Samples are linear; MASK compares authored material
// alpha but writes the instance alpha after surviving the cutoff.
struct ResolvedBaseColor {
    Vec3 rgb {};
    float alpha = 1.0f;
    bool discarded = false;
};

[[nodiscard]] ResolvedBaseColor resolveMaterialBaseColor(
    Vec4 drawColor,
    Vec4 baseColorFactor,
    Vec4 baseColorSample,
    MaterialAlphaMode mode,
    float alphaCutoff);

// Mirror energy is an effect, not a second rendering of the glTF material.
// Only authored base-colour RGB contributes detail; all authored opacity is
// deliberately ignored and the effect owns its alpha.
[[nodiscard]] Vec3 resolveMirrorEnergyBaseColor(
    Vec4 baseColorFactor,
    Vec4 baseColorSample);

} // namespace sokoban
