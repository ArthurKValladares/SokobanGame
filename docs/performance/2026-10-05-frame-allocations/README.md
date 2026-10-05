# Frame allocation audit — October 5, 2026

The measured steady frame paths below perform **zero C++ heap allocations
after warm-up**. Frame-only collections use `FrameArena` / `ArenaArray`; tiny
formatted names use stack buffers. Data that survives frames stays in owning
storage and is refreshed when its source changes.

This is not a claim that the entire process is allocation-free. Developer
inspectors and action/event boundaries still allocate, as detailed below.

## Changes

| Path | Storage / change |
|---|---|
| Gameplay render | Cached committed/projected mirror previews; arena-backed beam interpolation; borrowed projected state; stack-formatted splat names. |
| Selector preview | Shares the current render arena with the main scene; borrows projected state and cached mirror previews. Scene preparation and asset-requirement buffers retain capacity. Two-scene preparation uses bounded task coordination instead of futures. |
| Gameplay update | Cached progress/projected states and unsuccessful planning attempts. Failed plans refresh on scheduler changes, mechanical leg boundaries, reservation steps, timing settings, admission-policy changes, or controller changes. |
| Overworld render | Visible screen lists and region scratch use the render arena; fog volumes append directly to frame storage. Visibility callbacks borrow the list. |
| Editor render | Arena-backed neighbors; decoration transforms apply the origin without copying strings; gates borrow metadata; link groups and minecart routes persist until their exact source records change. Sightlines stream segments and use constant-space cycle detection. |
| Animation | Instance requests use a dedicated arena. GPU pose publication uses fixed open-addressed tables, with 512 slots for each 256-instance frame budget, reset only when the frame slot is reused. |
| Menus / prompts | UI layout nodes use arena storage with index-linked children; row geometry, choices, glyph scratch and binding labels share the UI arena. Glyph lookup uses string views and stack names. Save-slot labels use stack formatting. |
| CPU capture | The 240 historical slots retain event/thread/name storage. Hot-path analysis uses arena indices and scratch instead of temporary maps and vectors. |

The render arena keeps two alternating frame lifetimes, preserving the prior
prepared frame for picking. Each arena reserves two `renderFrameArenaBytes()`
budgets for the main scene and selector preview; each scene budget includes
2 MiB of scratch. Both scenes are built after one reset, on the main thread,
before scene preparation begins. The UI
arena has an additional 128 KiB for layouts and labels; a live layout permits
256 nodes. Animation requests reserve the render tile budget. Arena paths
have no heap fallback; the allocation tests also check arena exhaustion.

References to cached state, bindings, controller names, and editor data must
be consumed before their owners change. Arena arrays and labels expire at the
next arena reset. Owning/offline APIs remain available for snapshots and tests;
copying an arena-backed `FrameArray` still creates an owning heap copy.

## Measured coverage

`ScenePreparationAllocationTests.cpp` overrides scalar, array, aligned and
sized C++ allocation operators. Each fixture warms its retained storage, then
measures 64 iterations. Profiler capture warms all history slots twice first.
The tests use the shipped manifest, prompt atlases, font and overworld map.

All of these report zero allocations and zero allocated bytes:

- Serial and parallel scene preparation, and task-system coordination.
- Idle/moving gameplay updates, held movement retries, and gameplay rendering
  with committed/projected mirror previews.
- Main scene and selector preview construction together, parallel preparation,
  and merging their asset requirements.
- Animation preview scene construction and instance request collection.
- Normal/overview overworld visibility and fog construction.
- Editor rendering with linked gates, a minecart route, turret debug
  sightlines, neighbor gates and a decoration with a long model name.
- Input routing for gameplay, options and editing.
- All six options pages; keyboard and five controller theme presentations;
  selector fallback labels with all key modifiers.
- Title, save-slot and delete-confirmation pages.
- CPU profiler capture, including nested long scope names.

Behavior checks cover cache refresh after world commits, resets, admission
policy changes, rail edits, link-color edits and undo. Existing scheduler,
replay, rules, presentation, UI and editor suites exercise their shared APIs.

See [allocation measurements](evidence/allocations-dev-fast.txt) and
[regression results](evidence/ctest-dev-fast.txt).

## Remaining allocation boundaries

- New action planning, state deltas, reservations, undo history and presentation
  timelines own state across frames. Plan creation/commit and structural entity
  changes can allocate. The steady movement checks stay within an action's
  interpolation interval; they do not assert zero allocations at every action
  boundary.
- `GameplayLoop::UpdateResult` sound, turret-shot and swap event collections
  still own their event data. Emission can allocate on the triggering frame.
  Turret shots contain owning nested segment vectors and cannot be placed
  directly into this arena, which requires trivially destructible elements.
- Developer inspector UI is not allocation-free. `ProfilerDebugUi.cpp` copies
  the latest capture, constructs flame-chart maps and phase vectors, and builds
  performance-analysis text. `LogDebugUi.cpp` builds filtered index lists and
  formatted entries. `LevelEditorDebugUi.cpp` and the asset inspectors still
  format owning labels. These require a separate inspector scratch/view pass
  before claiming zero allocations with the whole developer workspace visible.
- Asset loads/publication, scene transitions, save operations, source-watcher
  filesystem scans and evidence export are outside the steady frame checks.
- The counter observes C++ `new`, including worker threads. It does not measure
  SDL/ImGui C allocation functions, audio backend allocation, Vulkan drivers or
  OS allocations. Renderer device smoke tests check behavior, not a complete
  process allocation trace.

## Validation

The optimized developer and Release builds each pass the allocation gate
(2,942 checks) and 93/95 CTest suites, including both Vulkan smoke tests.
The two failures are
`asset_manifest_editor` (external audio import) and `content_pipeline`
(`audio/missing.ogg` containment). Both reproduce with the unchanged Release
test executable that predates this pass; baseline outputs are included in
[evidence](evidence/).

A Debug run with required Vulkan validation renders 240 animated frames,
finishes successfully, and reports zero dropped draws and skinned poses.
The log contains the existing missing Epic overlay manifest loader message and
shader-output-not-consumed warnings; it contains no VUID validation failures or
arena-exhaustion warnings. This GPU run renders the main view; the simultaneous
selector preview is covered by the CPU allocation and preparation fixture.
See [validation render log](evidence/validation-render.txt),
[Release allocation measurements](evidence/allocations-release.txt), and
[Release regression results](evidence/ctest-release.txt).
