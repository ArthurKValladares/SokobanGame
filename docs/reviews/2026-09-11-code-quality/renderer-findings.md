# Renderer review evidence and recommendations

Reviewed 2026-09-11. These notes complete the existing renderer probes and distinguish reproduced failures from static findings. Production code was not changed. Paths and line numbers refer to the reviewed working tree.

## R1 — Account for every descriptor limit when choosing the texture heap

**Priority:** P2 correctness/portability (CQ-06 in the main review). **Confidence:** High; the sampled-image arithmetic was reproduced in the actual selection helper, and sampler-limit omissions are directly visible in the feature model and callers. No low-limit physical GPU failure was reproduced.

**Evidence:** `src/engine/render/VulkanDeviceSelection.cpp:90-100` subtracts the eight scene descriptors from the per-stage sampled-image allowance, but uses the entire `maxDescriptorSetSampledImages` allowance for the texture heap. `src/engine/render/VulkanDeviceContext.cpp:568-572` then checks per-stage usage as heap plus scene bindings, while passing only the heap to the descriptor-set limit check. The same call pattern is used during initialization at lines 378 onward. `VulkanDeviceSelection.hpp:14-31` and `VulkanDeviceContext.cpp:641-647` do not represent/query `maxPerStageDescriptorSamplers` or `maxDescriptorSetSamplers`. Both the scene samplers and heap are combined image samplers (`VulkanSceneDescriptors.cpp:52-66,200`).

The probe sets the sampled-image limits to 160 per stage and 128 across sets, then requests 70 textures plus reserves of 8 and 16 with eight scene bindings. The existing helper returns:

```text
heap=128 supported=1 aggregateSampledImages=136 maxDescriptorSetSampledImages=128
```

Vulkan's `maxDescriptorSetSampledImages` limit applies across all layouts in the pipeline layout; it is not an independent allowance for each set. Combined image samplers also consume both sampler and sampled-image limits. Relevant valid-usage rules include 03033 and 03016. See the official [VkPipelineLayoutCreateInfo reference](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineLayoutCreateInfo.html) and [Vulkan limits specification](https://docs.vulkan.org/spec/latest/chapters/limits.html).

**Effect:** A device with tighter limits may be accepted despite an invalid eventual pipeline layout. On a sampler-limited implementation, the heap can exceed a different limit even when sampled-image arithmetic is corrected. The current machine's successful smoke run does not exercise those hardware profiles.

**Suggested change:** Carry both sampler limits in `VulkanDeviceFeatureSupport`; calculate remaining capacity for every applicable limit after the other scene bindings, and take their minimum with the configured ceiling. Pass aggregate descriptor counts to the feature-tier checks. Keep the existing scene binding table/static assertion as the source for scene usage. Avoid independently hard-coding another eight. No descriptor-system redesign is required.

**Validation:** Extend `VulkanDeviceSelectionTests.cpp` with the 160/128/8 profile (heap at most 120), individually limiting per-stage and aggregate sampler profiles, exact-fit boundaries, and limits smaller than scene usage. Verify the caller's complete accepted layout fits, not only the heap helper's return value. Run the normal validation smoke afterward.

**Artifacts:** [probe source](renderer-probes/RendererReviewProbe.cpp), [probe harness](renderer-probes/run.ps1), and [retained finite-probe output](evidence/renderer/finite-results.txt), line 1.

## R2 — Make blocking asset waits terminate when residency admission cannot progress

**Priority:** P2 correctness/efficiency (CQ-07 in the main review). **Confidence:** High; reproduced persistent stalled state and a timed-out blocking call under deliberately constrained budgets. Normal-default gameplay impact was not observed.

**Evidence:** `src/engine/render/VulkanModelResources.cpp:453-477` loops while scheduler entries remain. Its publication loops revisit only `Loading` slots. `publishModel` can move an asset to `CpuReady`, fail residency admission, and return false while retaining prepared data (`:869-878`; also the skinned path around `:899-907`). The scheduler then refuses another preparation while retained bytes exceed its budget (`src/engine/render/AssetLoadScheduler.cpp:74-87`). With no active job and no `Loading` slot left to publish, this loop has no operation that can change the state. The terminal readiness check at `VulkanModelResources.cpp:509` is unreachable in that state. This is a busy loop, not a blocking wait on a live asynchronous job.

The existing probe requests two non-skinned manifest models with one concurrent preparation job, one publication per frame, a one-byte prepared budget, and a one-byte model-residency budget. A two-second bounded publication probe reports:

```text
queued=1 active=0 cpuReadyModels=1 prepared=67948 preparedLimit=1 oversizedResidencyBlocks=1010 failed=0
```

The direct `waitForAssets` probe prints `WAIT_ENTER models=2`, never prints `WAIT_RETURNED`, and is terminated by its harness after 15 seconds. The one-byte budget is a deliberately extreme accepted configuration that deterministically exposes the no-progress path; this does not establish that normal defaults hang. The same structural condition arises whenever retained prepared data prevents the next CPU job and its owner cannot become resident.

**Effect:** Offline callers such as asset-dependent editor/tools operations can hang while consuming a CPU core instead of reporting that the requested residency cannot be satisfied. Repeated nonblocking retries also retain prepared memory and inflate deferral counters without a terminal outcome.

