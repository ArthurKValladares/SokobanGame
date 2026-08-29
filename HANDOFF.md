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
well. `MeshMaterial` and the std430 `GpuMaterial` ABI carry the complete core
map representation, and resolved maps now flow through model requirements,
worker decode, render-thread publication and bounded residency. HDR presentation
now uses Khronos PBR Neutral by default, with persisted exposure and a Debug
straight-clamp comparison. Player-facing UI now has a dedicated fragment path,
leaving the scene fragment shader free of font, title-art, UI-texture and
rounded scene-image branches. All four core glTF material maps now affect scene
lighting: metallic-roughness, tangent-space normals, linear HDR emissive and
ambient-only material occlusion. Authored OPAQUE, MASK, BLEND and double-sided
state now control pass selection, coverage, sorting and culling, while the
mirror-energy effect has an explicit base-color-only material contract. SSAO
now reconstructs view-space position and geometric normals from copied depth
and samples with scene-unit radius and bias into a half-resolution target. Its
full-resolution composite now uses view-space depth and normal weights instead
of a box blur, while continuing to affect ambient light only. Its estimator
uses a full two-axis integer hash for kernel rotation so half-resolution noise
does not settle into horizontal bands. A deterministic
evidence runner now archives post-tonemap scenes, filtered AO, matched controls
and CPU/GPU frame statistics. Persistent renderable identities, stable world
bounds and conservative main-scene frustum culling are now implemented;
renderer memory now flows through one VMA-backed allocator seam.

The recommended order is:

1. Add KTX2 and platform texture compression through the completed allocation
   seam (A2).
2. Continue mip/LOD streaming and fence-based retirement after compressed byte
   accounting exists (A3 remainder).

Do not implement “V4” as one monolithic change. The device contract, descriptor
layout, runtime capacity, content discovery, material representation and shader
sampling have different failure modes and should be independently reviewable.

## Repository state at this handoff

- Language and platform: C++20, SDL3, Vulkan 1.3, GLSL compiled to SPIR-V.
- Runtime content: strict `assets/manifest.json`, staged by the content tool.
- Current manifest: 36 models, 42 textures and 6 named animations.
- Current tests: 68 CTest suites in the newest configured build tree.
- Current shaders: 16 GLSL files. `triangle.frag.glsl` is 669 physical lines;
  player-facing UI uses the 93-line `ui.frag.glsl` path.
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
- **F2**: an `R16G16B16A16_SFLOAT` scene target, a distinct display image, and
  a tonemap pass using Khronos PBR Neutral by default. Exposure is persisted in
  EV, straight clamp remains available as a Debug comparison, and UI remains
  outside the output transform.
- **F3**: cgltf material factors, tangents, a second UV set, alpha modes,
  double-sided metadata, a GPU material buffer and Cook-Torrance GGX. The CPU
  and GPU representations carry all core map types, runtime handle
  assignment/residency and sampling are complete for metallic-roughness,
  normal, emissive and occlusion maps. Alpha pass selection, cutoff, ordering,
  double-sided culling and the mirror-energy material subset are complete.
- **F4a**: player-facing solid rectangles, font glyphs, title art, runtime UI
  textures and rounded scene-image composition use a dedicated UI fragment
  shader. The lit scene fragment shader no longer branches on those modes.
- **T1/T2/T3/T6/T7**: instanced tiles, separate opaque drawing and sorting,
  model back-face culling, 4x default MSAA, and AO-gated depth copying.
- **V1/V3/V5/V6**: per-swapchain-image present semaphores, direct skinning
  SSBO indexing, Vulkan 1.3 optimized shader builds, and optional anisotropy.
- **V2**: vendored VMA 3.2.1 is owned by `VulkanDeviceContext`; persistent,
  swapchain-generation, upload, staging and readback images/buffers all use its
  explicit device-local, sequential-write or readback policies. The geometry
  arena and upload ring keep their higher-level fence-safe suballocation.
