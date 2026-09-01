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
now reconstructs view-space position and geometric normals from the published
single-sample depth resolve
and samples with scene-unit radius and bias into a half-resolution target. Its
full-resolution composite now uses view-space depth and normal weights instead
of a box blur, while continuing to affect ambient light only. Its estimator
uses a full two-axis integer hash for kernel rotation so half-resolution noise
does not settle into horizontal bands. A deterministic
evidence runner now archives post-tonemap scenes, filtered AO, matched controls
and CPU/GPU frame statistics. Persistent renderable identities, stable world
bounds and conservative main-scene frustum culling are now implemented;
renderer memory now flows through one VMA-backed allocator seam.
The content build now emits deterministic KTX2 artifacts with native BC7 mip
chains for every unique source interpretation. BC7-capable devices upload the
precomputed blocks directly, while unsupported devices and runtime-authored
textures retain the original RGBA decode path.

The post-A4 profile is complete. Uploads are bounded startup work: the normal
capture submitted and completed 32 texture uploads and ended with zero in
flight; the 64 KiB pressure capture completed 16 and also ended at zero. A
transfer queue is therefore not justified. Command recording was the durable
CPU cost, but the dominant cause was 204 directional-shadow quads encoded as
individual push/draw pairs rather than a lack of recording threads. The
directional pass now reads the existing frame-owned draw-instance buffer and
emits one instanced tile draw. Matched evidence reduced command recording from
2.817 to 1.332 ms and CPU frame time from 3.349 to 1.842 ms with byte-identical
scene/AO captures. Secondary command buffers are no longer the next action:
the captured frame is GPU-bound at 3.223 ms and the two remaining CPU game
buckets are only 0.559 ms of shadow work and 0.474 ms of scene work.

The point-shadow stress and Release GPU profile are now complete as well. A
deterministic eight-light mode keeps skinned casters in range so all 48 cube
faces and 7,560 tile-face candidates record every frame; Debug validation and
visual inspection are clean, with no frame-instance exhaustion. Fence-read GPU
timestamps now cover shadows, main scene color/depth, SSAO and output/UI. At
2880x1800 Release, the normal frame measured 3.715 ms GPU: 0.057 ms shadows,
1.870 ms scene, 1.601 ms SSAO and 0.182 ms output. AO-off measured 1.986 ms;
50% render scale measured 0.943 ms.

That subpass split is now complete. At native resolution, the baseline broke
down into 1.667 ms scene raster/resolve, 0.135 ms depth copy, 0.260 ms scene-
color snapshot, 0.368 ms AO estimation and 0.923 ms AO composite. The sampled
depth copy and its separate full-resolution D32 image have been removed: SSAO
and translucent water now read the single-sample depth resolve in
`DEPTH_READ_ONLY_OPTIMAL`, with explicit attachment restoration before another
scene render. The matched Release depth-publish boundary is 0.001 ms, native
GPU time moved from 3.616 to 3.441 ms, and native plus 50% scene captures are
byte-identical.

The color snapshot is now gone on the normal opaque-only AO path as well.
Opaque MSAA resolves directly into the sampled scene image and the unchanged AO
composite writes the existing HDR output target; translucent content keeps the
copy fallback. The snapshot boundary fell from 0.198 to 0.000 ms and the
matched native Release frame from 3.441 to 3.255 ms, with byte-identical scene
and AO captures.

Evidence MSAA is deterministic now. `--evidence-msaa` accepts only 1x, 2x, 4x
or 8x, defaults evidence to the 4x product setting without reading or rewriting
the saved preference, and records the sample count in report titles and artifact
names. In warmed native Release captures, 1x/4x/8x GPU-frame averages were
0.828/0.975/1.148–1.216 ms. At 4x, scene raster/resolve measured 0.239 ms and
SSAO composite 0.202 ms: neither is a sufficiently dominant target to justify
a speculative rewrite.

Translucent evidence and its first measured follow-up are complete. The tested
`--evidence-water` mode adds a visible exterior shoreline without changing
gameplay state, gives every report and image a `-water` suffix, and fails the
capture unless both the translucent scene pass and AO color-copy fallback were
actually recorded. Two warmed 4x Release runs produced identical scene and AO
hashes. The retained snapshot costs only 0.024–0.025 ms, so restructuring that
fallback is not justified. The water shader itself was the useful narrow
target: skipping its second projected-caustic evaluation when resolved depth
says there is no opaque geometry behind the surface reduced stable translucent
time from 0.466 to 0.388–0.390 ms and the GPU frame from 1.050 to 0.970–0.977
ms, with byte-identical output. The frame-scaling program is now at a measured
stopping point; resume it only for a concrete content or performance target.

Do not implement “V4” as one monolithic change. The device contract, descriptor
layout, runtime capacity, content discovery, material representation and shader
sampling have different failure modes and should be independently reviewable.

## Repository state at this handoff

- Language and platform: C++20, SDL3, Vulkan 1.3, GLSL compiled to SPIR-V.
- Runtime content: strict `assets/manifest.json`, staged by the content tool.
- Current manifest: 36 models, 42 textures and 6 named animations.
- Current tests: 76 CTest suites in the newest configured build tree.
- Residency eviction: `ResidencyBudget::needsEviction` takes `(bytes, limit)`
  and reads `retiring_` as its only running total. It used to take a third
  argument that duplicated `retiring_`, so each victim counted twice and the
  loop freed about half the shortfall per round. Measured over 200,000
  randomised pools the correction evicts the same victims and the same bytes;
  it only changes how many rounds a publication under pressure takes, from up
  to nine down to two. Two is the floor: retiring bytes stay charged until the
  fence clears, so one round chooses victims and the next admits.
  Captured on 1 September: texture residency 63,376 / 65,536 bytes, 37/46 mip
  levels, five reduced textures, 831,232 bytes omitted - the archived baseline
  reproduced exactly, with no validation errors. Eleven evictions across 240
  frames, and model residency never left 1,775,756 / 134,217,728 bytes, so that
  capture exercises the eviction path for textures only.
  STILL UNMEASURED: the round count the fix actually changes. The capture cannot
  show it, because 3,324 of its refusals are assets that can never fit a 64 KiB
  budget rather than eviction pressure. `residencyBudgetBlocks` has been split
  into `residencyOversizedBlocks`, `residencyMipPlanBlocks` and
  `residencyNoVictimBlocks` so the next run separates them; only the last is
  pressure. To measure the fix directly, run the same capture with
  `ResidencyBudget::needsEviction` reverted to subtracting a second tally and
  compare, or add a model-budget override so the model pool is stressed too.
- `RenderStats` carries every phase timing as a `RenderPhaseTiming`. The GPU
  frame timing is written only when the device reports timestamps, so an
  unsupported device keeps the last value instead of being overwritten with an
  unavailable summary each frame; keep that guard.
- Shader contract: `shaders/include/DrawMode.glsl` carries the draw-mode
  numbering and the `isModelDraw` sign test; `draw_mode` pins both against
  `VulkanRenderConstants.hpp` and fails if either spelling moves.
