# Code quality review — 2026-09-03

Reviewed revision: `bd4f9496d467613cc875a6cde7edf07b26457ed2`. The working tree was clean at the start. This review adds documentation and an isolated diagnostic harness; it does not change game behavior.

**Follow-up, 2026-09-07:** CQ-01 is resolved in the current working tree. Valid decoded profiles now survive migration/backup-repair write failures with an explicit persistence-error result, while unsupported formats are preserved and stop loading before an older build can overwrite them. New failure-injection and save-slot regressions pass in Debug and Release.

**Follow-up, 2026-09-07 (CQ-02):** Failed asynchronous snapshots now remain pending and retryable, flushing reports persistence failure, and channel replacement refuses to discard an unsaved outgoing profile. Slot switching keeps the original slot active and exposes the retained storage error until a later save succeeds.

**Follow-up, 2026-09-07 (CQ-03):** Ordinary puzzle sources and their derived runtime mirrors now use durable atomic replacement with checked completion. Saving returns a structured outcome that distinguishes complete failure from a committed source whose mirror is stale; the latter remains dirty and can be retried without losing the authoritative source.

**Follow-up, 2026-09-07 (CQ-04):** Successful editor publication now atomically regenerates and validates the existing runtime `content.index` while preserving its staged game version. Level and overworld transactions, splat maps, manifests, imported decorations, animation catalogs, and tile-thumbnail baking all use this contract. Source-only test and authoring roots remain unindexed. CQ-09 still governs whether imported GLTF/GLB dependency discovery itself is complete.

**Follow-up, 2026-09-07 (CQ-05):** Save deletion now belongs to `SaveStore`, which centrally owns the primary, backup, temporary, and displaced artifact set. A durable per-slot deletion marker commits the operation before cleanup and makes every old candidate ineligible for recovery across interruption or partial cleanup. The marker remains until a successful new save replaces the slot. Active-slot deletion is serialized with its async channel and discards a queued snapshot only after the marker commits.

**Follow-up, 2026-09-08 (CQ-06):** Binding capture now forwards keyboard, gamepad-button, and gamepad-axis events through an edge-suppressed input path. Physical held state, releases, and axis neutralization stay current, while controls pressed or moved during capture cannot create a menu or gameplay press edge when capture completes or is cancelled.

**Follow-up, 2026-09-08 (CQ-07):** Skinned-model publication now converts and validates the CPU-ready mesh without transferring ownership, asks the residency ladder for admission, and moves the source into its resident slot only after admission succeeds. The converted vertex/index payload is reused for upload. A device-backed regression denies admission once at this seam, then verifies the retained nonempty mesh reaches ready GPU residency on retry without another load request.

**Follow-up, 2026-09-08 (CQ-08):** Static loading and CPU/GPU skinning now share one source-to-model transform. Positions and tangents use its forward linear transform, normals use its inverse transpose, and tangent frames are normalized and orthogonalized while preserving handedness. Nonuniform 4×2×1 fixtures cover authored and generated tangents, fitting options, degenerate bounds, skinned bind poses, and full CPU/GPU basis parity.

**Follow-up, 2026-09-08 (CQ-09):** Decoration registration and distributable content staging now share the structured glTF external-file resolver for both GLTF and GLB documents. Registration validates every external buffer and image before copying or changing a manifest, ignores embedded data and unrelated JSON `uri` fields, registers a single external base-color texture from semantic material records, and refreshes the package index only after the runtime tree is complete. Loadable nested GLTF/GLB fixtures cover external buffers/images, embedded data, missing files, traversal rejection, runtime loading, and index validation.

**Follow-up, 2026-09-08 (CQ-10):** Completed-action diagnostics now use a scalar counter instead of retaining full actions with world states and presentation timelines. Functional undo remains in its separate history. A 1,000-pair move/undo regression verifies 2,000 reported completions, an empty undo stack, restored move count and state, and counter reset behavior.

**Follow-up, 2026-09-08 (CQ-11):** Every path that constructs `Application` now destroys it before validation results are read or logging shuts down. The process-wide validation result is also checked after exception unwinding, while an existing, more specific failure code retains precedence. A real-executable regression injects an error at the end of Vulkan device teardown and requires exit code 3.

**Follow-up, 2026-09-08 (CQ-12):** `TaskSystem` construction now catches worker-creation failures, signals the already-started workers to stop, joins them, and rethrows the original error. Construction failure and normal destruction share the same cleanup path. A one-shot fault-injection regression fails after two workers, verifies the caller receives the system error with zero live workers, and then successfully uses a new pool.

**Follow-up, 2026-09-08 (CQ-13):** The standalone compiler/linker script has been removed. `SOKOBAN_HEADLESS_TESTS_ONLY` now configures the real core, UI, and SDK-independent test targets while omitting Vulkan discovery, shader compilation, the application, and packaging. The `headless-tests` preset provides the supported local entry point, and Linux CI configures it without downloading the Vulkan SDK before building and running every registered headless test.

**Follow-up, 2026-09-08 (CQ-14):** Shipping validation now runs 240 real frames with an isolated profile, requires exit code 0 within a 60-second deadline, kills timeouts, and captures stdout, stderr, logs, and dumps. Smoke-mode initialization and renderer failures suppress dialogs and return failure. A Windows integration regression covers success, nonzero exit, hang termination, missing/corrupt content, and a real application initialization failure.

