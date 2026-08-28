# Sokoban 3D Engine Modernization Handoff

This document is the working handoff for the remaining engine-review work. It
describes the code as it exists now, the invariants that must survive, and the
recommended implementation order. It is deliberately forward-looking: Git
history is the record of how the current baseline was produced.

## Executive summary

The renderer modernization has a sound substrate now: Vulkan 1.3, an explicit
camera, instanced tile draws, an HDR scene target, a tonemap pass, cgltf-based
loading, a GPU material buffer, tangents and two UV sets, and Cook-Torrance GGX
lighting. The fixed texture ceiling has now been replaced by a device-bounded,
frame-safe descriptor heap. Read-only glTF inspection now feeds a resolved
content inventory with stable identities for external, buffer-view and data-URI
images. Sampling, UV and unsupported-transform semantics are now validated as
well. `MeshMaterial` now has the complete core map representation. The next
objective is to redesign `GpuMaterial` once around that complete map set.

The recommended order is:

1. Capture the remaining visual/performance baseline evidence.
2. Redesign `GpuMaterial` once around the complete map set.
3. Upload resolved glTF map handles through the existing residency system.
4. Finish tonemapping and exposure before judging mapped PBR output.
5. Split non-scene modes out of `triangle.frag.glsl`.
6. Implement and validate normal, metallic-roughness, emissive and occlusion
   map sampling.
7. Complete SSAO, then resume the larger scaling and memory work.

Do not implement “V4” as one monolithic change. The device contract, descriptor
layout, runtime capacity, content discovery, material representation and shader
sampling have different failure modes and should be independently reviewable.

## Repository state at this handoff

- Language and platform: C++20, SDL3, Vulkan 1.3, GLSL compiled to SPIR-V.
- Runtime content: strict `assets/manifest.json`, staged by the content tool.
- Current manifest: 36 models, 42 textures and 6 named animations.
- Current tests: 63 CTest suites in the newest configured build tree.
- Current shaders: 15 GLSL files. `triangle.frag.glsl` is 587 physical lines.
- Texture capacity: selected at startup from a configured 1,024-slot ceiling,
  device limits, 16 editor-reserved slots and 32 import-reserved slots. The
  validated RTX 4060 configuration selects 1,024 slots for 42 manifest entries.
- Descriptor layout: set 0 contains scene/frame resources; set 1 contains one
  variable-count runtime texture array. One texture set exists per frame in
  flight and every allocated slot has a valid fallback descriptor.
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
  double-sided metadata, a GPU material buffer and Cook-Torrance GGX. The CPU
  material now represents all core map types; GPU transport and sampling remain
  open.
- **T1/T2/T3/T6/T7**: instanced tiles, separate opaque drawing and sorting,
  model back-face culling, 4x default MSAA, and AO-gated depth copying.
- **V1/V3/V5/V6**: per-swapchain-image present semaphores, direct skinning
  SSBO indexing, Vulkan 1.3 optimized shader builds, and optional anisotropy.
- **V4**: Vulkan 1.2 descriptor-indexing features are queried and selectively
  enabled; model textures use a separate runtime-sized set with a device-bounded
  capacity. The manifest and shader build no longer have a 64-texture cap.
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
  texture heap is therefore the only binding in set 1; do not merge it back
  into the scene set, whose bindings continue through 12.

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
and all 63 registered CTest suites pass, including `vulkan_smoke`. Establishing
that baseline also exposed and repaired stale UI/settings assertions left by the
earlier default-MSAA change. Representative screenshots and frame statistics
still need to be captured before material-map behavior changes.

#### 0.1 Capture visual and performance evidence

- Record representative screenshots and frame statistics for comparison.
- Record the scene, UI and editor states that are most likely to expose texture
  descriptor or material-map regressions.

**Acceptance:** screenshots and frame statistics are archived against the green
Debug baseline before material-map behavior changes.

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

### 1. Descriptor-indexing device contract — complete

#### 1.1 Represent support explicitly — complete

`VulkanDeviceFeatureSupport` now records the Vulkan 1.2 descriptor features and
both relevant sampled-image limits. Device creation enables runtime arrays,
variable descriptor counts and non-uniform sampled-image indexing. Unsupported
devices are rejected with the first actionable feature or capacity reason.

`descriptorBindingPartiallyBound` is queried but deliberately not required or
enabled: the heap fills every allocated slot with a fallback. Update-after-bind
features are likewise not required.

#### 1.2 Runtime capacity policy — complete

The configured ceiling is 1,024 descriptors, bounded by per-stage and per-set
device limits, with 16 editor and 32 import slots reserved at admission. Unit
fixtures cover low-limit diagnostics, and logical-device layout support is
checked before descriptor allocation. Manifest texture IDs remain append-only.

### 2. Isolate the texture heap — complete

#### 2.1 Split the pipeline layout by update frequency — complete