- **V4**: Vulkan 1.2 descriptor-indexing features are queried and selectively
  enabled; model textures use a separate runtime-sized set with a device-bounded
  capacity. The manifest and shader build no longer have a 64-texture cap.
- **A1**: cgltf replaces the regex loader; validation, STEP and CUBICSPLINE
  sampling are implemented. A loader-level fixture covers the three-output
  layout, quaternion value normalization, tangent preservation and malformed
  output counts.
- **C1/S4/E1**: explicit camera data, closed frame-arena lifetime, MSVC CI and
  a headless Vulkan validation run.
- **0.1 evidence**: deterministic frozen-scene captures at 100% and 50% render
  scale, filtered-AO views, matched AO-off controls, GPU frame timings and
  validation-clean reproduction commands are archived under
  `docs/render-evidence/2026-08-28-ssao/`.
- **S2**: `IsoScenePreparer` retains Vulkan-free renderable identities and
  world AABBs for tiles, water surfaces and authored faces. Frame-local scenes
  snapshot identity, bounds and revision so cache updates cannot mutate an
  older leased frame. Main and preview scenes use separate caches, and runtime
  telemetry reports retained/reused/rebuilt counts.

## Corrections to the remaining review inventory

- **A3 is partly implemented, not untouched.** CPU preparation is asynchronous,
  publication is budgeted, requirements drive residency, and model/texture
  byte budgets can evict old resources. What remains is mip/LOD streaming,
  compressed-size accounting, more flexible publication budgeting, and
  removing the `vkDeviceWaitIdle` eviction fallback.
- **The material program is complete.** A runtime catalog assigns
  normal, metallic-roughness, emissive and occlusion handles from the resolved
  inventory and attaches only the relevant maps to each model's requirements.
  The main scene shader consumes every core map and honors authored alpha and
  sidedness; mirror energy deliberately consumes only base-color RGB detail.
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
- The default curve is Khronos PBR Neutral. Straight clamp is a Debug-only A/B
  selection, while user exposure is persisted in the safe `[-4, +4] EV` range
  with a 0 EV default.
- `PresentationSettings::outputTransform` is the runtime authority copied into
  each `RenderFrameData`; the tonemap pass receives only normalized frame data.
- The upscale blit, frame capture and developer Game Viewport read the display
  image, not the HDR scene target.
- Player-facing UI is composed after tonemapping.

### Ambient-mask contract

The scene target alpha channel carries the ratio of ambient contribution to
total lit contribution for opaque scene pixels. Total includes direct,
specular and emissive light, but the numerator contains ambient light only.
SSAO uses that ratio so occlusion does not darken direct or emissive light.

- `triangle.frag.glsl` and `ground_splat.frag.glsl` must calculate the same
  semantic ratio.
- Only opaque pipelines write the mask.
- Blended scene pipelines mask alpha writes so they inherit the opaque mask
  behind them.
- Scene-image UI samples scene RGB only.
- Material occlusion is resolved before the ratio: it scales the ambient term
  in both the numerator and total color, never direct, specular, emissive or
  the final composite. Screen-space AO then scales that reduced ambient share.

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

Current state on 29 August 2026: the full Visual Studio Debug build succeeds
and all 68 registered CTest suites pass, including `vulkan_smoke`. Establishing
that baseline also exposed and repaired stale UI/settings assertions left by the
earlier default-MSAA change. Representative scene/AO images and matched timing
controls are now archived and reproducible from the executable.

#### 0.1 Capture visual and performance evidence — complete

`--evidence-output` turns an ordinary bounded smoke run into a deterministic
capture: simulation is frozen, the developer workspace is hidden, render scale
is explicit, and the last frames archive the normal post-tonemap scene plus the
filtered-AO debug view. `--evidence-disable-ao` supplies a matched control rather
than attributing an entire render-scale delta to SSAO.