**Maintainability follow-up, 2026-09-08 (typed persistence results):** Asynchronous saves now assign ordered per-channel revisions and return typed persisted/retryable-failure outcomes from flush and channel replacement. Results carry both the newest requested revision and the latest durable revision plus the storage message, so slot switching and application shutdown no longer infer transaction state from a boolean followed by separate diagnostics. The public header now states which operations belong to the owner thread and which are synchronized producer/snapshot operations.

**Maintainability follow-up, 2026-09-08 (shared delta entity interpretation):** `StateDelta` now owns the canonical player/movable/enemy id order, append operation, and overlap query. Scheduling, saved-action validation, and ambient-motion filtering use those operations directly. This removes three copies of the same domain walk and avoids allocating a temporary id vector for every in-flight ownership check.

**Maintainability follow-up, 2026-09-08 (shared manifest texture identity):** Manifest texture declarations now have one conversion to the complete runtime source identity, including lexical path normalization, color space, addressing, and filtering. Content staging still applies its stricter package-containment checks before calling the shared conversion, while runtime catalog construction uses the same identity rules directly. A regression compares the public conversion with the runtime definition and covers every interpreted field.

## Assessment

The project has substantial engineering foundations: production code is shared with tests, gameplay has explicit state and presentation boundaries, content staging validates dependencies, save writes have recovery machinery, and Vulkan lifetimes have dedicated tracking and retirement helpers. Both current-source builds and all registered tests passed locally.

All actionable findings from this review are resolved. The remaining improvement backlog below contains measured cleanup and investigation candidates rather than demonstrated defects.

This review recorded **14 actionable findings: three P1 and eleven P2**. CQ-01 through CQ-14 have been resolved. Nine had direct reproductions against production libraries. Five were established by source/control-flow inspection, with their originally untested conditions identified below. Priority expresses impact and urgency, not how frequently the failure has been observed in normal play. There are no P0 findings.

- **P1:** prioritize before relying on persistence or authoring for valuable work; existing data or unsaved progress can be lost or abandoned.
- **P2:** schedule fixes for observable correctness, resource use, or validation gaps; several require unusual inputs or failure conditions.
- **Improvement backlog:** separately labeled recommendations needing design judgment or performance measurement, rather than claims of demonstrated bugs.

## Scope and evidence

The inventory covers first-party C++ sources/headers, shaders, tests, selected scripts, and CMake helpers. Build configuration, CI, packaging, README, and HANDOFF were also inspected. Review depth followed subsystem boundaries and risk: persistence/recovery, gameplay/undo, input/UI, editor/content, mesh/skinning/materials, asset scheduling/residency, renderer lifetimes, tasks/memory, diagnostics, and build/release tooling. This is a broad source review with targeted deep investigations, **not a claim that every line has been formally verified**. Vendored library internals and the correctness/licensing of every individual art asset were outside scope.

| Inventoried area | Files | Lines, including blanks/comments |
| --- | ---: | ---: |
| `src` | 294 | 75,820 |
| `tests` | 80 | 30,873 |
| `shaders` | 23 | 2,931 |
| `tools` | 7 | 1,584 |
| `cmake` helpers | 1 | 33 |
| Packaging PowerShell | 2 | 413 |

These counts use selected code extensions and exclude root `CMakeLists.txt`, documentation, generated files, assets, and third-party sources. The [inventory](evidence/inventory.json) contains file-level counts and exact cross-file duplication candidates. Duplicate windows overlap; their count is not a count of independent defects. File length is a navigation aid, not a quality score.

### Validation performed

| Check | Result | Evidence |
| --- | --- | --- |
| Rebuild Debug from current sources | Passed | Local `build/review-build-debug.log` |
| Debug CTest | 78/78 passed; 33.83 s | [Debug log](evidence/ctest-debug.txt) |
| Rebuild Release from current sources | Passed | Local `build/review-build-release.log` |
| Release CTest | 78/78 passed; 6.86 s | [Release log](evidence/ctest-release.txt) |
| Full application, 120 frames, validation required | Exit 0; no logged validation errors | [Application smoke log](evidence/vulkan-smoke.txt) |
| Focused failure/invariant probes | Nine findings reproduced; migration also tested through backup promotion | [Probe output](evidence/probe-results.txt), [source](probes/probes.cpp) |

Build commands were `cmake --build build --config Debug --parallel 4` and the Release equivalent. CTest used `ctest --test-dir build -C Debug --output-on-failure --no-tests=error --timeout 120 -j 4`, likewise for Release. The separate application run used `--smoke-frames 120 --require-validation --save-directory build/review/smoke-profile`.

The application smoke rendered at 1280×720, FIFO, 4× MSAA. It reported zero residency evictions, capacity blocks, dropped draws, or dropped skinned poses. It exercised a normal startup, not memory-pressure or adversarial asset paths. Shader-output-not-consumed warnings remain in the log. The Debug build also reported C4459 at `ApplicationDebugUi.cpp:77`: local `pi` shadows `sokoban::pi` from `Math.hpp:29`.

Current sanitizer, clang-tidy, Linux builds, interactive controller/editor acceptance, installer validation, and a multi-GPU/driver matrix were **not** rerun. Existing CI configuration is not evidence that those checks passed for this review. The smoke's clean log was inspected in addition to its exit status because CQ-11 weakens that status as a teardown gate.

## Prioritized findings

