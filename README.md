# Sokoban 3D

Sokoban 3D is a C++20, SDL3, and Vulkan 1.3 puzzle game and small game-engine
codebase. It supports layered levels, animated 3D presentation, persistent save
slots and settings, keyboard/gamepad remapping, a manifest-driven content
pipeline, and a headless editor model exposed through ImGui developer tools.

## Current Features

- Layered Sokoban movement with rocks, pressure-plate gates, pulse buttons, rotator
  plates and elevators, goals, undo, restart, multi-screen levels, and completion tracking.
- Ice, ladders, conveyors, falling, configurable water layers, and four
  directional mirror types that can reflect players and movable units.
- Four color-paired edge portals transport heroes and movable units from
  their front, rotating movement and sightlines to the exit orientation while
  preserving slide or conveyor momentum. Swirling sparks mark the front.
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
- Shaped Unicode text with hinted small-size coverage, scalable analytic
  outlines, and inline vector/texture icons. See [text rendering](docs/text-rendering.md).
- Versioned profiles with atomic writes, backups, corrupt-save recovery,
  per-screen checkpoints, exact entity state, and undo-stack persistence.
- Manifest-driven lazy asset loading with task-system CPU preparation and
  background prefetching for upcoming levels.
- A transactional level editor whose document and filesystem logic do not
  depend on ImGui, SDL, or Vulkan.
- Per-screen camera tilt and rotation authored in the level editor, with live
  preview, undo/redo, and saved angles for puzzle and overworld screens.
- Manifest-backed mesh decorations with free translation, Euler rotation, and
  non-uniform scale; they render without participating in gameplay or camera
  framing.
- Colored point lights attachable to mesh decorations, with per-light local
  offset, intensity, range, and omnidirectional shadows that add to the sun.

## Controls

Lecterns are fixed blocks that open a reading box when a hero walks into them.
Choose **Lectern** in the editor's tile palette, then select the stand in the
**Lecterns** section and enter its text. Text supports line breaks, wraps to the
box, and uses pages when needed. Use the page buttons to change pages. Walk
away from the lectern to close the box, or use Activate or Back (Escape by
default). Blocked moves keep the text open. Gameplay pauses while reading
without movement input, and opening the book does not spend a move. After using
Activate or Back, release movement before opening the same book again.

Screen grids use `T` for a lectern, with text stored before the layers as
`@lectern {"cell":[2,1,1],"text":"First paragraph.\n\nSecond paragraph."}`.
An example ready to open in the editor is `docs/examples/lectern.scr`.

Embed current binding icons with tags such as `<!Move Up!>`, `<!Undo!>`,
`<!Activate!>`, or `<!Menu Back!>`. Action names ignore case, spaces, hyphens,
and underscores; internal names such as `moveUp` also work. Icons follow
rebinding and switch between keyboard and controller prompts automatically.
Modifier chords show all required keys. Unknown actions, unbound actions,
and malformed tags show an inline error in red; keys without an atlas icon
fall back to their binding label.

Action identifiers, saved names, controls-menu labels, tutorial aliases, input
contexts, and default keys are defined together in `src/engine/InputActions.def`.
Add or change an action there; controls rows and lectern names are derived from
that registry. Runtime input and prompts use the player's current `InputBindings`,
including saved rebindings. Any controls-menu binding label also works in a lectern tag.

All lecterns share optional **Minimum Font Size** and **Maximum Font Size**
settings under **Tuning > Lecterns**, measured in screen pixels. Set either to
`0` to leave that limit disabled. Changes apply immediately; **Save to header**
stores them in `src/engine/ui/LecternConfig.hpp` for future builds. If the
minimum exceeds the maximum, the minimum takes precedence.

Space is Activate: it pulses every button occupied by a living hero and
activates all eligible mirrors together, across all characters and copies.
Buttons use `b` in screen grids (or `@plate ... b` beneath a starting unit),
and share the pressure plates' link colors. A pulse triggers linked rotators,
elevators and minecarts once per press; gates receive input through the next
game step and then close, using their normal obstruction and crushing rules.

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
`ctest --preset dev` runs them.