At 1280x720 output on the validated RTX 4060 Laptop GPU, the whole-frame GPU
timestamp was 0.753 ms with AO and 0.355 ms without it at 100% render scale; at
50%, it was 0.409 ms with AO and 0.238 ms without it. The matched difference
estimates the complete AO path at 0.398 ms and 0.171 ms respectively—a 57%
reduction. Images retain contacts and silhouettes without broad halos or
cross-wall bleeding. All four runs passed the validation gate and all 68 Debug
suites pass. See `docs/render-evidence/2026-08-28-ssao/README.md`.

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
plain/percent-encoded traversal rejection. The real 200-file asset tree stages
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

#### 4.2 Redesign `GpuMaterial` once — complete

`GpuMaterial` is now a 112-byte, 16-byte-aligned std430 record: three float
vectors carry color and scalar factors, while four explicit 32-bit integer
vectors carry five one-based handles, five UV selections, alpha mode, material
flags and double-sided state. Size, alignment and every field offset have
compile-time assertions, and both material-consuming shaders mirror the same
seven lanes with `vec4`/`uvec4` declarations. Disassembly of both compiled
SPIR-V modules confirms offsets 0–96 and an array stride of 112 bytes.

The material buffer is allocated and indexed from `sizeof(GpuMaterial)`, so a
renderer start creates the new-sized buffer rather than reinterpreting the old
64-byte layout. Existing shader behavior now reads base-color handle/UV, alpha
mode and scrolling directly from integer lanes. `GpuMaterialTests.cpp` covers
fallback entry zero, default conversion, every map and UV, all scalar factors,
alpha mode, double-sided state, scrolling flags and reserved-zero lanes.

#### 4.3 Upload map dependencies through existing residency — complete

`RuntimeTextureCatalog` assembles manifest textures and resolved glTF map uses
with source-plus-interpretation deduplication. Manifest slots remain at the low
end of the selected descriptor heap, while discovered maps occupy stable slots
at the high end. This leaves the middle available for append-only editor
textures without shifting either existing `RenderTexture` ids or imported-map
handles. A discovered glTF base-color image is intentionally ignored so the
existing manifest-owned base-color override stays authoritative.

Each model now owns an isolated list of texture dependencies and remapped
primitive-material bindings. Requests queue only those dependencies. External
files, supported data URIs and glTF buffer-view images decode to RGBA on worker
tasks through one loader, then reuse the existing render-thread upload,
publication, descriptor refresh, residency budget, eviction and failure-report
paths. Authored sRGB/linear interpretation, independent wrap axes, mag/min
filters and mip policy drive Vulkan image and sampler creation; mipmapped images
include their pyramid in residency estimates.

`RuntimeTextureCatalogTests.cpp` covers identity deduplication, interpretation
separation, shared-document models, unrelated-model isolation, stable low/high
descriptor mapping, the manifest base-color override and all three source
forms. `GltfDependencyTests.cpp` additionally verifies that map-only bindings
do not synthesize a base-color handle. The production catalog inspection, full
200-file content stage, Debug build and all 68 suites pass.

**Acceptance:** complete. Requesting a model requests all of its maps while an
unrelated model's maps remain absent from that request.

### 5. Finish HDR presentation before visual PBR acceptance — complete

#### 5.1 Implement F2c — complete

The output transform now applies exposure in linear light and uses the canonical
Khronos PBR Neutral curve by default. Straight clamp remains selectable in the
Debug Output panel for comparison. `Tonemap.cpp` supplies the matching CPU
reference used by focused tests.

User exposure is persisted in profile format 27, defaults to 0 EV and is
normalized to `[-4, +4] EV`. The Graphics options slider updates it in 0.25 EV
keyboard increments. `SettingsCoordinator` projects the persisted value into
`PresentationSettings::outputTransform`; both gameplay and editor frame builders
copy that single runtime value into `RenderFrameData`, and the tonemap pass sends
the frame value to the shader. The Debug curve choice lives alongside exposure
in the same presentation structure but is intentionally not persisted.

