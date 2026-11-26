# Sokoban 3D handoff

Updated 2026-11-25. This file describes current operating guidance, implementation
contracts and known limitations. [README.md](README.md) covers player controls,
authoring formats, supported commands and packaging.

## Current status

Sokoban 3D is a C++20, SDL3, Vulkan 1.3 project. Runtime content is declared by
`assets/manifest.json`, staged by `sokoban_content`, and validated against
`content.index` at startup.

Gameplay includes six puzzle hero types, pushable mirrors and copies, linked movable
objects, portals, buttons, persistent levers, rotators, lock plates, elevators, minecarts, and
lecterns. Gameplay and solution replay share the production action planner.
Developer builds expose transactional level/asset/animation editors, live tuning,
shader reload, profiling and source watching.

The Maker is the player-facing editor for puzzle and overworld drafts, opened
with Make on the title screen. Its storage and rules are in The Maker below.

The player UI shapes UTF-8 with HarfBuzz and SheenBidi, uses FreeType coverage
for small text and analytic outlines for large text, and supports inline vector
and texture icons. See [docs/text-rendering.md](docs/text-rendering.md).

CTest is the source of truth for suite availability: use
`ctest --preset dev --show-only`. SDK-free builds omit renderer/device suites;
the package gate is Windows-only. Keep counts out of scripts and operating
instructions.

See Current validation and limitations below for the latest local baseline.
Real-device, display, controller, signing, upgrade and uninstall acceptance
remains release-signoff work in
[packaging/ReleaseValidation.md](packaging/ReleaseValidation.md).

## Current validation and limitations

- Windows, 2026-11-24: the owner built `de8fba92` with MSVC 19.51 and
  reported that the build works. `dev-asan-check` passed all 139 tests at that
  commit, its first complete run. Later that day the owner built the `dev`
  preset with the parallel texture catalog scan and the `shadow.vert.glsl`
  rewrite and ran it; the scan took 0.5 s where it had taken 5.9 s. No test
  run was reported for that build. On 2026-11-25 the owner built `83af62f3`,
  which holds the optional renderer work of that day (bloom sampling the
  scene target in place, pipelines kept across a resize, the dead code
  removed, the energy-material helpers, the long scene-recorder functions
  split), and reported that everything looks fine and that all CI steps
  pass. Later that day the owner built `09c9c868`, which holds the cleanup
  that followed (the pipeline factory's named shader modules,
  `DrawInstanceBudget`, `appendTileFaces`, the one-shot helpers in the UI
  image upload and the ground chunk upload, three reformatted files), and
  reported that the build works. No test run was reported for it. Five
  shader files changed after that commit and have not been built on Windows:
  the alpha clamp in `mirror_energy.frag.glsl` and the explicit mip level in
  the shadow and atmosphere lookups.
- Asset streaming and startup, 2026-11-25: the per-frame publication
  budgets, the three load priorities, upload-ring backpressure, the startup
  content overlap, the lazy decoration mesh scan, the exact ground and cliff
  bodies and the overworld crossing warm were built on Ubuntu 24.04 with GCC
  13 and lavapipe as `ci-debug`, warnings as errors, on top of the island
  mist change below. All 140 tests passed, and a 100-frame
  `--require-validation` run of a new game reported nothing.
  `level_editor_debug_ui` has failed once in an earlier run on
  `fencePaletteBuildsSeparatePartsAndKeepsRandomPreviewStable`, on the
  unchanged tree as well: "Randomize Fences" can draw the arrangement it
  started with. The changed engine files were also compiled, not linked or
  run, with the `ci-release` flags, where developer tools are off. None of
  this has been built on Windows or run on a hardware GPU. Measured on
  lavapipe under validation with a Debug build, counting frames because a
  software rasterizer's milliseconds mean nothing: level 5 screen 4 showed
  its 59 visible assets after 25 frames where it had taken 283, and its
  queue drained after 147 assets and 68 frames where it had been 309 assets
  and 309 frames; a new game's overworld showed its 140 visible assets after
  64 frames where it had taken 156, and spent 21 more on the 52 assets of
  the crossing warm and the first screen's puzzles. A crossing on the
  shipped map brings 9 to 32 models and two or three textures into view
  that the settled frame had not asked for; those are what the warm loads
  ahead. Resolving one drawn set took at most 12 ms in that Debug build,
  the first plan of a session had seven sets and later ones none to three.
  A publication cost about 2 ms on lavapipe. The owner's Debug evidence
  runs record about 0.7 ms on the RTX 4060, so the same budgets should
  publish more per frame on that machine; the `Visible assets ready` log
  line reports what it actually does.
- Island mist, atmosphere reconstruction and cloud light, 2026-11-25: the
  cloud light was built as `ci-debug` on Ubuntu 24.04 with GCC 13, on top of
  the mist, the reconstruction and the asset-streaming change above, with
  Ubuntu's Vulkan 1.3.275 headers and shaderc 2023.8 in place of the pinned
  SDK, and run on lavapipe. All 141 tests passed, and `--require-validation`
  runs of a new game and of level 7 reported no error. The changed files
  were also compiled, not linked or run, with the `ci-release` flags. The
  owner committed the mist and the reconstruction as `41242f7e` without
  reporting a build or a test run; the cloud light, which came after, has
  not been built on Windows or run on a hardware GPU.
  Neither has had a synchronization-validation run, which needs the SDK's
  layer; neither adds an image or a barrier of its own. The mist costs one
  quarter-resolution integration of 24 steps and one full-resolution
  composite of nine taps a frame. The cloud light adds two noise lookups to
  every lit surface fragment and three to every step the atmosphere and
  the mist march. Only a software rasterizer has timed any of it, so read
  **Global** and **Bounded volumes** under the atmosphere in the profiler on
  real hardware, with **Cloud Light** on and off, before trusting the
  defaults.
- Fog-of-war and outline fixes, 2026-11-26: the shadow-free fog of war, the
  blurred-solid depth and the outlines resting on the floor were built and
  run the same way, on top of `1f9c071a`. All 141 tests passed, and
  `--require-validation` runs of a new game and of levels 5 and 7 reported
  no error with MSAA off and at four samples. They have no regression test
  of their own: captures of the overworld's first screen and of levels 5
  and 7 were compared by eye and by pixel difference. Not built on Windows
  or run on a hardware GPU. One fault is left: the fog column still spans
  absolute heights, zero to `fogOfWarHeight`, so anything on a screen's
  third layer reaches its top and shows through it. Raising the height
  hides that and covers about 0.7 of a row more of the discovered board in
  front of the camera for every unit.
- Wider bloom and ambient motes, 2026-11-26: built and run the same way on
  top of the fixes above. All 142 tests passed, and `--require-validation`
  runs of a new game and of levels 5 and 7 reported no error with MSAA off
  and at four samples. The bloom's skirt has no regression of its own
  beyond `bloomMix` in `scene_draw_lanes`; `ambient_motes` covers the motes
  on the CPU. Both were looked at only in 1280 by 720 software captures, in
  which a mote is two or three pixels, so their defaults are a first guess
  for a larger display. Not built on Windows or run on a hardware GPU. The
  skirt adds two half-resolution passes of 21 taps each.
- Linux, 2026-11-19: every CI preset was built and run on Ubuntu 24.04 with
  GCC 13, Clang 18 and lavapipe: `ci-debug` with its 240-frame validation
  capture, `ci-release`, `ci-dev-fast`, `headless-tests`, `ci-sanitize` with
  leak detection, `ci-tidy` and `ci-fuzz`. All passed, with no clang-tidy
  findings. After the changes of 2026-11-20, `ci-debug` with its validation
  capture, `ci-release`, `ci-dev-fast` and `ci-sanitize` were rebuilt and
  passed again, and clang-tidy was rerun on the files those changes touched.
  After the renderer fixes of 2026-11-22, `ci-debug` with a 60-frame
  validation run, `ci-release`, `ci-dev-fast` and `ci-sanitize` were rebuilt
  and passed, and clang-tidy was rerun on the touched files.
- Python: `tools/run_python_tests.py`, `tools/check_pbr_art_pass.py` and
  `tools/validate_current_ground.py` pass from a tree with LF line endings.
  The ground validation also passes on the Windows working tree, where the
  cap tables have CRLF.
- GitHub Actions has not been confirmed green since these fixes. Check the
  latest run before relying on it.

Known limitations:

- CI renders on lavapipe, and hosted Windows runners have no Vulkan ICD.
  Real-device, display, controller, signing, upgrade and uninstall acceptance
  is manual; see
  [packaging/ReleaseValidation.md](packaging/ReleaseValidation.md).
  A passing package fixture is not acceptance of a newly produced shipping
  ZIP. Follow the full release checklist before publication.
- Synchronization validation runs in CI as its own 60-frame step of the
  Debug job, with the SDK's layer. To run it locally, set
  `VK_LAYER_VALIDATE_SYNC=1` and run the smoke command with
  `--require-validation`. Layers 1.4.357 and 1.4.363 report nothing on the
  smoke run, on an evidence run with `--evidence-msaa 1`, or on the thumbnail
  bake, so any hazard is a regression. The 1.3.275 layer that Ubuntu 24.04
  packages ignores that variable and wrongly reports two classes: the depth
  resolve reading multisampled depth that draws of the same rendering pass
  wrote, and a geometry upload writing one range of an arena buffer while an
  earlier frame's indexed draw reads another. No hardware GPU has run it yet.
- GPU regressions compare 8-bit captures, and a software rasterizer can land
  one rounding step away from a hardware GPU. When a tolerance has to allow
  for that, state the reason in the test.
- `thumbnail_loading_budget` has a 30-second limit and takes 25 to 30
  seconds on a two-core machine with lavapipe, where it has timed out once.
  A timeout there on a slow machine is not a regression by itself.
- Optimized GCC builds print `-Warray-bounds`, `-Wmaybe-uninitialized` and
  `-Wstringop-*` reports from inlined standard-library code. They are
  deliberately not errors (see `sokoban_enable_warnings`); every other
  warning is.
- Debug runs on the development machine print a loader error for a missing
  Epic overlay manifest and unused shader-output warnings at location 10.
  Both predate the current renderer work and are not validation failures.
- Native tools need unsandboxed access locally: sandboxed MSVC reports C1902
  for its PDB server, and sandboxed content staging reports access denied
  while resolving its output directory.
- Player profiles use a fresh-start policy for older formats rather than
  migrations. Define compatibility before promising public-release save
  upgrades.
- Source models require a restart after edits.
- Text supports the configured fallback scripts; color emoji, bitmap-only
  fonts and a localization-resource system are not implemented. Additional
  scripts need licensed fallback fonts.
- The Maker has no player documentation in [README.md](README.md) yet.