| ID | Priority | Finding | Evidence type |
| --- | --- | --- | --- |
| CQ-01 | P1 | Migration/promotion write failures classify valid saves as corrupt | Reproduced |
| CQ-02 | P1 | Slot switching discards a failed outgoing save and clears its error | Reproduced |
| CQ-03 | P1 | Puzzle editor overwrites source files without atomic replacement | Source risk; partial commit reproduced |
| CQ-04 | P2 | Successful editor saves invalidate the package checked at next startup | Reproduced |
| CQ-05 | P2 | Deleted slots can return through leftover recovery files | Reproduced |
| CQ-06 | P2 | Binding capture suppresses releases and leaves movement held | Reproduced |
| CQ-07 | P2 | Residency deferral destroys the prepared skinned mesh | Source-confirmed |
| CQ-08 | P2 | Nonuniform mesh normalization does not transform normals correctly | Reproduced |
| CQ-09 | P2 | GLB registration omits external buffer dependencies | Reproduced |
| CQ-10 | P2 | Diagnostic history retains complete actions indefinitely | Resolved 2026-09-08 |
| CQ-11 | P2 | Validation exit status is computed before renderer destruction | Resolved 2026-09-08 |
| CQ-12 | P2 | Partial thread-pool construction can terminate the process | Resolved 2026-09-08 |
| CQ-13 | P2 | The standalone headless build script has drifted from the build graph | Resolved 2026-09-08 |
| CQ-14 | P2 | Shipping launch validation mistakes process survival for startup success | Resolved 2026-09-08 |

### CQ-01 — Preserve decoded saves when migration or promotion cannot write (resolved 2026-09-07)

**Location:** [SaveStore.cpp](../../../src/engine/SaveStore.cpp), lines 123–146 and 150–161; default replacement at 175–180.

The same `try` block handles decoding a save and writing its migrated/recovered replacement. Its broad exception handler archives the input as corrupt. A storage failure therefore changes the interpretation of already validated data. The subsequent fallback can install a new default profile.

**Reproduction:** Write a valid format-1 profile whose current level is 2, verify that the decoder reads level 2, then use the existing `atomicFile::failWriteAfterForTesting` hook to fail the migration write with `no_space_on_device`. `load()` returns `ResetCorrupt`, level 0, and “Corrupt player saves were archived; defaults were restored.” The valid original is renamed to a `.corrupt-*` file. A valid backup-only profile at level 3 has the same outcome when promotion fails.

This demonstrates an incorrect reset after a one-shot write failure. A persistently full device can instead cause the outer storage-unavailable fallback; that also must not invalidate the successfully decoded profile. The archived bytes may be manually recoverable, so this is not a claim that every failure permanently destroys all copies.

**Change:** Separate file-read/decode results from migration/promotion persistence. Only evidence of invalid content should classify a file as corrupt. Return the validated in-memory profile with a storage warning when its rewrite cannot complete, retain recovery candidates, and make retry explicit. Distinguish unsupported versions from malformed data as well.

**Regression gate:** Valid legacy primary and valid backup, each with failures at every atomic-write phase; assert retained progress, accurate disposition/status, and no corruption archive caused solely by I/O failure.

### CQ-02 — Make outgoing save success part of switching slots (resolved 2026-09-07)

**Location:** [AsyncSaveStore.cpp](../../../src/engine/AsyncSaveStore.cpp), lines 60–71; [SaveSlotManager.cpp](../../../src/engine/SaveSlotManager.cpp), `switchTo`, lines 279–341; [Application.cpp](../../../src/engine/Application.cpp), slot-switch flow around 1342–1383.

`replaceChannel()` calls `flush()`, replaces the channel's store, and clears its status. Flushing means the work has finished; it does not mean the write succeeded. A failed outgoing snapshot has no successful durable replacement, but switching still discards the live outgoing profile and its error.

**Reproduction:** Persist level 1, queue level 3, inject a write failure, then switch to the second slot. The switch succeeds, the old slot remains at level 1 on disk, and the exposed progress status is empty. Preparing the incoming slot before committing the switch is useful, but does not protect the outgoing slot.

**Change:** Return a channel-specific persistence result/revision from flushing or replacing. On failure, retain the outgoing profile and a retryable latest snapshot, keep the error visible, and decline the switch unless an explicit discard decision is made by the player. An empty queue must not serve as a success signal.

**Regression gate:** Fail outgoing writes with both immediate and deferred requests; verify the active slot and latest profile remain available and a later retry succeeds. Retain coverage for incoming-slot failures too.

### CQ-03 — Use atomic replacement for ordinary puzzle saves (resolved 2026-09-07)

**Location:** [LevelEditor.cpp](../../../src/engine/LevelEditor.cpp), lines 1722–1761. Compare the overworld transaction immediately above this branch and [LevelProjectStore.cpp](../../../src/engine/LevelProjectStore.cpp).

Ordinary puzzle saving opens the authoritative source file with `std::ios::trunc`, writes directly into it, and then repeats that process for the runtime mirror. A write error or interruption after truncation can destroy the prior valid source. `close()` is not checked. The two destinations also have no shared commit boundary.

**Reproduction:** Make the runtime mirror's target a directory while keeping the source writable. `saveDocument()` returns false, but the edited source has already been committed. The status correctly mentions the partial save; callers still receive a boolean that cannot describe which destination changed. This probe demonstrates the partial-commit behavior, not an actual power-loss event.

**Change:** At minimum, atomically replace the source using the existing `AtomicFile` machinery and check completion. Decide whether the runtime mirror is a derived cache or part of a transaction. If it is derived, return a structured “source saved, mirror refresh failed” result and track mirror staleness; if both must agree, use the project transaction mechanism. Do not imply a rollback occurred when it did not.

**Regression gate:** Source write/replace failures leave the prior bytes intact. Mirror failure has a specific result and recoverable retry path. Keep puzzle and overworld save guarantees explicit and consistent with the UI.