UI composition remains after tonemapping. The shader writes linear output and
the sRGB attachment remains the only output encode. `TonemapTests` covers the EV
range and multiplier, legacy clamp behavior, neutral low-range colors and HDR
highlight compression; profile, settings, presentation and UI suites cover the
full settings path. The complete Debug build and all 68 suites, including the
Vulkan smoke test, pass.

**Acceptance:** implementation and automated validation are complete. The
remaining representative screenshot capture is tracked in 0.1 rather than
blocking the next independent shader-structure packet.

### 6. Reduce the uber-shader before adding map branches — complete

#### 6.1 Extract non-scene modes — complete

`ui.frag.glsl` now owns every `UiDrawKind`: solid rectangles, font glyphs,
title art, runtime texture images and rounded scene-image composition. The UI
pipeline pairs that module with the existing instanced-quad vertex path, so the
draw-instance ABI, command batching, post-tonemap pass order and blend behavior
are unchanged.

`triangle.frag.glsl` no longer declares the UI font or title-art descriptors and
no longer contains material-mode branches 3, 4, 6 or 7. Scene lighting,
tile/model materials, scene blur, editor highlighting and the ambient-mask
contract remain in the scene shader. The rounded-image edge formula was moved
without changing its arithmetic.

Both SPIR-V modules pass `spirv-val`. Disassembly confirms the scene module no
longer declares bindings 3 or 4, while the UI module declares only scene color,
font, title art, the runtime texture heap and the shared draw buffer. The shader
catalog, compiler and content stage now carry all 16 modules, and the content
pipeline fixture explicitly requires the UI module. The complete Debug build,
200-file content stage and all 68 suites, including `vulkan_smoke`, pass.

**Acceptance:** complete for source separation, compiled interfaces, pipeline
creation and automated regression coverage. The post-tonemap reference capture
tracked by 0.1 remains the manual pixel-comparison evidence.

#### 6.2 Specialization policy — complete guardrail

No new specialization axis was needed for the split: UI is a separate fragment
module, and `writeAmbientMask` remains the one stable opaque-pipeline choice.
Do not create a combinatorial permutation system for material flags;
data-driven material differences belong in `GpuMaterial`.

### 7. Implement material-map sampling

Land map types separately so visual regressions remain bisectable.

#### 7.1 Metallic-roughness — complete

`triangle.frag.glsl` now samples the one-based metallic-roughness handle as
linear data, reads perceptual roughness from G and metallic from B, and
multiplies both by the authored scalar factors before clamping. A missing map is
equivalent to a white sample, preserving factor-only materials and the fallback
material. UV0/UV1 selection and the existing scrolling-material flag use the
same helper as base color, so packed maps do not drift away from animated color.

The resolved metallic value feeds both direct-light `f0`/diffuse partitioning
and the ambient approximation. Resolved roughness feeds every direct GGX light;
the current ambient approximation has no roughness-dependent environment term.
`PbrMaterial.*` provides a matching CPU reference, and `PbrMaterialTests.cpp`
covers fallback, G/B selection, ignored R/A channels, factor multiplication and
post-sample physical clamping. Existing glTF, GPU-material and runtime-catalog
tests cover linear interpretation, UV1, handles and residency. All shaders pass
compilation and SPIR-V validation; the full Debug build, 200-file content stage
and all 68 suites, including `vulkan_smoke`, pass.

**Acceptance:** complete for the mapped parameter path and automated numeric,
transport and runtime validation. Representative evidence is archived.

#### 7.2 Normal mapping — complete

`triangle.frag.glsl` now samples the one-based normal handle as linear data,
maps RGB into tangent-space `[-1, 1]`, applies glTF normal scale to X/Y and
normalizes the world-space result. It selects UV0/UV1 and scrolling through the
same material UV helper used by base color and metallic-roughness.

