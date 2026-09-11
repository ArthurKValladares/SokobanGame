# Sokoban 3D code-quality review

Reviewed September 10–11, 2026, at commit `d95324f9426907d606f1af302a95c1e7fe8ff95c`.

This document records suggested changes, their evidence, and an implementation order. Production code was not changed. Reproduction programs and build/test logs accompany the review.

## Assessment

The codebase has useful boundaries already: pure gameplay and menu logic, explicit persistence results, an SDK-independent core, shared GPU layouts, specialized rendering passes, and substantial regression coverage. The previous review's completed work should be retained.

The remaining problems concentrate at transitions between otherwise tested components: renumbering documents with cached drafts, publishing source textures with prepared artifacts, finishing concurrent actions, translating UI choices, recovering saves, and admitting or replacing GPU resources. Passing the existing tests does not exercise all of these combinations.

The recommendations below are deliberately scoped to those failures and to demonstrably obsolete code. File length, the presence of a numeric literal, or an opportunity to introduce another abstraction is not sufficient justification for a change.

## Scope and confidence

The inventory covers 294 C++ source/header files under `src` (78,146 lines), 23 shader files/includes (2,932 lines), 83 test/support files (33,503 lines), and 12 code/script files under `tools`, `cmake`, `packaging`, and `.github` (2,528 lines). The 1,376-line root CMake file, presets, analyzer/format configuration, installer/resources, manifest and level formats, README, handoff, and prior review were also inspected. See [inventory.json](evidence/inventory.json) for the file census.

This was a repository-wide, risk-based review, with deeper tracing and probes at ownership, state, persistence, and rendering boundaries. It is not formal verification or a claim that every possible input and device was exercised. Vendored third-party internals, artwork quality, and licensing audits were outside scope; first-party integration and packaging were included.

Evidence labels used below:

- **Reproduced:** an isolated program exercised production libraries and recorded the failure.
- **Source-confirmed:** the concrete control/data path establishes the defect, but the failure was not induced in a full application run.
- **Cleanup:** current call sites or comments establish unnecessary maintenance work; no performance gain is assumed.

P1 means address before further use of the affected authoring operation. P2 means a concrete defect to fix in the normal implementation queue. P3 means small, justified maintenance work after correctness fixes.

## Validation performed

| Check | Result | Evidence |
| --- | --- | --- |
| Fresh MSVC Debug build, warnings as errors | Passed | [configure](evidence/configure.txt), [build](evidence/build-debug.txt) |
| Full Debug CTest registry | 80/80 passed | [Debug tests](evidence/ctest-debug.txt) |
| Fresh MSVC Release build, warnings as errors | Passed | [Release build](evidence/build-release.txt) |
| Full Release CTest registry | 80/80 passed | [Release tests](evidence/ctest-release.txt) |
| Application, 240 frames with `--require-validation` and isolated saves | Exit 0; completed frames and teardown | [result](evidence/app-smoke-result.txt), [log](evidence/app-smoke-stderr.txt) |
| Additional core, editor/content, UI and renderer probes | Confirmed missing cases described below | [core](evidence/core-probes.txt), [content/editor](evidence/content-editor-probes.txt), [UI](evidence/ui-probes.txt), [renderer notes](renderer-findings.md) |

The application smoke log contains `Shader-OutputNotConsumed` warnings. It is an error-free validation run, not a warning-free one. Shader compilation and local smoke success do not rule out behavior undefined by GLSL or limits exceeded on another device.

The isolated build directory is `out/code-quality-review`. No Linux compiler/sanitizer/fuzzer job, clean-account installer exercise, controller hardware matrix, audio-device-loss test, or broad GPU matrix was run during this review. Existing CI configuration and historical evidence were inspected, not represented as new passing runs. The package CTest validates the gate and its fixtures; it is not acceptance of a newly built shipping installer.

## Suggested changes

### CQ-01 — Preserve document identity when inserting or deleting numbered content

**P1 · Reproduced · Correctness, maintainability**

**Location:** `LevelEditor` structural operations and its path-keyed draft cache; `LevelProjectStore` renumbering transactions. Exact anchors and the reproduction are in [content findings](content-findings.md).

Inserting a screen renames existing files but leaves cached drafts indexed by their old paths. The probe dirties screen 1, inserts a new screen 0, then opens the new occupant of screen 1. The cached old screen-1 wall appears even though that occupant's file contains no wall. Saving that document can overwrite the wrong puzzle. A path is functioning as both a storage location and a document identity, but the renumber operation updates only the storage side.

