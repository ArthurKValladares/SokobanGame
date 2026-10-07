# Sokoban 3D handoff

Updated 2026-10-06. This file describes current operating guidance, implementation
contracts and known limitations. [README.md](README.md) covers player controls,
authoring formats, supported commands and packaging.

## Current status

Sokoban 3D is a C++20, SDL3, Vulkan 1.3 project. Runtime content is declared by
`assets/manifest.json`, staged by `sokoban_content`, and validated against
`content.index` at startup.

Gameplay includes five hero types, pushable mirrors and copies, linked movable
objects, portals, buttons, rotators, lock plates, elevators, minecarts, and
lecterns. Rules, replay and solver search share the production action planner.
Developer builds expose transactional level/asset/animation editors, live tuning,
shader reload, profiling and source watching.

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

The October 6 completed-rim-surface cache built `dev-fast-all` and the Debug
game with warnings-as-errors. All 100 optimized suites passed across the full
run, the corrected Iso/cache/allocation reruns and the same two unchanged audio
suite reruns outside the sandbox. Warm serial/parallel rim preparation stays
allocation-free during camera/paint changes, reordering and selective profile
edits. The fixture and two real-screen captures match fresh pre-cache RGB images
exactly, with unchanged draws, triangles and GPU allocation memory. Settled
frames reuse all surfaces and generate none; retained capacity is roughly 3 KiB
per active rim tile. Mean preparation time fell in the short matched captures,
but real-screen p95 timings increased, so do not claim a frame-rate improvement.
A 240-frame Debug fixture produced no VUID errors and matches the optimized
image exactly; the existing Epic overlay loader error and unused shader-output
warnings remain. Ignored evidence is in
`out/ground-geometry/rim-surface-cache/comparison.md` with images, traces and logs.
Full Debug, headless, Linux and shipping suites were not run for this change.

The October 6 rim material fade built `dev-fast-all` and the Debug game with
warnings-as-errors. All 99 optimized suites passed across the full run and the
same two unchanged audio-suite reruns outside the sandbox. New checks cover
wall-map requirements in default/custom/editor/preview materials, coverage
interpolation at every neighbor layout, and serial/parallel metadata parity.
The 240-frame Debug fixture reported no VUID errors and exactly matches the
optimized RGB image; the flat fixture also exactly matches its prior image.
The known Epic overlay loader error remains. Evidence is in ignored
`out/ground-geometry/rim-material-fade/`, including comparison crops (before
above, fade below), `pixel-check.json`, and test/build logs. The material-only
change retains 25 fixture draws, 24,222 submitted triangles and the same GPU
allocation memory. Full Debug, headless, Linux and shipping suites were not
run for this revision.

The October 6 irregular-rim revision built `dev-fast-all` and the Debug game
with warnings-as-errors. All 99 optimized suites passed across the full run and
the same two unchanged audio-suite reruns outside the sandbox. Coverage includes
all 256 neighbor layouts at several world origins, exact joins along the actual
20 source/PBR model borders, positive facet areas/Jacobians, paint picking,
shadow consistency and the 5,000-cell draw-budget fallback. A 240-frame Debug
fixture reported no VUID errors; its RGB capture exactly matches the optimized
capture. The known Epic overlay loader error remains. Ignored evidence is in
`out/ground-geometry/irregular-rim/`: comparison crops show the uniform rim above
the new irregular rim, and `pixel-check.json` records the exact Debug comparison.
Matched flat/irregular fixture captures retain 25 draws and the same GPU
allocation memory; irregular caps add 1,044 submitted triangles. This is a
short visual experiment, not a performance guarantee. Full Debug, headless,
Linux and shipping checks were not run for this revision.

The October 6 cached-boundary/rim prototype also built `dev-fast-all` and the
Debug game with warnings-as-errors. All 99 optimized suites passed across the
full run and a two-suite rerun outside the sandbox; the sandbox denied an
ancestor lookup in Windows missing-path resolution for the unchanged audio
import tests. The actual renderer regression restores flat ground for 5,000
isolated rim tiles without dropped draw instances, matching an explicitly flat
frame exactly. A 240-frame Debug rim fixture reported 17 resolved rim tiles,
all six point-shadow faces reused, and no VUID errors. Its RGB image matches
the optimized fixture exactly. Detailed evidence is in ignored
`out/ground-geometry/rim-prototype/comparison.md`.