The fragment path re-orthogonalizes the interpolated world-space tangent against
the geometric normal, reconstructs the bitangent from the tangent handedness,
and supplies a deterministic orthogonal fallback for degenerate tangents. This
handles static meshes, GPU-skinned meshes and loader-derived tangents without
trusting interpolation or nonuniform transforms to preserve a perfect frame.
For authored double-sided materials, the final mapped normal is reversed on
back faces before every direct and ambient lighting calculation.

`resolveNormalMap` in `PbrMaterial.*` is the matching CPU reference. Its focused
suite now covers neutral samples, normal scale, tangent projection, handedness,
degenerate fallback and double-sided back faces. The synthetic glTF loader
fixture verifies both an authored negative-handedness tangent and the existing
derived-tangent path. The fallback remains a standard accumulated tangent frame,
not MikkTSpace; assets baked against MikkTSpace should continue to ship authored
`TANGENT` data for exact seam behavior.

All shaders compile and the affected vertex/fragment modules pass SPIR-V
validation. The full Debug build, 200-file content stage and all 68 suites,
including `vulkan_smoke`, pass.

**Acceptance:** complete for authored and derived tangent transport, mapped
normal resolution, double-sided lighting and automated runtime validation.
Representative evidence is archived.

#### 7.3 Emissive — complete

`triangle.frag.glsl` now resolves the emissive factor and optional one-based
emissive-map handle. The image is uploaded through an sRGB Vulkan format, so
the texture unit decodes it before the shader multiplies its RGB by the factor
and adds the result to the linear HDR scene color. Texture alpha is ignored,
the no-map case behaves like a white sample, and the result is deliberately
unclamped. UV0/UV1 selection and scrolling use the shared material UV helper.

Emissive is excluded from the ambient-mask numerator but included in the total
lit denominator. This preserves the composite's ambient-only subtraction:
screen-space AO cannot darken self-emission. `resolveEmissive` and
`ambientLightRatio` in `PbrMaterial.*` mirror those rules on the CPU. The
focused suite covers factor-only materials, linear RGB multiplication, ignored
alpha, values above 1.0 and the AO ratio, and all 25 checks pass. All shaders
compile and the edited SPIR-V validates; the full Debug build and all 68 CTest
suites, including `vulkan_smoke`, pass.

**Acceptance:** complete for emissive color-space handling, factor/map
resolution, HDR preservation, UV selection and ambient-mask isolation.
Representative evidence is archived.

#### 7.4 Occlusion — complete

`triangle.frag.glsl` now samples the optional one-based occlusion handle as
linear data, reads only R, and interpolates from an unoccluded value of one to
the sample using the clamped glTF strength. A missing map remains neutral.
UV0/UV1 selection and scrolling use the same shared helper as the other core
maps.

The resolved factor scales `ambientContribution` after both its diffuse and
metallic fill have been assembled, but before that contribution is added to
the lit color or measured for the ambient mask. Direct lights, GGX specular and
emissive are therefore unchanged. The screen-space composite receives the
ratio of the already material-occluded ambient term to total outgoing light,
so the two occlusion sources compose without double-applying either one to
non-ambient light. `ground_splat.frag.glsl` has no glTF material input and
continues to write the same semantic ratio without a map sample.

`resolveMaterialOcclusion` in `PbrMaterial.*` is the matching CPU reference.
The focused suite now passes 34 checks covering no-map fallback, R-only
sampling, strength endpoints/interpolation and clamping, plus material/SSAO
composition. All shaders compile, the edited SPIR-V validates, the full Debug
build stages all 200 content files, and all 68 CTest suites—including
`vulkan_smoke`—pass.

**Acceptance:** complete for occlusion color space, channel and strength
semantics, UV selection, ambient-only application and SSAO composition.
Representative evidence is archived.

#### 7.5 Alpha and mirror paths — complete

`MaterialRenderPolicy.*` is the CPU reference and runtime summary for authored
alpha/sidedness. Model publication records whether a mesh contains opaque/mask,
blend or double-sided materials. A mixed mesh uses the existing opaque and
blended pipelines: the recorder submits its OPAQUE/MASK primitives to the first
pass and BLEND primitives to the second, and `triangle.frag.glsl` rejects the
other subset per primitive. No new pipeline permutation or descriptor set was
added. Translucent model draws are stable-sorted farthest first; source order
inside one model remains the glTF author's responsibility.