The same operation boundary needs to account for positional content references: per-screen splat names/files and level-indexed music must continue to refer to the intended content when indices move. Treat these as part of the structural operation, not unrelated files the author has to repair afterward. The draft failure is directly reproduced; associated reference handling is source-traced in the supporting notes.

**Change:** have the project transaction return an explicit old-to-new identity/path map. Capture the active dirty document before mutation; remap cached drafts, active selection, and affected references only after the structural transaction succeeds. Preserve its existing rollback across source and runtime roots. On rollback retain the original mapping; if rollback itself fails, retain recovery information and expose that state. Deleted documents must not retain drafts capable of attaching to a later occupant. A permanent-ID format migration is not required for the first fix; an explicit remap is sufficient if it covers the actual owners.

**Acceptance:** insert/delete before and after the active screen/level, with both active and cached dirty drafts; reopen and save each surviving document and compare its contents with its original identity. Verify splat/music association and structural-transaction failure/rollback. Detach deleted drafts from reusable paths while preserving recoverable unsaved work. Preserve the separate source-commit/runtime-retry contract used by ordinary document saves. Avoid a fix that simply discards all unsaved work.

### CQ-02 — Load usable saves even when sibling-artifact maintenance fails

**P2 · Reproduced · Correctness, recovery**

**Location:** [SaveStore.cpp:195](../../../src/engine/SaveStore.cpp), `load`; lines 312–318, outer fallback; lines 322–368, interrupted-write recovery. Detailed reasoning: [core findings](core-findings.md).

`load()` performs recovery inspection and cleanup before its normal primary decode-and-return path. Recovery inspection itself decodes the primary to assess validity, but failure inspecting a backup or removing a stale sibling escapes to the outer catch and returns `StorageUnavailable` with a default profile. A valid primary can therefore be unusable because an auxiliary artifact is damaged.

The probe writes level-7 progress and creates a directory at `profile.backup.json`. Loading returns level 0/defaults, while the unchanged primary still decodes as level 7. The application refuses the unavailable load; this is a loss of access to saved progress, not evidence that the probe destroyed or overwrote the saved bytes.

**Change:** separate choosing and decoding authoritative data from repair/cleanup. Once valid primary data is established, failure maintaining non-authoritative siblings should return that profile with a persistence/maintenance error. Reuse the existing `LoadedWithPersistenceError` vocabulary. Preserve deletion-marker authority, recovery ordering, and future-format protection; never bypass those to obtain an apparently usable older save.

**Acceptance:** valid primary plus non-regular/inaccessible backup; valid primary plus failed stale cleanup; usable recovery data plus failed promotion; and controls for deletion markers, future versions, corrupt data, and entirely unavailable storage. Assert both returned progress and preserved files.

### CQ-03 — Invalidate or rebuild prepared textures when publishing edited pixels

**P2 · Reproduced · Correctness, derived-data ownership**

**Location:** `SplatPainter::save`, content publication/index refresh, `TextureSourceLoader` prepared-BC7 selection, and compressed-artifact identity. Exact anchors: [content findings](content-findings.md).

Painting saves the new PNG and refreshes `content.index`, but an existing BC7 artifact remains valid according to the loader's path/sampler identity. The probe saves a white source pixel, then reloads through the production prepared-texture path: it selects the old black compressed artifact, and package validation still succeeds. Live uploaded paint can look correct while the next launch displays old pixels.

**Change:** make publication responsible for invalidating/rebuilding every derived artifact affected by the changed source, before publishing a consistent index. Either rebuild the affected artifact or remove it and intentionally use raw decoding until rebuilt. If artifacts remain cacheable across writes, record and verify a source-content revision/hash plus conversion version. Merely refreshing file sizes in the package index cannot establish freshness. Apply the same policy to other authoring operations that overwrite texture bytes; avoid separate bespoke fixes in each tool.

**Acceptance:** stage/compress, edit and save, reconstruct the loader, and verify the newly selected texture content. Cover BC7-capable and raw paths, publication failure/retry, and a non-splat texture overwrite. Existing artifact round-trip tests should remain; add the missing source-edit-to-runtime-load scenario.

### CQ-04 — Keep setting values separate from choice indices

**P2 · Reproduced · Correctness, API clarity**