The preceding October 6 ground geometry experiment built `dev-fast-all` and the Debug
game with warnings-as-errors. `ctest --preset dev-fast` passed all 99 registered
suites, including ground exposure/mesh validation, content imports/staging,
solution replay, Vulkan smoke, text rendering and the package-validation
fixture. A 240-frame Debug `--require-validation` capture with dense ground,
water and point lights completed without VUID errors. Its missing Epic overlay
manifest and unused shader-output warnings also occur in the prior baseline.

The full Debug suite was not rebuilt or rerun for this experiment. Its prior
baseline passed 95 of 96: `scene_preparation_allocations` reports allocations in warmed
gameplay, editor, menu and profiler paths. Its scene-preparation and parallelFor
checks remain allocation-free. The Debug failure is an open validation issue;
the optimized configuration passes that suite. Reproduce it with
`ctest --preset dev -R '^scene_preparation_allocations$' --output-on-failure`
before claiming a passing Debug baseline.

Linux, sanitizer/static-analysis configurations, a fresh headless build, and
Release/shipping packages were not rebuilt for this experiment. A passing package
fixture is not acceptance of a newly produced shipping ZIP. Follow the full
release checklist before publication.

Player profiles still use a fresh-start policy for older formats rather than
migrations. Define compatibility before promising public-release save upgrades.
Source models require a restart after edits. Text supports the configured
fallback scripts; color emoji, bitmap-only fonts and a localization-resource
system are not implemented. Additional scripts need licensed fallback fonts.

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
  and `endStroke` writes that record. Any recorded edit clears the redo stack.
  Undo, redo, save shortcuts, draft playback and document switches close an
  open stroke first. Compound commands that make intermediate edits (such as
  `moveObject`) suppress recording and record once themselves; they must not
  resize the history vectors. Redo is carried everywhere undo is: the draft
  cache and screen-identity remaps.
- Every editor control is an `InputAction`. `inputActionContext` decides
  which actions may share a key; a new action needs a context, a default, a
  name, and (when it is a player-facing control) an Options row. Adding one
  changes the profile document, so bump the format.
- Keyboard bindings may carry Ctrl/Shift/Alt modifiers. Among the bindings on
  one key whose modifiers are all held, only those needing the most
  modifiers fire, for held and pressed queries alike. Capture records a chord
  on the non-modifier key's press and a lone modifier key on its release.

- Recorded solutions (`engine/Solution.hpp`, `solutions/`) use format 2.
  Each input carries its complete settled GameState, active hero controller
  and automatic-motion pause state. Update the solution state codec and its
  field-mutation/round-trip tests whenever GameState changes. Old formats are
  rejected and must be re-recorded through the current Driver.
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
  mechanical completion boundaries; never reintroduce presentation-frame work
  into this headless path. The recorder, the
  automatic in-game saving and `sokoban_solve_level` all go through the
  same driver. Changing the text format means bumping `format` and
  re-recording, not migrating.
- `solution::reconcileStore` (`engine/SolutionStore.hpp`) is the only code
  that decides where a recording lives: one `level<L>-screen<S>.solution`
  per current screen holding the shortest run (ties keep the file in
  place), unsaved-draft content in `solutions/drafts/` (gitignored), and
  displaced recordings parked there instead of deleted. Developer builds call
  it on a worker thread after every solve, once at startup and after any
  source level change; jobs run one at a time. It must stay free of game,
  UI and GPU state.
- `GameplaySession::inputLog()` lists the inputs that started actions since
  the last restart or restore, for solution recording. `resetToState` exists for
  the solver and the replay driver only; gameplay code restores snapshots.
