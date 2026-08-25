# Sokoban 3D

Sokoban 3D is a C++20, SDL3, and Vulkan 1.3 puzzle game and small game-engine
codebase. It supports layered levels, animated 3D presentation, persistent save
slots and settings, keyboard/gamepad remapping, a manifest-driven content
pipeline, and a headless editor model exposed through Debug ImGui tools.

## Current Features

- Layered Sokoban movement with rocks, pressure plates, goals, undo, restart,
  multi-screen levels, and completion tracking.
- Ice, ladders, conveyors, falling, configurable water layers, and four
  directional mirror types that can reflect the player and movable entities.
- Immobile animated enemies that track and attack adjacent players, can be
  pushed by blocks, and support skeleton-driven held-item attachments.
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
- Vulkan SDK 1.3+ with `glslc` available
- A Vulkan-capable GPU and driver

SDL3, miniaudio, nlohmann/json, stb, ImGui, and the Karla UI font are vendored.
Texture decoding uses stb_image rather than platform-specific image APIs.

## Build And Run

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\sokoban.exe
```

`SOKOBAN_ENABLE_VALIDATION` defaults to `ON`. Headless tests are built by
default and can be disabled with `-DSOKOBAN_BUILD_TESTS=OFF`.

Debug builds include the ImGui developer tools and can mirror edited source
levels into staged runtime content. Release builds use only packaged,
executable-relative assets.

## Tests

The project currently registers CTest suites covering rules, level parsing,
campaign and gameplay sessions, persistence and migrations, input routing,
player UI, renderer state, scene preparation and picking, editor transactions,
assets, animation, particles, tasks, logging, and content packaging.

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure --no-tests=error
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure --no-tests=error
```

The `Required Tests` GitHub Actions workflow performs clean, independent Debug
and Release builds on every push and pull request, runs the complete CTest
registry (including a hidden-window Vulkan device/submission smoke test) in
both configurations, and separately gates AddressSanitizer/UBSan,
clang-tidy's static analyzer, and a bounded libFuzzer run against hostile
player-profile input. Repository branch protection should require every
workflow check before merging to `main`.

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

## Shipping Package

The Windows `shipping` preset produces an optimized, editor-free x64 build.
It disables validation, tests, the Debug ImGui workspace, and all content
editing source files; enables MSVC LTO; and emits the optimized executable's
PDB into a separate Symbols ZIP rather than the player-facing Runtime ZIP.

Before publishing a build, follow
[`packaging/ReleaseValidation.md`](packaging/ReleaseValidation.md). Its
PowerShell package gate verifies the final ZIP from a fresh extraction and
launches it briefly; the accompanying Windows and GPU-driver matrix is the
required human acceptance record.

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
shared settings profile. Keyboard and Controller tabs show and remap their
respective bindings independently. Binding rows and contextual gameplay
prompts use Kenney Input Prompts glyphs. SDL3 identifies the active controller
and supplies its physical face-button labels, so Xbox, PlayStation, Nintendo
Switch, GameCube, and Steam Deck controls use their matching symbols; unknown
controllers fall back to the generic glyph set.

## Level Format

Screens are text `.scr` files containing sequential `@layer N` sections. An
optional `@water N` directive makes Air on that layer resolve to Water and
extends the water beyond the authored board without expanding camera bounds.
Any number of `@decoration` directives may reference manifest model names and
provide authored transforms. Metadata must appear before `@layer 0`.

```text
@water 0
@decoration {"model":"Tree","position":[4.5,2.5,1.0],"rotation":[0.0,0.0,30.0],"scale":[1.0,1.0,1.25]}
@decoration {"light":{"castsShadows":true,"color":[1.0,0.55,0.2],"intensity":3.0,"offset":[0.0,0.0,1.2],"range":6.0,"shadowBias":0.004,"shadowOpacity":0.9},"model":"Lantern","position":[2.5,1.5,1.0],"rotation":[0.0,0.0,0.0],"scale":[1.0,1.0,1.0]}

@layer 0
.....
.. ..
.....

@layer 1
#####
# C #
#####
```

Common tile symbols:

| Symbol | Tile | Symbol | Tile |
| --- | --- | --- | --- |
| space | Air | `.` | Ground |
| `#` | Wall | `C` | Player |
| `R` | Rock | `P` | Pressure plate |
| `E` | End | `I` | Ice |
| `L` | Ladder | `W` | Legacy explicit water |
| `^ v > <` | Conveyors | `1 2 3 4` | Mirror orientations |
| `D` | Decorative block | | |

Decorative blocks render but have no gameplay, support, occupancy, camera-fit,
or water-grid-bound semantics. New water layouts should use `@water N`; `W`
remains supported for older screens.

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
- Press `Z` to undo editor changes.
- Debug builds list these controls in the Level Editor panel and expose their
  configurable bindings under **Options > Controls > Editor Controls**.
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
