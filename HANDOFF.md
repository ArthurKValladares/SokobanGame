# Sokoban 3D Engine Modernization Handoff

This document is the working handoff for the remaining engine-review work. It
describes the code as it exists now, the invariants that must survive, and the
recommended implementation order. It is deliberately forward-looking: Git
history is the record of how the current baseline was produced.

## Executive summary

The renderer modernization has a sound substrate now: Vulkan 1.3, an explicit
camera, instanced tile draws, an HDR scene target, a tonemap pass, cgltf-based
loading, a GPU material buffer, tangents and two UV sets, and Cook-Torrance GGX
lighting. The next objective is to finish glTF material support without
preserving the fixed 64-texture ceiling or adding more branches to the current
uber-shader.

The recommended order is:

1. Establish a fresh validation baseline and add the missing loader fixture.
2. Query and enable the exact Vulkan descriptor-indexing feature tier.
3. Move model textures into a separate, frame-safe descriptor set.
4. Remove the compile-time 64-texture contract and introduce a runtime heap
   capacity.
5. Add a glTF dependency-inspection layer and a texture-source abstraction.
6. Carry glTF material-map handles through the CPU and GPU material models.
7. Finish tonemapping and exposure before judging mapped PBR output.
8. Split non-scene modes out of `triangle.frag.glsl`.
9. Implement and validate normal, metallic-roughness, emissive and occlusion
   map sampling.
10. Complete SSAO, then resume the larger scaling and memory work.

Do not implement “V4” as one monolithic change. The device contract, descriptor
layout, runtime capacity, content discovery, material representation and shader
sampling have different failure modes and should be independently reviewable.

## Repository state at this handoff

- Language and platform: C++20, SDL3, Vulkan 1.3, GLSL compiled to SPIR-V.
- Runtime content: strict `assets/manifest.json`, staged by the content tool.
- Current manifest: 36 models, 42 textures and 6 named animations.
- Current tests: 62 CTest suites in the newest configured build tree.
- Current shaders: 15 GLSL files. `triangle.frag.glsl` is 590 physical lines.
- Texture ceiling: `sokoban::maxModelTextures = 64`.
- Descriptor layout: one set with bindings 0 through 12; the fixed texture
  array is binding 2 and the GPU material buffer is binding 12.
- Working-tree policy: commits belong to the project owner. Keep changes
  separable and report each logical step.

Existing build products may predate the newest source. Before relying on a
binary, compare its timestamp with the commits being evaluated.

## Completed review work

The following items are implemented and should be treated as baseline, not as
future tasks:

- **F1**: shared math and geometry types, including frustums and bounds.
- **F2a/F2b**: an `R16G16B16A16_SFLOAT` scene target, a distinct display
  image, and a tonemap pass. F2c remains open.
- **F3**: cgltf material factors, tangents, a second UV set, alpha modes,
  double-sided metadata, a GPU material buffer and Cook-Torrance GGX. Texture
  maps other than the manifest-supplied base color are not implemented.
- **T1/T2/T3/T6/T7**: instanced tiles, separate opaque drawing and sorting,
  model back-face culling, 4x default MSAA, and AO-gated depth copying.
- **V1/V3/V5/V6**: per-swapchain-image present semaphores, direct skinning
  SSBO indexing, Vulkan 1.3 optimized shader builds, and optional anisotropy.
- **A1**: cgltf replaces the regex loader; validation, STEP and CUBICSPLINE
  sampling are implemented. A loader-level fixture covers the three-output
  layout, quaternion value normalization, tangent preservation and malformed
  output counts.
- **C1/S4/E1**: explicit camera data, closed frame-arena lifetime, MSVC CI and
  a headless Vulkan validation run.

## Corrections to the remaining review inventory

- **A3 is partly implemented, not untouched.** CPU preparation is asynchronous,
  publication is budgeted, requirements drive residency, and model/texture
  byte budgets can evict old resources. What remains is mip/LOD streaming,
  compressed-size accounting, more flexible publication budgeting, and
  removing the `vkDeviceWaitIdle` eviction fallback.
- **PBR maps are not “uploaded but unread.”** The scalar factors are uploaded.
  `MeshMaterial` and `GpuMaterial` do not yet contain normal,
  metallic-roughness, emissive-map or occlusion-map handles.
- **Descriptor indexing is core in Vulkan 1.2.** This renderer already requires
  Vulkan 1.3. Query and enable the Vulkan 1.2 feature struct rather than adding
  an extension-name requirement unnecessarily.