Ground kit. `assets/custom/source/ground_modules/README.md` is the authoring
reference for the 240 native bodies and 310 caps: revisions, editable groups,
export commands and the square, bottom and floating selections. Use
`python tools/validate_current_ground.py` for the current kit. It runs from a
clean checkout using the tracked source-bound
`assets/custom/source/ground_modules/current_source_audit.json.gz`, native
source, exports and cap tables, and binds the cap tables by content, so LF
and CRLF checkouts agree. After native edits and finalized exports, refresh
that bundle with `tools/snapshot_current_ground.py` in background Blender,
then rerun the audit. `tools/validate_regular_relief.py` and
`tools/validate_ground_shoulders.py` keep historical retention proofs: they
deliberately enforce their earlier exact geometry and need ignored local
snapshots under `out/ground-modules/`. Keep the frozen construction
collections (`GroundRegularReliefConstructionSources`,
`GroundShoulderConstructionSources`, `GroundModuleConstructionSources`)
intact and out of runtime exports.

Feature-by-feature validation narratives from before 2026-10-16 were removed
from this section on 2026-11-20. They are in Git: `git log -p -- HANDOFF.md`.
The rules they stated are in README.md, in the READMEs under
`assets/custom/source/` and in the contract sections below; the few that
were recorded only here moved into those sections.

## Text and lectern contracts

- Measurement and drawing share cached `TextLayout` results. Keep shaping,
  bidirectional ordering, grapheme boundaries and line breaking consistent;
  never split UTF-8 or a binding chord while wrapping lectern text.
- `FontAtlas` owns pinned HarfBuzz/FreeType integration and the analytic encoder.
  Upgrade the HarfBuzz GPU encoder and `shaders/include/HbGpu.glsl` together.
  Preserve fallback-face em scale, weight and baseline conventions. Detailed
  atlas budgets and lifetime rules are in [docs/text-rendering.md](docs/text-rendering.md).
- Atlas changes use fence-owned upload buffers and ordered sampling/write
  barriers. Runtime glyph insertion must not require queue/device idle.
  The display target remains at least native window resolution independently
  of scene render scale, including the docked Game Viewport.
- Input action names, saved keys, labels, aliases, contexts and defaults come
  from `InputActions.def`. Lectern tags resolve that registry and the current
  bindings/device theme. Unknown or unbound actions show an inline error.
- Reading a lectern pauses gameplay when there is no movement input. Walking
  away closes it; Activate or Back closes it and requires movement release
  before reopening the same book. Font limits live in `ui/LecternConfig.hpp`.

## Build and validation

Inner development loop (Ninja, Debug, editor tools; open the folder in Visual
Studio or use a Developer PowerShell):

```powershell
cmake --preset dev
cmake --build --preset dev        # sokoban and its content only
cmake --build --preset dev-all    # plus the test runners
ctest --preset dev
```

`--preset release` (and `release-all`, `ctest --preset release`) is the same
Ninja build optimized, in `out/release`, without editor tools. The
`dev-check`, `dev-fast-check` and `release-check` workflow presets each
configure, build all targets and run their tests.

`dev-asan` (`cmake --workflow --preset dev-asan-check`) is the Debug build
under MSVC AddressSanitizer, in `out/dev-asan`. `SOKOBAN_ENABLE_SANITIZERS`
with MSVC adds `/fsanitize=address` to Sokoban's own targets, removes `/RTC1`
from the Debug flags of the whole tree and turns incremental linking off,
because cl and link refuse those beside ASan. The standard library's
container annotations are disabled so that instrumented objects link with
the uninstrumented third-party libraries. There is no UBSan and no leak
detection on MSVC. This configuration was written on Linux from Microsoft's
documentation and has not been built yet; treat its first run as the test of
the configuration itself.

Optimized authoring uses `dev-fast`: RelWithDebInfo with the full editor, live
tuning, profiler, source-content paths, automatic recordings and debugger
symbols, without Vulkan validation. `dev-fast-all` builds every target;
`ctest --preset dev-fast` tests it; `cmake --workflow --preset dev-fast-check`
does all three steps. Run `out/dev-fast/RelWithDebInfo/sokoban.exe`.
`SOKOBAN_ENABLE_DEVELOPER_TOOLS` enables tools in Debug and RelWithDebInfo;
Release and MinSizeRel remain editor-free, and shipping/headless force it off.
Keep `SOKOBAN_DEVELOPER_TOOLS_ENABLED` as the single CMake condition for the
public class-layout flag, developer sources and source-content paths.

Full Windows validation build (the Visual Studio generator, as CI uses):

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel 4
ctest --test-dir build -C Debug --output-on-failure --no-tests=error --timeout 120 -j 4
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure --no-tests=error --timeout 120 -j 4
```

Vulkan-free build using the same production libraries and test declarations:

```powershell
cmake --preset headless-tests
cmake --build --preset headless-tests
ctest --preset headless-tests
```

Use `-DSOKOBAN_WARNINGS_AS_ERRORS=ON` for a local warning gate. CI enables it
for both GCC/Clang and MSVC. The Linux Debug job performs the standalone
240-frame render under Vulkan validation. Linux Release runs the registered
CTest device smoke without the separate validation-frame command. Hosted
Windows runners build and run the SDK-independent and packaging tests but omit
device smoke because they do not provide a Vulkan ICD.

CI (`.github/workflows/required-tests.yml`) runs one Linux job per preset:
`ci-debug`, `ci-release`, `ci-dev-fast`, `headless-tests`, `ci-sanitize`
(Clang with ASan, UBSan and leak detection), `ci-tidy` and `ci-fuzz`. It also
runs MSVC Debug, RelWithDebInfo and Release, the Python regressions on
Windows and Ubuntu, and a line-ending check. To reproduce a Linux job,
configure and build its preset; for the five presets with tests, then run
`ctest --preset <name>` under `xvfb-run` with `VK_DRIVER_FILES` pointing at
lavapipe. The jobs depend on these rules:

- The Debug Linux jobs first run `tools/check_core_is_vulkan_free.sh`, which
  compiles every `sokoban_core` source by hand. A new third-party include
  directory for core must be added to that script as well as to CMake.
- Clang-tidy treats every finding as an error, and a library that fails
  stops the targets that depend on it, so one run can hide later findings.
  Run the complete `ci-tidy` preset to zero before pushing. Suppress only
  with `// NOLINT(<check>)` on the reported line, next to the reason.
- `tests/lsan.supp` is applied to every test labelled `vulkan`; give a new
  GPU suite that label. Sanitizer builds multiply every test's time limit by
  four.
- The Python steps run from a fresh checkout. A test must create the `out/`
  subdirectory it uses, and no check may depend on the line endings, or on
  the last floating-point digits, of the machine that produced a tracked
  file.

Test suites are declared with `sokoban_add_test` and linked into shared
runners (`sokoban_tests`, `sokoban_vulkan_tests`); CTest runs
`<runner> <suite>` in a separate process per suite. A suite that changes
process-wide state must be `STANDALONE`, as `player_profile` is.
Sanitizer builds default to one executable per suite
(`SOKOBAN_TEST_RUNNERS=OFF`) because GNU ld's memory for an instrumented
runner exceeds ordinary CI runners.

Development content staging is incremental: `sokoban_content` skips an
unchanged tree using `<config>/assets.stage-record` and takes BC7 textures
from `content-cache/<config>/`. The cache key must describe everything the
encoder's output depends on. Change
`compressedTextureEncoderRevision` whenever `buildBc7Ktx2` would produce
different bytes for the same input. Shipping presets set
`SOKOBAN_INCREMENTAL_CONTENT=OFF` and stage from scratch. `sokoban_bc7enc16`
is built with Release flags in every configuration from
`cmake/bc7enc16/`.

The persisted rim stage was verified with `dev-fast-all`, all 102 optimized
CTest suites (the two Windows missing-audio-path suites passed outside the
filesystem sandbox), and a `dev` validation capture. Geometry staging produced
26 artifacts / 1,304 cap surfaces / 2,821,840 bytes; a subsequent stage skipped
the unchanged package. Level 3 screen 3, level 5 screen 0 and the initial
overworld imported all 44, 46 and 138 resolved caps respectively. Their scene
and SSAO images, plus the live-only rim fixture, match the prior runtime-cache
captures pixel for pixel. The 240-frame Debug capture exercised 44 baked caps
with zero VUID messages; existing loader/unused-shader-output diagnostics remain.
Local evidence is under `out/ground-geometry/processed-artifacts/` (ignored).

For shipping artifacts and human GPU acceptance, follow
[`packaging/ReleaseValidation.md`](packaging/ReleaseValidation.md). A package is
acceptable only after the bounded package gate succeeds from a fresh extraction
and the required real-device checks are recorded.

## Persistence and authoring contracts

- `SaveStore` owns primary, backup, temporary, displaced, and deletion-marker
  artifacts. A deletion marker commits deletion before cleanup and prevents old
  recovery candidates from reviving a slot.
- `AsyncSaveStore` management operations are owner-thread operations. Producers
  may request snapshots through the documented synchronized boundary. Flush and
  channel replacement return revisions and a typed durable/retryable outcome;
  an empty queue is not evidence that bytes reached storage.
- Player profiles are not migrated (see `currentPlayerProfileFormat`). Bump
  the format whenever the document shape or meaning changes; `SaveStore`
  renames older-format artifacts to `.obsolete-format-<N>-<stamp>` before
  loading, inspection reports such slots as empty, and the player starts
  fresh. A decoded valid profile remains usable if promotion cannot be
  persisted. Unsupported future formats remain preserved and do not fall
  through to an older writer.
- Puzzle source writes use durable atomic replacement. Runtime mirrors and
  package-index refresh have explicit outcomes, and a committed source with a
  stale mirror stays dirty and retryable.
- Successful authoring publication refreshes and validates `content.index`
  after the runtime tree is complete. Levels, overworld transactions, splat
  maps, manifests, decoration imports, animation catalogs, and thumbnail baking
  use this boundary.
- Structural level publication commits the level tree and its splat/music
  manifest associations together across source and runtime. Deleted levels keep
  a private association archive so restoration preserves their map paths and
  soundtrack at the newly assigned index.
- Overwriting an external texture invalidates all prepared interpretations of
  that source before `content.index` is refreshed. A failed invalidation leaves
  the authoring session dirty and retryable.
- Numbered puzzle paths are interpreted and constructed through `LevelCatalog`.
  Callers remain responsible for source/runtime root containment.
- Decoration registration resolves structured GLTF/GLB dependencies, validates
  external files before manifest mutation, and copies the complete dependency
  set before package-index publication.

## The Maker

The Maker and UI sources (`src/engine/Maker*` and `src/engine/ui/`) are
formatted with the repository's `.clang-format`. Run clang-format 18 over a
file there after editing it. Three renderer files are clean under it too:
`GroundChunkGeometry.cpp`, `GroundRimSurfaceCache.cpp` and
`VulkanGroundChunkCache.cpp`. The rest of the tree is not formatted with it;
see the note at the top of `.clang-format` before changing that.