For faster level authoring and in-editor solver work, use `dev-fast`. It builds
RelWithDebInfo with optimization and debugger symbols, retaining the full editor,
profiler, live tuning, source-content discovery, and automatic solution recording.
Vulkan validation is off, and optimized debugging may skip or reorder source
lines. Use `dev` when investigating validation errors or stepping through code
without optimization.

```powershell
cmake --preset dev-fast
cmake --build --preset dev-fast
.\out\dev-fast\RelWithDebInfo\sokoban.exe
```

`dev-fast-all` builds every target, and `ctest --preset dev-fast` runs its tests.
The `release` preset builds the optimized game without editor tools in
`out/release` (`release` and `release-all` build presets, `release` test preset).
Visual Studio's dropdown includes game-only and game-with-tests entries for
Debug, Optimized editor, and Release. "Headless tests (no Vulkan)" builds no
game, and the "Shipping" entries produce player packages.

The `dev`, `dev-fast`, and `release` presets enable warnings-as-errors, matching
Windows CI. To configure, build every target (including the performance runner),
and run the current tests in one command, use a Developer PowerShell:

```powershell
cmake --workflow --preset dev-check
cmake --workflow --preset dev-fast-check
cmake --workflow --preset release-check
```

`ctest` by itself runs existing executables; it does not rebuild changed
sources. Use these workflows when checking a change before pushing.

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

Debug and RelWithDebInfo include the ImGui developer tools when
`SOKOBAN_ENABLE_DEVELOPER_TOOLS=ON` (the default), and can mirror edited source
levels into staged runtime content. Set that option to `OFF` to omit the tools
from either configuration. Release and MinSizeRel use only packaged,
executable-relative assets. Shipping and headless presets force the tools off.
Vulkan validation remains controlled separately by `SOKOBAN_ENABLE_VALIDATION`
and is compiled in only for Debug.

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

The `Required Tests` GitHub Actions workflow performs clean Debug, optimized
editor, and Release builds on Linux and Windows for every push and pull request.
Linux runs the complete registered CTest matrix, including the hidden-window Vulkan device
smoke, and Debug additionally renders 240 frames under validation. Hosted
Windows runners have no Vulkan ICD, so they omit device execution while still
building the renderer and running the remaining tests and package gate. The
workflow separately gates AddressSanitizer/UBSan, clang-tidy's static analyzer,
a bounded player-profile fuzz run, and the Vulkan-free headless preset without
downloading the SDK. Repository branch protection should require every workflow
check before merging to `main`.

Clang-tidy builds require CMake 3.27 or newer and check the first-party
adapters and headers. ImGui, miniaudio,
VMA, cgltf, and stb implementation files skip linting, and vendor headers are
system includes. Linux CI uses SDL's X11 backend under Xvfb, so its presets
disable unused Wayland protocol generation. Automated smoke/evidence runs
start windowed to avoid fullscreen requests that need a window manager.
Both Vulkan sanitizer tests use `tests/lsan.supp` for known SDL/X11 and Vulkan
loader allocations; leak detection remains enabled for other allocations.

The Linux jobs use the same `ci-debug`, `ci-dev-fast`, `ci-release`, `ci-sanitize`,
`ci-tidy`, and `ci-fuzz` presets available locally. These select GCC 13 or
Clang/clang-tidy 18 explicitly and use separate directories in `out/`.
On Ubuntu 24.04, including a WSL installation, install the dependencies
listed in `.github/workflows/required-tests.yml` and expose its pinned
Vulkan SDK. Run these from the repository root:

```sh
bash tools/check_ci.sh                         # all seven Linux configurations
bash tools/check_ci.sh ci-release ci-tidy       # selected configurations
```

This preflight checks the toolchain dependencies and SDK header version,
selects lavapipe and validation layers using the CI helper, and runs the
builds, CTest suites, bounded fuzz run, Vulkan-free core check, and Debug
240-frame validation smoke. Independent configurations continue after a
failure, and Ninja uses `-k 0` to report all available compile failures.
It requires a real Linux environment; Git Bash cannot reproduce these gates.