- **Set 0 — scene/frame resources:** shadows, scene images, SSAO, frame UBO,
  skinning, draw instances, materials, UI font and title image.
- **Set 1 — sampled texture heap:** the runtime-sized model/content texture
  array as the final and only binding in that set.

All pipelines use this two-set layout. No per-material or per-draw descriptor
sets were introduced.

#### 2.2 Preserve frame-safe updates — complete

There is one texture set per frame in flight. Existing `FrameDescriptorSync`
generation tracking updates only the fence-completed frame, while nonresident
and reserved entries remain valid fallback descriptors.

#### 2.3 Remove the compile-time array contract — complete

The shaders now use a runtime descriptor array with explicit non-uniform
indexing. `MODEL_TEXTURE_COUNT`, CMake header scraping, `maxModelTextures`, fixed
shader clamps and manifest cap enforcement are gone. Parsing and runtime
registration are covered beyond 64 entries.

### 3. Discover glTF texture dependencies outside manifest parsing

#### 3.1 Add a side-effect-free glTF inspection API — complete

`inspectGltfAssetDependencies(path)` now validates document structure and
returns engine-owned buffer, image, sampler and material-texture metadata. It
records core map semantics, texture/image/sampler indices and names, UV sets,
normal scale, occlusion strength and `KHR_texture_transform` metadata. cgltf
types remain private to `GltfMesh.cpp`.

Inspection deliberately uses the structure-only parse seam: external buffers
and images are reported but never opened, no GPU resources are created, and
`AssetManifest::parse(string)` remains unchanged and free of filesystem I/O.
`GltfDependencyTests.cpp` writes synthetic external/data-URI `.gltf` and
embedded-image `.glb` fixtures and verifies this boundary.

#### 3.2 Introduce texture-source identity — complete

`TextureSource.hpp` owns a variant for asset-root-relative external files,
document-plus-buffer-view images and supported data URIs. A separate
`TextureInterpretation` is part of `TextureSourceIdentity`, so one byte source
used for both sRGB color and linear data intentionally occupies two inventory
entries.

`ContentInventory::textureSources` now contains unique manifest and glTF image
identities. `ContentPipeline` resolves URIs relative to each inspected document
under its explicit asset root, rejects absolute/schemed/escaped, query,
fragment, backslash and percent-encoded spellings, and stages every external
buffer and image. The old JSON regex scraper is gone; `.gltf` and `.glb` use the
validated inspector. Data and buffer-view sources remain contained in their
staged document rather than acquiring invented filesystem paths.

`ContentPipelineTests.cpp` covers canonical external-URI deduplication,
sRGB-versus-linear separation, supported data URIs, embedded GLB images and
plain/percent-encoded traversal rejection. The real 199-file asset tree stages
successfully through this path.

#### 3.3 Preserve glTF texture semantics — complete

`TextureInterpretation` now includes color space, independent U/V address
modes, magnification filtering and the complete glTF minification/mipmap mode.
Authored sampler values are preserved. An absent glTF sampler or filter maps
deliberately to repeat addressing, linear magnification and trilinear
minification; manifest textures retain their existing clamp/repeat and
nearest/linear behavior.

`ContentInventory::materialTextures` preserves one resolved record per authored
material map: document and asset label, material/texture names, semantic,
source identity, UV set and normal-scale/occlusion-strength value. UV0 and UV1
are accepted because those are the vertex sets the renderer carries. Higher UV
sets fail content validation.

`KHR_texture_transform` is deliberately rejected until `MeshMaterial` and the
shader path can represent it; it is never silently ignored. Transform, UV-set,
unsupported-source and sampler diagnostics include the asset/model, document,
material and texture context. Synthetic tests cover every color-space class,
authored and default sampling, UV1, sampling-sensitive identity, scale/strength
and contextual rejection.

### 4. Extend the material representations

#### 4.1 Extend `MeshMaterial` — complete

`MeshMaterial` now carries one-based optional handles and authored UV selections
for normal, metallic-roughness, emissive and occlusion maps, plus normal scale
and occlusion strength. `PrimitiveMaterialBinding` accepts independent optional
zero-based descriptor indices for those maps and converts them at the loader
boundary. Missing bindings retain the zero-handle sentinel while preserving
authored glTF parameters.

The existing manifest-owned base-color override is unchanged: the loader does
not substitute the glTF base-color image. A synthetic GLB acceptance fixture
covers default and fully bound handles, UV0/UV1, factors, map scalars, alpha
mode, cutoff, double-sided state and scrolling compatibility.

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
- `src/engine/TextureSource.hpp`: canonical external, buffer-view and data-URI
  identities plus color-space and sampling interpretation.
- `src/engine/render/GltfMesh.*`: cgltf boundary, mesh/material/animation decode.
- `tests/GltfDependencyTests.cpp`: structure-only dependency fixtures for
  external/data-URI glTF and embedded-image GLB inputs, plus loader-level CPU
  material binding/default coverage.
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