The Maker is the player-facing editor behind **Make** on the title screen, in
every build configuration. It edits one puzzle draft or one overworld draft at
a time. It is not the developer level editor: it uses `LevelEditor` as its
document model, but has its own UI, its own storage under the save directory,
and no access to source or staged content.

- Layers. `MakerSession` (`sokoban_core`) is the document: tools, strokes,
  selection, undo, problems, save and load. `ui/MakerView` draws the
  workspace and returns one `MakerViewAction` a frame; it keeps only view
  state such as scrolling, focus and the toolbar overflow. `MakerController`
  (`sokoban_ui`) owns the sessions, panels, dialogs, autosave, board cursor,
  viewport and Test Play state, and reaches the renderer, the window, the
  shell and the shared text input only through `MakerHost`, which
  `Application` implements. `tests/MakerControllerTests.cpp` drives it with
  a host that has no window and no GPU. `Application` still steps and
  renders a Test Play, because those share the campaign's effects and audio.
- Two sessions. The controller keeps one session for puzzles and one for the
  overworld and switches between them; each keeps its draft, history and
  unsaved state while the other is on screen. A new `MakerSession` edits a
  puzzle. It becomes an overworld session when it is given an overworld
  (`newOverworldDraft`) and never goes back: from then on `newDraft` and
  `loadDraft` create and load overworlds. `restoreRecovery` refuses an
  autosave of the other kind. Create the overworld session through
  `MakerController::ensureOverworldSession`.
- Storage. `maker-drafts/` and `maker-recovery/` in the save directory hold
  everything. Nothing is written to source or staged content and nothing
  here is in `content.index`. Every write is an `atomicFile` replacement
  under that directory's lock file (`.writer.lock`, `.recovery.lock`).
- Names. A draft name is 1 to 64 bytes of UTF-8 with no control characters,
  slashes, or leading or trailing spaces (`MakerDraftStore::maxNameBytes`).
  A name never appears in a path as text: file names carry its bytes in
  lowercase hex, so filesystem case folding and reserved device names cannot
  make two drafts collide.
- Puzzle drafts are `maker-drafts/level-<id>.level`. The ID is 32 lowercase
  hex digits chosen at the first save and never changes. Line 1 is a JSON
  header, `{"format":1,"name":"..."}` with optional `"legacyName"` and
  `"deleted"`, kept under `maxNameBytes * 4 + 64` bytes so that listing
  drafts never parses a board. The rest is ordinary level text
  (`Level::serializeDefinition`). The display name lives only in the header:
  `renameLevel` replaces that line and copies the board bytes unchanged.
- Deleting a puzzle draft rewrites its file as a header with
  `"deleted": true` and no board. The tombstone keeps the ID, the name and
  the legacy name retired, so that an old selector cannot attach itself to a
  later draft that reuses the name. Do not remove tombstones.
- Legacy puzzle drafts are `maker-drafts/draft-<hex name>.draft`: plain level
  text with no header, from before IDs. One is listed under the ID
  `legacy-<hex name>` and is never modified. Saving or renaming it writes
  `level-legacy-<hex name>.level`; from then on the `.draft` file is a backup
  and is no longer listed. Deleting it writes the tombstone first and then
  removes the `.draft` file.
- `legacyName` in a header is the name that selectors written before IDs
  used for this level. It is recorded the first time a level is renamed
  while name-only references still resolve to it.
- Selector targets. A `Level::ScreenSelector` has either `target`, a
  campaign `LevelLocation` that the Maker shows as "Legacy puzzle", or a
  Maker level given by `draftId`, `draftName`, or both (JSON keys `draftId`
  and `draft`); never both kinds. The Maker writes the ID and the name
  together, and the ID decides. `MakerDraftStore::resolveLevel` is the rule.
  An ID resolves to that ID or to nothing; it never falls back to a name. A
  name alone resolves to the legacy draft that had that name, and to nothing
  if that draft was deleted; failing that, to the level whose `legacyName`
  it is; failing that, and unless the name is retired, to the level now
  shown under that name.
  `makerLevelFromCatalog` in `MakerController.cpp` applies the same order to
  a list already in memory. Change the two together.
- Overworld drafts are `maker-drafts/draft-<hex name>.world`, one JSON object
  from `makerDraftCodec`: `{"format":1,"width":W,"height":H,"screens":[...]}`
  with one `{"id":N,"slot":[x,y],"screen":[level text lines]}` per screen.
  The name is the identity: there is no ID, a rename renames the file, and
  nothing refers to an overworld draft. `OverworldMap::validateDraft` decides
  what may be saved and runs on both encode and decode. Every screen's
  character is the Lorekeeper, and one screen at most holds the player
  start.
- Drafts may be incomplete. Saving needs only a well-formed grid
  (`makerDraftCodec::validateDraftGrid`). `MakerSession::problems` reports
  what would stop play (missing start, unpaired portal, broken selector,
  unwired device, unused signal source). Test Play needs a level that builds
  (`buildPlaytestLevel`), and a selector is entered only when its target
  loads as a playable level.
- Undo in a puzzle session is `LevelEditor`'s history (see Developer
  iteration contracts). An overworld session does not use it: the editor
  holds only the screen being edited, and `MakerSession` keeps whole-world
  history. One step is an `OverworldState`: the layout, one
  `shared_ptr<const Level::Definition>` per screen, and a revision. A state
  shares every definition it did not change, so a step costs one screen.
  Every edit installs a new definition object, and caches may key on the
  pointer; the neighbour hover preview does. A stroke copies the state at
  `beginStroke` and records one step at `endStroke` if anything changed;
  `updateUnsavedChanges` folds the editor's document back into the world for
  single edits. Undo and redo swap states and reload the editor from the
  current screen. The limit is `LevelEditor::HistoryLimits {}.maxRecords`
  states, by count only; overworld history has no byte budget. Unsaved means
  the state's revision differs from the saved revision, so undoing back to
  the saved state clears the flag.
- `MakerSession::editRevision` increases on every change to the document or
  to its saved state, including undo, redo, save and rename. Autosave and the
  Problems panel key on it. Anything that changes what would be saved must
  increase it.
- Autosave journals are `maker-recovery/puzzle.autosave.json` and
  `maker-recovery/overworld.autosave.json`, format 1: `draftName`, optional
  `draftId`, `document` (level text lines, or the `.world` object) and, for
  an overworld, `activeScreen`. They are working copies and never replace a
  named draft.
- Autosave policy (`MakerController::updateAutosave`): once a frame for the
  session on screen; never during a stroke or a Test Play; at most one
  request a second for each kind; only when `editRevision` has moved. A session with unsaved
  changes writes its journal. A session without them discards its journal by
  writing `null` through the same durable path. Closing the Maker, switching
  between puzzle and overworld, and shutdown force the write and end any
  stroke first; shutdown does both sessions.
- Journal writes run on `MakerAutosaveWriter`'s thread, because a durable
  replacement flushes the disk. The frame only copies the snapshot. A newer
  request replaces one that has not started, results arrive on a later frame
  through `applyAutosaveResults`, and `flush()` must come before any direct
  use of the recovery store.
- Recovery. The journals are read once, the first time the Maker opens. A
  journal equal to its saved draft is discarded without asking. Otherwise
  that slot is unresolved and the Maker asks whether to recover or discard,
  starting with the kind on screen. An unresolved slot is not autosaved, so
  a journal cannot be overwritten before the player has answered. Recovery
  follows a rename, because the ID finds the current name, and survives a
  deletion, where the draft comes back untitled. A recovered session always
  counts as unsaved.
- Test Play of an overworld resolves its selector targets once, when it
  starts. `MakerLevelProgress` belongs to that one playtest and is keyed by
  the resolved level, so several selectors for one saved level share its
  completion.
- `MakerViewportCamera` holds view-only pan, zoom, top-down and selection
  framing; the camera angles belong to the draft. Pan and zoom keep the
  board reaching 12% into the viewport (`limitPan`). The test uses the
  board's own four corners on its top surface, projected through
  `MakerHost::projectToScene` with the previous frame's camera, not their
  bounding box: at the default angle the corners of that box are empty, and
  limiting the box let the board leave the screen.
- Suites: `maker_session`, `maker_draft_store`, `maker_recovery_store`,
  `maker_autosave_writer`, `maker_view` and `maker_controller` run headless;
  `maker_thumbnail_gpu` and `maker_wiring_projection` need a device.

## Developer iteration contracts

- Shader hot reload (`ShaderHotReload`, developer builds) compiles with
  the same `SOKOBAN_GLSLC_FLAGS` list as the build rule; keep shader options
  in that one CMake list. Compiles run off the main thread into
  `<config>/shader-hot-reload/`, never inside the staged tree; a pass
  publishes only if every module in it compiled, then refreshes
  `content.index`. The renderer rebuilds pipelines through the
  pipeline-only reconfiguration path (`requestShaderReload`), and a pipeline
  creation failure caused only by a shader reload keeps the old pipelines.
- Tunables (`engine/Tuning.hpp`) are `inline constexpr` when
  `SOKOBAN_ENABLE_DEBUG_UI` is 0 and registered variables when it is 1. Read
  them only at runtime. Values that size arrays, feed `static_assert`, or
  define pipeline or resource shape must stay plain constants. The header
  writer replaces only the literal value arguments of single
  `SOKOBAN_TUNABLE_*` invocations; the `tuning` test checks that every
  registered header round-trips unchanged.
- `dev-session.json` lives in the save directory and is read and written
  only by developer builds; smoke and evidence runs neither resume nor
  save it. It never stores game progress, which stays in the save slot.
- Level editor edits go through `LevelEditor::recordDocumentChange`. While a
  stroke is open (`beginStroke`/`endStroke`) it folds changes into one record,
  and `endStroke` writes that record. Capture through
  `captureBeforeDocumentChange` to avoid per-cell snapshots during strokes or
  suppressed subcommands; commands that need rollback capture separately.
  Any recorded edit clears the redo stack; no-op edits preserve it.
  Undo, redo, save shortcuts, draft playback and document switches close an
  open stroke first. Compound commands that make intermediate edits (such as
  `moveObject`) suppress recording and record once themselves; they must not
  modify history directly. Redo is carried everywhere undo is: the draft
  cache and screen-identity remaps. Histories move into and out of cached drafts
  rather than being duplicated. `HistoryLimits` defaults to 128 records and
  64 MiB across active and cached undo/redo histories. Eviction removes an
  undo stack's oldest step or a redo stack's farthest future step, preserving
  a contiguous replay chain. An oversized edit clears the active document's
  history and still applies. `historyStats` estimates snapshot storage,
  including nested allocations; it excludes live draft documents, temporary
  stroke/transform rollback snapshots and allocator/container overhead.
  Screen-identity remaps remeasure history and enforce the limits again.