The Windows workflows cover the MSVC build and CTest suites. Linux adds GCC
and Clang diagnostics, platform-specific type and library behavior,
ASan/UBSan, and static analysis; passing Windows tests does not establish that
those checks pass. A 64-bit `uint64_t`, for example, aliases `unsigned long`
on Linux and `unsigned long long` on Windows, which matters for templated
types such as `std::future<T>`. CI also renders frames under lavapipe Vulkan
validation and checks Vulkan-free core dependencies and committed line
endings. The preflight covers the build and runtime gates; the committed
line-ending check remains an explicit step in the workflow.

As an additional Windows check, a portable GCC 13 toolchain can compile every
configured first-party file from the Release compilation database:

```powershell
cmake --preset release
python tools/check_compiler_warnings.py --database out/release/compile_commands.json --compiler C:/path/to/g++.exe
```

This audit produces real optimized `-O3 -Werror` object files, preserving the
configuration's macros and include paths while omitting PCH and vendored
implementation units. A syntax-only scan cannot find warnings that depend on
optimization. The script checks all files even after failures and saves logs
and `failures.json` under `out/compiler-warnings`. For a full clang-tidy policy
scan using that GCC toolchain's standard-library headers, use a complete
LLVM 18 distribution. Configure a separate Debug database with tests disabled,
matching `ci-tidy`'s feature macros (from a Developer PowerShell):

```powershell
cmake -S . -B out/lint-audit -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSOKOBAN_BUILD_TESTS=OFF -DSOKOBAN_PRECOMPILED_HEADERS=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
python tools/check_compiler_warnings.py --database out/lint-audit/compile_commands.json --compiler C:/path/to/g++.exe --tidy C:/path/to/clang-tidy.exe --sources-only --output out/compiler-tidy
```

These host audits supplement native builds. They still use Windows types,
platform branches, SDK, and drivers, so they do not establish that Linux CI
passes. Report each checked configuration separately; passing CTest alone
does not verify Release compiler warnings or the complete lint policy.

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
serial/parallel scene preparation at multiple scene sizes. Production effect
fixtures also measure mirror/witch swaps, turret muzzle/ribbon/impact bursts,
portal particles, blurred ice, gate/mirror energy, and linked-object auras,
both individually and under combined emitter pressure. Every case uses the
engine's bounded telemetry and CPU scopes, including worker-chunk scopes for
parallel workloads. It writes `performance-report.md`,
machine-readable `performance-results.json`, and a `cpu-trace.json` that opens
in Chrome tracing or Perfetto. The report ranks relative optimization
candidates such as task crossover points, cache value, and materially costly
nonlinear scene scaling; compare numeric results only on the same machine
and power state. Use `--filter scene-preparation` to isolate a group and
`--quick` for a 12-sample run. `RunPerformanceSuites.ps1 -Quick` also runs a
matched cold/warm application-startup pair plus baseline, point-light stress,
serial-scene, level 5 screens 3/5, and all six effect GPU fixtures. Debug runs
select the Level Editor in the developer workspace on puzzle/effect cases and add hidden
workspace and disabled CPU-profiler controls. The startup report records construction,
first-frame, and process wall time, while its logs break out Vulkan instance,
device, swapchain, render-resource, pipeline, audio, and renderer phases. Its
full mode adds render-scale, AO, translucency, frustum-culling, point-shadow,
and command-recorder A/B captures, all seven level 5 screens, and screen 5
water/reflections, render-scale, and 1x/8x MSAA controls. Repeated baselines
expose thermal or power-state drift. Every evidence report uses the same
ranked analyzer as the live profiler.

Puzzle captures use a fixed 1/60 s simulation step. Brief effects are retained
at a seeded visible phase so they remain present throughout warm-up and the
final 120 measured frames. Reports include complete application frame time,
frame intervals and pacing, update/UI/build costs, particle draw counts,
special-shader coverage, and a separate GPU particle phase. Screenshot
readback and encoding are excluded from the timing window. Debug evidence
also exports the application CPU trace. To reproduce the reported Debug
slowdown and its controls:

```powershell
.\tools\RunPerformanceSuites.ps1 -BuildDirectory out\visual-studio -Configuration Debug
```