Base-color factor and the selected base-color texture multiply RGB in every
model material mode, including manifest texture overrides. OPAQUE ignores
authored factor/texture alpha, MASK discards when their product is below the
authored cutoff, and BLEND multiplies the instance alpha by that product. A
sub-1.0 instance tint now puts the whole model in the translucent pass.

Fixed-function back-face culling remains enabled for wholly single-sided
models in either pass. Any double-sided material disables whole-draw culling;
the standard and mirror fragment paths then reject back faces only for the
single-sided primitives in a mixed model, while mapped normals continue to flip
for surviving double-sided back faces.

Mirror energy has an explicit narrow contract: base-color factor RGB, the
selected base-color texture RGB, UV0/UV1 selection and scrolling may shape its
detail. Factor/texture alpha, alpha mode/cutoff, metallic-roughness, normal,
emissive and occlusion do not affect the ghost; effect color and animation own
its opacity and lighting response.

The focused material suite now passes 55 checks covering pass selection,
OPAQUE/MASK/BLEND alpha math and the mirror subset. The scene-preparation suite
passes 1,564 checks including alpha-tinted model classification and persistent
identity/bounds behavior. The embedded
GLB loader fixture still covers all core maps, both UV sets, MASK and
double-sided metadata; production external glTF content exercises BLEND and
double-sided summaries. All 16 shaders compile, the Debug build stages 200
files, and all 68 CTest suites—including `vulkan_smoke`—pass.

**Acceptance:** automated material transport, policy and runtime validation are
complete. Representative appearance evidence is archived. MASK currently
cuts out the visible scene pass; the depth-only model-shadow pipelines still
cast the mesh silhouette and would need a texture-sampling fragment stage if
cutout shadow silhouettes become an authored-content requirement.

### 8. Complete V7 SSAO

#### 8.1 View-space inputs and physical sampling — complete

`isoClipFromView` exposes the exact scene projection without its rigid camera
transform. `VulkanSsaoPass` pushes that projection and its inverse to the AO
shader without extending the frame descriptor ABI. For each copied depth pixel,
`ssao.frag.glsl` reconstructs view-space position, derives a camera-facing
geometric normal from position derivatives, places twelve rotated samples in a
normal-oriented hemisphere, projects them back to the depth texture and
compares reconstructed positions in view units.

The fixed 10-pixel disk, raw-depth range and `0.0005` depth bias are gone.
`ssaoRadiusWorld` is 0.45 scene units and `ssaoBiasWorld` is 0.025 scene units,
so render resolution changes only the pixel footprint of the same physical
neighborhood. Euclidean view-space distance fades samples between one and two
radii, rejecting unrelated silhouettes without tying the threshold to the
camera's non-linear depth encoding.

`SsaoMath.*` mirrors projection round-tripping, negative-viewport Y handling,
normal orientation and sample comparison on the CPU. The first 15 checks cover
8.1, including identical physical reconstruction across two render resolutions
with the same aspect ratio; 8.2 extends the same suite with half-up sizing,
bilateral rejection and a two-axis rotation-noise regression. Both SSAO
modules compile, the edited SPIR-V validates and uses derivative instructions,
the Debug build stages all
200 files, and all 68 CTest suites—including `vulkan_smoke`—pass.

**Acceptance:** complete for view-space position/normal reconstruction,
scene-unit radius and bias, render-scale-independent sampling math and the
existing ambient-only composite. Appearance evidence is archived.

#### 8.2 Half-resolution bilateral path — complete