**Location:** [OptionsMenu.cpp:1317](../../../src/engine/ui/OptionsMenu.cpp), `drawStepperChoiceRow`; [UiControls.cpp:181](../../../src/engine/ui/UiControls.cpp), `choiceStepper`; [SettingsTypes.cpp:33](../../../src/engine/SettingsTypes.cpp), frame-cap normalization.

The options row holds semantic values such as 30, 60, or 120 FPS. Its view passes that value into a widget expecting a zero-based index into six labels. The widget clamps 60 to index 5 and reports a change even with no input. The view emits value 5, which settings normalization rejects and replaces with 0/Unlimited.

The probe records `input_cap=60 emitted_choice=5 persisted_cap=0` on an idle Graphics frame. Keyboard selection can therefore be immediately undone by the drawing adapter, and mouse steps do not reliably select the advertised FPS value.

**Change:** make the stepper accept value/label pairs, as the segmented control already does, or explicitly translate value → index → value at the adapter boundary. Name the variables `selectedValue` and `selectedIndex` according to their contract. Rendering a valid settings state without input must not emit a change.

**Acceptance:** exercise the view and reducer together for all six FPS values: idle rendering emits no intent, previous/next emit the adjacent semantic value, and keyboard selection survives the subsequent draw. Keep a display-mode control as a control case; its coincidentally contiguous values concealed the API mismatch.

### CQ-05 — Reconcile completion without resetting surviving actions

**P2 · Reproduced · Correctness, completing concurrent-action integration**

**Location:** [GameplayLoop.cpp:213](../../../src/engine/GameplayLoop.cpp); [GameplayPresentation.cpp:638](../../../src/engine/GameplayPresentation.cpp), `finishAction` and `syncToGameState`.

After committing a completed action, the loop synchronizes all visuals to committed state. That state deliberately excludes unfinished actions. If another action is still running and no frame time remains, its sampled visual is replaced by its older committed position for the rendered frame.

The probe pushes an ice block and moves the player independently. At the exact player-completion boundary the block moves visually from X=3.5 back to X=2, then jumps to X=4.125 on the next frame. Expected boundary X is 4.0. Committed puzzle state remains correct; the failure affects presentation and can also reset surviving actor animation state. The probe uses frame deltas below the application's cap.

**Change:** reconcile only completed entities, or synchronize then re-sample surviving actions even when the frame's remaining time is zero. Preserve insertion/removal handling and the existing action-ownership model; do not create another independent list of changed entities.

**Acceptance:** different-duration concurrent actions, exact and non-exact completion boundaries, surviving motion flags/clip state, mirror additions/removals, undo, and final idle state. See the [recorded position trace](evidence/core-probes.txt).

### CQ-06 — Size the texture heap against every applicable descriptor limit

**P2 · Reproduced arithmetic; source/spec-confirmed portability · Correctness**

**Location:** `VulkanDeviceSelection` descriptor-capacity helper and capability collection, descriptor-layout construction, and the matching boundary tests. See [renderer findings](renderer-findings.md) for exact anchors and official Vulkan rules.

The heap uses combined image samplers. Capacity selection considers sampled-image limits but omits sampler limits and fails to reserve scene descriptors from the aggregate descriptor-set sampled-image limit. The production helper accepts a 128-entry heap against an aggregate limit of 128 even though the layout includes eight additional scene image descriptors. Current tests encode that acceptance.

**Change:** compute the usable heap as the minimum remaining capacity across the applicable image, sampler, and per-stage limits, after reserving the relevant scene bindings. Carry the actual required device limits through capability discovery. Report a precise incompatibility when the remaining capacity cannot satisfy the manifest; do not rely on a later pipeline-layout failure.

**Acceptance:** synthetic devices where each individual limit is the bottleneck; exactly sufficient/one-short cases after reservations; a device with ample image slots but insufficient samplers; and Vulkan validation of constructed layouts. Correct the test's expected capacity rather than preserving the invalid expectation.

### CQ-07 — Make blocking asset admission terminate or make progress

**P2 · Reproduced under constrained budgets · Correctness, efficiency**

**Location:** `VulkanModelResources::waitForAssets`, CPU-ready admission/retry, and `AssetLoadScheduler` prepared-byte gating. Exact anchors and logs: [renderer findings](renderer-findings.md).