- The isometric projection exists as a scalar form and a matrix, on purpose -
  only the scalar one can clamp view-space z. They are algebraically identical
  but not bit-identical, because the scalar form applies `fitScale` once at the
  end while the matrix folds it into each coefficient. Measured over 200,000
  randomised camera states: zero difference at the median, 1.3e-4 relative at
  worst, never past 1e-3. `IsoScenePreparerTests` compares them after the
  perspective divide with an absolute tolerance of 5e-4, which is why that
  number is the number.
- Point shadowing lives once, in `shaders/include/PointShadow.glsl`. Each shader
  picks its filter with `#define POINT_SHADOW_TAPS` before including it: the
  scene asks for 5, the ground for 1. The split is deliberate - both loop over
  up to eight point lights, so five taps on the ground would be up to forty
  cube-map samples on the largest surface in the frame against eight now.
  Changing it is a one-token experiment and wants a `--evidence-point-light`
  capture, not a cleanup. The near plane the cube-face projections are built
  with is `POINT_SHADOW_NEAR_PLANE` there, pinned against
  `config::pointShadowNearPlane` by `draw_mode`; drift offsets every recovered
  distance by a constant and looks like acne, not like a bad number. Do not replace the two sampling bodies with a shared
  loop run once: glslc does not unroll a one-iteration loop and the ground pays
  204 bytes and 14 instructions of loop overhead for it. The directional filter
  is a 3x3 kernel in both shaders and already agrees.
- Current shaders: 16 GLSL files plus shared declarations under
  `shaders/include/`, which the shader build passes to `glslc` with `-I`.
  `triangle.frag.glsl` is 669 physical lines; player-facing UI uses the
  93-line `ui.frag.glsl` path.
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
  model back-face culling, 4x default MSAA, and AO-gated direct depth-resolve
  publication.
- **T8**: a dedicated one-worker frame-preparation lane overlaps independent
  shadow/particle list generation with main-scene projection, culling and
  sorting. When a screen preview exists, whole main/preview preparations run
  concurrently through their already-separate retained caches.
- **T5**: point-shadow face casters are conservatively range-culled in Vulkan-
  free preparation, static loaded models use transformed mesh bounds, and exact
  unchanged light/caster states reuse all six stored cube layers. Skinned
  casters fail open and force re-recording because bind-pose bounds and frame
  inputs are not a proof that the GPU pose is unchanged.
- **A4**: model recording retains candidate, resolved-draw, ordered-item and
  batch vectors across frames. The sorter writes into caller-owned output and
  translucent depth ties use an explicit source ordinal, avoiding both the
  returned batch allocation and `stable_sort` temporary storage.
- **9.9 recording profile and directional-shadow batching**: fixed-size phase
  histories attribute scheduling, fence, maintenance, acquisition, recording,
  submit/present, recorder subphases, publication events and upload completion.
  Summaries are produced only for stats consumers, not in the frame hot path.
  Directional tile casters use one instanced draw; point-light faces retain the
  capacity-safe push-constant path because six faces across all lights can
  exceed the shared frame-instance budget.
- **9.10 point-shadow stress and Release GPU phases**: an explicit eight-light
  evidence mode continuously exercises all 48 point-shadow cube faces without
  allowing cache reuse to hide the push path. Fence-owned timestamp queries
  report shadows, scene color/depth, SSAO and output/UI without a CPU/GPU wait.
- **9.11 GPU subpasses and direct depth-resolve sampling**: fence-read queries
  split scene raster/resolve, depth publication, translucency, scene-color
  snapshot, AO estimation and AO composite. SSAO and water sample the resolved
  depth attachment directly; the redundant D32 image and depth copy are gone.
- **9.12 direct opaque-color resolve for SSAO**: the normal opaque-only AO path
  resolves into the sampled scene image and composites into the HDR output,
  eliminating its full-resolution color snapshot. Translucent, preview and
  level-transition paths retain their safe copy contracts.
- **9.13 deterministic evidence MSAA**: evidence runs use an explicit tested
  1x/2x/4x/8x input, default to the 4x product setting independently of saved
  preferences, and encode the active sample count in reports and filenames.
  The matched Release matrix establishes the product-default baseline.
- **9.14/9.15 translucent evidence and safe water optimization**: a visible
  exterior water fixture proves the retained AO color snapshot, report fields
  identify the exercised path, and repeated captures are deterministic. The
  snapshot is too small to justify restructuring; skipping projected caustics
  over clear background instead improves the measured water pass without
  changing any scene or AO bytes.
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
- **A2**: the content tool emits KTX2 2D artifacts with complete BC7 mip
  chains, linear-light sRGB downsampling and stable source-interpretation
  identities. Runtime format queries select BC7 UNORM/SRGB when supported and
  retain source-image decoding as the compatibility and editor fallback.
  Residency uses the exact sum of uploaded compressed mip bytes.
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

- **A3 is complete for the current renderer.** CPU preparation is asynchronous,
  publication is budgeted, requirements drive residency, model/texture byte
  budgets evict through fence-owned retirement, and compressed textures select
  the finest complete mip tail that fits measured capacity. More flexible
  publication budgeting is conditional on profiling, not unfinished A3 work.
- **A transfer queue is not current work.** Both normal and hard-budget upload
  captures drained to zero in-flight submissions, while publication events
  were bounded startup costs. Reconsider queue ownership only for a sustained
  streaming workload with measured overlap or graphics-queue stalls.
- **Secondary command buffers are not current work.** After directional-shadow
  batching, the captured CPU frame is 1.842 ms against a 3.223 ms GPU frame,
  and the remaining shadow/scene recording buckets are 0.559/0.474 ms. The
  command-pool and worker synchronization cost does not have a measured CPU
  bottleneck to solve.
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

## Code-quality track

A separate, non-packet track runs against `codequality-review.html`, which holds
the full findings list and the evidence behind each change. It is status, not a
second roadmap: consult it before reopening any item below.

Landed so far: the line-ending policy and its CI gate; shared GPU struct
declarations under `shaders/include/`, with `gpu_abi` checking SPIR-V member
offsets against `offsetof` and `draw_mode` pinning `DrawMode.glsl` to
`DrawMaterialMode`; one `TestHarness.hpp` behind 55 suites; `ResidencyBudget`
and `MaterialRangeAllocator` extracted from `VulkanModelResources`;
`SceneDrawLanes` extracted from `VulkanSceneRecorder`; the eviction ladder's
double-counted victim corrected; the
`GpuDrawInstance` lane union traced and written down; the scene descriptor
layout made table-driven; the last three phase timings converted to
`RenderPhaseTiming`; `TextureDescriptorSpace` given the heap partition;
`GpuMappedBuffer` given the four owning buffer structs and three views;
`PointShadow.glsl` given both copies of the point-shadow filter; and
`Geometry.hpp`'s `Aabb` given the two hand-rolled bounds structs and the four
eight-corner expansions; and `beginOneShotCommands`/`submitOneShotCommands`
given the six copies of one-shot command-buffer and fence lifetime; and
`RenderFrameParts` given the 42% of `RenderFrameBuilder.cpp` that both the
gameplay and the editor builders were sharing without saying so.

Still open, in the order the report recommends: splitting
`VulkanModelResources` proper and collapsing its three copies of the load-state
machine, then the remaining long functions. There are 17 functions at or past
200 lines, not the eight the review first reported - `drawIsoFrame` (487),
`VulkanSceneRecorder::record` (305) and `Application::buildRenderFrame` (302)
are among those it never named. Five of the sixteen still open are ImGui panels
and are deliberately last: splitting one is mechanical, but the only check that
it still behaves is to look at the screen.

