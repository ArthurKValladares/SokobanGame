# Sokoban 3D — Engine and Shipping Code Review

**Review date:** 2026-08-24  
**Scope:** Entire repository, with emphasis on the engine work required to ship a modern 3D game.  
**Target inferred from the repository:** Windows desktop, SDL3, Vulkan, C++20.

## Executive summary

This is a solid bespoke engine for a small 3D puzzle game. Its deterministic
gameplay architecture, save-versioning discipline, editor tooling, content
reachability checks, and breadth of headless tests are notably mature.

It is not quite ship-ready. The initial review found two immediate release
gates; both P0 items are now fixed. Several reliability defects remain, along
with renderer/asset-system choices that work for Sokoban-sized scenes but will
not scale to a more content-heavy modern 3D game.

The right course is to harden the current architecture, not replace it or add
an ECS merely for convention. The highest-priority work is:

1. Correct swapchain acquisition synchronization.
2. Restore a fully green Debug and Release test matrix.
3. Make `TaskSystem::parallelFor` exception-safe.
4. Bound simulation catch-up after stalls and suspend/resume.
5. Make save replacement and deletion genuinely reliable.
6. Validate the complete runtime content package.
7. Add Vulkan capability tiers, diagnostics, and recovery.
8. Modernize asset publication, GPU memory allocation, skinning, and draw
   batching before expanding scene complexity.

## Validation performed

- Debug build: succeeded.
- Release build: succeeded.
- Staged runtime content: 196 files, 27,593,015 bytes.
- Initial Debug tests: 49 of 50 passed.
- Initial Release tests: 49 of 50 passed.
- Initial failing suite: `content_pipeline`, with Windows fast-fail exit code
  `0xc0000409` in Release.
- Current Debug tests after the P0 fixes: 50 of 50 passed.
- Current Release tests after the P0 fixes: 50 of 50 passed.
- Production compilation emitted no warnings during the review.
- `tests/GameplayLoopTests.cpp` emitted three ignored-`[[nodiscard]]`
  warnings.
- No source files were changed during the review itself.

## What is already strong

- Gameplay rules and presentation are separated well enough for deterministic,
  headless tests.
- Campaign/session/action scheduling code has explicit state and strong test
  coverage.
- Profiles use a versioned codec and forward migrations.
- Save writes use a temporary-file/replacement design and retain backups,
  providing a good foundation even though durability details need work.
- The asset manifest and content pipeline identify reachable runtime content
  rather than blindly packaging the complete source tree.
- The renderer is decomposed into focused Vulkan components rather than being
  one monolithic renderer class.
- The level editor has increasingly headless interaction and storage seams.
- Fifty registered suites cover most engine policy without requiring a Vulkan
  device.
- Logging and saving already move potentially blocking work off the main
  thread.

## Findings

### P0 — Swapchain synchronization is incorrect in the normal release path

**Status: Fixed on 2026-08-24.** The acquire semaphore now waits at both
`TRANSFER` and `COLOR_ATTACHMENT_OUTPUT`, covering the shipping upscale path
and the developer-workspace path without imposing an `ALL_COMMANDS` stall.