A CPU-ready payload retained after GPU admission denial consumes the prepared-data budget. That prevents a queued decode from starting. The blocking wait continues because queued work exists, but its retry path does not resolve the CPU-ready entry. The probe reaches queued=1, active=0, CPU-ready=1, with 67,948 retained bytes against a deliberately tiny budget; the blocking call exceeds the 15-second bound.

The tiny budget makes the failure deterministic. It does not show ordinary default-budget gameplay hanging or establish a general performance bottleneck. It does demonstrate that the advertised budget/admission state machine has a non-progress state.

**Change:** give every blocking-wait iteration an explicit progress or terminal/deferred outcome. Retry retained ready payloads when capacity can change; report impossible admission rather than spinning indefinitely. Sleeping alone does not fix a state with no active work capable of freeing the resource. Preserve retained payload ownership for retry and the prepared-memory bound.

**Acceptance:** permanently oversized asset, temporary pressure relieved by retirement, queued work behind retained ready data, cancellation, and failure propagation to loading UI/callers. Tests must be bounded and verify the outcome, not merely poll longer.

### CQ-08 — Publish replacement textures only after successful construction

**P2 · Source-confirmed exception safety · Correctness, ownership**

**Location:** texture replacement in `VulkanModelResources`, and [ApplicationTools.cpp:224](../../../src/engine/ApplicationTools.cpp), `pushPaintedSplatMap`. Full analysis: [renderer findings](renderer-findings.md).

The replacement path destroys the current texture before all fallible work to construct its replacement has succeeded. The editor catches upload exceptions and continues rendering. It also advances `uploadedSplatRevision` before the upload succeeds and ignores a false update result, suppressing a retry of that revision.

**Change:** construct/upload a replacement under temporary ownership; commit the handle, descriptors, and residency accounting only after success; retire the old resource according to the established frame-lifetime rules. Advance the uploaded revision only after a successful publication result. On failure the old texture must remain usable and the edited revision retryable.

**Acceptance:** deterministic failure injection at replacement creation/upload and a false-result path; verify old handles/accounting/descriptors remain consistent, no invalid handle is later used/destroyed, and the same edit revision can succeed on retry. The attempted oversized-image probe succeeded on this GPU, so this review does **not** claim to have reproduced allocation failure or a device crash.

### CQ-09 — Keep shader derivatives outside divergent water branches

**P2 · Source/spec-confirmed · Correctness**

**Location:** `water.frag.glsl` conditional caustic/ripple evaluation and `cellularRippleBands` derivative calculation. Exact lines and the GLSL rule: [renderer findings](renderer-findings.md).

Depth-dependent `geometryPresent` decides whether fragments call ripple logic containing `fwidth`. At geometry/background boundaries neighboring fragments can take different branches, where GLSL does not define these derivative results. A successful shader compile or Vulkan validation run does not detect this numerical correctness issue.

**Change:** evaluate the required derivatives in uniform control flow and pass the resulting footprint into the conditional work, or use an analytically justified footprint. Keep the existing optimization of skipping unnecessary caustic work where possible. Do not blindly move all expensive work outside the branch or assume a file split fixes derivative semantics.

**Acceptance:** water/geometry edges at different render scales, MSAA settings, camera angles and relevant preview/dither modes. Compare appearance and GPU time to ensure the correctness fix preserves the measured optimization's benefit. No visual artifact or speedup is claimed from the present static finding.

### CQ-10 — Keep settings controls reachable throughout the supported window range

**P2 · Reproduced · Correctness, layout maintainability**

**Location:** [OptionsMenu.cpp:1133](../../../src/engine/ui/OptionsMenu.cpp), fixed row layout; line 1556, panel sizing; [UiLayout.cpp](../../../src/engine/ui/UiLayout.cpp), overflow reporting; [Window.cpp:28](../../../src/engine/Window.cpp), resizable window; [UserSettingsConfig.hpp:36](../../../src/engine/UserSettingsConfig.hpp), 480-pixel minimum setting.

The panel shrinks with the viewport, but the rows retain their fixed vertical sizes and there is no scrolling/overflow handling. At height 600 the Graphics contents extend to Y=676 and Controls to Y=686; both also overflow at 480. At 720 the tested layouts fit. Existing tests check the larger size only.

**Change:** provide an explicit overflow policy, preferably scrolling or responsive row spacing with an accessible fixed Back action. Derive the required content height from the layout rather than maintaining page-height guesses independently of row definitions. Handle keyboard/controller focus visibility as well as mouse access. Raising the minimum window size is only an alternative if the supported product range is deliberately changed.

