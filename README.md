# Sokoban 3D

Sokoban 3D is a C++20, SDL3, and Vulkan 1.3 puzzle game and small game-engine
codebase. It supports layered levels, animated 3D presentation, persistent save
slots and settings, keyboard/gamepad remapping, a manifest-driven content
pipeline, and a headless editor model exposed through Debug ImGui tools.

## Current Features

- Layered Sokoban movement with rocks, pressure-plate gates, rotator
  plates and elevators, goals, undo, restart, multi-screen levels, and completion tracking.
- Ice, ladders, conveyors, falling, configurable water layers, and four
  directional mirror types that can reflect players and movable units.
- Animated enemies that track and attack adjacent players, participate in
  physical movement rules, and support skeleton-driven held-item attachments.
- Animated mirror beams, destination ghosts, sound, and particle effects.
- Pixel-blur world transitions when entering a puzzle from the overworld and
  returning after completion.
- Stylized procedural water with cellular ripples, two-tone shading,
  shorelines, tile borders, and submerged-entity rendering.
- Vulkan shadows, SSAO, MSAA, internal render scaling, deferred renderer
  reconfiguration, FIFO-by-default VSync, optional tearing/frame caps,
  GLTF models, skeletal animation, and real-font UI.
- Main menu, save-slot selection, options, remappable SDL3 keyboard/gamepad
  input, and animated top-down camera pitch.
- Versioned profiles with atomic writes, backups, corrupt-save recovery,
  per-screen checkpoints, exact entity state, and undo-stack persistence.
- Manifest-driven lazy asset loading with task-system CPU preparation and
  background prefetching for upcoming levels.
- A transactional level editor whose document and filesystem logic do not
  depend on ImGui, SDL, or Vulkan.
- Manifest-backed mesh decorations with free translation, Euler rotation, and
  non-uniform scale; they render without participating in gameplay or camera
  framing.
- Colored point lights attachable to mesh decorations, with per-light local
  offset, intensity, range, and omnidirectional shadows that add to the sun.

## Requirements

- CMake 3.25+
- Visual Studio 2022 or another C++20 compiler
- Vulkan SDK 1.3+ with `glslc` available for the game and renderer tests
- A Vulkan-capable GPU and driver to run the game

SDL3, miniaudio, nlohmann/json, stb, ImGui, and the Karla UI font are vendored.
Texture decoding uses stb_image rather than platform-specific image APIs.

## Build And Run

For day-to-day work, use the `dev` preset. It is a Debug build with the editor
and developer tools, generated for Ninja so every source file compiles in
parallel. Open the folder in Visual Studio (it picks up `CMakePresets.json`),
or run these from a Developer PowerShell for VS 2022:

```powershell
cmake --preset dev
cmake --build --preset dev        # the game and its content only
.\out\dev\Debug\sokoban.exe
```

`cmake --build --preset dev-all` also builds the tests, and
`ctest --preset dev` runs them. The `release` configure preset is the same
build optimized, in `out/release` (`release` and `release-all` build presets,
`release` test preset); it has no editor or developer tools, which exist only
in Debug. In Visual Studio these appear in the build-preset dropdown as
"Debug: game only", "Debug: game + tests", "Release: game only", and
"Release: game + tests". "Headless tests (no Vulkan)" builds no game, and the
"Shipping" entries produce player packages.

The Visual Studio solution generator still works and is what CI uses on
Windows:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\sokoban.exe
```

Building `ALL_BUILD` there also builds every test runner. Build the `sokoban`
target, or set it as the startup project, when you only want to run the game.

`SOKOBAN_ENABLE_VALIDATION` defaults to `ON`. Headless tests are built by
default and can be disabled with `-DSOKOBAN_BUILD_TESTS=OFF`.

To build and run every Vulkan-independent test on a machine without the Vulkan
SDK or `glslc`, install Ninja and use the shared CMake headless preset:

```powershell
cmake --preset headless-tests
cmake --build --preset headless-tests
ctest --preset headless-tests
```

`SOKOBAN_HEADLESS_TESTS_ONLY=ON` omits the game, content staging, renderer, and
seven SDK-dependent tests: `vulkan_smoke`, `application_validation_teardown`,
`vulkan_device_selection`, `vulkan_diagnostics`, `frame_descriptor_sync`,
`gpu_abi`, and `texture_upload_plan`. All other test declarations and their
production libraries are the same CMake targets used by full builds.

Debug builds include the ImGui developer tools and can mirror edited source
levels into staged runtime content. Release builds use only packaged,
executable-relative assets.

Sokoban's own libraries and the game compile with a precompiled header of
common standard headers plus `Math.hpp` (and `<vulkan/vulkan.h>` for the
renderer and the game). Configure with `-DSOKOBAN_PRECOMPILED_HEADERS=OFF` to
turn it off, for example when checking that a file includes what it uses.
clang-tidy builds turn it off automatically, and the shared test runners do
not use it (see the comment above `sokoban_enable_precompiled_headers` in
`CMakeLists.txt`).

## Tests

The project currently registers CTest suites covering rules, level parsing,
campaign and gameplay sessions, persistence, input routing,
player UI, renderer state, scene preparation and picking, editor transactions,
assets, animation, particles, tasks, logging, and content packaging.

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure --no-tests=error
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure --no-tests=error
```