The two largest frame-building offenders now sit alone in their own files -
`EditorFrameBuild` (692 lines) is all of `RenderFrameBuilderEditor.cpp` bar two
entry points, and `appendMirrorPreview` (279) is the largest thing left in
`RenderFrameBuilder.cpp`. That was the point of the split: neither is smaller,
but each is now the obvious next thing in its file rather than one of forty
helpers in a 2,500-line one.

One item is parked deliberately rather than fixed, because it needs a
judgement this review cannot make from the source alone:

- `GpuDrawInstance` is a 256-byte union, and inside the scene pipeline
  `materialOptions.y` is a grid cell width for a quad draw and a
  scrolling-material UV offset for a model. Nothing separates the two but
  `modelDrawMarkerAlpha`: `gridMask()` rejects a non-positive grid alpha and a
  model writes a negative one. Mirror-energy models carry no marker and are safe
  only because their pipeline uses a fragment shader with no `gridMask()` at
  all. The review's recommended fix - a `uvec4` of explicit integer lanes
  carrying a draw-kind tag - is a real ABI change in a struct that is already
  exactly full, and its only proof is that the picture still looks right, so it
  is left for an implementer who can run the renderer. The lane table on
  `GpuDrawInstance` is the current contract; a new lane needs its own slot
  rather than a third claimant.

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
- Eviction snapshots the submitted-frame mask and destroys resources only after
  every referencing frame fence clears its bit. Descriptor work must preserve
  this lifetime boundary.
- The material buffer reserves entry zero as the fallback. Published model
  ranges start after it, never move while live, and return to the free list only
  after fence-owned retirement.

## Itemized implementation sequence

Each numbered packet should be reviewable and verifiable on its own. Do not
combine adjacent packets merely because they touch the same files.

### 0. Refresh the baseline

Current state on 31 August 2026: the full Visual Studio Debug build succeeds
and all 69 registered CTest suites pass, including `vulkan_smoke`. Establishing
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

#### 0.3 Normalize line endings separately — complete

- The unused `src/engine/render/GpuModelInstance.hpp` has been deleted.
- `.gitattributes` pins the policy: LF in the repository, CRLF checked out for
  the MSVC-owned file types, `-text` for `third_party/**` and the vendored
  asset pack. The committed tree was already clean; the drift was working-tree
  only, with no content differences.
- A `line-endings` CI job runs `git add --renormalize .` and fails on any
  staged difference, so the policy cannot silently lapse.

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
shader without extending the frame descriptor ABI. For each resolved depth pixel,
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
the full published depth image, preserving the physical view-space sampling from
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

S2, T4, T5, V2, A2, A3, A4, T8, the post-A4 recording/upload profile, the
point-shadow stress gate and top-level GPU pass timestamps are complete.
Continue in this order:

1. Preserve the deterministic `--evidence-water` gate for every change to
   water, translucent ordering, resolved scene color or the retained AO copy.
   A counter-only run is insufficient: the water surface must remain visible
   and the scene/AO hashes must remain stable where output should not change.
2. Leave the retained translucent color snapshot in place. Its measured
   0.024–0.025 ms cost is below the threshold for another image-ownership
   rewrite; the background-caustic skip already addressed the larger safe
   water cost with exact output preservation.
3. Reconsider secondary command buffers only if a larger content scene makes
   CPU command recording a durable frame bottleneck again. Reconsider a
   transfer queue only for sustained streaming that leaves uploads in flight.
4. Keep S1 fixed-tick simulation, S3 task-graph work and F5 RHI work conditional
   on their original product requirements.

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

#### 9.4 A2 compressed texture pipeline — complete

`CompressedTextureArtifact` builds and validates little-endian KTX2 files with
native BC7 blocks. Artifact names are a stable digest of the complete
`TextureSourceIdentity`, so sRGB/linear and sampler-distinct uses never alias.
Mipmapped sources receive a complete CPU-generated chain; sRGB color is
filtered in linear light, while linear data remains linear. Physical KTX2 mip
data is smallest-first and the level index remains base-first as required.

The content tool stages one artifact for each of the 52 production texture
identities and includes it in `content.index`; source PNG/JPEG/glTF bytes stay
in the package. Runtime device format queries require sampled-image, linear
filter and transfer-destination support for both BC7 variants. Supported
devices load and upload every precomputed mip directly; unsupported devices,
missing legacy artifacts and newly painted editor textures use the existing
RGBA path. A present but malformed artifact fails instead of silently changing
quality. Texture budgets account the exact sum of BC7 mip bytes.

The Debug package contains 252 files. Artifact round-trip/corruption tests,
runtime selection/fallback tests and content-staging fixtures pass. All 69
CTest suites pass, and a validation-required 120-frame run on the RTX 4060
Laptop GPU completed with clean upload and teardown. The deterministic capture
under `build/a2-evidence` shows intact scene, character, ground and prop
textures without block-layout corruption.

#### 9.5 A3 fence-owned retirement and mip residency — complete

Residency eviction no longer calls `vkDeviceWaitIdle`. Models and textures move
to retirement queues carrying the exact mask of submitted frame slots that can
still reference them. Each completed frame fence clears its bit; only a zero
mask releases geometry allocations, image views and samplers. Retiring bytes
remain charged to the hard residency budget, so publication retries after the
fence instead of temporarily oversubscribing GPU memory. Texture eviction marks
the fence-owned descriptor sets dirty, while model material ranges remain
stable and return to a coalescing free list only after retirement.

`FrameResourceTrackerTests.cpp` covers overlapping two-frame retirement,
immediate zero-mask release and the rule that later submissions are not added
retroactively. The Debug panel exposes retiring model/texture counts and bytes.
`TextureMipResidency` selects full quality whenever the available hard-budget
capacity can hold it. Under pressure it chooses the finest source mip whose
complete KTX2 tail fits, creates a smaller Vulkan image with that source mip as
level zero, and uploads only the selected tail. Implicit shader LOD therefore
tracks the reduced base dimensions without shader or descriptor ABI changes.
Noncompressed compatibility/editor textures retain their existing path.

Residency telemetry reports resident/available mip levels, reduced-texture
count and omitted bytes. `--texture-residency-kib` provides an explicit smoke
override while zero retains the 256 MiB production default. Unit tests cover
full, intermediate, smallest-tail and cannot-fit decisions. All 69 CTest suites
pass. A normal validation-required run used all 162/162 available levels at
10,301,968 / 268,435,456 bytes. A 64 KiB validation stress run stayed bounded
at 63,376 / 65,536 bytes, retained 37/46 levels across its then-resident
textures, reduced five textures and omitted 831,232 bytes. Its deterministic
capture under `build/a3-mip-evidence` is intact, with expected pressure-induced
softness and no block/layout corruption.

That run is free of validation *errors*, which is not the same as clean: it
emits eight `WARNING-Shader-OutputNotConsumed`, all of them the same one.
`triangle.vert` writes a constant to `outMaterialIndex` at location 10 because
`triangle.frag` declares that input - which it needs, being the fragment stage
for `model.vert` and `skinned_model.vert` as well - while four of the pipelines
built from `triangle.vert` pair it with `water.frag`, `ground_splat.frag`
(twice) and `ui.frag`, none of which declare it. Four pipelines, and a smoke
run builds the swapchain twice. Harmless in itself, and worth removing so that
the next real warning is visible.