The image-available semaphore previously waited only at
[`VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT`](src/engine/render/VulkanRenderer.cpp#L370).

In the normal non-editor path, the first use of the acquired swapchain image
is a transfer operation:

- `VulkanSceneRecorder` calls
  [`upscaleSceneToSwapchain`](src/engine/render/VulkanSceneRecorder.cpp#L265).
- The acquired image is transitioned to transfer destination in
  [`VulkanSwapchainResources`](src/engine/render/VulkanSwapchainResources.cpp#L502).
- Its first access is identified as
  [`VK_PIPELINE_STAGE_2_TRANSFER_BIT`](src/engine/render/VulkanSwapchainResources.cpp#L507).

A semaphore wait scoped to color output does not prevent earlier transfer-stage
commands from executing. The renderer can therefore touch a swapchain image
before acquisition completes.

**Required fix:** Wait at the earliest applicable stage—`TRANSFER` for this
path, or conservatively `ALL_COMMANDS`. Validate both editor and release frame
paths with synchronization validation enabled.

### P0 — The committed test matrix is not green

**Status: Fixed on 2026-08-24.** The fixture now creates all seven code-owned
input-prompt atlas XML files plus the Kenney license, asserts that representative
prompt content is staged, and reports unexpected standard and non-standard
exceptions at the test entry point. All 50 tests pass in Debug and Release.

The content builder now requires seven input-prompt atlases in
[`ContentPipeline.cpp`](src/engine/ContentPipeline.cpp#L238), but
[`createValidContent()`](tests/ContentPipelineTests.cpp#L159) does not create
them. The first inventory build throws.

The test's [`main()`](tests/ContentPipelineTests.cpp#L539) has no exception
boundary, so the missing-file exception terminates the process without a useful
diagnostic. Release reports `0xc0000409`; the Debug invocation also left a
child process running after the harness reported failure.

**Required fix:** Add all required prompt assets to the fixture. Catch and
print unexpected exceptions at the test entry point so future fixture drift
produces an actionable failure message. Gate releases on both Debug and
Release tests.

### P1 — `TaskSystem::parallelFor` is not exception-safe

**Status: Fixed on 2026-08-24.** Parallel work now uses heap-owned shared
coordination state, stops assigning chunks after the first failure, waits for
every queued helper, and rethrows the captured exception on the caller. Tests
cover inline, caller-thread, and worker-thread failures plus pool reuse.

[`parallelFor`](src/engine/TaskSystem.cpp#L60) previously created worker lambdas that
capture its local `runChunks` closure and latch by reference.

Two fatal cases follow:

- If the callback throws on a worker, [`task()`](src/engine/TaskSystem.cpp#L55)
  lets the exception escape the thread, causing `std::terminate`. The latch is
  never decremented.
- If the callback throws on the calling thread at
  [`runChunks()`](src/engine/TaskSystem.cpp#L96), `parallelFor` unwinds while
  queued helpers still reference destroyed stack variables, producing a
  use-after-free.

CPU skinning uses this facility, so malformed animation data or a future
failure inside the skinning path can terminate the game.

**Required fix:** Move coordination state to shared storage, capture the first
`std::exception_ptr`, guarantee latch completion with RAII, wait for every
helper, and then rethrow on the calling thread. Add inline-chunk and
worker-chunk exception tests. A worker-loop exception boundary should also
prevent a defective raw task from killing the process.

### P1 — Unbounded frame delta can fast-forward gameplay

**Status: Fixed on 2026-08-24.** Active frame deltas are capped at 100 ms.
SDL minimize and application-background events set independent, thread-safe
suspension reasons; simulation receives zero delta while suspended and on the
first frame after every lifecycle transition. Tests cover long frames,
minimize/restore, overlapping background/minimize state, and a complete
suspend/resume cycle between rendered frames.

[`FrameTimer::tick()`](src/engine/Time.hpp#L89) previously returned the entire wall-clock
interval. [`Application`](src/engine/Application.cpp#L394) passes it directly
into simulation, and [`GameplayLoop`](src/engine/GameplayLoop.cpp#L184)
deliberately consumes the full delta in a catch-up loop.

After a debugger stop, suspend, long stall, minimized window, shader operation,
or device operation, one frame can commit multiple actions and advance:

- held or ambient movement;
- animation timelines;
- particles and transitions;
- gameplay elapsed time; and
- audio cadence.

Input is cleared on focus loss, but there is no simulation pause, timer reset,
minimized-window throttle, or catch-up ceiling.

**Required fix:** Reset the timer on focus/suspend transitions and clamp
real-time delta immediately. The scalable solution is a fixed-step simulation
accumulator with a maximum catch-up count and a separate presentation clock.

### P1 — Atomic saves can install data before it is fully flushed

[`AtomicFile::write`](src/engine/AtomicFile.cpp#L42) writes the temporary file
and calls `replace()` while the output stream is still open.

Consequences:

- Buffered output may not be flushed until after the file becomes the live
  save.
- A close/flush failure is not observed before replacement.
- There is no `FlushFileBuffers`/`fsync`, so success does not mean the data
  survived a power failure.
- The compatibility fallback moves the live destination to `.replace-old`
  before installing the temporary file. A crash between those operations
  leaves the primary path absent, and startup does not recover `.replace-old`.

**Required fix:** Explicitly flush and close the temporary file, verify the
result, and only then replace the destination. Use `ReplaceFileW` or
`MoveFileExW(..., MOVEFILE_WRITE_THROUGH)` on Windows and `fsync` plus directory
synchronization on POSIX. Recover `.tmp` and `.replace-old` artifacts at
startup.

### P1 — Save deletion silently succeeds when filesystem deletion fails

[`SaveSlotManager::deleteSlot`](src/engine/SaveSlotManager.cpp#L285) ignores
errors from deleting the primary and backup files and immediately marks the
summary cache empty.

For the active slot, [`Application`](src/engine/Application.cpp#L1060) resets
the in-memory campaign before deletion. If a file is locked, read-only, or
inaccessible:

- the UI reports an empty slot;
- the old file remains;
- deleted progress returns after restart; and
- subsequent play may resurrect or overwrite the supposedly deleted slot.

**Required fix:** Return a result or throw on deletion failure. Mutate the
cache and active campaign only after both files have been removed or confirmed
absent. Surface a recoverable UI error.

### P1 — The runtime content index is mostly decorative

The package writer emits format, game version, file count, total size, and
every file entry in [`ContentPipeline.cpp`](src/engine/ContentPipeline.cpp#L662).
Startup reads only format and game version in
[`RuntimeContent.cpp`](src/engine/RuntimeContent.cpp#L28).

A package can therefore launch with missing or truncated files, incorrect
sizes, or a truncated index, then fail during a later level transition or
render operation.

**Required fix:** Parse and validate the complete index. Check inexpensive
metadata at boot and larger files before publication. If corruption or tamper
detection matters, store hashes instead of sizes alone. A versioned packed
content archive is a sensible longer-term evolution.

### P1 — Vulkan support is unnecessarily narrow and lacks recovery

[`VulkanDeviceContext`](src/engine/render/VulkanDeviceContext.cpp#L182)
requests Vulkan 1.4 and rejects anything below it. Device selection also makes
the full push-constant footprint, `fillModeNonSolid`, cube arrays, extended
dynamic state, synchronization2, and dynamic rendering mandatory in
[`isDeviceSuitable`](src/engine/render/VulkanDeviceContext.cpp#L401).

That can be acceptable for a controlled hardware target, but it is too rigid
for broad desktop distribution. Debug wireframe support should not disqualify
an otherwise release-capable GPU.

Related issues:

- [`compositeAlpha`](src/engine/render/VulkanSwapchainResources.cpp#L660) is
  set to opaque without checking `supportedCompositeAlpha`.
- There are no `VK_ERROR_DEVICE_LOST` or `VK_ERROR_SURFACE_LOST_KHR` recovery
  paths.
- Validation can be compiled into every configuration despite being described
  as a Debug option.
- There is no debug-utils messenger, GPU object naming, command-buffer markers,
  or GPU timestamps.
- There is no Vulkan pipeline cache.

**Required fix:** Document a minimum GPU contract and introduce feature tiers.
Make debug-only features optional, choose only supported surface modes, and add
graceful device-loss reporting. Add debug-utils integration, GPU timestamps,
and pipeline caching before undertaking larger rendering optimization.

### P2 — Asset loading still blocks the render thread

[`VulkanModelResources::ensureAssets`](src/engine/render/VulkanModelResources.cpp#L188)
starts asynchronous requests and then publishes every required animation,
model, and texture with `wait=true`.

Preloading masks this for known transitions, but a cache miss or dynamically
introduced asset blocks the render thread on glTF parsing, image decoding,
animation parsing, and GPU resource publication.

There are no explicit loading states, placeholders, cancellation, priorities,
memory budgets, or eviction.

**Required fix:** Preserve the existing request/publish split but make
requirement resolution non-blocking. Expose pending/ready/failed states and let
a loading state continue rendering while publication completes incrementally.
Add per-category residency accounting before the asset set grows substantially.

### P2 — GPU memory and animation architecture will not scale

Static mesh upload in
[`VulkanModelResources::uploadMesh`](src/engine/render/VulkanModelResources.cpp#L790):

- allocates separate vertex and index buffers for every mesh;
- performs a dedicated `vkAllocateMemory` for every buffer; and
- stores static geometry in host-visible coherent memory rather than
  device-local memory.

Skinned models in [`SkinnedMeshUpdater`](src/engine/render/SkinnedMeshUpdater.cpp#L84):

- skin every vertex on the CPU each frame;
- allocate dedicated host-visible buffers per animated instance/frame slot;
- map, copy, and unmap the complete vertex stream each update; and
- retain instance resources without an active-instance pruning pass.

Rendering issues one `vkCmdDrawIndexed` per visible model in
[`VulkanSceneRecorder`](src/engine/render/VulkanSceneRecorder.cpp#L1779),
without mesh/material batching or instancing.

This is adequate for the current board sizes. The recommended upgrade path is:

1. Suballocated device-local static vertex/index heaps with staging uploads.
2. Persistently mapped per-frame upload rings.
3. Joint-matrix skinning in the vertex shader or compute shader.
4. Opaque draw sorting by pipeline, material, and mesh.
5. Instancing for repeated board pieces and decorations.
6. Indirect drawing only if profiling later justifies it.

A render graph or meshlet system is not the next priority; allocation,
skinning, and batching will provide most of the benefit first.

### P2 — Frame pacing is incomplete

[`vsync`](src/engine/UserSettingsConfig.hpp#L9) defaults to false. Present mode
selection uses mailbox, otherwise immediate, otherwise FIFO in
[`choosePresentMode`](src/engine/render/VulkanSwapchainResources.cpp#L870).

There is no CPU frame limiter or minimized/unfocused throttle. On hardware
without mailbox, the default can become uncapped immediate presentation,
wasting CPU/GPU power and producing uneven pacing.

VSync is persisted and read during renderer construction, but it is not
exposed in the current options UI and has no runtime reconfiguration path.

**Required fix:** Implement an explicit presentation policy covering FIFO,
mailbox, optional tearing, a configurable frame cap, and unfocused/minimized
throttling. Add CPU and GPU frame-time telemetry.

### P2 — Release packaging remains developer-oriented

[`add_executable(sokoban ...)`](CMakeLists.txt#L310) creates a console
executable. The Windows package also lacks:

- icon and version resources;
- `SDL_SetAppMetadata`;
- an installer and upgrade/uninstall path;
- code signing;
- crash dump/minidump collection;
- symbol packaging or a symbol-server process;
- a fatal user-facing error dialog;
- an LTO/IPO release option; and
- a defined shader optimization/reflection policy.

CPack emits a ZIP only, and the project version is still `0.1.0`.

**Required fix:** Create a reproducible shipping preset that excludes
editor/debug code, retains usable external symbols, packages notices, and can
be smoke-tested from a clean machine without a Vulkan SDK or source checkout.

### P2 — CI and diagnostic coverage trail the engine's maturity

The fifty headless suites are a strong foundation, but there is no repository
CI configuration. Documentation and auxiliary tooling have drifted:

- [`README.md`](README.md#L64) says 39 suites.
- [`tools/build_headless_tests.sh`](tools/build_headless_tests.sh#L6) says 41
  of 43 suites.
- CMake currently registers 50.

Important missing gates include:

- ASan/UBSan where supported;
- static analysis;
- fuzzing for saves, levels, JSON, glTF, and the content index;
- a real Vulkan device/swapchain smoke test;
- validation-layer-clean render runs;
- shader interface/reflection checks;
- suspend/resume, minimization, DPI, monitor, and audio-device-change tests;
  and
- GPU/driver matrix testing.

Fix the three ignored-`[[nodiscard]]` test warnings and consider warnings as
errors in CI.

### P2 — Accessibility settings are mostly placeholders

`highContrast`, `largeText`, `subtitles`, and `screenShake` are versioned and
persisted but have no runtime consumers outside serialization and tests. Only
`reducedMotion` changes behavior through
[`SettingsCoordinator`](src/engine/SettingsCoordinator.cpp#L28).

**Required fix:** Either implement these settings before exposing them or
defer them from the public schema. Shipping accessibility should also cover UI
scaling, clear remapping conflicts, color-independent puzzle cues,
screen-shake intensity, subtitle presentation, and pause-on-focus-loss.

### P3 — Lower-severity cleanup

- [`FrameArena`](src/engine/FrameArena.cpp#L35) underflows
  `available - padding` in its error message when alignment padding exceeds
  remaining capacity. Allocation still fails correctly, but the diagnostic
  reports a huge bogus byte count.
- [`Log`](src/engine/Log.cpp#L334) appends forever to one `log.txt`. Add bounded
  rotation and session/build metadata.
- [`AudioSystem::available`](src/engine/AudioSystem.cpp#L161) defines overall
  audio availability as engine initialization plus loaded footsteps,
  conflating subsystem health with one sound category.
- Audio supports graceful no-device startup but not device-loss/reconnect,
  buses, voice priority, or concurrency limits.
- Runtime glTF parsing is reasonable at the current scale. A larger game
  should move toward cooked binary meshes, compressed textures, dependency
  metadata, LODs, and pack-file streaming rather than continually expanding
  the source-format runtime loader.
- `Application`, `RenderFrameBuilder`, `LevelEditor`, and
  `VulkanSceneRecorder` are large but mostly cohesive. File size alone is not
  a reason to split them. The valuable boundaries are platform lifecycle,
  simulation timing, loading state, and renderer recovery.

## Recommended implementation sequence

### Phase 0 — Restore correctness and a reliable release gate

1. [Complete] Fix the swapchain semaphore wait stage.
2. [Complete] Repair the content-pipeline fixture and test exception reporting.
3. [Complete] Make `parallelFor` exception-safe.
4. [Complete] Clamp/reset simulation timing and add suspend/minimize tests.
5. Make both Debug and Release test runs mandatory and green.

### Phase 1 — Persistence and startup reliability

1. Flush, close, and durably replace save files.
2. Recover interrupted `.tmp` and `.replace-old` writes.
3. Report save deletion failures and preserve live state on failure.
4. Parse and validate the complete content index.
5. Add corruption, interrupted-write, permissions, and disk-full tests.

### Phase 2 — Renderer hardening

1. Select only supported surface and composite-alpha modes.
2. Establish Vulkan feature tiers and reduce debug-only requirements.
3. Add validation messenger integration and GPU object names.
4. Add GPU debug markers and timestamp queries.
5. Persist a Vulkan pipeline cache.
6. Provide user-facing diagnostics for device/surface loss and unsupported
   hardware.

### Phase 3 — Rendering and asset scalability

1. Make asset readiness non-blocking.
2. Add loading-state presentation, cancellation, priorities, and budgets.
3. Introduce device-local suballocated geometry storage and staging uploads.
4. Add persistently mapped upload rings.
5. Move skinning to the GPU.
6. Sort and instance repeated opaque draws.
7. Add resource residency and frame-time telemetry before further
   optimization.

### Phase 4 — Productization

1. Add CI, sanitizers, static analysis, fuzzing, and Vulkan smoke tests.
2. Create an editor-free shipping preset with LTO and external symbols.
3. Add application metadata, icon/version resources, installer, and signing.
4. Add crash dumps, rotating logs, and actionable fatal-error UI.
5. Complete frame-pacing controls and minimize/unfocused behavior.
6. Implement or remove placeholder accessibility settings.
7. Validate the final package on clean supported machines and GPU drivers.

## Ship assessment

After Phase 0 and Phase 1, this engine should be reliable enough to ship the
current small-scale Sokoban game to controlled hardware. Phase 2 and the
productization portions of Phase 4 are needed for a credible public desktop
release. Phase 3 becomes mandatory before substantially increasing asset
volume, animated character count, scene complexity, or world size.

The codebase does not need a wholesale engine rewrite. Its strongest seams—
deterministic gameplay, explicit presentation data, content manifests,
versioned persistence, and focused Vulkan components—are the right foundation
for the required work.