**Suggested change:** Give the blocking path an explicit progress/terminal-state contract covering `Queued`, `Loading`, `CpuReady`, and GPU-upload states. Retry publishable `CpuReady` assets when retirement or eviction can help. If the remaining required set cannot be admitted and there is no job, upload, or retirement capable of changing that fact, fail with the asset and relevant budget/admission reason. Keep an explicit policy for oversized required assets (reject, or documented temporary allowance); do not silently lift all budgets or add a sleep as the sole fix. Preserve prepared-state ownership on ordinary temporary deferrals.

**Validation:** Add a bounded integration case equivalent to this probe that must complete with a precise error or documented admission outcome. Cover an ordinary successful wait, a temporary admission refusal resolved by retirement, and a required set exceeding residency capacity. A timeout guards the regression test; it should not be the normal production error policy.

**Artifacts:** [probe source](renderer-probes/RendererReviewProbe.cpp), [probe harness](renderer-probes/run.ps1), [finite-probe output](evidence/renderer/finite-results.txt), line 135, [blocking-wait stdout](evidence/renderer/wait-stdout.txt), [blocking-wait stderr](evidence/renderer/wait-stderr.txt), and [timeout result](evidence/renderer/wait-result.txt).

## R3 — Preserve the old painted texture and dirty revision until replacement succeeds

**Priority:** P2 correctness/maintainability (CQ-08 in the main review). **Confidence:** High for the exception path from static inspection; failure was not reproduced on the available GPU.

**Evidence:** `src/engine/render/VulkanModelResources.cpp:2137-2142` destroys the current texture before creating its resized/uncompressed replacement. `VulkanTextureUploader.cpp:95-108` cleans up the attempted replacement and rethrows on creation/upload failure. The slot's publication state and existing descriptor sets are not transactionally restored. Descriptor refresh occurs only after a successful resource update returns to `VulkanRenderer::updateTexture` (`:848` onward). Meanwhile `src/engine/ApplicationTools.cpp:230-236` advances `uploadedSplatRevision` before uploading and catches the exception so the application continues.

**Effect:** A recoverable failed replacement can leave a ready slot with destroyed resources and stale descriptors. The unchanged paint revision is then considered uploaded, suppressing a retry. An update that returns false is likewise ignored by this caller. This matters specifically because the application deliberately catches upload exceptions and keeps running.

**Suggested change:** Create the replacement in temporary owned resources, and commit the image, sampler, dimensions, memory accounting, and descriptor-dirty state together only after upload success. Keep the previous texture valid through a failed attempt and clean up temporary resources through existing ownership conventions. Advance `uploadedSplatRevision` only on successful publication; distinguish a deferred/unpublished texture from a completed upload and retain the dirty state or explicitly retry on asset publication. Add a small retry/backoff or error-state policy if permanent failure would otherwise log every frame.

**Validation:** Inject a replacement creation/upload failure without exhausting machine memory. Assert that the previous texture, sampler, resident byte accounting, descriptor references, and pending paint revision remain valid; follow with a successful retry. Also cover a not-yet-ready texture returning false.

**Negative result that must remain explicit:** The existing repaint probe's 4097-by-4097 image succeeded on this machine. The [retained repaint output](evidence/renderer/repaint-results.txt), line 135, says `REPAINT_UNEXPECTED_SUCCESS`. That run is not proof of an allocation failure or damaged texture. Do not cite it as a reproduced bug.

## R4 — Keep water derivatives outside fragment-varying control flow

**Priority:** P2 shader correctness/portability (CQ-09 in the main review). **Confidence:** High for the language-rule violation; static finding, no visual artifact reproduced.

**Evidence:** `shaders/water.frag.glsl:588-610` derives `geometryPresent` from a per-fragment scene-depth sample and branches on it. The conditional call to `waterRipplePatterns` (`:611-619`) calls `cellularRippleBands` (`:312-324`), which computes `fwidth(distanceToBoundary)` (`:268-271`). At opaque/background boundaries, neighboring fragments can take different sides of that branch. GLSL specifies that derivatives in non-uniform control flow are undefined; `fwidth` is formed from fragment derivatives. See [GLSL 4.60, section 8.14.1](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html#derivative-functions).

**Effect:** The antialiasing width of projected water caustics at geometry edges is not portable. A shader compile and a warning-free Vulkan validation run would not establish defined derivative behavior or image equivalence here.

**Suggested change:** Separate derivative/footprint calculation into a uniform region and pass the required data into a conditional derivative-free function, or develop an analytical footprint with explicit visual tests. Preserve the optimization that skips unnecessary projected-caustic work where possible. An unconditional projected-pattern calculation followed by the coverage mask can serve as a correctness comparison, but should not become the production fix without measuring its additional cellular-evaluation cost. Merely hoisting a derivative of branch-produced values after the branch is insufficient unless all participating fragments define those values consistently.

**Validation:** Compile affected shader variants and compare moving-camera water over geometry/background boundaries at different resolutions. Check at least two GPU vendors when available. Profile before restoring the conditional optimization; do not claim a measured performance improvement without measurements.

## Suggested renderer implementation order

1. Fix R1 with pure selection tests; this is a small, isolated portability correction.
2. Fix R2 with progress-state regression coverage; keep the change focused on the admission and publication transition.
3. Fix R3 transactionally, then exercise the injected failure and successful retry.
4. Fix R4 and visually verify water; use the resulting functions as stable boundaries for any separately justified shader readability work.

These notes intentionally do not add fresh refactoring hypotheses or turn shader output-not-consumed warnings into correctness failures.