#### 9.6 T8 Vulkan-free frame preparation — complete

`VulkanRenderer` owns a dedicated one-worker preparation lane, isolated from
the asset-loading queue so synchronous frame work cannot wait behind texture or
model jobs. A normal one-scene frame prepares shadows and particles on that
worker while the calling thread performs retained-bound reconciliation,
projection, frustum classification and stable draw-list sorting. A frame with a
screen preview uses the coarser split instead: the main and preview scenes run
concurrently through their separate `IsoScenePreparer` caches and separate
prepared-frame scratch subobjects. Vulkan calls and command recording remain
downstream of the join.

`--serial-scene-preparation` keeps a reproducible diagnostic control, and the
evidence report states which mode produced it. In matched validation-required
240-frame runs at 2880x1800, parallel preparation reduced the 120-sample scene
average from 1.670 ms to 1.571 ms and p95 from 1.812 ms to 1.744 ms. Average CPU
frame time fell from 3.396 ms to 3.238 ms and p95 from 3.923 ms to 3.768 ms.
Scene/AO captures are byte-identical between modes, as are draw, triangle,
retained-bound and culling counts. Evidence is under `build/t8-serial` and
`build/t8-parallel`.

The focused test compares serial and parallel ordered outputs across both
initial bounds construction and retained-bound reuse; it passes 1,696 checks.
The complete Debug build, both Vulkan validation runs and all 69 CTest suites
pass. The later 9.9 profile confirmed that secondary recording is not the
current bottleneck; keep T8 focused on Vulkan-free preparation.

#### 9.7 T5 point-shadow range culling and unchanged-face cache — complete

The Vulkan-free auxiliary-preparation job now builds exact AABBs alongside sun
shadow faces and filters each point light's face-caster indices against its
influence sphere without changing source order. The closest-point sphere/AABB
test includes a floating-point boundary guard, so exact range contact fails
open. At recording time, loaded static models use their real mesh AABB
transformed into world space. Unready bounds and skinned models fail open;
bind-pose bounds are not conservative for an animated pose.

`PointShadowFaceCache` retains exact light position/range, selected world-space
faces, complete static model-tile state and asset readiness. It deliberately
does not hash the key. An exact match reuses all six cube layers without a
depth transition, clear or rendering pass. Light, geometry, ordering,
transform, animation input or readiness changes re-record all six faces.
Any in-range skinned caster invalidates reuse because animation-controller
history is not fully represented by the frame tile. The cache remains safe
across the two frames in flight because submissions share the ordered graphics
queue and the cube array outlives swapchain generations.

`--evidence-point-light` adds a deterministic evidence-only light, while
`--disable-point-shadow-optimizations` restores the all-casters/all-frames
control. At 2880x1800, the optimized frozen frame retained 186 of 945 face
candidates, culled 11 of 14 static-model candidates and avoided 4,554 projected
face draws. It reused all six cube faces instead of adding six render passes.
The 120-sample CPU-frame average fell from 12.297 ms to 3.043 ms; GPU average
fell from 3.634 ms to 3.055 ms. Scene and AO captures are byte-identical.
Evidence is under `build/t5-legacy` and `build/t5-optimized`.

Geometry coverage pins inside, touching, separated, invalid and negative-radius
sphere/AABB cases. Scene-preparation coverage pins stable filtered ordering,
disabled-control counts, emitter exclusion and serial/parallel equivalence.
The exact cache tests cover light, face order/geometry, model state, readiness
and explicit invalidation. Both validation-required 240-frame evidence runs,
the complete Debug build and all 69 CTest suites pass.

#### 9.8 A4 retained recorder scratch — complete

`VulkanSceneRecorder` now owns one synchronous model-recording scratch object.
Opaque, translucent, main and preview passes clear and reuse its candidate,
resolved-draw, ordered-sort-item and batch vectors; no command or prepared
frame retains references to them after `record()` returns. Capacity grows only
when a later scene exceeds the prior high-water mark. The existing point-shadow
model-state vectors remain independently retained per light because their
contents feed each light's exact cache decision.

`OpaqueDrawSorter` now writes batches into caller-owned storage. Translucent
models use `std::sort` with an explicit source ordinal as the depth tie-breaker,
preserving the former stable order without `stable_sort` temporary storage.
`--disable-recorder-scratch-reuse` constructs fresh per-pass scratch for a
matched diagnostic control. Debug UI and evidence report capacity growths and
retained model-recording bytes.

At 2880x1800, the steady control frame grew four vector capacities totaling
9,600 bytes; retained mode reported zero capacity growths with the same 9,600-
byte high-water mark. The 120-sample CPU-frame average moved from 3.126 ms to
2.955 ms and p95 from 3.738 ms to 3.328 ms. GPU average remained effectively
flat at 3.028 versus 3.037 ms. Scene and AO captures are byte-identical.
Evidence is under `build/a4-transient` and `build/a4-retained`.

The sorter regression test proves repeated calls preserve caller-owned batch
storage and replace prior output correctly. Both validation-required 240-frame
runs, the complete Debug build and all 69 CTest suites pass. Treat the timing
delta as run-specific; the deterministic acceptance result is zero steady-
state capacity growth with unchanged commands and pixels.

#### 9.9 Post-A4 profiling and directional-shadow batching — complete

`VulkanRenderer` and `VulkanSceneRecorder` now retain fixed-size timing histories
for asset scheduling, frame-fence waits, asset maintenance, acquisition,
command recording, submit/present, asset-publication events, recorder setup,
game, shadows, scene color/depth, SSAO, preview and output/UI. Evidence also
reports publication counts and texture upload submissions, completions and
current in-flight work. Timing histories only accept samples on the frame path;
their average/p95/maximum summaries are sorted when `renderStats()` is consumed,
avoiding the diagnostic regression found in the first instrumented run.

The normal startup capture completed 32 texture uploads and the 64 KiB pressure
capture completed 16; both ended at zero in flight. Asset publication events
were startup-bound rather than a steady queue-ownership cost. Do not add a
transfer queue without a new sustained-streaming profile.

The corrected recorder split identified directional shadows as 1.949 ms of a
2.487 ms game-recording bucket: 204 tile casters each projected four vertices,
pushed a full `GpuDrawInstance`, and issued one draw. `shadow.vert.glsl` now
reads the existing frame-owned draw-instance SSBO for directional tile casters.
The recorder writes their projected vertices consecutively and emits one
instanced draw. Point-light cube faces deliberately retain push constants: up
to six faces across the full point-light capacity can multiply caster entries
beyond the shared frame buffer, and T5 already bounds/caches that path.

At 2880x1800 Debug with validation, command recording moved from 2.817 to 1.332
ms, shadow recording from 1.949 to 0.559 ms, and CPU frame time from 3.349 to
1.842 ms. Scene recording was 0.474 ms after the change. GPU average was 3.223
ms in the final run, so secondary CPU recording is not the next bottleneck.
The scene and filtered-AO PNG hashes match the pre-batch capture exactly.
Evidence is under `build/post-a4-recording-split`,
`build/post-a4-upload-pressure` and `build/batched-shadow-final`.