Each suite is still its own CTest test, but suites are linked into a few
runner executables instead of one program each: `sokoban_vulkan_tests` for
suites that need the renderer, `sokoban_tests` for the rest, and
`sokoban_profile_tests` on its own because it replaces global `operator new`.
To run one suite directly, pass its CTest name: `sokoban_tests rules`
(`sokoban_tests --list` prints them). Sanitizer builds default to one
executable per suite (`SOKOBAN_TEST_RUNNERS=OFF`); see `sokoban_add_test` in
`CMakeLists.txt` for why.

The `Required Tests` GitHub Actions workflow performs clean Debug and Release
builds on Linux and Windows for every push and pull request. Linux runs the
complete registered CTest matrix, including the hidden-window Vulkan device
smoke, and Debug additionally renders 240 frames under validation. Hosted
Windows runners have no Vulkan ICD, so they omit device execution while still
building the renderer and running the remaining tests and package gate. The
workflow separately gates AddressSanitizer/UBSan, clang-tidy's static analyzer,
a bounded player-profile fuzz run, and the Vulkan-free headless preset without
downloading the SDK. Repository branch protection should require every workflow
check before merging to `main`.

For local diagnostics with a Clang or GCC toolchain:

```powershell
cmake -S . -B build-sanitize -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DSOKOBAN_ENABLE_SANITIZERS=ON
cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure --no-tests=error

cmake -S . -B build-tidy -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DSOKOBAN_ENABLE_CLANG_TIDY=ON
cmake --build build-tidy --target sokoban --parallel

cmake -S . -B build-fuzz -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DSOKOBAN_BUILD_TESTS=OFF -DSOKOBAN_BUILD_FUZZ_TESTS=ON
cmake --build build-fuzz --target sokoban_player_profile_fuzz --parallel
.\build-fuzz\sokoban_player_profile_fuzz.exe -max_len=65536 -max_total_time=60
```

Production code is compiled once into `sokoban_core`, `sokoban_ui`, and
`sokoban_render_vulkan`; tests link those libraries rather than recompiling
engine implementation files.

### Performance suites

Performance measurements are kept out of CTest because absolute timings are
hardware, thermal, and power-state dependent. Build and run the dedicated
suite in an optimized configuration instead:

```powershell
cmake --build build --config Release --target sokoban_performance_tests
.\build\Release\sokoban_performance_tests.exe --full --output build\performance-results\Release

# Shorter iteration while changing a suspected hot path:
cmake --build build --config Release --target performance

# Full CPU, Vulkan startup/streaming, and GPU evidence A/B matrix:
.\tools\RunPerformanceSuites.ps1 -BuildDirectory build -Configuration Release
# Equivalent build target after configuration:
cmake --build build --config Release --target performance-comprehensive
```

The suite covers profiler overhead, process-memory sampling, frame-time
statistics, frame-arena versus heap allocation, opaque draw sorting and
batching, task dispatch and parallel scaling, and cold/warm plus
serial/parallel scene preparation at multiple scene sizes. Every case uses the
engine's bounded telemetry and CPU scopes, including worker-chunk scopes for
parallel workloads. It writes `performance-report.md`,
machine-readable `performance-results.json`, and a `cpu-trace.json` that opens
in Chrome tracing or Perfetto. The report ranks relative optimization
candidates such as task crossover points, cache value, and materially costly
nonlinear scene scaling; compare numeric results only on the same machine
and power state. Use `--filter scene-preparation` to isolate a group and
`--quick` for a 12-sample run. `RunPerformanceSuites.ps1 -Quick` also runs a
matched cold/warm application-startup pair plus a short baseline, point-light
stress, and serial-scene GPU matrix. The startup report records construction,
first-frame, and process wall time, while its logs break out Vulkan instance,
device, swapchain, render-resource, pipeline, audio, and renderer phases. Its
full mode adds render-scale, AO, translucency, frustum-culling, point-shadow,
and command-recorder A/B captures, then repeats the baseline to expose thermal
or power-state drift; every evidence report uses the same ranked analyzer as
the live profiler.

Every quick and full GPU matrix also records a `vsync-disabled` control. Its
report names the presentation mode the driver actually selected, allowing a
large FIFO frame-fence wait to be separated from GPU backpressure or an
actionable synchronization stall. For an isolated comparison, pass
`--evidence-disable-vsync` with an ordinary `--evidence-output` capture.

`scene_preparation_allocations` is a separate deterministic CTest regression:
after warm-up, serial scene preparation, parallel scene preparation, and
`TaskSystem::parallelFor` coordination must complete 64 representative runs
with zero general-heap allocations. The parallel scene path uses
`TaskSystem::scopedTask`, whose callable and completion state stay on the
waiting stack instead of allocating `packaged_task`/`future` shared state;
`parallelFor` keeps its join-before-return coordination state there too.

## Shipping Package

The Windows `shipping` preset produces an optimized, editor-free x64 build.
It disables validation, tests, the Debug ImGui workspace, and all content
editing source files; enables MSVC LTO; and emits the optimized executable's
PDB into a separate Symbols ZIP rather than the player-facing Runtime ZIP.

Before publishing a build, follow
[`packaging/ReleaseValidation.md`](packaging/ReleaseValidation.md). Its
PowerShell package gate verifies the final ZIP from a fresh extraction and
runs a bounded 240-frame smoke test with isolated diagnostics; the accompanying
Windows and GPU-driver matrix is the required human acceptance record.

For a shareable validation report from each test machine, run
`packaging/CollectReleaseValidationEvidence.ps1`; it packages the package-gate
result, GPU/driver/monitor details, Vulkan and DirectX diagnostics, hashes,
log tail, and your manual-checklist answers into one ZIP.