- **`UPDATE_AFTER_BIND` is not an immediate requirement.** The renderer owns
  per-frame descriptor sets and updates a set only after that frame's fence.
  Preserve that safety model until concurrent mutation is a measured need.
- **A variable-count binding must be the highest binding in its set.** The
  current texture array at binding 2 cannot simply be marked variable-count
  while bindings 3 through 12 remain in the same set. Isolate the texture heap.

## Rendering invariants

These rules fail visually or under validation if they drift.

### Color targets and tonemapping

- The scene target is floating-point linear light. It is never presented or
  sampled by ImGui.
- The display image uses the surface sRGB format and contains tonemapped scene
  color before UI composition.
- `tonemap.frag.glsl` writes linear values to the sRGB display attachment. The
  attachment performs the frame's only linear-to-sRGB encode. Do not add a
  shader-side gamma `pow()`.
- The upscale blit, frame capture and developer Game Viewport read the display
  image, not the HDR scene target.
- Player-facing UI is composed after tonemapping.

### Ambient-mask contract

The scene target alpha channel carries the ratio of ambient contribution to
total lit contribution for opaque scene pixels. SSAO uses it so occlusion does
not darken direct sunlight.

- `triangle.frag.glsl` and `ground_splat.frag.glsl` must calculate the same
  semantic ratio.
- Only opaque pipelines write the mask.
- Blended scene pipelines mask alpha writes so they inherit the opaque mask
  behind them.
- Scene-image UI samples scene RGB only.
- Normal and occlusion map work must preserve this definition. The occlusion
  texture scales ambient lighting, not direct light or the final composite.

### Camera, transforms and vertices

- `SceneFrameUniform` owns camera and lighting data.
- Scene vertex buffers carry world-space geometry; per-draw transforms live in
  the draw-instance SSBO.
- Static and skinned vertex layouts use locations 8 and 9 for tangent and UV1.
  Locations 5 through 7 belong to skinning.
- The tangent is glTF `vec4`: xyz direction and handedness in w.
- Model normals and tangents are transformed through `worldFromModel`; do not
  restore the retired Euler path.

### Descriptor and resource lifetime

- Descriptor updates are frame-local. `FrameDescriptorSync` updates a set only
  after the corresponding fence has completed.
- A fallback texture keeps nonresident manifest slots safe to sample.
- Eviction currently waits for device idle before destroying resources that an
  older descriptor set may reference. Descriptor work must not silently remove
  this safety without replacing it with explicit lifetime tracking.
- The material buffer reserves entry zero as the fallback. Published model
  ranges start after it and may move only while no frame can read them.

## Itemized implementation sequence

Each numbered packet should be reviewable and verifiable on its own. Do not
combine adjacent packets merely because they touch the same files.

### 0. Refresh the baseline

Current state on 27 August 2026: the full Visual Studio Debug build succeeds
and all 62 registered CTest suites pass, including `vulkan_smoke`. Establishing
that baseline also exposed and repaired stale UI/settings assertions left by the
earlier default-MSAA change. Representative screenshots and frame statistics
still need to be captured before descriptor behavior changes.

#### 0.1 Capture visual and performance evidence

- Record representative screenshots and frame statistics for comparison.
- Record the scene, UI and editor states that are most likely to expose texture
  descriptor or material-map regressions.

**Acceptance:** screenshots and frame statistics are archived against the green
Debug baseline before descriptor behavior changes.

#### 0.2 Loader-level CUBICSPLINE fixture — complete

`AnimationControllerTests.cpp` now writes a synthetic glTF and binary buffer,
loads them through `loadGltfAnimationClip`, and covers `valuesPerKey == 3`,
value-slot selection, tangent preservation, quaternion normalization of values
only, and malformed output counts.

**Gate:** keep `animation_controller` in the required regression suite.

#### 0.3 Normalize line endings separately

- The unused `src/engine/render/GpuModelInstance.hpp` has been deleted.
- Add a small `.gitattributes` policy and normalize line endings in its own
  commit, never mixed with semantic changes.

### 1. Define the descriptor-indexing device contract

#### 1.1 Represent support explicitly

- Extend `VulkanDeviceFeatureSupport` with the Vulkan 1.2 descriptor features
  the selected design actually uses.
- Query `VkPhysicalDeviceVulkan12Features` in the physical-device feature chain.
- Query the corresponding descriptor-indexing properties and limits.
- Unit-test feature-tier acceptance and rejection.