- Reusable solution search lives in `engine/solver/Solver.hpp`; the
  `sokoban_solve_level` executable owns only level/solution file orchestration.
  Solver limits use generated states, while its statistics distinguish
  generated nodes, uniquely expanded canonical positions, duplicate rejection,
  walking-region work, early-canonicalization work and frontier peaks. State
  identity is the packed, lossless key in `engine/solver/StateKey.*`; when a
  `GameState` field is added, update its key and the field-by-field solver test.
  Walking members of every retained canonical position enter the bounded exact
  membership cache, allowing later equivalent successors to skip their
  canonicalization flood; only members reachable from a retained position may
  enter it. Cache eviction may add work but must never reject a new position.
  Do not add another unbounded raw-successor set: the retained canonical set is
  the lossless fallback and grows only to `Options::maxStates`. Search depth
  counts significant actions, with raw input length used only as a priority
  tie-breaker. `engine/solver/Heuristic.*` precomputes an optimistic graph:
  ordinary empty-board pushes, water treated as potentially bridged, and
  independent static mirror reflection. It uses a distinct unit-to-plate
  assignment and affects ordering only, never correctness. Keep new mechanics
  optimistic unless their exact relaxed transition is modeled.
  Significant directional results discovered during a walking flood are
  already settled production-rule transitions; reuse them rather than running
  the same input through `solution::Driver` again. Settled mirror previews and
  hero-controller switches are likewise exact successors. Mirror results with
  pending automatic motion must continue through the Driver. Local exact-state
  deduplication happens before dead-position and canonicalization work, and the
  solver statistics expose reused, driven, and locally duplicated successors.
  `engine/solver/DeadPosition.*` always applies the safe
  movable-unit/plate cardinality proof. Its per-plate reverse-push tables also
  require a complete rock-to-plate matching, but that stronger proof is
  deliberately feature-gated: do not enable it for a new mechanic until that
  mechanic is included in the reachability proof. Rogue-only layouts add a
  still stricter 2x2 freeze proof: every cell in the square must be either a
  live rock or static blocker, and frozen rocks only match plates they already
  occupy. Do not enable this for Knight chain pushes. Dead-position statistics
  distinguish cardinality, static-matching, and frozen-cluster rejections.
  Every returned solution must
  still pass through `solution::record`/`solution::Driver` before it is stored.
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
  when non-zero and read it as optional. The
  solver key includes it. Solution digests hash rotator links only for screens
  that have rotators. Gate links and start-open state are also hashed; changing
  either requires a fresh recording.
- Plates are the tiles with `TileProperty::Plate`, including pressure plates,
  buttons, rotators, lock plates, portals, rail stops and Ends. New plate kinds get
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
  it only for start-open gates, so older digests are unchanged. The solver
  heuristic treats a gate as floor (optimistic); gates already keep the
  static dead-position proof off.
- Live mirrors are movable entities: position and rotation travel in
  `GameState::movables` and `StateDelta::movables`. The cell-keyed
  `turnedMirrors`/`StateDelta::mirrors` path supports static-mirror compatibility.
  Rules resolve orientation through `rules::mirrorTileAt`; callers must use
  live state for moved mirrors. Heuristics remain optimistic and affect search
  ordering rather than acceptance.
- Completion requires every living hero on an End and every End occupied by a
  hero. Pressure plates drive devices; a rock or mirror on an End cannot satisfy
  completion. Space/Activate pulses all buttons occupied by living heroes and
  activates eligible mirrors together. Button state belongs in deltas, solver
  identity and replay expectations.
- Pressure-plate links are authored by color, and only in the editor.
  `LevelEditor` keeps a color for every pressure plate
  (`Document::plateColors`) and device; a device is linked to the plates of
  its color (`linkGroups`, `linkedPressurePlates`, compared as 8-bit RGB).
  Editor device records keep empty `pressurePlates`; `linkedDefinition` is
  the single place colors become explicit lists, for saving, drafts and
  `documentToLevel`. Loading derives colors from the explicit lists
  (`colorLinks`), recoloring legacy groups so links survive a save.
  `Level::LinkColor` (`@linkcolor`) stores only the colors of plates that
  drive nothing and is never read by gameplay, the solver or solution
  digests. Gameplay, rules and rendering must keep using the explicit
  `pressurePlates` lists, never color comparisons.
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
  `StateDelta::elevators` carries platform changes by index, the solver key
  appends them only when present, the profile writes `elevators` only when
  non-empty, and solution digests hash elevator records only
  for screens that have them. Reservations claim a moving platform's whole
  shaft (`addElevatorReservations`). Presentation animates platforms as
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
  preserve handedness.

## Renderer contracts

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
  layers; sampledTextures is the residency list. Ground passData[0] carries
  base/detail normal handles in xy and base/detail ORM handles in zw;
  passData[1].x carries specular strength. The splat and model shaders share
  PbrLighting.glsl, retaining their distinct point-shadow tap counts. Terrain
  maps are linear repeat data and use the color layers' world UVs and weights.
  GroundRock model bodies use dedicated wall maps and UV0 tangents. Regenerate
  terrain maps before model variants; see docs/pbr-art-pass.md for coverage and
  remaining procedural-surface art gaps.
- Scene alpha stores the ambient-to-total-lit ratio for opaque pixels so SSAO
  attenuates ambient contribution without darkening direct or emissive light.
  Blended scene pipelines preserve the opaque mask behind them.
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

## Documentation rule

Update this file when an enduring contract, supported command, current limitation
or immediate next step changes. Keep maintained design documentation and examples
under `docs/`; write generated reports, captures and temporary probes under
ignored `out/` or the build directory. Completed history is available through Git.
Replace stale validation results instead of appending a chronological roadmap.