`VulkanSsaoPass` now owns an `R8_UNORM` AO image whose dimensions are the
ceiling of half the scene-render dimensions. Odd and one-pixel extents remain
covered, recreation recomputes both extents, and only the estimator uses the
smaller viewport/scissor/render area. The composite still covers the complete
scene target. The estimator maps its half-resolution fragment coordinates over
the full copied depth image, preserving the physical view-space sampling from
8.1. Its per-pixel kernel rotation now comes from a full two-axis integer hash.
The prior interleaved-gradient pattern changed much more slowly between rows
than columns, so its row-correlated error survived upsampling as horizontal
bands on large flat surfaces.

`ssao_composite.frag.glsl` replaces the full-resolution 5x5 box blur with the
native four-candidate bilinear footprint of the half-resolution AO image. For
each candidate it reconstructs full-resolution view-space position and a
camera-facing geometric normal. Gaussian plane-distance weights in scene units
and normal-agreement weights reject samples across depth discontinuities and
adjoining faces before normalization. Background candidates are excluded, and
the filtered AO still scales only the scene alpha's ambient share. The scene
color snapshot remains necessary because the unblended composite cannot sample
the color attachment it is simultaneously writing; half-resolution AO does not
change that data dependency.

`SsaoMath.*` now provides overflow-safe half-up target sizing, a CPU reference
for the bilateral weight, and the exact shader rotation hash. The focused suite
passes 2,110 checks, including even, odd, one-pixel and empty extents,
same-plane retention, depth/normal-edge rejection, deterministic hash output,
both-axis variation and bounded per-row distribution. Both edited shaders
compile and pass Vulkan 1.3 SPIR-V validation;
the Debug build stages all 200 files, Vulkan smoke passes, and all 68 CTest
suites pass.

**Acceptance:** implementation complete for half-resolution storage/recording,
full-resolution silhouette-aware upsampling, ambient-only composition and
resize edge cases. Representative images and matched GPU timings are archived
under `docs/render-evidence/2026-08-28-ssao/`.

The banding regression capture is archived separately under
`docs/render-evidence/2026-08-29-ssao-banding/`; its filtered-AO frame retains
contact occlusion without the long periodic horizontal stripes from the
reported debug view.

**Acceptance:** AO is stable across render scales and camera changes, does not
bleed across silhouettes, and does not darken direct highlights or emissive
surfaces.

### 9. Resume frame-scaling work

With S2, T4 and V2 complete, continue by measured frame and memory cost:

1. **A2:** KTX2 plus BC7/appropriate platform formats in the content tool.
2. **A3 remainder:** mip/LOD streaming, compressed byte accounting and
   fence-based eviction retirement instead of `vkDeviceWaitIdle`.
3. **T8:** parallelize Vulkan-free scene preparation first; add secondary
   command buffers or a transfer queue only after profiling.
4. **T5:** range-cull point-shadow casters, then cache unchanged faces.
5. **A4:** reuse recorder scratch storage after scene data structures settle.

#### 9.1 S2 persistent renderables and stable bounds — complete

The preparer reconciles tiles, water surfaces and authored faces into retained
cache slots. Stable semantic sources keep their identity while geometry changes
increment a bounds revision; unchanged geometry reuses its AABB. Each prepared
frame receives a value snapshot, preserving the two-frame lease model, and the
preview path uses a separate cache so it cannot churn main-scene identity.
Dynamic gameplay entities carry presentation identity even when not animated.
Debug and evidence telemetry expose retained/reused/rebuilt counts; a frozen
live frame reported 204 retained renderables, 204 reused bounds and zero rebuilt
bounds.

The current focused scene-preparation suite passes 1,503 checks, the validation smoke
run is clean, and all 68 Debug suites pass.

#### 9.2 T4 frustum culling — complete

`IsoScenePreparer` extracts the camera frustum from `isoClipFromWorld` and
classifies retained AABBs before building the main color/depth face and model
lists. Exact cube, water and authored-face bounds may be rejected. Model-backed
tiles fail open because their retained unit transform is not yet the loaded
mesh's true local AABB. Invalid bounds also fail open. Picking faces, sun-shadow
casters and point-shadow casters deliberately ignore this classification.