The complete Debug build, shader compilation, validation-required 240-frame
capture and all 69 CTest suites pass. Vulkan loader diagnostics about an absent
Epic overlay manifest and the existing unused shader output warnings are host
environment/pre-existing warnings, not validation failures from this packet.

#### 9.10 Point-shadow stress and Release GPU phase profile — complete

`--evidence-point-light-stress` installs eight deterministic point lights
around the evidence board. Their ranges cover the scene and no emitter is
excluded, so the animated character keeps the caster state uncacheable. This
forces all 48 cube faces through the retained push-constant path every frame
instead of allowing T5's unchanged-face cache to turn the stress run into a
reuse test. The ordinary `--evidence-point-light` mode remains unchanged for
the original matched T5 control.

The Debug stress capture reported 7,560/7,560 tile-face candidates in range,
120/120 models in range, 48 cube faces rendered and zero reused on the final
frame. It completed 120 frames with validation enabled, did not exhaust the
shared draw-instance buffer, and produced a visually coherent multi-light
scene. Its large 76.364 ms Debug command-recording average is intentional: it
is a worst-case safety workload with thousands of per-face push/draw pairs,
not a representative shipping frame.

At the 9.10 checkpoint, `VulkanGpuProfiler` owned ten queries per frame slot:
the original frame pair plus begin/end pairs for shadows, main scene
color/depth, SSAO and output/UI. Section 9.11 expands that same fence-read pool
with subpass pairs. All queries reset while recording, are read only after the
existing frame fence, and feed fixed histories. The stress Release phase sum (1.757 +
2.433 + 1.594 + 0.164 ms) matches its 5.951 ms whole-frame average within
rounding, validating the query boundaries.

The representative 2880x1800 Release capture measured 3.715 ms GPU: shadows
0.057 ms, main scene 1.870 ms, SSAO 1.601 ms and output/UI 0.182 ms. AO-off
measured 1.986 ms total with 1.777 ms scene and zero SSAO. At 50% render scale,
the GPU frame fell to 0.943 ms, with 0.472 ms scene and 0.283 ms SSAO. Both
large buckets therefore scale predominantly with pixel workload; shadows,
secondary command recording and transfer ownership are not the next target.

Evidence is under `build/point-shadow-stress-debug`,
`build/gpu-phases-release-normal`, `build/gpu-phases-release-point-stress`,
`build/gpu-phases-release-ao-off` and `build/gpu-phases-release-scale-50`.
The complete Debug build, Release executable, validation stress run, focused
CLI/profiler coverage and all 69 CTest suites pass.

#### 9.11 GPU subpasses and direct depth-resolve sampling — complete

The fence-read query pool now owns paired timestamps for scene raster/resolve,
depth publication, translucency, the SSAO scene-color snapshot, AO estimation
and the full-resolution composite. Conditional passes still write both query
ends, including AO-off frames, so a skipped pass cannot leave the entire
frame's non-blocking query result unavailable.

The native 2880x1800 Release baseline measured 3.616 ms GPU. Scene split into
1.667 ms raster/resolve, 0.135 ms depth copy and no translucency; SSAO split
into a 0.260 ms scene-color snapshot, 0.368 ms half-resolution estimator and
0.923 ms full-resolution composite. At 50% scale those same pieces measured
0.459, 0.014, 0.027, 0.071 and 0.184 ms. AO-off wrote all query pairs and
reported zero for every inactive depth/SSAO subpass.

The redundant depth path is gone. The main opaque pass already produces the
single-sample depth image that SSAO and water need, either as the MSAA depth
resolve or as the one-sample attachment itself. That image now transitions to
`DEPTH_READ_ONLY_OPTIMAL` for shader consumers and back to attachment state
before another main/preview render. Translucent rendering no longer declares a
second depth resolve when depth writes are disabled. The separate sampled D32
image, its copy command and transfer usage were removed, saving one native
full-resolution depth payload (about 19.8 MiB at 2880x1800).

The optimized native run measured 3.441 ms GPU with a 0.001 ms depth-publish
boundary; 50% measured 0.921 ms with the same 0.001 ms boundary. The native
and 50% Release scene PNGs are byte-identical to their pre-change captures,
as is the matched validation Debug scene. AO-on and AO-off validation runs are
clean. Evidence is under `build/gpu-subpasses-release-baseline`,
`build/gpu-subpasses-release-ao-off`, `build/gpu-subpasses-release-scale-50`,
`build/gpu-subpasses-depth-direct-debug`,
`build/gpu-subpasses-depth-direct-ao-off-debug`,
`build/gpu-subpasses-depth-direct-release`,
`build/gpu-subpasses-depth-direct-release-ao-off` and
`build/gpu-subpasses-depth-direct-release-scale-50`.

The full Debug build and all 69 CTest suites pass. This made the normal path's
HDR color snapshot the next exact-output target addressed in 9.12 below.

#### 9.12 Direct opaque-color resolve for SSAO — complete

When SSAO is active, both SSAO pipelines are valid and the prepared main scene
has no water or authored BLEND material, the opaque pass now resolves directly
into `sceneColorImage_`. That image transitions from color attachment to shader
read, and the existing unblended SSAO composite writes `resolvedColorImage_`.
This is a ping-pong between the two existing HDR images: no new image,
descriptor mutation, shader branch or filter change was introduced.

The optimization is deliberately conditional. AO-off continues to render
straight into the resolved HDR output. Water/authored BLEND retains the old
resolved-output plus scene-color-copy path because its fragment shader samples
the opaque scene while the translucent pass resolves. Preview feathering and
level transitions also retain their copies. `sceneColorImage_` now has color-
attachment usage, and its tracked layout explicitly covers attachment,
shader-read and transfer-destination ownership.

Against the 9.11 direct-depth Release result, native 2880x1800 GPU time moved
from 3.441 to 3.255 ms. The SSAO bucket moved from 1.430 to 1.246 ms and its
scene snapshot from 0.198 to 0.000 ms; estimator/composite work remains 0.327/
0.919 ms. At 50% scale, the snapshot moved from 0.028 to 0.000 ms and whole-
frame GPU time from 0.921 to 0.789 ms. These whole-frame deltas include normal
run-to-run variance, while the isolated snapshot result is the direct measure.

Native and 50% Release scene captures, native AO-debug captures and AO-off
captures are byte-identical to their 9.11 counterparts. AO-on and AO-off Debug
validation runs are clean. Evidence is under `build/gpu-color-direct-debug`,
`build/gpu-color-direct-ao-off-debug`, `build/gpu-color-direct-release`,
`build/gpu-color-direct-release-scale-50` and
`build/gpu-color-direct-release-ao-off`.

The 9.12 evidence logs report 8x MSAA because that older runner inherited the
persisted user setting. Treat those timings as a valid matched 8x optimization
comparison, not a default-settings benchmark; 9.13 below replaces that
accidental input.

#### 9.13 Deterministic evidence MSAA and corrected baseline — complete

`--evidence-msaa <1|2|4|8>` now selects anti-aliasing before renderer creation.
It is evidence-only, rejects missing, malformed and unsupported values, and is
rejected without `--evidence-output`. When omitted from an evidence run it uses
the 4x product default rather than the saved profile, while ordinary play still
uses the player's setting. Settings initialization does not reapply or persist
the override.