The CPU suite also measures repeated editor document classification and level
browser scans, plus repeated reads from the retained browser snapshot.
The browser refreshes external changes within half a second and invalidates
immediately after saves and project mutations. If `nvidia-smi` is available,
GPU cases archive clocks,
temperature, power, and throttling reasons before and after the run. Use those
snapshots and the repeated baselines to identify hardware drift.

See [the October 3 investigation](docs/performance/2026-10-03-gameplay/README.md)
for measurements, fixes, and the remaining optimization priorities.
The [October 5 water follow-up](docs/performance/2026-10-05-water/README.md)
records the static cell feature cache, paired GPU measurements and image
comparisons. Use `--disable-water-cell-cache` for its procedural control;
the full performance matrix includes this comparison on level 5, screen 5.

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
It disables validation, tests, the ImGui workspace, and all content
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
An optional `@camera {"pitch":45,"yaw":90}` directive sets that screen's camera
angles in degrees. Pitch ranges from 0 (straight down) to 89, and yaw from
-180 to 180, rotating the +Y viewpoint toward +X. Screens without it use the
default 30-degree pitch and zero yaw. The level editor's **Screen Camera** panel
provides **Tilt**, **Rotation**, and **Reset Camera To Default** controls; changes
preview immediately and are saved with the screen. Gameplay, draft play, and
screen previews use the authored angles, and the top-down control temporarily
sets pitch to zero before returning to the authored tilt.
Any number of `@decoration` directives may reference manifest model names and
provide authored transforms. Each Gate tile has an accompanying `@gate`
directive that identifies its cell, the pressure plates that open it, and its
RGB color. Each Rotator tile likewise has an `@rotator` directive with the
same fields, and each Elevator tile an `@elevator` directive with the same
fields plus `levels`, its list of stop layers. A `@plate` directive records
a plate authored beneath something already standing on it (see Plates below).
A `@objectlink {"cell":[x,y,z],"color":[r,g,b]}` directive gives a rock,
ice block, or turret a linked-object color. Movable objects of the same color
repeat one another's successful moves when their own destination is available.
Portals come in four edge types: North (`O`), East (`o`), South (`p`),
and West (`q`). Each uses `@portal {"cell":[x,y,z],"color":[r,g,b]}` metadata.
Exactly two entrances with the same 8-bit RGB color in one screen form a pair;
other group sizes stay inactive. The front faces into the owning tile, marked
by swirling colored sparks. Units can stand on or enter that tile normally.
Only moving outward through its portal edge transports a unit, emerging into
the paired portal's owning tile from its edge. Crossing from the back is an
ordinary move. Direction rotates to point away from the exit edge, preserving
incoming slide or conveyor momentum. The exit tile must be free.
Portals can connect different layers and transport pushed units; turret,
mirror and witch rays follow the same front-edge geometry. Their colors are
independent of linked-object and pressure-plate groups of the same color.
Units can be authored on a portal with `@plate` metadata. All four portal
brushes use the active Link Color and the existing color tools.
A playable example is [portals.scr](docs/examples/portals.scr).

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
@objectlink {"cell":[2,2,1],"color":[0.2,0.4,1.0]}
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
#CR  #
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

A closed Gate is a solid block that fills its whole tile: it blocks every
entity and sightline, and units can stand on top of it, so a Gate on a floor
layer works as a bridge or trapdoor. It opens only while every pressure plate
listed in its `plates` array is occupied by a living player, movable object, or
enemy; a Gate with no linked plates stays closed. An optional
`"startOpen":true` in its `@gate` record inverts it: open while its plates are
not all pressed, closed while they are (an unlinked start-open Gate is always
open). Set it with the Start Open checkbox under Gates in the level editor's
Tiles palette, where such gates are drawn faded. An open Gate is empty space:
units pass through it and fall through it. When a Gate opens, the column of
units resting on top of it drops at once (bottom first), and can land on
further plates. A Gate that closes on a hero, enemy or turret kills it; a rock
or ice block in its cell cannot be destroyed and holds it open until it leaves.
A unit with nothing below an opened Gate to land on stays where it is, as a
move into a bottomless column is refused. The gate and its linked plates share
the configured color in the game. Links are authored with link colors in the
level editor (see Link Colors under Level Editor).