- Tile rotation is stored as optional sorted `@tilerotation` records and
  survives saves, drafts, moves, undo/redo and overworld composition.
  Directional devices still rotate their existing enums, which keeps gameplay
  semantics and older screen symbols compatible. Connected ground and cliffs
  constrain effective rotations to the symmetries that preserve their joins.
- Decoration layer locking uses the unit-height interval that contains each
  decoration's pivot. Locked editor rendering, picking, point lights, placed
  lists and inspectors exclude other layers without renumbering saved
  decoration indices, and switching layer or lock state ends the current
  transform session. A fence's pivot belongs to the base layer of the wall
  that supports it.
- Every editor control is an `InputAction`. `inputActionContext` decides
  which actions may share a key; a new action needs a context, a default, a
  name, and (when it is a player-facing control) an Options row. Adding one
  changes the profile document, so bump the format.
- The arrow keys and Enter are the one fixed exception. They always move
  through lists and dialogs, beside whatever is bound to the Move and Confirm
  actions, and Enter also uses the tool at the Maker board cursor. They are
  read in one place: `InputRouter::NavigationKeys`.
- The Maker board cursor moves with its own four actions, `MakerBoardUp`,
  `Down`, `Left` and `Right`, on every device. Their defaults are the arrow
  keys, the D-pad and the left stick. It does not use the Move actions,
  because in the Maker W, A, S and D are editing shortcuts. Help and the
  status line name these controls through
  `makerHelp::boardCursorDirectionsLabel`, so a rebound key shows there.
- Keyboard bindings may carry Ctrl/Shift/Alt modifiers. Among the bindings on
  one key whose modifiers are all held, only those needing the most
  modifiers fire, for held and pressed queries alike. Capture records a chord
  on the non-modifier key's press and a lone modifier key on its release.

- Recorded solutions (`engine/Solution.hpp`, `solutions/`) use format 2.
  Each input carries its complete settled GameState, active hero controller
  and automatic-motion pause state. Update the solution state codec and its
  field-mutation/round-trip tests whenever GameState changes. Old formats are
  rejected and must be re-recorded through the current Driver.
  `activeLevers` records the cells of levers left on; missing means empty in
  existing format-2 recordings. New recordings emit it even when empty.
  The `solution_replay` gate requires matching, passing recordings for all
  current screens by default. `solutions/coverage.json` allows only explicit
  unfinished-screen exceptions pinned to a gameplay digest and a reason;
  gameplay edits or removed screens invalidate them.
  Recordings are matched to
  screens by `solution::levelDigest`, which hashes only what gameplay reads:
  the layers with trailing spaces trimmed, the water layer and the hero (plus
  gate/rotator/lock links, elevator/minecart records, object/portal groups,
  and covered-plate records on screens that have them).
  If gameplay starts reading another part of the definition, add it to the
  digest. A replay applies each input with `solution::Driver` and waits for
  it and every slide, conveyor and enemy reaction to settle before the next
  one, so recordings do not depend on frame timing. The driver uses the same
  `GameplaySession` planner/scheduler as the game but advances directly between
  mechanical completion boundaries. Keep this headless path free of
  presentation-frame work. Developer builds record a player's campaign or
  draft clear through the same driver. `solution::auditCoverage` replays those
  files in `solution_replay` without writing to the store. Bump `format` and
  record fresh playthroughs when the text format changes.
- `solution::reconcileStore` (`engine/SolutionStore.hpp`) is the only code
  that decides where a recording lives: one `level<L>-screen<S>.solution`
  per current screen holding the shortest run (ties keep the file in
  place), unsaved-draft content in `solutions/drafts/` (gitignored), and
  displaced recordings parked there instead of deleted. Developer builds call
  it on a worker thread after every solve, once at startup and after any
  source level change; jobs run one at a time. It must stay free of game,
  UI and GPU state.
- `GameplaySession::inputLog()` lists the inputs that started actions since
  the last restart or restore. Record from the authored start so the input
  sequence replays against the screen's initial state. Replay fixtures and
  tests use `resetToState`. Live gameplay restores snapshots.
- The source watcher (`SourceWatcher`, `Application::serviceSourceWatcher`,
  developer builds) polls stamps every 500 ms and never runs in smoke
  or evidence runs. It writes only into the staged tree and content index,
  never into source. `LevelEditor::reloadFromDisk` refuses while the
  document is dirty or a stroke or transform is open, and ignores files that
  match the document (its own saves). `AssetManifest::adoptLiveFields` is the
  list of manifest fields that may change without a restart; anything else
  it reports as structural. Models are deliberately not reloaded (see the
  `VulkanModelResources` guidance below).
- Precompiled headers hold standard headers, `Math.hpp` and, for the
  renderer and game, `<vulkan/vulkan.h>`. Do not add config, tuning or
  render-type headers: every edit to them would rebuild the PCH. The shared
  test runners stay without a PCH, because GCC rejects a PCH built without
  the runners' per-source `-Dmain=` define. `RenderTypes.hpp` must not
  include `WaterConfig.hpp` or `LevelCatalog.hpp` again; water defaults come
  from `RenderFrameData::defaultWaterRendering()` in `RenderTypes.cpp`, and
  `LevelLocation` has its own header.

## Gameplay and input contracts

- Action planning uses `planPlayerStep`, `planSlides` and `planConveyorRides`.
  Slide and conveyor batches arbitrate simultaneous motion in one scope;
  their tests must exercise these runtime entry points. Checkpoint validation
  replays scoped rules independently of the planner's transient legs.
- Ends always render active. Pressure plates control linked devices, while
  completion requires every hero on an End and every End occupied by a hero;
  there is no separate end-unlock query or state.
- `StateDelta` owns canonical player, movable, and enemy entity order, append,
  and overlap interpretation. Scheduling and gameplay validation must use it
  instead of rebuilding changed-ID lists.
- Completing one scheduled action synchronizes committed/structural presentation
  state, then re-samples every surviving action at its existing elapsed time.
  This must also happen when completion consumes the frame's remaining time.
- Functional undo history is separate from the scalar completed-action
  diagnostic counter. Do not retain completed world-state actions for telemetry.
- Binding capture continues forwarding physical state changes while suppressing
  action edges. Releases and axis neutralization during capture must remain
  visible when capture completes or is cancelled.
- Rotator plates are resolved inside `rules` steps, not as separate actions.
  `MicroStepResolver` samples which rotators are engaged (every linked plate
  occupied) when a step begins and, once the step's movement settles, turns
  the occupant of every rotator that became engaged. That keeps rotation
  stateless (no latch in `GameState`), deterministic for replay and undo, and
  identical between whole-world and scoped steps: a turned unit joins the
  step's closure, so `StateDelta` and reservations see it. Mirror activation
  applies the same rule. A turned turret sweeps its new line of fire once.
  An enemy's `quarterTurns` has no rule effect; `GameplayPresentation` holds
  the turn only until the next action begins, then the enemy faces the
  nearest hero again.
- Every unit carries `quarterTurns` (0-3, clockwise). Turret firing direction
  is `rules::turretDirection` (authored tile turned by `quarterTurns`); do not
  read turret direction from the tile type alone. Profiles write the field only
  when non-zero and read it as optional. Replay expectations include it.
  Solution digests hash rotator links on screens that have rotators. The
  digest also includes gate links and start-open state. Changing either
  requires a fresh recording.
- Plates are the tiles with `TileProperty::Plate`, including pressure plates,
  buttons, levers, rotators, lock plates, portals, rail stops and Ends. New plate kinds get
  the property in the tile table; code asks
  `tileTypeIsPlate`, and anything that asks "what plate is here" must use
  `Level::plateAt`, not `tileAt`: movable units, including mirrors, leave the
  underlying plate in the static grid. `@plate`
  records (`Level::Plate`, `Level::coveredPlates`) hold plates authored beneath
  an occupant; the editor keeps them in `document_.plates` and every command
  that moves, crops or deletes cells must update them like gate records.
- Gates are full-tile blocks: closed is solid (blocks entry and sight,
  supports units above through `cellAllowsEntity` in `fallTarget`), open is
  Air. `rules::isGateOpen` is `activated != Gate::startOpen`, except that a
  live rock or ice block in the cell holds the gate open (they cannot die).
  Gate state stays derived (nothing in `GameState`). `applyGateChanges`
  applies the consequences: a gate that was closed when last sampled and is
  open now drops the live column on top of it, and a closed gate with a live
  unit in its cell (a hero, enemy or turret) kills it in place; it repeats
  until nothing changes. `MicroStepResolver` keeps `gatesOpen_` current and
  runs it after every micro-step and after elevators and minecarts; mirror
  activation runs it too. Dropped units join the closure as moved; a unit that
  was outside the scope is also marked done so it cannot take the step's
  input. Crushed units join the closure finished. A unit with nothing to land
  on stays put (the rule that rejects moves into bottomless columns).
  `startOpen` is written to `@gate` only when true, and solution digests hash
  it only for start-open gates, so older digests are unchanged.
- Live mirrors are movable entities: position and rotation travel in
  `GameState::movables` and `StateDelta::movables`. The cell-keyed
  `turnedMirrors`/`StateDelta::mirrors` path supports static-mirror compatibility.
  Rules resolve orientation through `rules::mirrorTileAt`; callers must use
  live state for moved mirrors.
- Completion requires every living hero on an End and every End occupied by a
  hero. Pressure plates drive devices; a rock or mirror on an End cannot satisfy
  completion. Space/Activate pulses all buttons and toggles all levers occupied
  by living heroes, and activates eligible mirrors together. Button pulses
  expire after the next step; lever signals persist until toggled again.
  Both states belong in deltas and replay expectations.
- Levers, buttons and pressure plates animate through ordinary action
  timelines. Control travel is recorded for chained movement, concurrent
  actions, undo/restart and profile persistence, and a profile without
  control tracks restores the settled poses. Lever and button cycles take
  0.22 seconds; pressure travel takes up to 0.12 seconds, including zero-time
  mirror actions. Gameplay draws the generator's fixed and moving component
  pairs with affine model transforms; editor previews and thumbnails keep the
  combined models.
- Buttons and levers use N/E/S/W edge placements facing the owning cell's
  center. Button characters are `b`, `y`, `z`, `~`; lever characters are `%`,
  `&`, `[`, `]`. The original Button value, `b`, legacy `Button` plate spelling
  and solution digest remain north-compatible. Append new enum variants to
  preserve existing raw tile IDs. Use `buttonOrientationQuarterTurns` and
  `leverOrientationQuarterTurns` from the north-edge asset orientation.