Evidence report titles, scene/AO filenames and report references include the
renderer-reported active sample count (for example,
`scene-scale-100-msaa-4.png`). This makes archives self-identifying even when
several sample-count runs share a parent directory. Parser coverage pins all
four supported choices, the 4x default and the invalid-input cases.

The first 240-frame pass exposed GPU clock/startup outliers in different phase
buckets, so the decision matrix uses 600-frame runs and the final 120-sample
steady windows. At native 1280x720 on the RTX 4060 Laptop GPU:

| MSAA | GPU frame avg | Scene raster/resolve avg | SSAO composite avg |
| --- | ---: | ---: | ---: |
| 1x | 0.828 ms | 0.182 ms | 0.177 ms |
| 4x product default | 0.975 ms | 0.239 ms | 0.202 ms |
| 8x, repeated | 1.148–1.216 ms | 0.347–0.362 ms | 0.635–0.671 ms |

The 4x result corrects the old inherited-8x baseline. Scene raster and SSAO
composite are close at the product default, so the data does not support a
large rewrite of either path. The repeated 8x cost is useful as a high-quality
stress result, not as the product target. Section 9.14 uses this explicit 4x
baseline to evaluate the retained translucent copy fallback.

The complete Debug build and all 69 CTest suites pass. A 120-frame explicit-4x
Debug evidence run completed with the Khronos validation layer active and no
Vulkan usage errors. Release evidence is under
`build/gpu-msaa-release-steady-1x`, `build/gpu-msaa-release-steady-4x`,
`build/gpu-msaa-release-steady-8x` and `build/gpu-msaa-release-steady2-8x`.

#### 9.14 Deterministic translucent-water fallback evidence — complete

`--evidence-water` now adds a non-pickable exterior water strip beside the
frozen evidence board. It uses the normal water elevation, shoreline shader,
depth sampling, opaque-color snapshot and translucent resolve without changing
the level or save. Evidence water tuning is reset to the authored defaults so a
saved Debug preference cannot change the pixels. Keeping the fixture outside
the board is deliberate: a first counter-valid version overlaid solid ground
and was completely depth-occluded, which proved that recording a translucent
draw alone is not visual evidence.

The flag is rejected without `--evidence-output`, remains off by default and
adds `-water` to report and image names. Captures fail if the prepared scene has
no translucency or, with AO enabled, if the SSAO color-copy fallback did not
execute. Reports state fixture, translucency and fallback status explicitly.

At native 1280x720, explicit 4x MSAA and a warmed final 120-sample Release
window, the opaque control used 0.706 ms GPU. Repeated water runs used 1.048–
1.050 ms, including 0.466 ms of translucency and only 0.024–0.025 ms for the
retained scene snapshot. Both water runs produced scene hash
`9354DB0F4D7FF8D86C23E9BAB42235AF965364228579EA4162D8F4B9D72BCF0F`
and AO hash
`61F4519D30268B7335887E1F6DF023DBC972E2F09539210CA791D7ABC81AE871`;
the control retained the 9.13 scene hash. The copy is therefore necessary but
not a worthwhile optimization target.

#### 9.15 Skip inoperative projected water caustics — complete

The water shader used to evaluate its costly ripple field a second time for
projected underwater caustics even when resolved depth contained only the clear
background. That result was subsequently multiplied by zero. The second
evaluation is now conditional on opaque geometry being present; surface
ripples, refraction, shoreline foam and every geometry-backed caustic remain
unchanged.

Two stable warmed Release repetitions measure 0.388–0.390 ms translucency and
0.970–0.977 ms for the full GPU frame, down from 0.466 and 1.050 ms
respectively. The pre-change, Debug validation and all three post-change
Release scene/AO pairs have the exact hashes above. The optimized 120-frame
Debug run is validation-clean, the water shader compiles to Vulkan 1.3 SPIR-V,
and all 69 Debug suites pass.

Evidence is under `build/gpu-water-debug-4x`,
`build/gpu-water-release-control-4x`, `build/gpu-water-release-4x-a`,
`build/gpu-water-release-4x-b`, `build/gpu-water-caustic-skip-debug-4x` and
`build/gpu-water-caustic-skip-release-4x-{a,b,c}`.

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

- `src/engine/render/IsoScenePreparer.*`: completed retained bounds,
  main-scene culling and T8 auxiliary-list split; preserve deterministic list
  order, fail-open model behavior and disjoint worker output ownership.
- `src/engine/render/RenderTypes.hpp`: source render data and frame-level
  ownership boundaries that the persistent representation must not blur.
- `src/engine/Geometry.*`: completed bounds/frustum primitives.
- `src/engine/render/VulkanRenderer.*`: completed T8 main/preview and
  main/auxiliary preparation orchestration; Vulkan recording begins only after
  the worker join.
- `src/engine/render/VulkanMemoryAllocator.*`: completed VMA ownership and the
  only renderer memory-policy seam; extend this instead of reintroducing raw
  Vulkan allocation calls.
- One-shot submits go through `vulkanResources::beginOneShotCommands` and
  `submitOneShotCommands`. Every upload or readback outside the frame loop -
  geometry, texture, compressed texture, frame capture, tile thumbnails, UI
  images - allocates a primary command buffer, begins it `ONE_TIME_SUBMIT`,
  records, ends, fences and submits. Only the recording differs, and only the
  recording belongs at the call site. Both helpers return the handle they
  built and release it themselves if a later step throws, so a caller never
  holds a half-constructed buffer or fence; the catch blocks that free them on
  success paths still guard on null and must keep doing so.
  The `label` argument is not decoration: the six sites spelled their
  diagnostics `"<vkFunction> <label> failed"` and `vkCheck(result, call,
  label)` reproduces that text exactly, composing only when it throws. The
  `vulkan_diagnostics` suite pins all thirty messages, so renaming a label
  changes a test, not a crash report nobody reads until it matters.
  `VulkanUiResources` shares the prologue but not the epilogue: it submits
  against no fence and waits on the whole queue. That is wider than it needs
  and is left as it was, with a comment, because narrowing it is a change to
  queue synchronisation and wants a run.
- `src/engine/RenderFrameParts.{hpp,cpp}`: the frame pieces both builders
  build - water, ladders, decorations, the screen selector, the camera extent,
  animation lookups and the small shaping helpers. The review called
  `RenderFrameBuilder.cpp` "two independent builders in one file"; it was not.
  The editor called 16 gameplay helpers directly and 6 more through them, 718
  of 1,698 lines. Those are here now, grouped by subsystem, in
  `sokoban::renderFrameParts` - a namespace of its own because names like
  `shade` and `animationFor` had internal linkage before the split and would be
  collision-prone in `sokoban`. Both builders open it with `using namespace
  renderFrameParts;`.
  Five entries are templates over a tile or shoreline lookup. That is the only
  thing the two callers disagree about - gameplay asks the level, the editor
  asks the document under edit - so it is the only thing parameterised; resist
  adding a second template parameter for anything else.
  The gameplay builder stays in `RenderFrameBuilder.cpp`; the editor builder,
  both `buildEditor` overloads and `tileVisual` are in
  `RenderFrameBuilderEditor.cpp`. Anything a new builder needs from both goes
  in `RenderFrameParts`, not copied into one of them.