Visible/culled counters, a developer checkbox, rolling scene-preparation time
and an evidence-only `--evidence-disable-frustum-culling` control make the
policy inspectable. In the frozen 1280x720 capture, 74 of 204 retained records
were outside the main frustum. Draw calls fell from 51 to 43 and submitted
triangles from 14,504 to 14,068. Preparation moved from 1.743 to 1.768 ms,
CPU draw-frame time from 5.504 to 5.489 ms and GPU frame time from 1.649 to
1.280 ms on the captured RTX 4060 Laptop GPU. The timing deltas are run- and
hardware-specific; the deterministic claims are the list/count reductions and
byte-identical composed-scene and AO images.

The focused suite passes 1,503 checks, including culling on/off coverage that
keeps picking and shadow counts identical. The complete Debug build and all 68
CTest suites, including Vulkan smoke, pass. Evidence is archived under
`docs/render-evidence/2026-08-29-frustum-culling/`.

#### 9.3 V2 allocation consolidation — complete

VMA 3.2.1 is vendored under `third_party/vma` and wrapped by
`VulkanMemoryAllocator`, one instance per `VulkanDeviceContext`. Its public
policy distinguishes device-local resources, persistently mapped sequential
writes and persistently mapped readback. All renderer image and buffer owners
now use that seam: swapchain-generation targets, SSAO and shadow images, model
textures, UI/thumbnail images, model/material/frame buffers, geometry backing
blocks, the shared upload ring, transient staging and frame capture readback.
There are no direct `vkAllocateMemory`, `vkFreeMemory`, `vkMapMemory` or
`vkUnmapMemory` calls left under `src/engine/render`.

The existing owners still determine lifetime. VMA replaces memory selection,
allocation, binding and mapping; it does not replace the geometry arena's slice
allocator, upload-ring reservations, fences, retired render-resource sets or
descriptor publication rules. The Vulkan smoke test now allocates a mapped
host buffer and device image, checks VMA statistics, frees both and verifies
the live allocation count returns to zero. A validation-enabled 120-frame game
run exercised render targets, runtime asset uploads and teardown. The complete
Debug build and all 68 suites pass.

#### 9.4 A2 compressed texture pipeline — next

Add KTX2 as a content artifact and select BC7 or the appropriate platform
format at build time. Keep source-image interpretation in the content tool,
preserve sRGB versus linear semantics, retain an uncompressed compatibility
fallback, and account residency in actual compressed mip bytes. Do not couple
this packet to a scheduler rewrite; A3 retirement and mip/LOD policy follow
after the format path is independently validated.

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

- `src/engine/render/IsoScenePreparer.*`: completed retained bounds and
  main-scene culling; preserve fail-open model, picking and shadow behavior.
- `src/engine/render/RenderTypes.hpp`: source render data and frame-level
  ownership boundaries that the persistent representation must not blur.
- `src/engine/Geometry.*`: completed bounds/frustum primitives.
- `src/engine/render/VulkanRenderer.*`: preparation orchestration and completed
  before/after culling telemetry; preserve its retirement boundaries while A2
  changes texture creation.
- `src/engine/render/VulkanMemoryAllocator.*`: completed VMA ownership and the
  only renderer memory-policy seam; extend this instead of reintroducing raw
  Vulkan allocation calls.
- `src/engine/render/VulkanModelResources.*` and the content tool: next A2
  boundary for compressed artifacts, format selection and byte accounting.
- `src/engine/render/VulkanSceneRecorder.*`: consumer of the prepared visible
  lists; keep Vulkan recording downstream of Vulkan-free culling.
- `tests/IsoScenePreparerTests.cpp`: stable identity/bounds behavior is covered;
  add conservative visibility cases before changing recorder behavior.
- `src/engine/render/VulkanSsaoPass.*`, `shaders/ssao*.glsl` and
  `tests/SsaoMathTests.cpp`: completed V7 baseline; revisit only if visual or
  timing evidence exposes a concrete regression.

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