- Signal-source links are authored by color, and only in the editor.
  `LevelEditor` keeps a color for every pressure plate, button and lever
  (`Document::plateColors`) and device; a device is linked to the plates of
  its color (`linkGroups`, `linkedPressurePlates`, compared as 8-bit RGB).
  Editor device records keep empty `pressurePlates`; `linkedDefinition` is
  the single place colors become explicit lists, for saving, drafts and
  `documentToLevel`. Loading derives colors from the explicit lists
  (`colorLinks`), recoloring legacy groups so links survive a save.
  `Level::LinkColor` (`@linkcolor`) stores only the colors of plates that
  drive nothing. Rules, rendering and solution digests use the explicit
  `pressurePlates` lists.
- The level editor's tool cursors (eyedropper while Pick Tile is held, a
  brush tinted with the active link color while Paint Link Color is held)
  are drawn in code (`EditorCursorArt.hpp`) and owned by `ApplicationTools`.
  `updateEditorInteraction` records which one the pointer wants;
  `Application::update` applies it once a frame, before any early return, so
  menus, modals and the fly camera restore the system cursor. While either
  key is held, `updateEditorToolModifier` owns the pointer outright: it ends
  any tile, ground or gizmo drag, sets no `hoverCell` (so no preview), and
  nothing else in `updateEditorInteraction` runs.
- Elevators (`Level::Elevator`, tile `=`) keep their platforms in
  `GameState::elevators`: one entry per level record, in record order, holding
  the platform cell and cycle phase (`rules::elevatorStopIndex`/
  `elevatorNextPhase`). Activation uses the rotator edge rule and resolves in
  `MicroStepResolver::resolveElevators` after rotators, and in mirror
  activation. Rules that ask about static solidity or support around a
  platform must use `rules::liveTileAt`/`cellAllowsEntity`, never
  `Level::tileAt`, because the authored cell is open shaft once the platform
  leaves. A move is all or nothing; a blocked shaft drops the press. Carried
  units join the closure and count as moved for turrets and attacks.
  `StateDelta::elevators` carries platform changes by index. The profile
  writes `elevators` only when non-empty. Solution digests hash elevator
  records for screens that have them. Reservations claim the full shaft of a
  moving platform (`addElevatorReservations`). Presentation animates platforms as
  `EntityKind::Elevator` motion tracks; a leg in which a platform moves runs
  longer by its travel time (`config::elevatorSecondsPerLayerPerStep`), riders
  walk first and then ride, and `GameplaySession` takes the longer timeline as
  the action duration. Platform drawing keeps the model's top flush with the
  top of its layer (`ElevatorVisuals.hpp`); do not apply per-tile scale to it.
- The content pipeline accepts `KHR_texture_transform` only when it is an
  identity (the Kenney kits attach one naming just the texture coordinate set
  in use). Any real offset, scale, rotation or texcoord override is still
  rejected until MeshMaterial represents UV transforms.
- Starting a new game on a selected slot is one dependent shell command. The
  start action runs only after the slot switch commits successfully; a switch
  failure preserves the current profile and title error state.
- Options pages measure their required height from the declared layout rows.
  When space is short, the page scaffold, row geometry, prompt glyphs, control
  strokes, and type share one vertical scale so all rows and Back remain usable.
- Static loading, CPU skinning, and GPU skinning share the source-to-model
  transform. Positions and tangents use the forward linear transform; normals
  use its inverse transpose. Tangent frames remain normalized, orthogonal, and
  preserve handedness. `skinGltfMesh` remains a single-clip CPU reference for
  geometry and attachment tests. Runtime animation uses `sampleGltfSkinPose`
  and `sampleGltfSkinPoseBlended` to build GPU palettes; the unused blended
  CPU vertex-skinning API has been removed.

## Renderer contracts

- Model geometry arena buffers are reclaimed when their last slice is released.
  Runtime eviction releases slices only after every referencing frame fence
  clears the retirement mask; uploads must also complete before release.
  Empty slots are reused without renumbering live allocations, and allocation
  rollback reclaims newly empty buffers while preserving shared live owners.
- Geometry statistics separate requested slice payload from reserved VkBuffer
  capacity (free space and alignment gaps included). Both uploading and retiring
  slices count until release. Capacity is included in VMA allocation totals;
  VMA allocation padding and heap blocks are separate. The model residency
  budget still limits payload, not a strict cap on geometry capacity: partly
  occupied blocks and the preferred 32 MiB vertex / 16 MiB index sizes retain
  slack. Do not interpret geometry reclamation as a strict physical-memory cap.
- `frameArenaMemoryStats()` reports all live CPU `FrameArena` reservations and
  their peak combined capacity. Accounting changes only on construction and
  destruction; bump allocations, reset and borrowed array views do not change
  reserved bytes. This payload storage is already included in process memory
  and excludes allocator overhead. Keep it separate from resident memory and
  from each arena's used-byte high-water mark.
- Legacy ground glTF/bin sources and PBR GLBs remain inputs to current ground
  validation, geometry/GPU fixtures and optional PBR authoring. Their absence
  from the active runtime manifest does not make them deletion candidates.
- `AnimationController::updateInstances` owns playback per nonzero
  `animationInstanceId` and emits a pose for every valid instance on every
  frame, including paused clips and previews. Frame-indexed GPU palettes need
  that publication even when clip/time are unchanged. Preview overrides only
  instances of its selected model. Crossfades use the separate frame transition
  clock, so clip resets, paused clips and reverse playback preserve fade timing.
  `clear()` resets loaded clips, preview inputs and all instance playback.
- Ground geometry processing derives exposure from the final emitted tiles on
  the same layer, even when loading baked rim caps.
  Eligible terrain is opaque, fixed, unanimated unit ground at its authored
  integer cell, using the canonical GroundRock01–10 source model contracts.
  Hidden screens, custom models, scaled/transformed tiles and editor previews
  cannot occlude eligible ground. Validate all side patches and the bottom
  before using source index variants. A candidate hidden side
  remains visible until its neighbor has a validated, resident mesh.
  Index variants share original vertices/materials and retain the original
  index prefix, painted top and bottom. Main draws, shadows, batching and the
  point-shadow cache must use the resolved mask. Account for every appended
  index in upload/residency budgets; compare draw calls and GPU time using
  `--disable-ground-geometry` before expanding the experiment.
- Portals share `renderFrameParts::appendPortalVisual` across gameplay, editor
  and thumbnails. An invisible upright hull retains the authored edge's pick
  cell and camera bounds. The elongated oval aperture stands upright on that
  cardinal edge; its horizontal face normal follows that edge's cardinal axis.
  Animated rings surround a nearly black cool-purple center, and a diffuse
  horizontal beam fades away from it without defined trails. Tiny subtle dots
  concentrate near the center. The aperture uses `PortalEnergy`; twelve radial
  axial sheets use `PortalBeam`, with translucent, two-sided HDR rendering and
  no shadows. Composite `PortalEnergy` after the beam and other translucent
  faces so the dark core covers their intersections, while retaining depth
  testing against the opaque world. The 64 tiny round dots reuse `ParticleGlow`.
  Shader animation uses the existing effect clock. Evidence fixtures must
  retain `isoFaces` as well as particles and tiles so their rings and beams
  reach the same renderer path. Thumbnail crops include the energy geometry.
  Visual Studio's default launch uses `out/dev/Debug/sokoban.exe`; rebuilding
  `dev-fast` leaves that executable unchanged. Verify the user's launch build
  and capture an authored portal screen from that executable after changes.
- `ProcessedGround` is the Vulkan-free boundary description: cells are sorted
  by layer/row/column, with exposed side masks, side/diagonal model identities,
  and convex/concave corner masks. Side order is N/E/S/W; corner/diagonal order
  is NW/NE/SE/SW. `GroundGeometryCache` compares exact sorted eligible cell/model
  signatures, rather than trusting a hash. Ground edits, model assignments,
  move/delete source previews and view/layer visibility changes rebuild it;
  paint colors, unrelated actors and render-list ordering do not. Application
  owns separate gameplay, editor and screen-preview caches. Cache storage must
  retain capacity, and descriptions must not borrow frame or manifest pointers.
  GPU readiness remains a renderer decision and must not invalidate this cache.
- `GroundRimSurfaceCache` owns completed world-space cap patches, normals and
  wall coverage, keyed by exact resolved geometry inputs. It stores only active
  rim surfaces and retains working capacity. Reordering tiles, painting and
  camera movement reuse geometry; changed inputs rebuild affected surfaces.
  Main and preview preparers own separate caches. Update before auxiliary tasks
  start, then keep lookups immutable until those tasks finish. Prepared faces
  and shadow lists own copies, so older frame leases survive subsequent updates.
  Readiness and draw-budget fallback must update the cache from the newly
  resolved flat/rim profiles before preparing either pass. Keep paint/material
  state per-frame. Snapshot counters and retained bytes in each prepared scene
  for evidence; a cache hit does not eliminate projection/culling/sorting work.
  `compileGroundRimSurfaces` is the owning renderer-independent live compiler;
  the optional chunk path below avoids per-frame cap projection and sorting.
- `GroundChunkGeometryCache` compares exact sorted eligible cell and surface
  keys, plus the optimizer flag, and owns immutable 8x8-cell chunks per layer.
  Each vertex stores world position, facet normal, normalized quad coordinates,
  all four wall-coverage weights and stable tile slot. Full-record deduplication,
  cache ordering and fetch compaction use the pinned meshoptimizer v1.3 subset;
  no simplification, quantization, degenerate removal or winding change occurs.
  Material, camera and source-order changes reuse geometry. Exact baked cap
  matches seed construction; `.grm` does not persist the chunk payload yet.
  `PreparedGroundChunkDraw` requires a complete, unique, ready chunk with exact
  keys and valid effective paint textures. It suppresses individual caps in
  main/sun-shadow lists, while retaining body models and logical selection.
  On-demand picking projects indexed triangles; old scenes own their generation
  and captured per-slot pickability. Main chunks cull using completed cap AABBs;
  invalid bounds fail open and sun caster records remain. Active point-shadow lights use the legacy
  path. Canonical gameplay opts in through `--ground-chunks` with the default rim settings or
  tuning; drafts and previews stay on the legacy path for this experiment.
  `VulkanGroundChunkCache` publishes only after its upload fence signals; stale
  generations remain until both upload and referencing frame fences finish.
  Budget is 8 MiB per generation / 32 MiB retained device-local buffers, with
  separately reported temporary staging bytes. Warm frames upload no geometry;
  dynamic SSBO entries carry per-tile paint/tint/grid state. Preserve draw-budget
  reserve for these entries and fallback caps while publication is pending.
  Preserve the requested immutable generation through temporary flat draw-budget
  fallback so its upload can finish and a fresh frame can adopt the rim chunks.
  `--disable-ground-meshoptimizer` retains chunks for lossless-pass comparisons.