### CQ-04 — Coordinate authoring updates with runtime-package validation (resolved 2026-09-07)

**Location:** [RuntimeContent.cpp](../../../src/engine/RuntimeContent.cpp), line 19; [ContentPipeline.cpp](../../../src/engine/ContentPipeline.cpp), lines 1040–1044 and 1088–1090; editor mirror writes in [LevelEditor.cpp](../../../src/engine/LevelEditor.cpp), lines 1738–1761.

Startup unconditionally validates each packaged file's size and membership against `content.index`. Editor operations change runtime files without refreshing that index. A size-changing level edit, new file, or changed manifest can therefore work in the current process and prevent the next launch until content is restaged.

**Reproduction:** Create a small indexed package that first passes `validateContentPackage`, load its level in the real editor, resize the document and save. Saving returns true. Repeating the exact validation used by startup fails with “runtime content file size does not match index: levels\\level0\\screen0.scr.” This tests the startup gate directly rather than opening a fatal-error dialog.

**Change:** Define one authoring/publication contract. Options include an editor transaction that regenerates the complete index, or a deliberately separate mutable development content root and immutable staged shipping root. Apply the contract to levels, splat maps, manifests, imported assets, and animation edits. Do not simply disable shipping validation.

**Regression gate:** Stage → edit through each supported editor → close/reopen or invoke the same startup loader. Cover file resize, addition, deletion, and dependency changes. Test this with CQ-03 and CQ-09 because they share the same publication boundary.

### CQ-05 — Delete the complete save recovery set (resolved 2026-09-07)

**Location:** [SaveSlotManager.cpp](../../../src/engine/SaveSlotManager.cpp), lines 355–375; [SaveStore.cpp](../../../src/engine/SaveStore.cpp), `recoverInterruptedWrite` around line 216.

`deleteSlot()` removes the primary and backup only. Recovery also considers atomic-write leftovers such as `.tmp` and `.replace-old`. Clearing the summary cache does not remove these candidates.

**Reproduction:** Create a second-slot primary plus a valid `profile-slot2.json.tmp` containing level 3. Deletion returns success; selecting the supposedly empty slot restores level 3 from the leftover file.

**Change:** Centralize the complete set of live/recovery paths in the save-store layer. Deletion should remove or invalidate every recoverable candidate for both primary and backup, coordinated with queued writes. For interrupted deletion, consider a deletion marker whose semantics recovery understands. Preserve diagnostics only if they are not eligible for automatic restoration.

**Regression gate:** Delete with each primary/backup temporary/replacement artifact present, then recreate the manager and reload. Also cover a deletion failure partway through and an active-slot pending write.

### CQ-06 — Keep physical release state synchronized during binding capture (resolved 2026-09-08)

**Location:** [InputRouter.cpp](../../../src/engine/InputRouter.cpp), lines 62–74; physical held-state processing in [Input.cpp](../../../src/engine/Input.cpp).

Binding capture suppresses key-up, gamepad button-up, and axis-motion events along with presses. If a control was already held before capture, its release never reaches `InputState`. Leaving capture can restore gameplay while the old physical control still appears held.

**Reproduction:** Route W-down normally; route W-up with `bindingCapture=true`; finish capture. The release is not forwarded, `keyDown(W)` remains true, and gameplay's up action remains down.

**Change:** Keep physical device state current while suppressing gameplay action generation. At minimum, preserve releases and axis neutralization; use a deliberate transition policy for presses held through capture. Handle both completion and cancellation, and avoid generating accidental menu/gameplay edges.

**Regression gate:** Press before capture, release during it, then finish/cancel for keyboard, controller buttons, and axes. Existing press-suppression assertions alone do not establish this invariant.

### CQ-07 — Preserve prepared skinned data until residency admission succeeds (resolved 2026-09-08)

**Location:** [VulkanModelResources.cpp](../../../src/engine/render/VulkanModelResources.cpp), lines 830–845.

Skinned publication moves `SkinnedMeshData` out of `slot.prepared` into `slot.skinnedSource` before checking the residency budget. When `makeModelResident()` declines admission, the code resets `skinnedSource` and returns while the slot remains `CpuReady`. The next attempt consumes the moved-from prepared mesh, sees empty geometry, and records a publication failure.

This is a source-confirmed retry defect. Temporary admission failures are supported by the residency design, including protected visible resources and bytes awaiting GPU retirement. The normal smoke did not create that pressure, and this review did not reproduce it on the GPU. The static-mesh branch checks admission without consuming its prepared mesh.

**Change:** Calculate the required size and admit before moving, or retain the shared prepared mesh intact across retries. Keep ownership changes aligned with load-state transitions. Admission denial must leave an equivalent retryable state.

**Regression gate:** At the actual publication seam, deny admission once, later permit it, and verify a nonempty skinned mesh uploads without re-decoding or becoming `Failed`. A residency arithmetic unit test cannot detect this ownership error.

### CQ-08 — Apply nonuniform normalization to the normal/tangent basis (resolved 2026-09-08)

**Location:** [GltfMesh.cpp](../../../src/engine/render/GltfMesh.cpp), `normalizedVertex`, lines 618–678; [GpuSkinning.cpp](../../../src/engine/render/GpuSkinning.cpp), `sourceNormalTransform`, around lines 70–87.

Default fitting divides position components by the corresponding bounds extents. When these extents differ, this is nonuniform scaling. Normal and authored tangent handling applies axis/orientation conversion but does not account for the same scaling. The normal can cease to be perpendicular to the surface, producing incorrect lighting. CPU and GPU paths sharing the omission can agree while both are wrong.