**Acceptance:** 640×480, a 600-pixel-high window, 1280×720 and a larger viewport, with every settings page reachable by mouse and keyboard/controller. Use focused layout/interaction assertions; a screenshot at the default size is insufficient.

### CQ-11 — Remove unreachable completion and level-selection UI

**P3 · Source-confirmed cleanup · Maintainability, readability, avoiding unnecessary work**

**Location:** [LevelCompleteOverlay.cpp:39](../../../src/engine/ui/LevelCompleteOverlay.cpp); [Application.cpp:1053](../../../src/engine/Application.cpp), actual puzzle-completion behavior; [ShellFlow.cpp:86](../../../src/engine/ShellFlow.cpp); [OptionsMenu.cpp:565](../../../src/engine/ui/OptionsMenu.cpp), current main rows; [TitleScreen.cpp:270](../../../src/engine/ui/TitleScreen.cpp), dormant level/screen selection.

No production caller opens either completion-overlay mode. Completing a puzzle saves and loads the overworld directly. The options menu never emits a LevelSelect row, and the remaining routes to level selection originate from that absent row or the unopened overlay. Unit tests can invoke these APIs directly, but that does not make the product routes reachable.

These dormant flows still add Application members, routing facts/branches, draw calls, menu variants, metadata/progress preparation and tests. `allLevelsCompleted` is populated in shell facts but never consumed by the reducer. This is an appropriate deletion boundary because the current overworld flow already replaces the behavior.

**Change:** remove the dormant overlay and level/screen-menu routes and their exclusive plumbing, tests, and computed data. Keep persistent progress formats, completion records and metadata needed by the editor, selectors, or compatibility paths. Do not remove those merely because the old menu used them too. If the dormant UI is explicitly brought back as a future feature, it needs a real product entry point and integration coverage at that time.

**Acceptance:** prove New Game, Continue, slot selection/deletion, options, puzzle completion, overworld return and quit still work; verify no production reference to removed routes remains; keep relevant profile compatibility tests. Remove only tests exclusive to the deleted feature, not nearby active-flow assertions.

The probe also found overflow in `MenuKit::formatDuration` for very large accepted profile times. All current production call sites are inside these dormant views. It is therefore not counted as a separate player-facing bug to fix before deletion. If a caller survives, bound the number before conversion rather than simply changing the integer width.

### CQ-12 — Stop a dependent New Game operation when selecting its slot fails

**P2 · Source-confirmed failure path · Correctness, explicit outcomes**

**Location:** [ShellFlow.cpp:45](../../../src/engine/ShellFlow.cpp), `NewGameOnSlot`; [Application.cpp:1399](../../../src/engine/Application.cpp), caught switch failure; line 1484, unconditional command iteration.

`NewGameOnSlot` emits `SwitchSlot` followed by `StartNewGame`. `switchSaveSlot` catches a storage/switch error, displays it, and returns `void`; the outer command loop then still starts the new game on the previously active slot. The reachable case is choosing a slot on the first-run empty-slot screen while writing the selected-slot marker fails. The requested target is not honored and the error screen is left. This review does not claim that this first-run route overwrites an existing played save.

**Change:** execute new-game-on-slot as one operation that starts only after successful selection, or give command execution a typed failure result that aborts dependent commands. Prefer the narrow combined operation over a general transaction framework for every shell command.

**Acceptance:** inject slot-marker/switch failure, choose a different empty slot, and verify active slot, live profile and title/error state remain unchanged; then retry successfully and verify the game starts on the requested slot. Test the executor boundary, since testing the reducer's two emitted commands alone misses this defect.

### CQ-13 — Correct misleading comments and broken indentation locally

**P3 · Cleanup · Human readability**

**Locations:** [Application.hpp:53](../../../src/engine/Application.hpp) says “Both fields” above a much larger options struct; line 248 says Linux CI never runs rendering although its Debug job does. [Application.cpp:1114](../../../src/engine/Application.cpp) calls an asynchronous checkpoint “durable” before the context change. [OptionsMenu.cpp:693](../../../src/engine/ui/OptionsMenu.cpp) describes three audio sliders but emits two. [RenderFrameBuilder.cpp:841](../../../src/engine/RenderFrameBuilder.cpp) through line 921 contains a function body flush with namespace indentation after extraction.