```powershell
cmake --preset shipping
cmake --build --preset shipping
cpack --preset shipping
```

The two ZIPs must be published together with matching version/build metadata:
give players only the `-Runtime.zip` archive and retain the `-Symbols.zip`
archive for crash-dump symbolication. The shipping preset is intentionally
MSVC-only because PDB separation is part of its output contract.

Windows builds embed a version resource, a per-monitor-DPI manifest, and the
temporary navy-and-gold `S` application icon from `resources/Sokoban.ico`.
The icon has a checked-in generator (`tools/GenerateWindowsIcon.ps1`) and SVG
source so it can be replaced deliberately rather than being a machine-local
asset. Update `SOKOBAN_APP_PUBLISHER` before publishing under a real company
or legal identity.

To create the normal per-user Windows installer, install Inno Setup 6 and run:

```powershell
cmake --preset shipping-installer
cmake --build --preset shipping-installer
```

This produces `out/shipping-installer/installer/Sokoban3D-<version>-Windows-x64-Setup.exe`.
It stages CMake's `Runtime` component first, deliberately excluding the PDB
symbols package. The installer supports upgrades and uninstalls without
administrator privileges.

The exact installer validation and evidence-collection commands are documented
in [`packaging/ReleaseValidation.md`](packaging/ReleaseValidation.md#installer-workflow).

For a public release, use a code-signing certificate in the current user's
Windows certificate store. Keep its SHA-1 thumbprint in the release environment
instead of source control; Inno Setup 6 and the Windows SDK's `signtool.exe`
must be installed:

```powershell
$env:SOKOBAN_SIGNING_CERTIFICATE_THUMBPRINT = "YOUR_CERTIFICATE_THUMBPRINT"
cmake --preset shipping-signed
cmake --build --preset shipping-signed
```

The signed preset signs and verifies the game executable before staging, then
signs and verifies the final installer with SHA-256 and an RFC 3161 timestamp.
It fails rather than emitting an unsigned artifact when the certificate or
signing tools are unavailable.

## Crash and Log Diagnostics

At startup, the game opens its diagnostic log next to the player profile
directory. `log.txt` is capped at 2 MiB and retains the five newest prior
sessions as `log.txt.1` through `log.txt.5`, preventing a long-lived install
from consuming unbounded disk space. Each session begins with the game version.

On Windows, both unhandled crashes and fatal exceptions caught during startup
or the game loop produce a local minidump in `crashes/`. The fatal dialog names
the available log and dump paths and asks players to include them in a support
report. The dump is intentionally kept out of the install folder; its matching
PDB belongs in the separately retained Symbols ZIP.

## Default Controls

| Action | Keyboard | Gamepad |
| --- | --- | --- |
| Move | `W`, `A`, `S`, `D` | D-pad or left stick |
| Undo | `Z` | West button |
| Restart | `R` | North button |
| Hold top-down view | `T` | Remappable |
| Confirm / interact (including mirrors) | `Space` | South button |
| Menu back/options | `Escape` | Start button |

Bindings can be changed from Options > Controls and are persisted in the
shared settings profile. A keyboard binding may be a chord: hold Ctrl, Shift
or Alt while pressing the key during capture. When several bindings on one key
match the held modifiers, only the one needing the most modifiers fires, so
`Ctrl+S` does not also move down.

The ImGui workspace's top row provides a persistent 0x-10x Simulation Speed
slider. Ctrl-clicking the slider accepts an arbitrary non-negative
value, including values above 10x. Rendering and editor input stay at normal
speed while gameplay, animation, transitions, timers, and particle effects
advance from the same scaled simulation clock.

Saves and settings are not migrated between profile formats while the game is
in early development. When a build changes the format, files from older
builds are renamed to `<name>.obsolete-format-<N>-<stamp>` in the save
directory and the game starts fresh. Keyboard and Controller tabs show and remap their
respective bindings independently. Binding rows and contextual gameplay
prompts use Kenney Input Prompts glyphs. SDL3 identifies the active controller
and supplies its physical face-button labels, so Xbox, PlayStation, Nintendo
Switch, GameCube, and Steam Deck controls use their matching symbols; unknown
controllers fall back to the generic glyph set.

## Level Format

Screens are text `.scr` files containing sequential `@layer N` sections. Each
authored screen declares `@character rogue`, `@character knight`,
`@character druid`, or `@character witch`; legacy screens without the
directive default to the rogue.
An optional `@water N` directive makes Air on that layer resolve to Water and
extends the water beyond the authored board without expanding camera bounds.
Any number of `@decoration` directives may reference manifest model names and
provide authored transforms. Each Gate tile has an accompanying `@gate`
directive that identifies its cell, the pressure plates that open it, and its
RGB color. Each Rotator tile likewise has an `@rotator` directive with the
same fields, and each Elevator tile an `@elevator` directive with the same
fields plus `levels`, its list of stop layers. A `@plate` directive records
a plate authored beneath something already standing on it (see Plates below).
A `@linkcolor {"cell":[x,y,z],"color":[r,g,b]}` directive is level-editor
bookkeeping: the color of a pressure plate that drives nothing yet (see Link
Colors under Level Editor). Gameplay ignores it and reads links only from the
explicit `plates` arrays. Metadata must appear before `@layer 0`.

```text
@character rogue
@water 0
@gate {"cell":[4,1,1],"plates":[[1,1,1],[2,1,1]],"color":[1.0,0.72,0.12]}
@rotator {"cell":[3,1,1],"plates":[[2,1,1]],"color":[0.24,0.62,0.92]}
@elevator {"cell":[5,2,0],"color":[0.38,0.8,0.44],"levels":[0,3,5,7],"plates":[[1,1,1]]}
@plate {"cell":[3,1,1],"tile":"Rotator Clockwise"}
@decoration {"model":"Tree","position":[4.5,2.5,1.0],"rotation":[0.0,0.0,30.0],"scale":[1.0,1.0,1.25]}
@decoration {"light":{"castsShadows":true,"color":[1.0,0.55,0.2],"intensity":3.0,"offset":[0.0,0.0,1.2],"range":6.0,"shadowBias":0.004,"shadowOpacity":0.9},"model":"Lantern","position":[2.5,1.5,1.0],"rotation":[0.0,0.0,0.0],"scale":[1.0,1.0,1.0]}

@layer 0
......
......
......

@layer 1
######
#PP1G#
#C   #
```

Common tile symbols:

| Symbol | Tile | Symbol | Tile |
| --- | --- | --- | --- |
| space | Air | `.` | Ground |
| `#` | Wall | `C` | Player |
| `Q K U H B` | Rogue / Knight / Druid / Witch / Bard starts | | |
| `R` | Rock | `P` | Pressure plate |
| `G` | Gate | `E` | End |
| `)` | Rotator (clockwise) | `(` | Rotator (counter-clockwise) |
| `=` | Elevator platform | | |
| `I` | Ice | `L` | Ladder |
| `W` | Legacy explicit water | | |
| `^ v > <` | Conveyors | `1 2 3 4` | Mirror orientations |
| `D` | Decorative block | `N` | Enemy |
| `n e s w` | Turrets facing north/east/south/west | | |

Decorative blocks render but have no gameplay, support, occupancy, camera-fit,
or water-grid-bound semantics. New water layouts should use `@water N`; `W`
remains supported for older screens.

A closed Gate blocks every entity. It opens only while every pressure plate
listed in its `plates` array is occupied by a living player, movable object, or
enemy; a Gate with no linked plates stays closed. The gate and its linked
plates share the configured color in the game. Links are authored with link
colors in the level editor (see Link Colors under Level Editor).

A Rotator is a cogwheel floor plate that units can stand on. Each time every
pressure plate in its `plates` array becomes occupied (the same rule as a
Gate), it turns the living unit standing on it a quarter turn: clockwise for
`)`, counter-clockwise for `(`. It fires once per press; staying on the plate
does nothing more, and a plate already pressed when a step begins does not
fire it. Heroes, rocks, ice blocks, turrets and enemies can all be turned. A
turned turret fires along its new direction, immediately shooting any hero,
enemy or turret already standing in that line. A turned enemy keeps
that turn only until the board next changes, then goes back to facing the
nearest hero. A Rotator with no linked
plates never turns. The plate, its icon (a darker shade of the same color) and
its linked pressure plates share the configured color; a pressure plate linked
to both a Gate and a Rotator shows the Gate's color. The plate models are
generated by `tools/make_rotator_models.py`.

An Elevator is a moving platform, drawn with the Kenney Platformer Kit
`platform` model. Its tile marks where the platform starts: a platform resting
on layer L is a solid block in that cell whose top is flush with the top of
the other layer-L blocks, so units stand on it from layer L + 1. Its
`levels` list holds the layers it stops at, in travel order, and must include
the tile's own layer (where it starts). Each time every pressure plate in its
`plates` array becomes occupied (the rotator rule: once per press) it moves
one stop along the list and turns back at either end, so `[0, 3, 5, 7]`
travels 0 -> 3 -> 5 -> 7 -> 5 -> 3 -> 0 -> 3 and so on. A platform authored
part-way along the list starts there, heading towards the end of the list.
It carries the column of heroes, rocks, ice blocks, turrets and enemies
stacked on it. If anything blocks the shaft it would sweep through (a static
tile, a closed gate, another platform, or a unit that is not riding), it does
not move and the press is lost. A carried unit counts as having moved:
turrets fire at a unit carried into their line, a carried turret fires along
its line at the new level, and enemies and heroes carried next to each other
attack. While the platform is elsewhere its starting cell is open shaft that
units can fall into. Static tiles in the shaft, including a pressure plate
authored on top of the platform, stay where they are. An elevator with one
stop or no linked plates never moves. Its stops are edited under Elevator
Stops in the ImGui Tiles palette: type them as `0, 3, 5, 7`, and the editor
shows the other stops as dithered platforms.

Pressure plates, Rotators and Ends share the Plate tile property
(`TileProperty::Plate`): they are floor tiles that heroes, rocks, ice blocks,
turrets, enemies and mirrors can stand on, and each reacts to what occupies it.
A screen can start with something already on a plate: the layer grid holds the
occupant and a `@plate {"cell":[x,y,z],"tile":"<plate name>"}` line records the
plate beneath it (the tile name is `Pressure`, `End`, `Rotator Clockwise` or
`Rotator Counter-Clockwise`). In the example above, a mirror starts on the
Rotator. In the editor, painting a unit or mirror onto a plate, or a plate under
a unit or mirror, stacks the two; erasing lifts the occupant off and leaves the
plate; any other tile replaces the whole stack. Units and mirrors can also be
moved onto an unoccupied plate.

A mirror on a Rotator is turned by it like any unit, changing the corners it
reflects between. A mirror on a pressure plate holds that plate pressed for
good. An End under a mirror can never hold a hero, so it does not count
towards completion.

A screen is complete when every living hero stands on an End and every End
holds a hero. Pressure plates no longer affect completion directly. A screen
with more Ends than heroes therefore needs mirror copies of a hero to finish;
a rock on an End does not count.

Turrets are pushable movables. A turret shoots a player or enemy whenever that
unit moves into its cardinal line of sight; walls, rocks, and other live units
block the shot.

The rogue uses the original one-object push rules and is always used in the
overworld. The knight can push any-length contiguous chains containing rocks,
ice blocks, turrets, and enemies, provided the entire chain has a valid place
to move. Instead of pushing, the druid compulsively pulls a movable unit
directly behind it into every cell it vacates. When the witch moves toward the
nearest visible movable unit in a cardinal line, it swaps positions with that
unit instead; other heroes and walls block the spell's line of sight. Rocks,
ice blocks, turrets, and enemies are all movable units for these abilities,
mirrors, pressure plates, conveyors, and ice momentum. Hazards and enemy
attacks still resolve normally after forced movement.

The bard surrounds itself with a 5x5 musical aura that also reaches one layer
above and below. Whenever the bard moves, each live movable unit in that area
tries to move one tile in the same direction. Affected units remain locked to
the elevation where the aura caught them, allowing them to float across drops
while the aura continues to affect them. Invalid individual moves are skipped;
an affected unit may push one movable block when the block has a valid
destination.

Mesh decoration positions are world-space tile coordinates, rotations are XYZ
Euler degrees, and scales must be positive. Their `model` names must exist in
the `models` section of `assets/manifest.json`. Decorations are non-pickable
during gameplay and do not alter level bounds, rules, support, or camera fit.
An optional `light` object attaches a point light in the decoration's local
space, so its offset follows the mesh's scale and XYZ rotation. Up to eight
visible point lights are active in one rendered view; each has independent
color, intensity, range, shadow bias, and shadow opacity. Point-light shadow
bias is measured in world units and is automatically increased at grazing
surface angles to suppress shadow acne without erasing distant shadows.

## Level Editor

Debug builds expose the headless `LevelEditor` through ImGui. The UI invokes
editor commands but does not own document or filesystem policy.

- Link Colors: in the editor, every pressure plate, gate, rotator and
  elevator has a link color, and a device is driven by exactly the pressure
  plates of its color. To link a plate to two gates, give all three the same
  color; to make a rotator need two plates, give it and both plates one
  color. Colors match as the 8-bit RGB the picker shows. Newly painted plates
  and devices take the Link Color chosen in the Tiles palette; painting a
  plate or device over itself recolors it to that color. Holding `Alt` turns
  the cursor into an eyedropper: clicking picks up the tile and, from a plate
  or device, its link color. Holding `Ctrl` turns it into a brush dipped in
  the Link Color: clicking or dragging gives every plate or device it
  touches that color, the whole drag as one undo step. The Links list shows each color group with its
  members, warns about devices with no plates, and can recolor a whole group
  (choosing another group's color merges them). Colors are only an authoring
  aid: saving writes each device's explicit `plates` list, which is all
  gameplay reads. Screens authored before link colors open with their links
  intact, giving devices that shared a color but not their plates distinct
  colors; links that colors cannot express at all are regrouped and the
  editor says so.
- Click normally paints above the selected cell.
- Hold `R` while clicking to replace on the resolved layer.
- Hold `M` and click a tile object, then its destination, to move it as one
  undoable edit. The move tool is independent of the active palette: ordinary
  tiles, mirrors, pressure plates, ends, and overworld selector flags all use
  the same workflow. Flags retain their IDs and screen assignments. Both the
  source and destination previews are dithered; releasing `M` cancels a pending
  move.
- Hold `D` while clicking to delete; the target is shown with a dithered
  preview while invisible pick geometry keeps hover selection stable.
- Hold the button and drag to paint (or, with `D`, delete, or with `R`,
  replace) every cell the pointer crosses. Each board column is edited at
  most once per drag, and the whole drag is one undo step. Hold `Shift` to
  keep the drag on its starting row or column. A drag only extends the board
  from its first cell.
- Press `Z` to undo editor changes and `Y` (or `Ctrl+Shift+Z`) to redo them.
  Like undo history, redo history travels with a document's unsaved draft.
- More editor shortcuts, active while the game view has keyboard focus
  (click it after using a panel). These are the defaults:

  | Key | Action |
  | --- | --- |
  | `Ctrl+S` | Save the document back to the file it came from (or the ground splat map while painting it) |
  | `F5` | Play the draft; `F5` again returns to the editor without the confirmation dialog |
  | `Shift+F5` | Play a puzzle draft with its first hero moved to the cell under the pointer; the document is unchanged |
  | Hold `Alt` + click | Eyedropper: pick up the tile under the pointer, and its link color |
  | Hold `Ctrl` + click or drag | Link-color brush: give each pressure plate or device touched the Link Color |
  | `1`-`9` | Choose from the recent-tiles strip at the top of the Tiles palette |
  | `PageUp` / `PageDown` | Change the active layer |
  | `L` | Lock edits to the active layer |
  | `Tab` | Cycle Tiles, Mesh Decorations and (overworld) Screen Selectors |
  | `T` / `R` / `S` | Decoration gizmo: move / rotate / scale |

- Every editor control above can be rebound under **Options > Controls >
  Editor Controls** (Debug builds, Keyboard tab), which groups them as
  Editing, Playtest and Recent Tiles. The Level Editor panel lists the
  current bindings. Editor-only bindings may reuse gameplay keys; Undo, Back
  and Play/Stop Draft are live in both and so conflict with both.
- `+ Layer Below` and `+ Layer Above` insert undoable Air layers and preserve
  water-layer numbering.
- Painting one cell beyond an edge expands every layer transactionally.
- The Mesh Decorations tool scans source `assets/` for `.gltf` and `.glb`
  files in Debug builds. Any discovered mesh can be selected: an unregistered
  mesh is automatically added to the source and staged manifests, along with
  its external glTF buffer/image dependencies. A glTF using one external
  base-color atlas automatically reuses or registers that texture and binds it
  to the model. Imported decorations set `preserveSourceScale`, so scale
  `[1,1,1]` retains the mesh's exported units and origin instead of fitting
  its bounds into one tile. Meshes can be placed on the top surface under the
  cursor, selected, translated, rotated, non-uniformly scaled, duplicated,
  deleted, and undone. A selected decoration can attach or detach a point
  light and edit all of its lighting and shadow settings in place.
- Source saves, runtime mirroring, screen/level insertion and renumbering,
  soft deletion, restore, and guarded permanent deletion are handled by the
  tested editor/project APIs.

## Developer Iteration Tools

Debug builds with developer tools add these to the workspace:

- **Live profiler.** The Profiler tab charts total CPU, renderer CPU, and GPU
  frame time against an adjustable budget; shows a thread-aware CPU flame
  chart and exclusive/inclusive hot-path table; breaks CPU command recording
  and Vulkan timestamp queries down by render phase; and tracks process
  memory, VMA allocations, peaks, churn, fragmentation, and per-heap budget
  pressure. Its ranked optimization candidates use those same measurements
  to call out CPU/GPU budget overruns, synchronization, dominant passes,
  submission granularity, cache reuse, jitter, and memory pressure. Capture
  can be paused or cleared, and **Export Chrome trace**
  writes the bounded 240-frame history to `profiling/cpu-trace.json` for
  Chrome or Perfetto. Add `SOKOBAN_PROFILE_SCOPE("Name")` to any engine scope
  that needs to appear in the timeline; task-system work is collected by
  thread and scopes that cross frame boundaries are retained.
- **Shader hot reload.** Saving a file under `shaders/` recompiles it with the
  build's `glslc` and flags, writes the new SPIR-V into the staged asset
  tree, and rebuilds the renderer's pipelines at the next frame boundary.
  Editing a file in `shaders/include/` recompiles every shader. F6 or
  **Shaders > Recompile All** forces a full pass. A compile error leaves the
  last good shaders running and shows the compiler's message over the game
  and in the Shaders tab.
- **Tuning tab.** Values declared with `SOKOBAN_TUNABLE_*` in a
  `*Config.hpp` header (see `src/engine/Tuning.hpp`; fog of war uses it
  today) can be edited while the game runs. **Save to header** writes the
  edited literals back into that header, so the next build compiles them as
  the new defaults. Builds without developer tools compile the same
  declarations as plain `constexpr` constants.
- **Source watcher.** Every half second the game checks the source
  `levels/` tree, `assets/manifest.json`, `assets/animation_catalog.json`
  and every texture the manifest names, and applies edits made outside the
  game:
  - A `.scr` or level `.json` is mirrored into the staged tree. The editor
    reloads it if it is the open document and has no unsaved changes (it
    keeps an unsaved draft and says so in the Log). If it is the puzzle
    screen you are playing, the screen restarts from the new layout.
  - A texture is re-read and replaces the GPU copy in place.
  - A manifest edit that only changes tile scales or sound and music
    volumes applies live. Anything structural (a model, texture, animation,
    role or tile model) shows "needs a restart" in the Asset Manifest tab.
  - The animation catalog is reloaded unless the Animation tab has unsaved
    edits.
  - Models are not reloaded; restart for those.
- **Resume on launch.** When a Debug session ends, the game records where
  you were in `dev-session.json` in the save directory. The next launch
  skips the title, continues the active save slot, and reopens the editor
  document you were editing. Turn this off in the **Session** menu, or
  launch with `--title` once.

Launch options for jumping straight to what you are working on:

| Option | Effect |
| --- | --- |
| `--continue` | Continue the active save slot instead of showing the title (any build). |
| `--title` | Show the title even if the developer session would resume. |
| `--level <n> [--screen <m>]` | Continue, then enter puzzle screen `m` (default 0) of level `n`. Debug developer builds only. |
| `--edit <path>` | Continue, then open a level document in the editor, e.g. `--edit levels/level3/screen2.scr`. Debug developer builds only. |

In Visual Studio's Open Folder mode, add arguments with the startup item's
**Debug and Launch Settings** (`launch.vs.json`, kept under `.vs/`).

## Solutions

`solutions/` holds one recorded solution per puzzle screen. The
`solution_replay` test replays each one against the screen whose content
matches it and fails, naming the step and the first hero, block or enemy
that went somewhere else, when a rule change breaks a recording. It also
fails if a recording solves its screen early. Screens without a recording
are listed as notes, not failures.

A file looks like this:

```text
format 1
level-digest a7799b56118fdd89
recorded-for level0/screen0
step up p1=6,4,2
step up p1=6,3,2
```

Each `step` is one input (`up`, `down`, `left`, `right`, `cycle`,
`interact`, `undo`) followed by what it changed: `p` players, `m` movable
blocks and `e` enemies, each `id=x,y,z` with `+dead`, `+fallen` or
`+drowned` when that changed. The digest covers the gameplay layers, water
and hero, not decorations, so re-decorating a screen keeps its solution.
Solutions live outside `levels/` because the content pipeline rejects
unexpected files there, and they are matched by digest, so renumbering
screens does not break them. After changing a screen's layout, record it
again.

Debug builds record every solve automatically. When you solve a campaign
screen or an editor draft, a worker thread replays your inputs one at a time,
waiting for each to finish, and stores the run as
`level<L>-screen<S>.solution` if it is the first recording of that content
or shorter than the stored one. The Log and **Level Editor > Solutions**
say what happened. A run that only worked because of real-time timing
(moving during a slide) does not replay and is not stored.

A solved draft that has not been saved yet is kept in
`solutions/drafts/<digest>.solution` (ignored by git) and moves into place
when a screen with that content is saved. When a screen is edited, its old
recording is parked in `drafts/` once the new layout has a recording, and
comes back if the edit is undone. Smoke and evidence runs never write here.

`sokoban_solve_level` searches for solutions instead. The `release-all`
build preset builds it:

```powershell
.\out\release\tools\Release\sokoban_solve_level.exe levels solutions --level 3 --screen 2 --best-first --max-walking-cache 1000000 --progress-interval 100000
```

Without `--level` it tries every screen that has no current recording. It
skips screens whose recording still matches unless given `--overwrite`, and
stops a screen after `--max-states` (default 2,000,000) generated positions.
The exact walking-membership cache is limited by `--max-walking-cache`
(default 1,000,000 keys); zero disables it, while a larger value trades memory
for fewer repeated canonicalization floods.
The default breadth-first search minimizes significant state-changing moves,
not necessarily recorded inputs; `--best-first` is usually much faster on
large screens but the result may be longer. Final output distinguishes
generated and expanded positions, duplicate states, early-canonicalization
floods/walk states, walking-component cache hits/current and peak size,
evictions, precomputed-versus-driven successors, local successor duplicates,
and peak frontier size. Solved screens also report their significant
state-changing move count.
`--progress-interval N` prints the same counters, including dead-position
checks and prunes broken down by unit count, static matching, and frozen
clusters, after approximately every N generated positions. Use a Release
build; the search is slow in Debug.

The search itself is the reusable `engine/solver/Solver.hpp` API. It reports
solved, exhausted, state-limit and cancelled outcomes separately, accepts an
optional progress/cancellation callback, and exposes deterministic work
counters suitable for regression benchmarks and future difficulty grading.
Search identity is a packed, lossless key over every dynamic gameplay field.
Every walking state discovered while canonicalizing a retained position enters
a bounded exact-membership cache, so later walking-equivalent successors can be
rejected without repeating the flood. When full, the cache discards half its
entries; a miss can only cause extra work because the retained-position
canonical set remains lossless and is itself bounded by `maxStates`. There is
no separate unbounded raw-successor table. New components are canonicalized
before they can consume retained-node or frontier capacity.
Best-first depth counts significant actions while raw input length only breaks
ties, preventing a long harmless walk from outweighing a useful push. Its
precomputed relaxed graph models push geometry, potentially bridged water, and
mirror reflection, then finds a minimum-cost distinct assignment of live
movable units to plates. Ordinary settled steps use the production action
planner directly. State-changing results found during the walking flood are
reused as successors instead of being simulated a second time, including
settled mirror previews and hero switches; equivalent raw outcomes from one
expansion are discarded locally. Automatic ice, conveyor and turret
consequences still fall back to the full replay driver.
Pressure-plate feasibility rejects states with too few
surviving movable units on every level. Classic flat push-only screens also
precompute reverse-push reachability for each plate, rejecting both rocks on
cells that cannot reach any plate and sets of rocks that cannot be assigned to
distinct plates. Rogue-only screens additionally reject permanently sealed 2x2
groups of rocks and static blockers, while preserving groups whose frozen rocks
already cover the required plates. Knights are excluded because chain pushes
can break that pattern. Mechanics that can invalidate either proof disable its
static portion automatically.

### Improving the solver (future work)

The feature-aware best-first solver solves level 3 screen 2, which the earlier
Manhattan-guided search could not solve after 5 million positions. Further
ideas, roughly in order of payoff:

- **More deadlock patterns.** Unit-count feasibility, static dead cells,
  complete rock-to-plate reachability matching, and sealed 2x2 rock/blocker
  freezes are implemented for the feature sets where each proof is sound. Next
  are recursive multi-rock freezes, larger wall groups, and feature-aware
  proofs for water, mirrors, ice, and character abilities.
- **Extend the relaxed estimate.** Distinct unit-to-plate assignment over
  push, bridgeable-water, and mirror edges is implemented. Next, assign heroes
  to Ends through their reachable walking components and price the rocks that
  must actually be sacrificed to bridge water, plus ice and conveyor motion.
- **Mirror-aware moves.** Treat "walk to a spot and interact with a mirror"
  as one move and skip mirror activations that change nothing, so hero
  copies do not multiply the search.
- **More symmetry reduction.** Packed binary keys, walking-region
  canonicalization, and cached walking-component membership are implemented.
  Sorting truly interchangeable rocks (and proving which other entities are
  interchangeable) would merge more equivalent positions without sacrificing
  mechanic-specific identity.
- **Iterative deepening (IDA\*)** so memory stops being the limit, and
  running independent branches on several threads.
- **Seeding from a human solve.** Start from a recorded solution and
  search for shorter ones, which also checks that a screen still has the
  intended difficulty after edits.

## Content Pipeline

`assets/manifest.json` is the strict, versioned source of runtime models,
textures, animations, sounds, music, tile visuals, and material behavior. A
normal build runs `sokoban_content`, validates all reachable content, compiles
shaders, and stages only required files beside the executable.

`assets/animation_catalog.json` is the source of truth for animation usage,
playback tuning, and animation ordering. Each manifest animation records its
validated source duration and a global speed. Every code-declared semantic use
such as `player.idle` or `enemy.attack` selects a clip, contributes its own
speed multiplier, and may own normalized timeline events. A use may declare a
`startAfter` gate naming an event on another use. The shipped catalog places
`attack-connected` at 90% of `enemy.attack` and gates `player.death` on it;
drowning and other deaths without a concrete attacking enemy still begin
immediately.

Debug builds expose all of this in the Developer Tools `Animation` tab. The
Timeline Events section first presents a selected semantic use and its named
event list. `Edit` and `Add New Event` open a focused editor with preview
visibility, playback/frame-step controls, and one scrubber showing both source
seconds and normalized percentage. `Add at Cursor` commits the entered name at
that position. Its clip, cursor, and playback state are independent from the
free-form Animation Preview below it. That browser can
select a skinned manifest model and any source glTF/GLB animation, render that
pairing on an isolated 3x3 stage, and provide play/pause, looping, speed,
frame-step, and exact timeline scrubbing controls. The content build rejects
missing, duplicate, cyclic, or stale uses, gates, source durations, and clips.
`Save Animation Catalog` atomically writes the source catalog and mirrors it
into the running Visual Studio build's staged assets, so tuning survives an
immediate restart without requiring a rebuild.

Models default to normalized unit-tile geometry. Set
`"preserveSourceScale": true` on free-form scenery that should retain its
authored dimensions and origin. Automatic decoration import currently binds
single external base-color atlases; multi-atlas or embedded-image GLTF/GLB
materials still require explicit manifest material entries.

```powershell
cmake --build build --config Debug --target sokoban_content
```

Outside the shipping presets, staging is incremental
(`SOKOBAN_INCREMENTAL_CONTENT`). The tool still collects and validates the
inventory on every build, but it leaves the staged tree alone when a record
beside it (`assets.stage-record`) shows that no source file, the tool, and the
staged `content.index` have changed. When a stage does run, BC7 textures come
from a per-configuration cache in the build tree (`content-cache/<config>/`),
keyed by the bytes of every file a texture reads, so only changed textures are
re-encoded. Deleting the cache is always safe. The shipping presets turn both
off and stage from scratch.

The game loads from the staged `assets/` tree. Runtime asset requests are lazy;
CPU work uses the task system, and requirements for the current and next level
are prefetched to reduce level-transition stalls. Decoration model references
participate in the same requirement collection, validation, staging, and
prefetch path as gameplay models.

## Release Package

```powershell
cmake --build build --config Release
cmake --install build --config Release --prefix build\install
cmake --build build --config Release --target package
```

CPack produces a platform/architecture-named ZIP containing the executable,
staged assets, and third-party licenses.

## Architecture

- `src/engine/Rules.*`: pure gameplay rules over `Level` and `GameState`.
- `src/engine/solver/`: reusable solution search, limits, cancellation, and
  deterministic search-work statistics; the CLI is only file orchestration.
- `src/engine/GameplaySession.*`: commands, timing, state, and undo history;
  committed actions retain a semantic presentation timeline so undo can replay
  movement and actor animations in their exact reverse order.
- `src/engine/GameplayPresentation.*`: interpolation, visual animation, and
  forward/reverse evaluation of recorded action timelines.
- `src/engine/AnimationCatalog.*`: strict semantic animation bindings plus
  source durations, timeline events, start gates, global/per-use playback
  speeds, and atomic JSON persistence.
- `src/engine/AnimationEventSequencer.*`: Vulkan-free, actor-instance-aware
  timeline event evaluation.
- `src/engine/AnimationCatalogEditor.*`: headless dirty/reload/save workflow
  that keeps source and staged runtime catalogs synchronized.
- `src/engine/AnimationPreviewScene.*`: Vulkan-free construction of the
  isolated 3x3 animation-authoring stage.
- `src/engine/LevelEditor.*`: headless document, history, validation, and
  transactional project filesystem operations.
- `src/engine/DecorationMeshCatalog.*`: Debug-authoring discovery of source
  GLTF/GLB files and their manifest-registration state.
- `src/engine/DecorationAssetRegistry.*`: headless automatic manifest
  registration and staged dependency mirroring for selected decoration meshes.
- `src/engine/Application.*`: composition, SDL event loop, and lifecycle.
- `src/engine/ui/`: reusable player-facing UI and pure menu reduction.
- `src/engine/render/`: Vulkan-free scene preparation plus decomposed Vulkan
  device, swapchain, pass, descriptor, model, pipeline, and recorder owners.
- `src/engine/TaskSystem.*`, `AsyncSaveStore.*`, and `LogQueue.*`: background
  work for assets, persistence, and bounded asynchronous logging.
- `shaders/`: GLSL compiled to SPIR-V by CMake.
- `tests/`: headless regression suites.

See `HANDOFF.md` for implementation invariants, subsystem details, historical
decisions, and guidance for continuing development.