**Reproduction:** Load the included nonuniform triangle fixture with source bounds 4×2×1 and a valid non-axis-aligned normal. The dot product of the loaded normal with a loaded surface edge is `-0.447214`; it should be approximately zero.

**Change:** Represent the source-to-model linear transform once. Apply its inverse transpose to normals, its forward linear transform to tangents, then normalize/orthogonalize and maintain handedness. Handle degenerate bounds using the existing fitting policy. Share the convention between static loading, CPU skinning, and GPU skinning.

**Regression gate:** Check geometric perpendicularity under nonuniform fitting, authored and generated tangents, axis remapping, preserved scale/aspect options, and skinned bind poses. Retain CPU/GPU parity checks as an additional test, not the only oracle.

### CQ-09 — Stage external dependencies of GLB files (resolved 2026-09-08)

**Location:** [DecorationAssetRegistry.cpp](../../../src/engine/DecorationAssetRegistry.cpp), `meshFiles`, around lines 103–125.

Dependency collection returns the mesh alone whenever its extension is not `.gltf`. A GLB can still refer to external buffers. Registration can therefore publish a model whose geometry is unavailable in the runtime root.

**Reproduction:** Register the supplied GLB fixture that references `triangle.bin`. Registration succeeds, but the buffer is absent from the runtime directory and `loadGltfMesh` fails to open the buffer. The fixture and generation script are included with the review.

**Change:** Use the existing glTF dependency inspector/resolver shared with content staging for both container types. Stage all required files before committing a manifest entry. Also use structured image/material dependency records rather than recursively treating every JSON `uri` as a file dependency.

**Regression gate:** GLTF and GLB with external buffers, external images, embedded data, nested paths, and missing/invalid dependencies. Successful registration must imply the runtime loader can resolve all required data. Include index refresh under CQ-04.

### CQ-10 — Replace unused full-action history with bounded telemetry (resolved 2026-09-08)

**Location:** [GameplaySession.cpp](../../../src/engine/GameplaySession.cpp), line 466; [GameplaySession.hpp](../../../src/engine/GameplaySession.hpp), `historySize` and `moveHistory_`; [ApplicationDebugUi.cpp](../../../src/engine/ApplicationDebugUi.cpp), around line 880.

Every completed action is copied into `moveHistory_`, including undo. These records contain before/after states and presentation data. The production consumer only displays the vector's size; functional undo uses separate history. No production reader of these stored records was found.

**Reproduction:** Run 1,000 move/undo pairs. Functional undo contains zero entries while diagnostic history retains 2,000 complete actions. The vector also exists in Release builds.

**Change:** If the intended feature is a count, replace the vector with a counter. If action inspection is intended, make it an explicitly bounded debug history. Keep the actual undo history and checkpoint behavior unchanged unless a separate product decision changes them.

**Regression gate:** Long move/undo loops keep diagnostic storage bounded while the counter and functional undo semantics remain correct. The retained-action count was measured; no unsupported claim about exact memory bytes or frame-rate loss is made here.

### CQ-11 — Check validation errors after application teardown (resolved 2026-09-08)

**Location:** [main.cpp](../../../src/main.cpp), lines 134–149; `Application app` is still in the surrounding `try` scope.

The validation-error count is read after `app.run()` but before `Application` and the renderer are destroyed. Errors emitted during GPU cleanup can therefore arrive after the exit code has been decided. A lifecycle regression can pass the validation smoke gate.

**Change:** End the application scope before reading the accumulated validation count, while keeping logging and the process-wide counter available. Ensure early-return modes have intentional teardown/logging ordering too.

**Regression gate:** A controlled validation error during renderer teardown must yield the validation-failure exit code. The reviewed smoke log did not contain such an error; the defect is in what the gate would detect.

### CQ-12 — Roll back partially created worker threads (resolved 2026-09-08)

**Location:** [TaskSystem.cpp](../../../src/engine/TaskSystem.cpp), lines 90–100.

The constructor starts `std::thread` objects in a loop with no rollback. If a later thread creation throws after an earlier worker has started, constructor unwinding destroys joinable threads and calls `std::terminate`; the normal `TaskSystem` destructor never runs. Allocation of the vector up front does not prevent operating-system thread creation from failing.

**Change:** On partial construction failure, set the stop condition, notify, join all started workers, and rethrow. Alternatively use a cooperative `jthread` design whose stop/wakeup and member lifetimes make constructor unwinding safe. Merely replacing the type with `jthread` without adapting the blocking loop is insufficient.

**Regression gate:** Inject failure on creation of a later worker; assert earlier workers stop and the caller receives an exception without termination or deadlock. This failure was not injected into the live application during the review.

### CQ-13 — Retire or rebuild the stale headless compilation script (resolved 2026-09-08)

**Location:** Headless option and renderer branches in [CMakeLists.txt](../../../CMakeLists.txt); supported entry point in [CMakePresets.json](../../../CMakePresets.json); enforcement in [required-tests.yml](../../../.github/workflows/required-tests.yml).

The helper extracts `.cpp` lists from CMake but does not inherit target definitions, dependency targets, include directories, or the current test registry. It omits the required `SOKOBAN_ENABLE_DEBUG_UI` definition and newer cgltf/BC7 dependency wiring. Its 41-of-43-suite description is also obsolete. Extracting source filenames is not enough to keep a second build graph synchronized.

This conclusion comes from comparing the script with current compiler requirements; the Linux script was not executed on this Windows host.