- `src/engine/Geometry.hpp`'s `Aabb` is the only axis-aligned box type. There
  were three: `Aabb`, `GltfMesh.cpp`'s `SourceBounds` (character-for-character
  the same struct, with `includeBounds` as `expand`) and
  `VulkanModelResources::ModelBounds` (a zeroed box plus a stored `bool
  valid`). `Aabb`'s default is *inverted*, not zero, which is what lets a fold
  start from it with no first-point special case - so `boundsOf` lost its seed
  and its empty-input branch as well as its six min/max lines. The stored flag
  is gone: validity is derived, and every reader must call `valid()` before
  touching the extent, because an invalid box holds `+FLT_MAX`/`lowest`, not
  points in the scene. Construct from a known-ordered pair with `Aabb { min,
  max }` and not `aabbFromMinMax`, which sorts and would quietly repair an
  inverted box into a valid one. `corners(Aabb)` gives the eight corners, x
  varying fastest; four call sites wrote that list out, and two of them index
  into it, so the order is fixed and tested rather than incidental.
- Frame budget overflow is no longer fatal. More draws than
  `maxDrawInstancesPerFrame`, or more skinned poses than
  `maxSkinnedInstancesPerFrame`, used to throw a plain `runtime_error` from
  inside recording; nothing caught it, so an over-full level reached main's
  fatal handler and closed the game. Overflowing draws are now pointed at
  `drawInstanceDiscardSlot` - the last entry of each frame's range, zeroed at
  creation and never written, so its four identical corners give both triangles
  zero area and it rasterizes nothing. Skinned overflow drops the instance,
  which leaves it in the same state the recorder already skips. An invalid
  frame index or unmapped buffer still throws: those are programming errors.
  Both drops are counted cumulatively and reported in the smoke summary. Do not
  hand `drawInstanceDiscardSlot` out to a real draw, and do not write to it.
- `src/engine/render/GpuMappedBuffer.hpp`: one host-visible buffer type and one
  buffer view, replacing four identical owning structs (the skinning palette,
  the draw-instance array, the material table, and `OwnedBuffer` in
  `VulkanSceneDescriptors`) and three identical views. Creation does not catch:
  every caller already wraps creation and its first fill in one try/catch that
  destroys and rethrows, and catching inside would destroy twice. `destroy()`
  is idempotent and accepts a null allocator, because teardown runs on
  half-built objects.