**Change:** correct these specific contracts, indent the extracted body consistently, and replace nearby “moved verbatim / formerly N lines” history with a short explanation of present responsibility when touching that code. Preserve comments explaining lifetimes, color space, budgets and unusual policy.

**Why this is needed:** the incorrect comments can mislead future work about persistence guarantees and test coverage; lost nesting makes control flow harder to read. This is a small targeted patch, not authorization for a repository-wide formatting rewrite.

**Acceptance:** review the diff for behavior neutrality and verify each corrected comment against its implementation. No new tests are needed for indentation or prose alone. Existing warning builds suffice if source structure is changed.

## Suggested implementation order

| Packet | Work | Reason and dependencies | Completion evidence |
| --- | --- | --- | --- |
| 1 | CQ-01 document/draft remapping; CQ-02 save usability | Protect authored work and access to existing progress first. Implement as separate changes. Finish CQ-01's dependent asset associations in packet 3. | Fault/renumber regressions and existing editor/persistence suites |
| 2 | CQ-04 setting value/index mapping; CQ-05 concurrent presentation | Small independent player-facing fixes with deterministic reproducers. | View+reducer and exact-frame-boundary regressions |
| 3 | CQ-03 source/prepared-texture publication; remaining CQ-01 splat/music associations | Establish a single derived-data freshness policy, then use packet 1's remapping to preserve moved asset references and complete CQ-01. | Stage → edit → publish → reload tests; renumbered paint/music retain their owners |
| 4 | CQ-06 descriptor limits; CQ-07 bounded admission; CQ-08 texture replacement | Separate changes to capability calculation, admission progress and replacement lifetime. Preserve existing allocators/fence ownership. | Synthetic limits, bounded pressure/failure probes, Vulkan validation |
| 5 | CQ-09 water derivatives | Correct the shader without undoing measured cost reductions. | Edge images and matched before/after GPU measurements |
| 6 | CQ-10 small-window layout; CQ-12 failed slot selection | Complete the settings and shell interaction failure paths. Can run independently of renderer work. | Small-size interaction matrix and injected slot-switch failure |
| 7 | CQ-11 dead UI removal; CQ-13 local readability | Remove the obsolete route as one coherent cleanup, then correct remaining local commentary. Avoid polishing code that packet 7 deletes. | Active-flow regression pass, reference search, concise behavior-neutral diff |
| 8 | Full supported validation matrix | Cross-cutting changes need final Debug/Release warning builds, full registry and relevant real-device checks. | New logs for the implemented revision, not this baseline |

Each packet should leave a reviewable result with its focused regression and a clear acceptance outcome. Do not batch all changes into an Application/renderer rewrite. Use the repository's existing production libraries and test harness rather than compiling a second implementation into permanent tests. The standalone programs here are investigation tools; integrate the meaningful assertions into their owning suites during implementation.

## Refactoring and optimization decisions

**Shaders:** UI, water, mirror energy, ground splatting, SSAO/composite, tonemapping and transitions already have dedicated shaders. Shared scene-frame, material, draw-mode, skinning, ambient-mask and point-shadow contracts already have includes. No blanket ubershader split is needed. If water shading is later divided for readability, use its real responsibilities—depth reconstruction, procedural patterns, compositing—while making derivative inputs explicit; that organization is subordinate to CQ-09's correctness fix.

**Large renderer files:** do not split `VulkanModelResources` or `VulkanSceneRecorder` solely to reduce their line counts. Resource preparation, publication, residency and retirement share lifetimes. CQ-07 and CQ-08 identify the transitions that need clarification. Extract a component only if it gains a coherent owner and makes a transition easier to verify.

**Numbers:** preserve ordinary coordinates, channel indices, enum/layout values, mathematical constants and clearly grouped artistic tuning. Name a value when its units, purpose or cross-language agreement are otherwise unclear. CQ-04 is a concrete case where an integer's semantic meaning needs a clearer API. Do not replace every literal with a remote constant or turn the existing magic-number analyzer counts into a rewrite quota.

**Duplication:** retain intentional differences such as scene/ground shadow tap counts and render/editor behavior. ActionPlan's positional step comparison has a documented no-insertion/removal invariant; no live mismatch with StateDelta was established. Building a complete delta for every slide leg merely to remove similar-looking code is not justified. CQ-03 warrants shared publication policy because duplicated ownership already permits stale results.