**Change:** Prefer a real CMake headless/test-only option with renderer SDK discovery inside the renderer branch, preserving shared target definitions. Remove the bespoke script once a supported equivalent exists, or temporarily fail fast with an accurate deprecation message. Do not keep silently patching a second list of compiler/linker requirements.

**Regression gate:** A clean Linux environment without Vulkan/glslc configures and runs the supported headless target set; compare registered headless tests with CI. Document the small SDK-dependent subset accurately.

**Resolution:** The bespoke script was deleted instead of acquiring another partial copy of target metadata. `SOKOBAN_HEADLESS_TESTS_ONLY` forces tests on and developer/Vulkan smoke targets off, skips Vulkan and `glslc` discovery, and does not declare the renderer, application, content-staging, install, or package targets. Core and UI retain their ordinary source lists, dependencies, public `SOKOBAN_ENABLE_DEBUG_UI` definition, cgltf wiring, and BC7 target. Only the seven tests whose declared dependencies require the renderer or compiled shaders are absent; the README names them. The Linux `Headless tests (no Vulkan SDK)` matrix entry performs the clean configure, build, and complete CTest run without restoring or exposing the SDK.

### CQ-14 — Require successful bounded execution of the shipped executable (resolved 2026-09-08)

**Location:** [ValidateShippingPackage.ps1](../../../packaging/ValidateShippingPackage.ps1); fatal-error handling in [main.cpp](../../../src/main.cpp); process-result propagation in [Application.cpp](../../../src/engine/Application.cpp).

The package check launches the game, waits, and asserts that its process is still alive. It then closes/kills it. A blocked fatal-error dialog or startup hang can satisfy this check; it does not prove the live Vulkan window claimed by its comment. The final shutdown result is not checked.

**Change:** Launch the packaged executable with the existing bounded smoke mode and an isolated temporary save directory. Require completion within a timeout and a successful exit code, capture logs, and make smoke-mode fatal errors noninteractive so failures cannot wait indefinitely for a dialog. Keep manual visual/gameplay acceptance as a separate requirement.

**Regression gate:** A good package completes a bounded smoke; missing/corrupt content, initialization failure, and a hang fail with captured diagnostics. The script itself was inspected but a deliberately broken installed package was not launched in this review.

**Resolution:** The gate invokes the existing `--smoke-frames` mode for 240 frames under an isolated `--save-directory`, waits at most 60 seconds, requires exit code 0, and terminates an over-time process. Its native process wrapper drains stdout and stderr asynchronously, and failure messages include those streams plus the last 200 lines of each game log. Optional retained diagnostics feed the release-evidence bundle directly instead of reading a real player's profile. Smoke runs suppress both top-level fatal dialogs and renderer failure dialogs; renderer device/surface failures now propagate through `Application::run()` to a nonzero process result. The registered Windows package-gate integration test exercises a successful fixture, exit code 23 with captured output, a killed hang, missing and corrupt indexes, and the real executable failing during asset initialization without blocking.

## Maintainability, readability, reuse, and incomplete work

These recommendations are distinct from the defects above. They identify concrete seams and the benefit expected from changing them; they are not a request for a repository-wide rewrite.

### 1. Put transaction results and state transitions in types

CQ-01 through CQ-05 show that the central weakness is ambiguous completion: decoded, queued, drained, persisted, mirrored, and indexed are different states. `bool`, `void`, and incidental status strings cannot express all of them reliably. Introduce small result types at the persistence/publication boundaries: persisted revision and storage error; source commit and mirror/index status; retryable deferral versus permanent failure. Use exhaustive enum handling where it protects transitions. Keep UI wording outside the underlying error classification.

`AsyncSaveStore` also needs an explicit ownership/concurrency contract for public management operations. Its worker synchronization is not a blanket promise that callers may concurrently reconfigure and load channels. Document which operations are main-thread-only and which are safe producers; avoid adding locks indiscriminately.

**Progress, 2026-09-08:** The persistence portion is implemented. `AsyncSaveStore::PersistenceResult` distinguishes durable completion from a retained retryable failure and reports requested/durable revisions. Multi-channel flush returns one result per channel, channel replacement returns the outgoing channel's result, and `SaveSlotManager` exposes named progress/settings results. Slot switching handles the outcome enum directly, while shutdown logs the returned storage failure instead of consulting mutable diagnostics after the fact. CQ-03/CQ-04 already supplied `LevelEditor::SaveResult` for source, mirror, and index outcomes. Regression coverage verifies coalesced revisions, independent channel outcomes, retained failed revisions, rejected replacement, and successful retry.

### 2. Consolidate repeated domain interpretation

| Repeated logic | Current locations | Focused extraction |
| --- | --- | --- |
| Changed player/movable/enemy IDs | Centralized in `StateDelta`; consumed by `ActionScheduler` and `GameplaySession` | Completed 2026-09-08; canonical kind order is tested, append preserves existing entries, and overlap checks avoid per-action id-vector allocation |
| Manifest texture → source identity | Centralized beside `AssetManifest`; consumed by `ContentPipeline` and `RuntimeTextureCatalog` | Completed 2026-09-08; one tested conversion owns normalization, sampler, color-space, and texture identity fields while callers retain their distinct containment policy |
| Mesh dependency discovery | Centralized by CQ-09; consumed by decoration registration and distributable content staging | Completed 2026-09-08; shared structured discovery covers GLTF/GLB buffers and images while ignoring embedded data and unrelated URI fields |
| Source/model position, normal and tangent transforms | Centralized by CQ-08; consumed by static loading and CPU/GPU skinning | Completed 2026-09-08; shared transform and tangent-frame rules have nonuniform-scale parity coverage |
| Numbered level/screen naming and path interpretation | `LevelCatalog`, `LevelProjectStore`, `SplatPainter`, editor helpers | Share only identical domain rules; distinguish parsing, lexical normalization and canonical containment |
| Test assertions and temporary directories | `tests/TestHarness.hpp` versus remaining local helpers such as SaveSlotManager/PlayerProfile tests | Incremental migration to one useful harness and scoped temporary-directory utility |