- `GroundLevelGeometry` compiles authored static ground through `tileVisual`,
  manifest scales and shared exposure rules. Content staging writes versioned,
  bounded, checksummed `.grm` cap artifacts for every canonical screen and the
  fully composed overworld in normalized world coordinates, with package-index
  entries. Increment `processedGroundArtifactCompilerRevision` when topology,
  fracture, normals, coverage or geometry-key semantics change. The incremental
  stage verifies full artifact parsing and current semantic fingerprints before
  skipping; missing/corrupt/stale derived geometry must trigger a restage.
  `RuntimeGroundGeometryStore` checks the semantic fingerprint before publishing
  an owning immutable artifact. Main/preview stores are separate; stable source
  and boundary revision performs no I/O or allocation. Invalidate on canonical
  publication and manifest reload. A cached miss retries after invalidation or
  revision change, so new build output may require a level reload for adoption.
  Frames retain artifact ownership after store changes. Surface-cache import
  requires exact resolved model/origin/elevation/mask/profile keys; visibility,
  GPU readiness and draw-budget fallback remain authoritative. Drafts use the
  live compiler. Cached baked provenance may outlive the provider; active baked
  count, imports this preparation and total imports describe different things.
- The default-on **Smooth Ground Rim (prototype)** control, or `--ground-rim`,
  requests broad chipped facets with nominal tile-unit width/depth. It requires
  ground processing. Disable it with the control or `--disable-ground-rim`.
  `GroundRimGeometry` and `GroundRim.glsl` share the same
  triangulated height field. Integer hashes of world grid corners determine
  width and depth; exposed outer borders interpolate corner depths linearly
  so arbitrary authored body edge segments seal. Hidden cap borders share
  corner radii/ramps, and concave corner patches use the opposite diagonal.
  Irregular inner strip knots straddle the tile midpoint; their inset widths
  stay below the corner diagonals and 0.40 tiles to keep the central fan valid.
  CPU cap tessellation, body deformation and shadows must agree on that field.
  Rim patches carry per-vertex wall coverage: lowered border vertices are one,
  flat inner vertices are zero. Preserve coverage through preparation and
  triangle encoding. GroundSplat passData[2].xyz holds published wall
  albedo/normal/ORM handles; passData[3] holds the four coverage weights.
  materialOptions.x and textureOptions.w hold the source tile origin for
  wall UVs; passData[1].yz still holds the independent paint-region origin.
  Interpolate coverage on the same quad triangles, then smooth it in the
  fragment shader. Blend wall/paint albedo, ORM and world-space normals before
  lighting. Wall UV0 uses tile-local tangent and source height / 2.5, so the
  lowered outer border starts at V=0.4. Blend side projections at corners and
  preserve N/E versus S/W normal handedness. The cap uses an average sandstone
  tint; authored wall plates retain their individual factors and inherited
  per-tile UV seams. Missing/unpublished wall albedo disables the material
  blend; optional data maps have neutral fallbacks. Require all rim maps via
  GroundSplatTextures::sampledTextures and check publication without allocating
  a descriptor snapshot. Geometry, shadow silhouettes and draw counts stay
  unchanged by this material treatment.
  Compress only the upper body band; maximum varied depth (1.35 times nominal)
  must remain below that band to keep
  its deformation monotone. Preserve UVs and tangent handedness, transform
  normals with the inverse transpose Jacobian and tangents with the forward
  Jacobian.
  Logical square cells and full-cell picking remain intact; paint follows the
  visible cap. Resolve the treatment for the whole participating view only
  after every source mesh is resident and validated, and recheck after asset
  maintenance. Readiness changes must reprepare the cap scene and its shadows.
  `--evidence-ground-rim-fixture` requires a bounded `--evidence-output` run;
  compare defaults against `--disable-ground-rim` to inspect boundaries and cap/body seams.
  Main/preview/UI share the existing draw-instance budget. Check the prepared
  scene reserve before recording and flatten both rim cohorts if it does not
  fit; point-shadow batches use the same reserve and can fall back to pushes.
  Keep the fallback latched for that prepared-frame lease and report resolved
  rim tiles/fallback in evidence. Curved rims, arbitrary mesh unions and
  cross-layer terrain shaping remain
  outside this prototype. Ground-body counters omit the extra cap geometry;
  compare total triangles, draws, memory and timings before making cost claims.
- The scene target is floating-point linear light. Tonemapping writes linear
  values to the sRGB display attachment, which performs the only display encode.
  Player-facing UI is composed after tonemapping.
- Terrain companion textures use `<albedo manifest name>Normal` and `...Orm`.
  GroundSplatTextures resolves them for default, overworld and editor-assigned
  layers; sampledTextures is the residency list. Ground masks store four
  normalized material weights in RGBA, with alpha used as texture 3 coverage.
  Legacy grayscale masks convert to (1-gray,gray,0,0) before upload/BC7 and
  painting; RGBA saves preserve every layer. Ground passData[0] packs normal
  handles (0,2)/(1,3) in xy and ORM handles (0,2)/(1,3) in zw as exact 24-bit
  integers (two 12-bit handles). passData[2].w packs albedo handles 2/3;
  passData[1].x carries specular strength. The splat and model shaders share
  PbrLighting.glsl, retaining their distinct point-shadow tap counts. Terrain
  maps are linear repeat data and use the color layers' world UVs and weights.
  GroundRock model bodies use dedicated wall maps and UV0 tangents. Regenerate
  terrain maps before model variants; see docs/pbr-art-pass.md for coverage and
  remaining procedural-surface art gaps.
- Scene alpha stores the ambient-to-total-lit ratio for opaque pixels so SSAO
  attenuates ambient contribution without darkening direct or emissive light.
  Blended scene pipelines preserve the opaque mask behind them.
- The 3D sky draws once at the start of each initial opaque scene pass,
  including preview insets. Its three procedural cloud planes use fixed
  negative world Z heights and the shared effect animation clock for parallax
  and drift. It writes zero ambient-mask alpha and no depth, and is present
  in water refraction, atmosphere and world transitions. Thumbnail and animation
  preview studio frames disable `skyCloudsEnabled`. Shader colors are linear;
  tuning lives in `shaders/sky_clouds.frag.glsl` and supports shader hot reload.
  Noise corners use integer coordinates and hashing so neighboring cells agree
  exactly; floating hashes can introduce seams through multiply-add contraction.
- Set 0 contains scene/frame resources. Set 1 contains the variable-count
  sampled-texture heap and is the only binding in that set.
- Manifest textures own stable low descriptor indices. Discovered glTF textures
  own stable high indices. Appended manifest entries grow upward; newly
  discovered maps claim free high slots downward. Existing descriptors never
  move, and the two ranges must not overlap.
- Editor-appended models run the same material resolver and binding remapper as
  startup. Normal, metallic-roughness, emissive, and occlusion maps must work
  immediately without a restart.
- Descriptor updates are frame-local and occur only after the corresponding
  fence. Every allocated texture slot has a fallback descriptor. Evicted GPU
  resources remain alive until all submitted frames that reference them retire.
- Material-buffer entry zero is the fallback. Published model ranges remain
  stable while live and return to the allocator only after fence-owned
  retirement.
- Prepared skinned meshes retain their source payload when residency admission
  is deferred. Ownership moves only after admission succeeds, so retry does not
  require decoding again.
- Combined-image-sampler heap capacity is the minimum remaining capacity across
  per-stage and aggregate sampled-image and sampler limits after scene bindings.
  Every device-selection caller must provide all four descriptor requirements.
- A blocking asset wait retries retained CPU-ready payloads. It throws when
  queued work is blocked by retained data and no active operation can change
  admission; it must never poll that terminal state indefinitely.
- Resized painted textures publish transactionally. A failed replacement keeps
  the old handles and residency accounting live, and the paint revision remains
  pending until the renderer reports a successful update.
- Projected water-ripple screen derivatives execute before the depth-dependent
  geometry branch. Conditional cellular searches may consume an analytically
  propagated footprint, but must not invoke implicit derivatives themselves.
- Water's static cell features are shared by the compute cache and procedural
  fallback in `WaterCellFeatures.glsl`. Binding 16 uses one buffer per frame in
  flight; update only after that frame's fence and preserve the compute-write
  to fragment-read barrier. Pipeline reloads invalidate both windows. Fragment
  specialization constant 1 enables the cache; `--disable-water-cell-cache`
  removes lookup work for a procedural performance control. Compare it using
  `tools/RunPerformanceSuites.ps1` on the same hardware and power state.
- One-shot command-buffer and fence lifetime belongs to
  `vulkanResources::beginOneShotCommands` and `submitOneShotCommands`. Preserve
  their cleanup behavior and diagnostic labels.
- A barrier that discards an image (`UNDEFINED` old layout) still names how the
  previous frame used that image as its source scope; an empty scope does not
  order it after that frame. The swapchain image's first barrier uses a stage
  the submit waits for the acquire semaphore on, and its last one names a
  destination stage so the render-finished signal covers it. A depth resolve
  target is written at `COLOR_ATTACHMENT_OUTPUT`, not at the fragment-test
  stages. A read-only depth attachment is stored with `STORE_OP_NONE`.
- A frame that runs out of draw-instance entries drops draws instead of
  failing. A single-instance draw may use the zeroed discard slot. A draw of
  several instances covers only `contiguousDrawInstances` of them, and a draw
  that needs all of its entries, such as a ground chunk, is skipped whole and
  counted with `recordDroppedDrawInstances`.
- A skinned glTF vertex may only carry weight on a joint of its skin; the
  loader rejects any other index. The index beside a zero weight is not
  checked.
- Animation key times are finite and never descend; `loadGltfAnimationClip`
  rejects a channel that breaks this, because the sampler bounds its search by
  the last key time. Equal neighbouring times are allowed.
- `collectRuntimeTextureCatalog` inspects its glTF documents on the shared
  task system and merges the results in manifest order. Its output and the
  failure it reports must not depend on thread timing: a failure is reported
  for the earliest step a sequential walk of the manifest would have failed.
  `inspectGltfAssetDependencies` and `resolveGltfMaterialTexturesUnder` are
  called concurrently and must stay free of shared mutable state.
- `VulkanSwapchainResources::beginOutput` ends drawing into the scene target
  once per frame, before bloom. Bloom and the tonemap both sample that target
  in place through binding 11. Neither refreshes the scene snapshot of
  binding 1, so a pass after bloom that samples binding 1 sees the last
  mid-frame copy, not the finished scene. Only the preview overlay does, and
  it wants the copy taken before the preview.
- A resize or a render-scale change keeps the pipelines. The two descriptor
  set layouts live in a shared `VulkanSceneDescriptorLayouts` that the next
  generation of descriptors adopts, and `createRenderResources` leaves the
  active pipelines in place when `VulkanPipelineFactory::builtFrom` says a
  new factory would be built from the same inputs. A field added to
  `VulkanPipelineFactory::CreateInfo` must be added to `builtFrom`, and a
  pipeline must not start to depend on a render target's size.