**Efficiency:** the review establishes no new steady-state bottleneck requiring a transfer queue, secondary command buffers, a different task system, an entity database, or replacement allocators. Preserve the measured arenas, scratch reuse, upload scheduling, draw sorting, mip residency, compression and shadow caching. CQ-07 eliminates proven non-progress; CQ-11 removes unnecessary plumbing/computation, but neither should be marketed with an unmeasured frame-rate gain. Measure workload, baseline, target and result before additional performance work.

**Incomplete and old code:** the concurrent-action completion seam, prepared-artifact publication and choice adapter have concrete unfinished integration behavior. CQ-11 identifies unreachable product UI. Historical save migrations, supported compatibility adapters, evidence switches and vendor-specific resource policies are not obsolete merely because they are old or specialized.

## Coverage and remaining limits

| Area | Review focus | Outcome |
| --- | --- | --- |
| Rules, action planning/scheduling, reservations, deltas, undo | Admission, commit order, surviving actions, state identity | CQ-05; retain canonical state/ownership boundaries |
| Campaign, profiles, saves, atomic replacement | Recovery, deletion, migration, asynchronous durability, slot switching | CQ-02 and CQ-12; no ordinary filesystem-exception escape established for save destruction |
| Input, timing, tasks, arenas, math, logs/crash diagnostics | Capture/held-state behavior, worker lifetime, frame timing, bounded ownership | Existing contracts retained; no evidence for new architecture |
| Levels, overworlds, authoring and asset pipelines | Rename transactions, draft identity, dependencies, source/runtime/derived publication | CQ-01 and CQ-03; source/runtime failure states require focused regression coverage |
| Animation, particles and audio integration | Catalog resolution, presentation, lifetime and device boundaries | CQ-05 overlaps animation; real audio/controller matrix remains unexecuted |
| Application, shell and player UI | State orchestration, reachability, semantic intents, layout | CQ-04, CQ-10, CQ-11, CQ-12, CQ-13 |
| Scene building, renderer, shaders | Descriptor limits, publication/retirement, budget progress, shader control flow | CQ-06 through CQ-09; no generic shader/file split |
| Tests, CMake, CI, tools, packaging and docs | Production-library linkage, warning gates, headless separation, staging, bounded package validation | Local warning builds and complete registries passed; targeted documentation corrections in CQ-13 |

Source-confirmed findings should gain their focused failure-injection or interaction test during implementation. Do not turn unexecuted Linux, hardware or installer rows into claims of defects; those remain the existing platform-evidence work. Supporting notes distinguish proved behavior, rejected hypotheses, and limitations: [core](core-findings.md), [content/editor](content-findings.md), [renderer](renderer-findings.md).

This assessment supersedes the “no repository-internal items remain” conclusion in the [earlier roadmap](../2026-09-03-code-quality/README.md) for this commit. It does not reopen its resolved historical findings.

## Reproducing the checks

The local evidence uses Windows x64, Visual Studio 2022 Community/MSVC 19.44, and Vulkan SDK 1.4.309.0. From the repository root, with CMake and the installed SDK available:

```powershell
cmake -S . -B out/code-quality-review -G "Visual Studio 17 2022" -A x64 -DSOKOBAN_WARNINGS_AS_ERRORS=ON
cmake --build out/code-quality-review --config Debug --parallel
ctest --test-dir out/code-quality-review -C Debug --output-on-failure --no-tests=error --timeout 120 -j 4
cmake --build out/code-quality-review --config Release --parallel
ctest --test-dir out/code-quality-review -C Release --output-on-failure --no-tests=error --timeout 120 -j 4

& ./docs/reviews/2026-09-11-code-quality/core-probes/run.ps1
& ./docs/reviews/2026-09-11-code-quality/run-content-editor-probe.cmd
& ./docs/reviews/2026-09-11-code-quality/ui-probes/run.ps1
& ./docs/reviews/2026-09-11-code-quality/renderer-probes/run.ps1
```

The probe scripts use the installed Visual Studio/SDK paths above and the fresh Debug libraries; adjust those tool paths for another machine. They write disposable fixtures/binaries beneath `out/code-quality-review/probes`. The renderer harness bounds the deliberately stalled wait to 15 seconds and terminates only its probe process. Its optional `-RepaintOnly` run records the negative replacement-failure experiment; success there does not reproduce CQ-08. These investigative programs print observations rather than acting as permanent regression gates. Retained logs in `evidence` describe the reviewed baseline, not later reruns or implementations.