Linked rocks, ice blocks, and turrets use their 8-bit RGB link color as a
group identity. Whenever one moves for any reason, each other object of that
color attempts the same one-cell direction; a partner whose destination is
blocked simply stays put and does not cancel the original move. Adjacent
partners are resolved front-to-back so a whole row can advance. Each linked
object is surrounded by a translucent, animated smoky aura made from a
slightly enlarged copy of its own model and tinted with the group color.
Identical colors in separately authored overworld screens remain separate
groups after those screens are composed.

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

A Lock Plate (`J`) holds any living unit standing on it in place, including
heroes, rocks, ice, mirrors, turrets and enemies. Units may enter it or start
on it using `@plate` stacking. It prevents walking, slides, pushes, pulls,
linked movement and teleportation while enabled. The editor links it to all
pressure plates of its color and provides a **Start Enabled** toggle. With
the toggle off, pressing every linked plate enables the lock; with it on,
pressing them disables the lock. Without links it keeps its start state.
Its screen record is `@lockplate` with `cell`, `plates`, `color`, and optional
`startEnabled: true`. The rounded-square plate and padlock model is generated by
`tools/make_lock_plate_model.py`; disabled locks appear dimmed in gameplay.

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

Minecart Gates use `g` in screen grids. Paint one onto any rail piece to keep
the track underneath, or paint the rail beneath an existing gate. The editor
saves that rail in an `@plate` record. The striped horizontal barrier aligns
with the track and lifts as a cart passes, closing once it clears. Empty carts
and carts carrying characters can pass in either direction; walking characters
and loose blocks cannot. A block loaded onto a cart also stops it at the gate.
A cart parked inside a gate holds it open until it departs. Gates need no
pressure links, and removing one leaves its rail in place.

Mirrors are pushable units and reflect from their current position. A mirror
on a Rotator is turned by it, changing the corners it reflects between. A
mirror presses a pressure plate while standing on it. Pushing a mirror off an
End exposes it for a hero, and that End counts towards completion.

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

Developer builds expose the headless `LevelEditor` through ImGui. The UI invokes
editor commands but does not own document or filesystem policy.