Avoid generic utility abstractions that merely hide two short loops. Prioritize repeated logic whose disagreement changes saved data, asset identity, gameplay conflict detection, or package contents. Do not merge subtly different platform/path semantics just because the code looks similar.

### 3. Finish runtime material import deliberately

`VulkanModelResources::syncManifestModels` around lines 1712–1740 explicitly limits appended models to manifest/base-color bindings. Normal, metallic-roughness, emissive, and occlusion discovery occurs at startup and has no matching append path. This is an acknowledged incomplete feature, rather than a newly discovered undocumented algorithm error.

Choose a supported contract: incrementally run the same material/dependency resolver used at startup, or clearly tell the editor user which features require a restage/restart. Combine this with CQ-04; a restart is not a workable fallback if the editor has just invalidated the package index. Add an import fixture with several PBR maps and compare startup-loaded versus editor-appended bindings.

### 4. Refactor by ownership and responsibility, not file length

The largest implementation files include `VulkanSceneRecorder.cpp` (2,799 lines), `LevelEditor.cpp` (2,465), `Application.cpp` (2,089), `VulkanModelResources.cpp` (1,866), `VulkanRenderer.cpp` (1,837), and `IsoScenePreparer.cpp` (1,834). Their size increases review/navigation cost, but splitting a file alone does not improve invariants.

Good candidates are the ones already exposed by failures: editor publication/transactions, save lifecycle coordination, shared material resolution, and prepared-asset ownership. Keep the application as composition/flow orchestration and give persistent state to the component that owns its lifecycle. Scene recording's long argument/reference lists would benefit from cohesive frame/pass contexts with clear lifetimes; avoid replacing them with an unrestricted catch-all context.

Historical HANDOFF notes explicitly rejected a mechanical texture-store split after examining interleaved lifetimes. This review does not resurrect that change merely to shrink `VulkanModelResources`. Any extraction there should first make load-state transitions and retirement ownership easier to prove.

At call sites with several booleans, such as overlay/render-pass mode selection, use a small enum or named options structure when combinations have meaning. Preserve simple booleans when they are genuinely independent and readable.

### 5. Remove or repair demonstrably outdated material

- Replace the obsolete “Nothing reads it yet; F3b-2…” comment at `shaders/skinned_model.vert.glsl:32`; the material varying is now part of the fragment interface. Historical implementation phases obscure the current shader contract.
- HANDOFF references `codequality-review.html` and `enginereview.html`, neither present in the tracked workspace inspected here. Its chronological texture-extraction guidance also changes direction across sections. Replace the current-status section with a compact authoritative status and move history to a dated archive; preserve reasoning behind rejected changes.
- Remove `_to_delete/check_members_are_defined.sh` once any useful purpose has a maintained replacement. Its current location already marks it as disposal material.
- Reconcile the headless script's obsolete test counts and README/CI statements about configuration-specific smoke coverage. The Linux workflow enables application smoke on Debug; the Release matrix entry does not enable the same smoke options.
- Resolve the actual C4459 warning in `ApplicationDebugUi.cpp` and align Windows warnings-as-errors with the intended Linux policy. Avoid treating a build with warnings as warning-free because tests passed.
- Treat `.clang-tidy` exclusions as a named, owned backlog. Re-enable targeted categories after resolving their remaining findings. Replace stale measured-count comments as those counts change.
- Keep useful invariant comments; shorten narratives about abandoned experiments and migration chronology in production code. Do not mass-delete comments or apply a repository-wide formatter just to reduce line counts. `.clang-format` itself says the repository has not yet adopted a formatting pass; choose that policy separately.

Do not delete apparently unused art, shaders, or public helpers solely from a textual reference search. Manifests, content tooling, editor discovery, generated variants and external workflows can constitute real use. Require reachability evidence and a clean staged build for removals.

## Efficiency review

CQ-10 is a demonstrated avoidable retention problem. The following are concrete optimization candidates whose impact still needs measurement.

| Candidate | Reason to investigate | Useful measurement / acceptance condition |
| --- | --- | --- |
| Prepared asset memory/backpressure | CPU job slots can become available while decoded `CpuReady` payloads await limited GPU publication; GPU residency limits do not bound this memory | Track queued, decoding, prepared, uploading and resident bytes separately under large prefetch; impose a justified prepared-byte budget |
| Duplicate skinned packing | `publishModel` builds GPU vertex/index vectors to measure size, then `uploadSkinnedMesh` constructs upload data again | Reuse prepared packed data or calculate byte requirements without packing; count allocations and peak temporary bytes |
| Telemetry query cost | `FrameTimeTelemetry::summary()` copies/sorts the sample window for each query; renderer stats request many summaries | Count queries per frame and measure CPU time; cache summaries until samples change or sample debug statistics less often |
| Profile/undo copying | Checkpoints contain history, serialization normalizes copies, and asynchronous requests snapshot profiles | Benchmark save-request latency, serialization time, and peak memory late in a long level; optimize only after agreeing on undo persistence semantics |
| Repeated state diffs/ID vectors | Scheduler and gameplay derive overlapping changed-entity lists | Profile large entity counts and overlapping actions; derive once from the accepted plan/delta when lifecycle permits |
| Startup asset inspection | Lazy GPU loading still includes synchronous document/material discovery | Separate manifest/inspection, decode, upload and first-playable-frame timing; consider cached metadata only with reliable invalidation |