- Whether a surface is one of the animated energy materials is asked of
  `isEnergyMaterial`, `isEnergyEffect` and `energyMaterialOrStandard` in
  `IsoScenePreparer.hpp`. A new energy effect is added there, not to the
  call sites.
- Primitive topology is set once per pass, not per quad:
  `bindScenePassState` sets it for `recordScenePass` and `bindUiPassState`
  for `recordOverlayRendering`. That holds only while the post-process
  pipelines, which carry a static topology, are never bound inside one of
  those passes.
- The passes in `VulkanSceneRecorder.cpp` are a short function that names
  the steps and one member function per step. The split of 2026-11-25 moved
  code without changing a command: captures before and after are
  pixel-identical. A step that binds its own pipeline or changes depth state
  in the middle of a pass puts back what the next step expects, as
  `drawSkyClouds` and `drawModelBatches` do.
- `VulkanPipelineFactory::create` loads each shader module into a local
  named after its catalog entry, and the modules are destroyed when `create`
  returns or throws. Every pipeline accessor returns a valid handle once
  `create` has returned, so the recorder does not test them.
- Every one-shot command buffer goes through `beginOneShotCommands` and
  `submitOneShotCommands`. The UI image upload waits on its own fence, not
  on the whole queue.
- The draw-instance reserve that the rim guard and point-shadow batching
  share is in `DrawInstanceBudget.hpp`.
- A shader that samples after a per-fragment branch, early return or loop
  exit names its mip level with `textureLod`. An implicit level of detail is
  undefined there. The shadow maps and the atmosphere target have one level,
  so the lookups in `DirectionalShadow.glsl`, `PointShadow.glsl`,
  `atmosphere.frag.glsl` and `atmosphere_composite.frag.glsl` ask for level
  zero.
- The island mist is a second, bounded medium that hangs below the level, so
  the sun has air to draw shafts in beside the island and under it.
  `RenderFrameData::Lighting::IslandMist` carries it and is off by default:
  a frame that builds its own lighting, such as a test or the thumbnail
  studio, has none, and `AnimationPreviewScene` turns it off.
  `PresentationSettings::renderLighting` fills it on every call from the
  tunables in `IslandMistConfig.hpp`, behind the Lighting panel's
  **Island Mist** switch. The recorder hangs its box under
  `framedBoardArea(frame)`, the rectangle the camera frames, so in the
  overworld the mist stays with the screen in view instead of filling the
  air under the whole map. `framedBoardArea` repeats the extent arithmetic of
  `calculateIsoLayout`; keep the two in step. The mist is recorded in the
  `AtmosphereVolumes` GPU phase before the fog of war. The box ends at the
  top height, and the composite leaves alone any surface the camera ray
  reaches before the box, so the mist never changes a pixel of the level
  above it. The `island_mist` regression
  (`sokoban_preview_rendering_tests --island-mist`, captures in
  `out/island-mist-evidence`) checks that, the island's shadow in the mist,
  and the smoothness described below. It sets its own mist values, so
  retuning the header does not move it.
- The volumetric pass carries a sun shadow on past the far end of the sun's
  depth range. `sunVisibility` in `atmosphere.frag.glsl` compares against the
  far plane there instead of calling the sample lit, which relies on the
  shadow map being cleared to one. `DirectionalShadow.glsl` still treats such
  a point as lit, because no surface lies there. On background pixels a
  bounded medium is marched through its whole box: only a surface ends the
  ray, since the camera's far plane sits about a tile behind the scene.
- A medium thins above its base height by `heightFalloff` and below it by the
  depth falloff in `volumeProfile.x`, the `textureOptions` lane at
  push-constant offset 240. `volumeProfile.y` measures a medium's thickness
  straight down instead of along the ray, so that it does not thicken toward
  the top of the screen or under a flatter camera. The island mist sets
  both; the atmosphere and the fog of war leave them zero.
  `atmosphereDensityAtHeight` is the CPU reference for the falloffs. A
  bounded medium's shape, noise and ambient share reach the shader through
  the recorder's `AtmosphereVolumeStyle`; the fog of war and the island mist
  each fill one.
- `atmosphere_composite.frag.glsl` reconstructs the quarter-resolution medium
  from a 3x3 texel neighbourhood with per-axis weights `(1 - f) / 2`, `1 / 2`
  and `f / 2`. Every parity of texel gets the same weight, which cancels the
  integration pass's 4x4 ordered dither; the bilinear weights used before
  left it visible as a weave in any medium dense enough to see.
  `atmosphereReconstructionWeights` is the CPU reference, and
  `atmosphereIntegrationRect` keeps two integration texels around a volume's
  scissor for that reach. Change the dither, the weights and the margin
  together. The island mist needs its 24 samples: at 12 the shafts' edges
  stair-step.
- The cloud light breaks the sun up as if clouds passed overhead: a drifting
  pattern of gaps that dims the sun on surfaces and in the air, so pools of
  light cross the board and the mist's glow separates into shafts.
  `RenderFrameData::Lighting::CloudLight` carries it and is off by default,
  like the island mist: a frame that builds its own lighting has none, and
  `AnimationPreviewScene` turns it off.
  `PresentationSettings::renderLighting` fills it from the tunables in
  `CloudLightConfig.hpp`, behind the Lighting panel's **Cloud Light**
  switch. `cloudLightLanes` in `SceneDrawLanes.hpp` packs it into the last
  four lanes of `SceneFrameUniform`, which is 10400 bytes now, and
  `shaders/include/CloudLight.glsl` reads them. All four are zero when it is
  off, and the shaders test the strengths. A lane added to that block goes
  in three places: the struct, `SceneFrame.glsl`, and the member lists in
  `GpuAbiTests.cpp`, where `gpu_abi` compares the two.
- Every shader evaluates the cloud pattern where the sun ray through its own
  position crosses height zero (`cloudLightCoordinate`, with a CPU reference
  of the same name), so a shaft in the air and the pool under it are one
  piece. The sun counts as no lower than `lowestCloudLightSunHeight` there.
  The clouds drift by the view's effect clock, which
  `VulkanSceneDescriptors::updateFrame` takes as its last argument; the main
  view and the preview each pass their own. An evidence run freezes that
  clock, so its captures show the pattern where it starts unless
  `--evidence-animate` is given.
- The cloud light dims the sun's direct term and nothing else.
  `triangle.frag.glsl` and `GroundSplatFragment.glsl` multiply their shadow
  factor by `cloudLightOnSurface`; ambient light, point lights and water,
  which has no sun term, are untouched. A surface is never brighter than
  under an open sun, and it sees the broad pattern only. A new lit shader
  that takes the sun should do the same. A medium calls `cloudLightInMedium`
  with its own strength and gain, which `cloudLightMedium` chooses and the
  recorder passes in `volumeProfile.z` and `.w`. The atmosphere and the
  island mist get the medium strength and the gain. They also see finer
  breaks inside the gaps, drawn out along `cloudLightRayAxis`, the direction
  a line of sight sweeps across the pattern, because round breaks average
  out along a ray of sight. That needs `SceneCamera::forward`, which
  `cameraFor` fills from the iso layout. The fog of war, whose
  `AtmosphereVolumeStyle` sets `readsAsSurface`, is lit as the ground under
  it is: the surface strength, the broad pattern only and no gain. It says
  so by passing `cloudLightGainOfASurface`, zero, where a real gain is never
  below one.
- `cloudLightGap` spreads its two octaves about one half by 2, and the
  detail's single octave by 1.45, so that each value is close to its own
  percentile and **Coverage** is a share of the sky. Change the octave
  weights and the spreads together. The `cloud_light` regression
  (`sokoban_preview_rendering_tests --cloud-light`, captures in
  `out/cloud-light-evidence`) checks that a cloud never brightens the island
  or, without gain, the mist; that part of each is dimmed and part keeps its
  full sun; that the pattern moves with time; and that the gain lifts the
  gaps. It sets its own values, so retuning the header does not move it.
  `scene_draw_lanes` covers the lanes.
- The fog of war takes no cast shadows. It reads as a cover, and a shadow
  inside it showed as a dark hole beside every cliff and tall object, so
  `atmosphereConstants` turns the shadow map off for a medium whose style
  sets `readsAsSurface`. The island mist and the atmosphere keep theirs.
- Ice and crystal are blurred solids: the translucent pass draws them and
  writes no depth. `drawBlurredSolidDepth` writes their depth, and no
  colour, at the end of the opaque pass, so the published scene depth holds
  them, and the fog of war, SSAO and water end at their surface instead of
  at the ground behind it. It draws them with the outline shaders at no
  width, which place a mesh where it stands and `outlineDepthMaskBias`
  behind its surface, so the solid's own colour still passes the depth test
  in the translucent pass. A new blurred solid needs only `blurBehind`.
- An outline hull is pushed outward on the screen. Under a camera that looks
  down, that carries the vertices around an object's foot below the floor,
  where the ground hid them. `outlineRestingOnFloor` in `Outline.glsl`
  slides such a vertex back along its line of sight until it rests on the
  plane of the object's own base, the height of its model transform, so the
  outline neither thins nor comes away along the bottom. A vertex that was
  below that plane before the push is left where it is. Hulls write no
  depth, so the fog of war still covers the part of an outline that lies
  outside its object.
- A blended scene shader must not write an alpha above one. The scene target
  is floating point and does not clamp, so the blend would subtract the
  background. `mirror_energy.frag.glsl` boosts alpha for its rim and pulse
  and clamps the product.
- The SSAO CPU time in the recorder telemetry covers the SSAO phase alone
  since 2026-11-25. Before that it also covered atmosphere recording and the
  mirror continuation, so earlier numbers are not comparable.
- A shader must not copy a whole `DrawInstance` into a local. It costs 256
  bytes per invocation, and glslang 14 gives the local an array type that the
  spirv-val of validation layers 1.4.363 rejects.
- `GameplayPresentation` keeps its world animation clock and its transition
  clock as `double` and hands them out as `float` wrapped at
  `clockWrapSeconds` (65,536 s). Everything driven by them may jump once at
  the wrap. A consumer must not store a reading and compare it with a later
  one without allowing for that; `AnimationController` ends a crossfade in
  progress when the transition clock wraps. The conveyor scroll offset is
  taken from the unwrapped clock and does not jump.
- Both bloom passes space their taps in texels of the half-resolution bloom
  target. The extract pass keeps nine discrete taps so that each lands on a
  target texel centre and no source column is skipped.