- Both model paths map texture indices the same way. `create()` and
  `syncManifestModels()` (the editor's append path) route dependencies through
  `TextureDescriptorSpace::descriptorIndexFor` and bindings through
  `remapBindingTextures`. `syncManifestTextures()` must keep running before
  `syncManifestModels()` - `ApplicationTools` does - so the manifest range
  covers every index the append path sees. Editor-appended models bind base
  colour only, and that is the manifest format, not an omission: a manifest
  primitive material has a texture and a scroll flag, while normal,
  metallic-roughness, emissive and occlusion handles come from glTF discovery
  at startup.
- `src/engine/render/TextureDescriptorSpace.hpp`: how the texture descriptor
  heap is split - manifest textures on stable low indices, glTF textures
  discovered at load time taking the top downward, and the rule that the two
  must never meet. That rule was written in three places in three wordings and
  the index formula in two; both live here now, with 707 checks over them. The
  header owns the arithmetic but not the throw: `RuntimeTextureCatalog` raises
  `std::out_of_range` and `VulkanModelResources` raises `std::runtime_error`,
  with different messages, and those are part of their contracts. Note that
  `RuntimeTextureCatalog::descriptorIndex` answers manifest indices *before*
  testing for overlap, deliberately - a heap whose ranges collide can still
  answer for its low half.
- `src/engine/render/VulkanModelResources.*`: the three publish functions share
  one entry gate (`publishGate`) and one failure tail (`recordPublishFailure`).
  The tail is the part that must not drift - remember the exception, mark the
  slot failed, rethrow when a caller is waiting, otherwise log - and it had five
  copies. Their middles stay separate on purpose: what a model does with a
  CpuReady slot is genuinely not what a texture does. Before merging the gate,
  the three ladders were compared over every load state against both values of
  `wait` (forty reachable cases, checking the return value *and* whether
  `throwIfFailed` was reached) and reproduce exactly; animations never reach
  `Uploading`, which is why that case is excluded rather than assumed.
- `src/engine/render/VulkanModelResources.*` and `TextureMipResidency.*`:
  completed A3 residency; preserve fence-owned retirement, finest-fitting mip
  tails, exact-byte accounting and stable material ranges. Two policies have
  been lifted out of this class and are now Vulkan-free and directly tested:
  `ResidencyBudget.hpp` (byte accounting, victim choice, eviction ladder) and
  `MaterialRangeAllocator.hpp` (the material buffer's bump pointer, first-fit
  reuse and coalescing free list). Change either policy there, with its test,
  rather than reintroducing the logic here.
- `src/engine/render/VulkanSceneRecorder.*`: the packing shared by its quad
  draws now lives in `SceneDrawLanes.hpp` - sun/ambient radiance, the face
  shadow lane, the grid-validity predicate and the four-corner vertex packing -
  and is covered by `scene_draw_lanes`. Build a draw's lanes from those rather
  than restating the arithmetic; there is deliberately no shared base instance,
  because water and mirror-energy claim the same slots for other data. It is
  also the consumer of the prepared visible
  lists and owner of retained model-recording and point-shadow model-state
  scratch; it also owns the low-overhead recorder-phase histories and batches
  directional tile shadows through the draw-instance buffer. Keep Vulkan
  recording downstream of Vulkan-free culling, keep scratch synchronous to
  `record()`, and do not convert capacity-multiplying point faces without a
  separate bounded storage contract.
- `src/engine/render/FrameTimeTelemetry.hpp` and `RenderTypes.hpp`: fixed-size
  evidence histories and public phase summaries. Record samples in-frame but
  calculate sorted summaries only for an explicit stats consumer.
- `src/engine/render/VulkanGpuProfiler.*`: fence-read whole-frame, top-level
  and scene/SSAO subpass GPU queries. Preserve the per-frame-slot reset/read
  ownership and write every query pair on conditional paths; never add waits.
- `src/engine/render/PointShadowFaceCache.*`: exact unchanged-depth key. Keep
  skinned casters uncacheable unless a real GPU-pose revision joins the key.
- `tests/IsoScenePreparerTests.cpp`: stable identity/bounds behavior is covered;
  add conservative visibility cases before changing recorder behavior.
- `src/engine/render/VulkanSsaoPass.*`, `shaders/ssao*.glsl` and
  `tests/SsaoMathTests.cpp`: completed V7 baseline; revisit only if visual or
  timing evidence exposes a concrete regression.
- `src/engine/CommandLineOptions.hpp`, `Application.cpp` and
  `ApplicationEvidence.cpp`: evidence-only inputs and artifact identity.
  Preserve the 4x deterministic default and keep overrides out of persistence.

## Build and test commands

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure --no-tests=error
```

### Compiler check without the SDK

Every file under `src/` parses with GCC 11 on Linux using only vendored
headers - no Vulkan SDK and no installed SDL. SDL vendors the Khronos headers,
which is what makes this possible:

```bash
INC="-I src -isystem third_party/SDL/src/video/khronos -isystem third_party/SDL/include \
     -isystem third_party/vma -isystem third_party/imgui -isystem third_party/imgui/backends \
     -isystem third_party/imgui/misc/cpp -isystem third_party/cgltf -isystem third_party/stb \
     -isystem third_party/nlohmann -isystem third_party -isystem third_party/miniaudio \
     -isystem third_party/bc7enc16"
DEF="-DSOKOBAN_ENABLE_DEBUG_UI=1 -DSOKOBAN_ENABLE_VALIDATION=1 -DSOKOBAN_GAME_VERSION='\"0.0.0\"' \
     -DSOKOBAN_SOURCE_LEVEL_DIR='\"/l\"' -DSOKOBAN_SOURCE_ASSET_DIR='\"/a\"'"
find src -name '*.cpp' | xargs -P "$(nproc)" -I{} g++ -std=c++20 -fsyntax-only -Wall $DEF $INC {}
```

All 128 translation units pass, in both `SOKOBAN_ENABLE_DEBUG_UI` settings,
with two `-Wrange-loop-construct` warnings in `PlayerProfileMigrations.cpp`
that are GCC false positives - both loop variables are 16-byte trivially
copyable pairs, where the copy is cheaper than the reference GCC asks for.

Ten suites also compile and run with no library link, which makes them usable
without a Windows build: `command_line`, `math`, `geometry`,
`frame_descriptor_sync`, `frame_pacing`, `material_range_allocator`,
`residency_budget`, `scene_draw_lanes`, `texture_descriptor_space`,
`draw_mode` and - with a stub loader supplying the `vk*` entry points -
`vulkan_diagnostics`. `draw_mode` needs `-DSOKOBAN_TEST_SHADER_INCLUDE_DIR='"<repo>/shaders/include"'`;
`gpu_abi` needs built `.spv` modules and so needs `glslc`. Everything else in
`tests/` links `sokoban_core` and needs a real build. This is a fast pre-check,
not a substitute for MSVC: it will not catch a `/W4` diagnostic, a link error
or anything that has to run on a GPU.

Exercise normal and forced mip residency with validation enabled:

```powershell
.\build\Debug\sokoban.exe --smoke-frames 120 --require-validation
.\build\Debug\sokoban.exe --smoke-frames 240 --require-validation --texture-residency-kib 64 --evidence-output build\a3-mip-evidence
```

Reproduce the T8 matched preparation profile:

```powershell
.\build\Debug\sokoban.exe --smoke-frames 240 --require-validation --serial-scene-preparation --evidence-output build\t8-serial
.\build\Debug\sokoban.exe --smoke-frames 240 --require-validation --evidence-output build\t8-parallel
```

Reproduce the T5 point-shadow control and optimized runs:

```powershell
.\build\Debug\sokoban.exe --smoke-frames 240 --require-validation --evidence-point-light --disable-point-shadow-optimizations --evidence-output build\t5-legacy
.\build\Debug\sokoban.exe --smoke-frames 240 --require-validation --evidence-point-light --evidence-output build\t5-optimized
```

Reproduce the A4 transient and retained recorder-scratch runs:

```powershell
.\build\Debug\sokoban.exe --smoke-frames 240 --require-validation --disable-recorder-scratch-reuse --evidence-output build\a4-transient
.\build\Debug\sokoban.exe --smoke-frames 240 --require-validation --evidence-output build\a4-retained
```

Reproduce the current recording/upload profile and directional-shadow result:

```powershell
.\build\Debug\sokoban.exe --smoke-frames 240 --require-validation --evidence-output build\batched-shadow-final
.\build\Debug\sokoban.exe --smoke-frames 240 --require-validation --texture-residency-kib 64 --evidence-output build\post-a4-upload-pressure
```

Reproduce the point-shadow stress and Release GPU phase matrix:

```powershell
.\build\Debug\sokoban.exe --smoke-frames 120 --require-validation --evidence-output build\point-shadow-stress-debug --evidence-point-light-stress
cmake --build build --config Release --target sokoban
.\build\Release\sokoban.exe --smoke-frames 240 --evidence-output build\gpu-phases-release-normal
.\build\Release\sokoban.exe --smoke-frames 120 --evidence-output build\gpu-phases-release-point-stress --evidence-point-light-stress
.\build\Release\sokoban.exe --smoke-frames 240 --evidence-output build\gpu-phases-release-ao-off --evidence-disable-ao
.\build\Release\sokoban.exe --smoke-frames 240 --evidence-output build\gpu-phases-release-scale-50 --evidence-render-scale 50
```

Reproduce the subpass baseline and direct-depth result:

```powershell
.\build\Debug\sokoban.exe --smoke-frames 120 --require-validation --evidence-output build\gpu-subpasses-depth-direct-debug
.\build\Debug\sokoban.exe --smoke-frames 60 --require-validation --evidence-disable-ao --evidence-output build\gpu-subpasses-depth-direct-ao-off-debug
.\build\Release\sokoban.exe --smoke-frames 240 --evidence-output build\gpu-subpasses-depth-direct-release
.\build\Release\sokoban.exe --smoke-frames 240 --evidence-disable-ao --evidence-output build\gpu-subpasses-depth-direct-release-ao-off
.\build\Release\sokoban.exe --smoke-frames 240 --evidence-render-scale 50 --evidence-output build\gpu-subpasses-depth-direct-release-scale-50
```

Reproduce the direct-color result:

```powershell
.\build\Debug\sokoban.exe --smoke-frames 120 --require-validation --evidence-output build\gpu-color-direct-debug
.\build\Debug\sokoban.exe --smoke-frames 60 --require-validation --evidence-disable-ao --evidence-output build\gpu-color-direct-ao-off-debug
.\build\Release\sokoban.exe --smoke-frames 240 --evidence-output build\gpu-color-direct-release
.\build\Release\sokoban.exe --smoke-frames 240 --evidence-render-scale 50 --evidence-output build\gpu-color-direct-release-scale-50
.\build\Release\sokoban.exe --smoke-frames 240 --evidence-disable-ao --evidence-output build\gpu-color-direct-release-ao-off
```

Reproduce the explicit-MSAA product baseline and stress matrix:

```powershell
.\build\Debug\sokoban.exe --smoke-frames 120 --require-validation --evidence-msaa 4 --evidence-output build\gpu-msaa-debug-4x
.\build\Release\sokoban.exe --smoke-frames 600 --evidence-msaa 1 --evidence-output build\gpu-msaa-release-steady-1x
.\build\Release\sokoban.exe --smoke-frames 600 --evidence-msaa 4 --evidence-output build\gpu-msaa-release-steady-4x
.\build\Release\sokoban.exe --smoke-frames 600 --evidence-msaa 8 --evidence-output build\gpu-msaa-release-steady-8x
```

Reproduce the translucent fallback and optimized water result:

```powershell
.\build\Debug\sokoban.exe --smoke-frames 120 --require-validation --evidence-msaa 4 --evidence-water --evidence-output build\gpu-water-caustic-skip-debug-4x
.\build\Release\sokoban.exe --smoke-frames 600 --evidence-msaa 4 --evidence-output build\gpu-water-release-control-4x
.\build\Release\sokoban.exe --smoke-frames 600 --evidence-msaa 4 --evidence-water --evidence-output build\gpu-water-caustic-skip-release-4x
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