- Link Colors: in the editor, every pressure plate and device has a link
  color, and a device is driven by exactly the pressure plates of its color.
  Rocks, ice blocks, and turrets can also be painted into a color group; they
  repeat the moves of the other movable objects in that group. To link a plate
  to two gates, give all three the same color; to make a rotator need two
  plates, give it and both plates one color. Colors match as the 8-bit RGB the
  picker shows. Newly painted plates and devices take the Link Color chosen in
  the Tiles palette; painting a linkable tile over itself recolors it to that
  color. Holding `Alt` turns
  the cursor into an eyedropper: clicking picks up the tile and, from a plate
  or device, its link color. Holding `Ctrl` turns it into a brush dipped in
  the Link Color: clicking or dragging gives every plate, device, or movable
  object it
  touches that color, the whole drag as one undo step. While either key is
  held, that tool is all a click does, in any editor tool: nothing is
  placed, deleted, moved or painted onto the ground, and the tile preview is
  hidden. The Links list shows each color group with its
  members, warns about devices with no plates, and can recolor a whole group
  (choosing another group's color merges them). Colors are only an authoring
  aid for plates and devices: saving writes each device's explicit `plates`
  list. Movable-object colors remain gameplay metadata in `@objectlink`
  records. Screens authored before link colors open with their links
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
  | Hold `Ctrl` + click or drag | Link-color brush: give each pressure plate, device, or movable object touched the Link Color |
  | `1`-`9` | Choose from the recent-tiles strip at the top of the Tiles palette |
  | `PageUp` / `PageDown` | Change the active layer |
  | `L` | Lock edits to the active layer |
  | `Tab` | Cycle Tiles, Mesh Decorations and (overworld) Screen Selectors |
  | `T` / `R` / `S` | Decoration gizmo: move / rotate / scale |

- Every editor control above can be rebound under **Options > Controls >
  Editor Controls** (developer builds, Keyboard tab), which groups them as
  Editing, Playtest and Recent Tiles. The Level Editor panel lists the
  current bindings. Editor-only bindings may reuse gameplay keys; Undo, Back
  and Play/Stop Draft are live in both and so conflict with both.
- `+ Layer Below` and `+ Layer Above` insert undoable Air layers and preserve
  water-layer numbering.
- Painting one cell beyond an edge expands every layer transactionally.
- The Mesh Decorations tool scans source `assets/` for `.gltf` and `.glb`
  files in developer builds. Any discovered mesh can be selected: an unregistered
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

The `dev` and `dev-fast` builds add these to the workspace:

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
- **Resume on launch.** When a developer session ends, the game records where
  you were in `dev-session.json` in the save directory. The next launch
  skips the title, continues the active save slot, and reopens the editor
  document you were editing. Turn this off in the **Session** menu, or
  launch with `--title` once.

Launch options for jumping straight to what you are working on:

| Option | Effect |
| --- | --- |
| `--continue` | Continue the active save slot instead of showing the title (any build). |
| `--title` | Show the title even if the developer session would resume. |
| `--level <n> [--screen <m>]` | Continue, then enter puzzle screen `m` (default 0) of level `n`. Developer builds only (`dev` and `dev-fast`). |
| `--edit <path>` | Continue, then open a level document in the editor, e.g. `--edit levels/level3/screen2.scr`. Developer builds only (`dev` and `dev-fast`). |

In Visual Studio's Open Folder mode, add arguments with the startup item's
**Debug and Launch Settings** (`launch.vs.json`, kept under `.vs/`).

## Solutions

`solutions/` holds one recorded solution per finished puzzle screen. The
`solution_replay` test replays each one against the screen whose content
matches it and fails, naming the step and the first hero, block or enemy
that went somewhere else, when a rule change breaks a recording. It also
compares the complete settled gameplay state after every input, naming the
first differing field, and fails if a recording solves its screen early.
Missing, stale, malformed, or failing recordings for current finished screens
fail the suite; all current screens require coverage by default.

`solutions/coverage.json` may exempt an unfinished screen with its level and
screen indices, current gameplay digest, and a nonempty reason. A gameplay edit
or a removed screen invalidates that exception until it is reviewed. The
minecart demonstration at level 5, screen 2 has no End tile and is the current
exception. New finished screens must be solved and recorded before merging.

A file looks like this:

```text
format 2
level-digest a7799b56118fdd89
recorded-for level0/screen0
step up p1=6,4,2
state {"activeButtons":[],"activeHeroController":1,"automaticMotionPaused":false,"elevators":[],"enemies":[],"minecarts":[],"movables":[],"players":[{"cell":[6,4,2],"character":"rogue","controller":1,"dead":false,"drowned":false,"id":1,"quarterTurns":0,"sliding":null}],"turnedMirrors":[]}
step up p1=6,3,2
state {"activeButtons":[],"activeHeroController":1,"automaticMotionPaused":false,"elevators":[],"enemies":[],"minecarts":[],"movables":[],"players":[{"cell":[6,3,2],"character":"rogue","controller":1,"dead":false,"drowned":false,"id":1,"quarterTurns":0,"sliding":null}],"turnedMirrors":[]}
```

Each `step` is one input (`up`, `down`, `left`, `right`, `cycle`,
`interact`, `undo`) followed by what it changed: `p` players, `m` movable
blocks and `e` enemies, each `id=x,y,z` with `+dead`, `+fallen` or
`+drowned` when that changed. The digest covers the gameplay layers, water
and hero, gate links and start state, device links/stops, object and portal
groups, and covered plates. Decorations and camera angles do not affect it,
so re-decorating a screen keeps its solution. Each step must be followed by
one complete JSON `state` line. It preserves entity identity, type, controller,
position, death flags, slide momentum and rotation, mirror turns,
elevator/minecart positions and phases, button pulses, the active hero, and
automatic-motion pause state. Format 1 is rejected; re-record its inputs
through the current Driver rather than migrating its old expectations.
Solutions live outside `levels/` because the content pipeline rejects
unexpected files there, and they are matched by digest, so renumbering
screens does not break them. After changing a screen's layout, record it
again.

Developer builds record every solve automatically. When you solve a campaign
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

- **Cooperative elevator search completeness.** Level 5 screen 1 has a verified
  68-input production replay, but the current best-first search reported
  exhausted after 25,593 generated positions. Investigate walking-region
  canonicalization when one hero boards an elevator and another activates it.
  That cause is not yet established; an exhausted search is not currently
  proof that such a puzzle is unsolvable.
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

The Developer Tools `Asset Manifest` tab's `Sounds` section supports
native file selection through `Browse` and `+ File`. Files outside `assets/`
are copied into `assets/custom/audio/` with unique names. `Play` auditions the
selected file immediately through the game's audio engine, using the sound
set, sound-effects, and master volumes; `Stop Preview` ends the audition.
Selections can be previewed before saving. `Save` also copies selected sounds
into staged runtime assets. New sound sets start empty, and empty sets or
missing sound files do not block builds; staging warns and skips missing sound
files. Other required assets and sound path containment remain validated.

Gameplay sounds include portal travel, gates opening/closing, minecart gate opening, rotator turns,
pressure plate press/release, button pulses, and the manifest's `laser` effect
at turret muzzle flash time. `minecart-travel` and `elevator-moving` loop only
while their platforms visibly move, with short start/stop fades. One-shot
mechanic cues are transient and do not replay from undo or saved history.
The original WAV effects in `assets/custom/audio/` can be regenerated with
`python tools/generate_mechanic_audio.py` (NumPy required); both movement loops
use periodic synthesis for seamless playback.

Portals and conveyor belts also have atmospheric loops (`portal-ambience` and
`conveyor-ambience`). They follow the controlled hero's animated position,
including height: silent at or beyond two tiles, half volume at one tile,
and the sound set's full volume on its tile. All matching tiles share one loop
at the nearest tile's volume, so long belts and nearby parallel belts do not
stack voices or become louder. Brief fades smooth entering/leaving range and
hero switches. Menus, screen transitions, editor mode and suspension stop these
loops; loading a screen or draft rebuilds the emitter positions.

In the Asset Manifest sound editor, enable `Atmospheric loop` and choose its
`Tile source`, `Audible distance (tiles)`, `Full-volume distance (tiles)`, and
`Falloff exponent`. An exponent of 1 gives linear falloff; higher values fade
faster. These settings and the set volume take effect after `Save` without
restarting. In JSON they live in the sound entry's optional `atmosphere`
object (`source`: `portal` or `conveyor`, `audibleDistanceTiles`,
`fullVolumeDistanceTiles`, `falloffExponent`). The full-volume distance must be
non-negative and less than the audible distance, and the exponent positive.
Atmospheric sounds still use the global sound-effects and master volume.

Selecting a bard replaces the level soundtrack with `Alpha Dance.ogg`, using a
0.6-second crossfade in both directions. Switching to another hero restores
the level soundtrack, or fades to silence if that level has no music. Repeated
requests and rapid switches keep playing tracks' positions and fade levels.
Character music is declared with `"character": "bard"` instead of `"level"`
in a manifest music entry. The Music editor's `Character soundtrack` option
exposes the selector and track volume; playback uses the music and master
volume controls. Character tracks also apply to editor draft playback.

`assets/animation_catalog.json` is the source of truth for animation usage,
playback tuning, and animation ordering. Each manifest animation records its
validated source duration and a global speed. Every code-declared semantic use
such as `player.idle` or `enemy.attack` selects a clip, contributes its own
speed multiplier, and may own normalized timeline events. A use may declare a
`startAfter` gate naming an event on another use. The shipped catalog places
`attack-connected` at 90% of `enemy.attack` and gates `player.death` on it;
drowning and other deaths without a concrete attacking enemy still begin
immediately.

Developer builds expose all of this in the Developer Tools `Animation` tab. The
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
- `src/engine/DecorationMeshCatalog.*`: Developer-authoring discovery of source
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