The existing frame arenas, scratch reuse, suballocators, draw sorting, shadow caching, compressed artifacts, upload scheduling, and residency tracking should be retained unless measurements justify changes. The normal smoke does not establish their performance across large scenes. Do not replace them with a new task graph or allocator architecture based only on this review.

## Coverage map and remaining uncertainty

| Area | Review emphasis | Outcome / remaining work |
| --- | --- | --- |
| Rules, actions, scheduling, campaign, transitions | State deltas, undo, concurrent action claims, restore boundaries | Existing test coverage is extensive; CQ-10 and duplicate changed-entity logic stand out. No exhaustive proof of every puzzle interaction was attempted. |
| Save/profile/settings/slots/atomic files | Migration, backup promotion, failures, switching, deletion | CQ-01, CQ-02 and CQ-05; prioritize failure sequences spanning several components. |
| Input, bindings, menu/UI integration | Physical state versus capture, routing, ownership | CQ-06; still needs real controller and interactive cancel/focus testing after a fix. |
| Level/overworld/decorations/animation authoring | Save guarantees, mirrors, staging, imports, runtime sync | CQ-03, CQ-04, CQ-09; appended PBR material handling remains incomplete. |
| Meshes, animation, skinning, materials, shaders | Transform conventions, dependencies, normal/tangent and CPU/GPU correspondence | CQ-08; fixtures need non-axis-aligned/nonuniform cases. No claim of exhaustive animation/glTF conformance. |
| Vulkan resources, scheduling, descriptors, retirement | Retry ownership, admission, upload/publication, frame-lifetime ordering | CQ-07 and CQ-11; pressure and teardown injection were not exercised on hardware. |
| Core utilities, tasks, memory, diagnostics, audio | Construction/destruction, bounded storage, thread contracts | CQ-12; telemetry candidate above. No complete audio-device or crash/minidump fault matrix was run. |
| Build, tests, CI, packaging, documentation | Shared target graph, flags, source drift, observable release gates | Actionable findings resolved; warning-policy and stale-documentation cleanup remain. Linux/toolchain and installer execution remain unverified here. |

Potential concerns were not promoted to findings when existing code supplied the missing invariant. In particular, the renderer's later color-output/overlay ordering matters when evaluating swapchain synchronization; a transfer operation alone was not treated as proof of a semaphore-stage bug. Similarly, storage designed for stable references was not labeled invalid merely because a container grows. Findings require a supported failure path, not just a suspicious isolated line.

## Suggested implementation sequence

1. **Protect player and author data:** CQ-01, CQ-02, CQ-03 and CQ-05. Establish explicit storage outcomes and small deterministic regression tests. Preserve existing formats and public behavior where possible.
2. **Unify editor publication:** CQ-04 and CQ-09, then decide the appended-material contract. Test a complete stage/edit/restart cycle. This is a coherent boundary change, not several unrelated file helpers.
3. **Repair runtime invariants:** CQ-06, CQ-07 and CQ-08 with focused state/retry/geometric tests. These can be reviewed separately from persistence changes.
4. **Make the release gates trustworthy (completed):** CQ-13 and CQ-14; align warning policy and current documentation with what actually runs.
5. **Do measured cleanup:** shared identity/delta helpers, stale artifacts/comments, incremental test-harness consistency, and performance experiments from the table above. Avoid broad churn while correctness changes are under review.

Each packet should include a concrete failing-before/passing-after regression and one focused reviewable implementation. Rerun the relevant tests during development and the full Debug/Release suites before completion. Add sanitizer/tidy/platform gates where required by the affected code, rather than repeatedly running every expensive check after cosmetic changes.

## Reproducing the included probes

The [probe harness](probes/CMakeLists.txt) links the real Windows/MSVC Debug `sokoban_core`, `sokoban_ui`, SDL and BC7 libraries from the repository's existing `build` directory. It assumes the usual Debug developer-tools/test-hook configuration. It is a review diagnostic, not a registered regression suite: several cases intentionally demonstrate incorrect behavior, and a zero process exit code means the diagnostic completed, **not** that those behaviors are correct. Inspect its labeled output and convert the cases into assertions when implementing fixes.

From the repository root, after a normal configured Debug build:

```powershell
cmake --build build --config Debug --parallel 4
python docs/reviews/2026-09-03-code-quality/probes/make_fixtures.py build/review/fixtures
cmake -S docs/reviews/2026-09-03-code-quality/probes -B build/review/preserved-probes
cmake --build build/review/preserved-probes --config Debug --parallel 4
build/review/preserved-probes/Debug/review_probes.exe build/review/new-probe-output build/review/fixtures
```

Use a Python 3 executable available on your machine; this review used the bundled runtime because `python` was absent from PATH. The output directory must not already exist; the harness refuses to overwrite it. Keep the working directory at the repository root so it can copy the project manifest. All edited saves, levels and imported models are created below that new output directory. The atomic-write failure hook is linked only in this isolated diagnostic process.

The recorded evidence is for the revision at the top of this report. Its timestamps and absolute temporary paths reflect the local run; they are not application fixtures or new runtime dependencies.