At minimum evaluate:

- `runtimeDescriptorArray`;
- `descriptorBindingPartiallyBound`;
- `descriptorBindingVariableDescriptorCount`;
- `shaderSampledImageArrayNonUniformIndexing` if an index can vary within the
  relevant invocation group.

Do not require update-after-bind features in this packet.

**Acceptance:** unsupported GPUs are rejected with an actionable reason, and
supported Vulkan 1.3 devices enable exactly the queried feature chain.

#### 1.2 Define a runtime capacity policy

- Choose a configured texture-heap ceiling bounded by the device's descriptor
  limits, rather than using the raw maximum blindly.
- Reserve explicit headroom for editor-added splat maps and imported assets.
- Fail startup with required, available and reserved counts in the error.
- Keep texture handles stable for the life of a manifest generation.

**Acceptance:** a manifest can exceed 64 textures on capable hardware, and a
low-limit fixture fails before descriptor allocation or shader execution.

### 2. Isolate the texture heap

#### 2.1 Split the pipeline layout by update frequency

Use the smallest useful split:

- **Set 0 — scene/frame resources:** shadows, scene images, SSAO, frame UBO,
  skinning, draw instances, materials, UI font and title image.
- **Set 1 — sampled texture heap:** the runtime-sized model/content texture
  array as the final and only binding in that set.

Do not create per-material or per-draw descriptor sets. Material and draw
selection already use SSBO indices; adding sets there would reverse the
bindless direction.

**Acceptance:** all pipelines use the two-set layout, shader reflection agrees
with C++, and rendered output is unchanged.

#### 2.2 Preserve frame-safe updates

- Allocate one texture set per frame in flight, matching the existing scene-set
  safety model.
- Update only the completed frame's texture set after its fence.
- Keep fallback descriptors for nonresident entries unless partially-bound
  behavior is proven safe for every shader access.
- Retain generation tracking and add tests for growth, publication and eviction.

**Acceptance:** streaming a texture or evicting one never rewrites a set still
referenced by an in-flight command buffer.

#### 2.3 Remove the compile-time array contract

- Replace fixed `MODEL_TEXTURE_COUNT` declarations with the runtime descriptor
  array form used by the chosen feature tier.
- Remove CMake's regex extraction of `maxModelTextures`.
- Replace shader clamps against the compile-time count with validated handles
  or a runtime count where defensive bounds checking remains useful.
- Remove `maxModelTextures` from manifest validation and runtime registration.

**Acceptance:** a test manifest with more than 64 textures validates, shader
compilation no longer needs `MODEL_TEXTURE_COUNT`, and missing/nonresident
textures still resolve to the fallback.

### 3. Discover glTF texture dependencies outside manifest parsing

#### 3.1 Add a side-effect-free glTF inspection API

Create a loader-adjacent API such as `inspectGltfAssetDependencies(path)` that
returns materials, image sources, samplers and buffer dependencies without
creating GPU resources. Keep cgltf types private to the implementation.

Do not make `AssetManifest::parse(string)` perform filesystem I/O. Enrich a
parsed manifest/catalog in a separate content-resolution step that has an
explicit asset root.

**Acceptance:** pure manifest parsing remains deterministic and unit-testable;
dependency inspection has synthetic `.gltf` and `.glb` fixtures.

#### 3.2 Introduce texture-source identity

The existing texture model assumes a filesystem path. Replace or extend it
with a source that can identify:

- an external URI resolved relative to its glTF;
- an image embedded in a GLB buffer view;
- a supported data URI.

Deduplicate by canonical source plus interpretation. The same bytes interpreted
as sRGB and linear data are distinct GPU resources unless the image/view design
explicitly supports both views.

**Acceptance:** external, embedded and data-URI fixtures enter the content
inventory without path traversal, ambiguous identity or unstaged dependencies.

#### 3.3 Preserve glTF texture semantics

- Base color and emissive maps are sRGB.
- Normal, metallic-roughness and occlusion maps are linear.
- Preserve or deliberately map wrap S/T and min/mag/mipmap filtering.
- Include texture coordinate set selection.
- Decide explicitly how `KHR_texture_transform` is handled; support it or emit
  a clear unsupported-feature diagnostic rather than silently rendering wrong.

**Acceptance:** content validation reports unsupported combinations with model,
material and texture names.

### 4. Extend the material representations

#### 4.1 Extend `MeshMaterial`

Add optional handles and glTF parameters for:

- normal map, UV set and normal scale;
- metallic-roughness map and UV set;
- emissive map and UV set;
- occlusion map, UV set and strength.

Keep the existing base-color override behavior for authored manifest entries
until migration is explicit. Do not silently change the look of all existing
assets in the same packet.

#### 4.2 Redesign `GpuMaterial` once

- Keep color and scalar factors in aligned float vectors.
- Store texture handles, UV selections, alpha mode and flags in explicit
  32-bit integer lanes rather than encoding new indices as floats.
- Mirror the exact std430 layout in shared comments and static assertions.
- Version or rebuild the material buffer as needed; never reinterpret an old
  mapped layout.

**Acceptance:** CPU-to-GPU conversion tests cover defaults, every map, alpha
mode, double-sided state, scrolling compatibility and fallback entry zero.

#### 4.3 Upload map dependencies through existing residency

- Add discovered material textures to per-model requirements.
- Stage them through the content pipeline.
- Decode on worker threads and publish on the render thread like current
  textures.
- Account for them in the existing texture residency budget and diagnostics.

**Acceptance:** requesting a model requests all of its maps, while an unrelated
model's maps remain nonresident.

### 5. Finish HDR presentation before visual PBR acceptance

#### 5.1 Implement F2c

- Add Khronos PBR Neutral as the default tonemap curve.
- Retain straight clamp as a Debug comparison mode.
- Add a user-facing exposure setting with a safe range and default of 0 EV.
- Pass exposure and curve selection through one authoritative settings path.
- Keep UI after tonemapping and keep hardware as the only sRGB encode.

**Acceptance:** bright highlights roll off instead of clipping, neutral colors
remain stable, screenshots and the developer viewport match the presented game,
and the clamp comparison remains available in Debug builds.

### 6. Reduce the uber-shader before adding map branches

#### 6.1 Extract non-scene modes

- Move UI font, title art, rounded scene-image composition and other unlit UI
  modes out of `triangle.frag.glsl` into a dedicated fragment shader/pipeline.
- Keep scene lighting, tile/model materials and the ambient-mask contract in
  the scene shader.
- Prefer a small shared include for genuinely shared declarations over copying
  large functions.

**Acceptance:** UI output is unchanged, the scene shader no longer branches on
UI/title modes, and shader-interface reflection passes.

#### 6.2 Add permutations only for stable axes

Use specialization constants for stable pipeline choices such as lit/unlit or
ambient-mask output. Do not create a combinatorial permutation system for every
material flag; data-driven material differences belong in `GpuMaterial`.

### 7. Implement material-map sampling

Land map types separately so visual regressions remain bisectable.

#### 7.1 Metallic-roughness

- Sample glTF roughness from G and metallic from B.
- Multiply by the scalar factors already in `MeshMaterial`.
- Confirm the direct and ambient BRDF paths use the same resulting values.

#### 7.2 Normal mapping

- Build TBN from the world-space normal, tangent xyz and tangent handedness.
- Sample the normal texture as linear data and apply normal scale.
- Handle back faces of double-sided materials consistently.
- Test both authored tangents and the derived-tangent fallback.

#### 7.3 Emissive

- Sample emissive as sRGB color, multiply by `emissiveFactor`, and add it in
  linear light.
- Do not count emissive as ambient light in the AO mask.

#### 7.4 Occlusion

- Sample the R channel and apply strength only to ambient lighting.
- Keep screen-space AO and material occlusion composable without multiplying
  direct light.
- Re-check the ambient mask in both scene lighting shaders.

#### 7.5 Alpha and mirror paths

- Revalidate MASK cutoff, BLEND ordering, double-sided culling and base-color
  alpha after map migration.
- Make an explicit decision about which map effects mirror-energy ghosts use;
  do not let them accidentally inherit the full lit path.

**Acceptance:** an off-the-shelf external-texture glTF and an embedded-texture
GLB render correctly; all four map types, both UV sets, alpha modes and
double-sided materials have focused fixtures.

### 8. Complete V7 SSAO

- Reconstruct or provide view-space position and normals.
- Express radius and bias in view/world units rather than pixels and raw depth.
- Render AO at half resolution.
- Replace the 5x5 box blur with a depth/normal-aware bilateral filter.
- Preserve the ambient-only application contract.
- Profile the existing scene-color snapshot; remove it only if the new data
  flow makes it unnecessary.

**Acceptance:** AO is stable across render scales and camera changes, does not
bleed across silhouettes, and does not darken direct highlights or emissive
surfaces.