- The bloom has two scales. The extract and blur passes leave a tight core in
  the bloom target. `bloom_wide_horizontal.frag.glsl` then spreads that core
  across into the extract target, which is free again, and
  `bloom_wide_vertical.frag.glsl` spreads it down and lays it over the core
  with the atmosphere's blend, source + target * source alpha. Each of the
  two reads only the target the other writes, so they need separate shader
  files. `bloomMix` in `SceneDrawLanes.hpp` turns the two intensities in
  `BloomConfig.hpp` into the tonemap's multiplier and the shares that pass
  writes, and a skirt of no strength is not drawn. Only a frame whose
  `OutputTransform::bloomWideEnabled` is set gets the skirt, which
  `PresentationSettings::renderOutputTransform` does: a frame that builds
  its own output transform keeps the core alone, so the GPU regressions that
  measure where an effect lands are not washed by it. The skirt's taps may stand
  `bloomWideSpread` texels apart only because the core is already a Gaussian
  1.7 texels wide; past about twice that, a bright point breaks into dots. Its
  width is in texels, so it is narrower on a larger display.
- Ambient life is decoration that is no part of the game's state, today the
  motes in the air. `RenderFrameBuilder::GameplayInput::ambientLife` asks for
  it and only the two frames the player watches set it, so a screen preview
  stays still and a test that counts an effect's particles counts only
  those. The Lighting panel's **Ambient Motes** switch gates it.
  `appendAmbientMotes` in `AmbientMotes.cpp` keeps no state: a mote is a
  function of the time and of its cell in a lattice that drifts with the
  wind, so the field can move or resize without a mote jumping, and each one
  fades at the faces of its box. It asks `cloudLightGap`, the CPU twin of the
  shader's function in `SceneDrawLanes.hpp`, how much sun reaches it. Motes
  are ordinary particles: depth-tested, never on top, and covered by the fog
  of war like anything else drawn without depth. Their texture reaches the
  renderer through the frame's own requirements, so on a level that loads it
  for nothing else they appear a few frames after the level does.
- The GPU material table holds 4096 entries. A catalog-wide regression checks
  that every current slot fits, the fallback and attachments included.
- The Vulkan smoke and preview regressions use an isolated legacy-ground
  fixture for processing, rim and chunk checks, and the current staged native
  kit for authored-ground rendering checks.
- `sokoban_core` must remain Vulkan-free. Shared arithmetic belongs in an
  SDK-independent header; `tools/check_core_is_vulkan_free.sh` enforces the
  boundary in CI.

## Decisions that require new evidence

Do not split `VulkanModelResources` merely to reduce file length. Its texture,
model, residency, publication, and retirement state has interleaved lifetimes;
extract a component only when the new owner makes a transition easier to prove.

Use a measured workload before adding a transfer queue, secondary command
buffers, update-after-bind descriptors, or a new task/allocator architecture.
Run the maintained performance suite and application evidence modes to establish
current CPU/GPU costs, synchronization waits and publication pressure.

Keep point-shadow sampling policy deliberate: the scene and ground select
different tap counts from `shaders/include/PointShadow.glsl` because their cost
scales differently. Validate changes with the point-light evidence modes.

Startup construction overlaps audio initialization and independent Vulkan
resource setup, applies the saved window mode before the first swapchain, and
emits first-frame plus Vulkan/audio subphase timings. The comprehensive
performance runner records matched cold/warm application launches. Collect a
fresh baseline when changing those paths.

Package validation, the manifest parse and runtime texture discovery start
before the platform window exists and run on workers
(`Application::contentRootStartup_` and the two futures after it). Each
worker logs its own phase; `content-wait` and `manifest-wait` are how long
the main thread stood still for them, which is the part that costs launch
time. Texture discovery reads the worker's manifest and `assetManifest_` is
a copy of it, so nothing may change the manifest between that copy and
renderer construction. Join discovery before physical-device selection,
preserving the exact descriptor requirement and stable texture identities.
A renderer that is not handed a discovery future starts its own, as the
tests do. SDL and Vulkan initialization still run on the main thread.
Window, instance, application first-frame and UI subphase logs distinguish
driver waits from asset work. Compare binaries under the same complete
executable path: vendor Vulkan layers can behave differently for renamed
diagnostic executables.

Asset requests have three priorities. `Visible` is what a frame draws, and
`drawFrame` asks for its own exact set every frame. `Upcoming`
(`VulkanRenderer::warmAssets`) is what the loaded world will need but may
not be drawing: a puzzle's level-wide set, and the action clips and brief
effects of both worlds. `Prefetch` is speculative. Work starts and publishes
in that order. `applyLevel` warms rather than ensures, and the first frame
then asks for what it draws. A world change cancels queued upcoming work
together with prefetches; other prefetch refreshes cancel prefetches only.

A level-wide set names the native ground and cliff bodies the board draws,
not every body of the variants it uses: 8 to 79 models for a shipped puzzle
screen, where naming every body made it 27 to 265. Which body a cell draws
depends on which of its neighbours are drawn with it, so
`renderAssetRequirementsForLevel` builds the tiles those cells emit
(`tileVisual`, the authored rotation, the support below) and runs
`processCliffWallGeometry` and `processGroundTileGeometry` over them, the
functions frames use. Do not give the planner selection rules of its own;
they would drift from the frame's. Rock over a cell with no fixed support,
in a level that has ice, gets both its regular and its bottom body, because
a block can arrive under it or leave. Anything else that lets play change
which ground is drawn has to be added there. A miss is never a wrong
picture, because the frame still asks for what it draws, but that body
streams in late and the board shows the loading overlay for it.
`ground_tile_geometry` checks every shipped puzzle screen and every
overworld view against the frame it produces. An editor changes bodies with
every stroke, so `Application::warmEditedModuleBodies` warms every body of
the variants an edited frame draws (`moduleBodyVariantsInFrame`,
`renderAssetRequirementsForModuleVariants`), for the developer editor and
for Maker edit frames; a hover preview is a tile of the frame and warms its
variant as soon as the brush shows it.

Do not queue the whole campaign or composed overworld at startup. After the
first draw, speculative work covers only the overworld screens one crossing
away and the selectable targets on the active overworld screen, or the
immediate next puzzle screen. Refresh after screen transitions and profile
changes. On the title, a saved puzzle warms only its
current screen, and a saved overworld warms the visible neighborhood of its
active screen (`renderAssetRequirementsForOverworldNeighborhood`) ahead of
that screen's selectable puzzles. The neighborhood is planned from the
composed level with a cell filter: a single overworld screen has no hero
start and cannot be loaded as a level on its own.

In the overworld a frame draws the active screen and the screens around it,
and a step across a seam makes a screen sharing an edge the active one.
`OverworldTransitionAssetPlanner` warms one such crossing ahead, at
`Prefetch` priority and before the selectable puzzles: the board content of
every screen around an edge neighbour, and the bodies of each set of
screens a crossing draws, which is both neighbourhoods while the view slides
and the destination's once it settles. Bodies are resolved per drawn set and
not per screen because the sets differ at their edges: the last drawn cell
of a run of rock is an end, and a square cohort follows what is drawn.
Resolving a set is one pass of the frame's ground selection over all of its
tiles, about 10 ms in a Debug build for the shipped map, and a first plan
has up to eight sets. `serviceUpcomingAssetPrefetch` therefore resolves one
set a frame, and the planner keeps resolved sets for the map's fingerprint,
so a crossing back, or a plan from a screen seen before, resolves nothing
again. The puzzles are queued once the planner has finished. A live manifest reload
discards what the planner resolved, because tile scales decide which ground
is native. The four screens two slots away on both axes are left out, since
no single crossing shows them. The active screen is whichever holds the
player, so anything that moves the player further than one screen streams
from its frames as before, and so does the whole-map overview.

Source-watcher scanning begins after the first draw. Hidden docked editor
tabs must not run content callbacks for ImGui's initial sizing: they can
synchronously load thumbnails and scan source assets. The decoration mesh
library walks the whole source asset tree, so it scans on first use
(`ApplicationTools::decorationMeshes`) and not at launch. The workspace's
ImGui regression covers lazy tabs, tab focus and floating windows.
Visible palette rows request thumbnails without decoding or uploading inline;
at most one pending thumbnail loads before each later UI frame. Missing files
remain cached, and invalidation clears both cached and queued work. The
`thumbnail_loading_budget` Vulkan regression checks the deferred upload budget,
deduplication, cached misses and progressive reloads.

Asset discovery reads only GLB JSON and KTX2 headers, mip indices and format
descriptors; actual background loads still read the full payload. Both metadata
paths preserve container validation, and KTX2 estimates include mip descriptors
so prepared-memory admission matches the loaded payload. Startup asset logs
include validation, manifest/catalog, font and prompt phases; the comprehensive
runner retains them in `AssetPhasesMicroseconds`.

A frame publishes assets until it reaches
`AssetLoadingBudget::maxPublicationsPerFrame` (64) or a time budget:
`visiblePublicationMicroseconds` (4 ms) while the presented frame is missing
assets it asked for, `backgroundPublicationMicroseconds` (2 ms) for
everything else. The time is checked after each publication, so the first
one of a frame always completes; zero removes a time limit. One publication
per frame, the previous rule, made a scene's load time its asset count in
frames. Three things make more than one safe, and the `asset_streaming`
Vulkan regression holds each:

- Every finished decode is collected each frame, whatever the publication
  limits (`collectDecodedAssets`). A result waiting to publish must not hold
  a worker slot. Decoded payloads waiting to publish are bounded by
  `preparedAssetBytes`.
- Assets the latest visible request named publish before anything else.
- An upload that does not fit the shared ring is postponed to a later frame
  and counted in `uploadRingDeferrals`; it used to throw and fail the asset
  for good. The blocking loader waits for the uploads in flight instead. A
  single upload larger than the whole ring still fails.

The application sets `maxConcurrentCpuJobs` from the worker count
(`assetCpuJobsForWorkers`: two workers stay free for the frame's parallel
loops, between two and eight jobs). Decode jobs share the task system's
queue with `parallelFor` helpers, which wait behind them; do not raise the
cap past the spare workers without giving decoding its own lane.

The loading overlay and `boardAssetsLoading()` count only what the presented
frame is missing (`visiblePendingAssets`), not warm or prefetched work. The
renderer logs `Visible assets ready` and `Asset queue drained` with asset,
publication and frame counts and milliseconds each time a stretch of
streaming ends; use them to judge a budget change on real hardware. The
Vulkan startup benchmark uses staged game assets and retires uploads between
bounded publications. The `--asset-publication-budget` mode checks the
renderer's count cap with both time limits off.

## Documentation rule

Update this file when an enduring contract, supported command, current limitation
or immediate next step changes. Keep maintained design documentation and examples
under `docs/`; write generated reports, captures and temporary probes under
ignored `out/` or the build directory. Completed history is available through Git.
Replace stale validation results instead of appending a chronological roadmap.