### 9. Resume frame-scaling work

After the material path is complete, prioritize by measured frame cost:

1. **S2:** persistent renderables and stable bounds.
2. **T4:** frustum culling using the existing `Frustum` and `Aabb` math.
3. **V2:** VMA or deliberate image/buffer suballocation.
4. **A2:** KTX2 plus BC7/appropriate platform formats in the content tool.
5. **A3 remainder:** mip/LOD streaming, compressed byte accounting and
   fence-based eviction retirement instead of `vkDeviceWaitIdle`.
6. **T8:** parallelize Vulkan-free scene preparation first; add secondary
   command buffers or a transfer queue only after profiling.
7. **T5:** range-cull point-shadow casters, then cache unchanged faces.
8. **A4:** reuse recorder scratch storage after scene data structures settle.

`S3` task-graph/work-stealing work should follow an observed scheduling
bottleneck. `S1` fixed-tick simulation is conditional on continuous physics or
gameplay motion. `F5` RHI work is conditional on a real second renderer or
platform; do not add virtual abstractions speculatively.

## Verification matrix

Every packet should use the relevant rows below.

| Change | Required verification |
| --- | --- |
| Device features/limits | Unit tests for accepted and rejected feature tiers; real-device startup on the supported GPU matrix |
| Descriptor layouts | Compile all shaders; SPIR-V reflection for every set/binding; Vulkan validation smoke test |
| Loader/material layout | Synthetic fixtures; differential corpus digest where behavior should remain unchanged |
| Content discovery | External `.gltf`, embedded `.glb`, data URI, missing file, traversal and duplicate-source fixtures |
| Tonemap/lighting | Reference screenshots, HDR values above 1.0, Debug A/B mode, no validation errors |
| Resource lifetime | Two or more frames in flight; publish and evict while rendering; validation and stress run |
| Performance claims | CPU/GPU timestamps and draw/pipeline/descriptor statistics before and after |

For loader or vertex-format changes, reuse a field-by-field differential digest
over every manifest-reachable model, skinned mesh, animation and pose. Avoid raw
struct hashing: padding and deliberate layout changes make it unreliable.

For shader changes, compile every shader, not only edited files. Descriptor set
layout changes affect modules that appear unrelated to the immediate feature.

## Source map for the next packets

- `src/engine/AssetManifest.*`: parsed declarations and stable runtime handles.
- `src/engine/ContentPipeline.*`: dependency validation, inventory and staging.
- `src/engine/render/GltfMesh.*`: cgltf boundary, mesh/material/animation decode.
- `src/engine/render/ImageData.*`: decoded RGBA image abstraction; currently
  path-based and therefore a required seam for embedded GLB images.
- `src/engine/render/VulkanDeviceContext.*`: feature/property queries and logical
  device feature chain.
- `src/engine/render/VulkanDeviceSelection.*`: testable release feature tier.
- `src/engine/render/VulkanSceneDescriptors.*`: descriptor layouts, pools, sets
  and writes.
- `src/engine/render/FrameDescriptorSync.hpp`: per-frame descriptor generations.
- `src/engine/render/VulkanModelResources.*`: async preparation, GPU publication,
  texture views, material buffer and residency/eviction.
- `src/engine/render/VulkanPipelineFactory.*`: pipeline layouts, specialization
  constants, blend/cull/write-mask state.
- `src/engine/render/VulkanSceneRecorder.*`: pass ordering and descriptor binds.
- `src/engine/render/VulkanRenderConstants.hpp`: CPU/GPU shared layouts.
- `shaders/triangle.frag.glsl`: current scene/UI uber-shader.
- `shaders/ground_splat.frag.glsl`: second ambient-mask writer.
- `shaders/mirror_energy.frag.glsl`: separate material-texture consumer.
- `shaders/tonemap.frag.glsl`: exposure and output transform.
- `shaders/ssao*.glsl`: remaining V7 work.

## Build and test commands

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure --no-tests=error
```

Build content explicitly when changing manifest, glTF or texture discovery:

```powershell
cmake --build build --config Debug --target sokoban_content
```

Run Release tests and the packaging validation before calling a phase complete.
The canonical shipping and sanitizer commands remain in `README.md` and
`packaging/ReleaseValidation.md`.

## Documentation rule

Update this handoff and `enginereview.html` in the same change that completes a
packet. Keep only current status, enduring invariants, the next actionable
sequence and evidence needed by the next implementer. Do not append a second
historical roadmap beneath a newer one.
