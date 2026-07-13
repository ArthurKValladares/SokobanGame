# Sokoban 3D Project Handoff

This file is intended for another coding agent picking up work on the project. It summarizes the current shape of the game, the codebase, level format, asset pipeline, implemented mechanics, recent decisions, and rough areas that still need attention.

## Repository Note

Treat the local checkout as the canonical project location:

```text
C:\Users\arthu\Documents\Projects\Sokoban Game
```

## Project Idea

This is a small C++20 Sokoban-like 3D puzzle game. It uses SDL3 for platform/window/input and Vulkan 1.4 for rendering. The game is tile/grid based, but rendered as an isometric-ish 3D board with GLTF assets, lighting, shadows, animated character movement, and an in-game ImGui level editor in Debug builds.

The core loop is classic Sokoban-inspired:

- The player moves on a layered grid.
- Movable objects can be pushed.
- The goal/end unlocks when all pressure plates are occupied.
- Levels can contain multiple screens and multiple vertical layers.
- Additional mechanics include ice/sliding, water/falling, ladders, and conveyors.

## Build And Run

Main dependencies:

- CMake 3.25+
- Vulkan SDK 1.4+ with `glslc`
- Visual Studio 2022 / C++20 compiler
- SDL3 is vendored in `third_party/SDL` and built statically.
- ImGui is optional but used in Debug if `third_party/imgui` exists.

Common commands:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\sokoban.exe
```

Headless rules tests (no SDL/Vulkan needed at runtime; built by default via `SOKOBAN_BUILD_TESTS`):

```powershell
cmake --build build --config Debug --target sokoban_rules_tests
.\build\Debug\sokoban_rules_tests.exe
```

Debug builds define `SOKOBAN_ENABLE_DEBUG_UI=1`, which enables ImGui engine controls and the level editor. Release builds may not have the editor/debug UI available.

## Important Source Map

- `src/main.cpp`: process entry point.
- `src/engine/Application.*`: main loop, input handling, animation/presentation, level loading, editor integration, frame construction for rendering. Gameplay state lives in a single `GameState state_` member; per-movable animation data lives in a parallel `movableVisuals_` vector.
- `src/engine/Rules.*`: headless gameplay rules engine. `GameState` (player + movables + fallen flags + slide momentum) plus pure functions in `sokoban::rules` — `step` advances the whole world one discrete step (simultaneous one-tile moves, pushes, ladder climbs, momentum, falls, water, conveyors); `hasPendingMotion` reports whether the world would keep moving without input; queries cover conveyors, unfilled water, pressure plates, and end unlock. No SDL/Vulkan/rendering dependencies; tested by `tests/RulesTests.cpp`.
- `src/engine/Level.*`: level file parsing, serialization, layered grid storage, walkability/support rules, player/movable extraction.
- `src/engine/TileTypes.*`: tile enum, character mapping, colors, helper predicates such as `tileTypeAllowsEntity`.
- `src/engine/LevelEditor.*`: ImGui level editor, document state, painting/deleting, file browser, draft play mode, deleted-level handling.
- `src/engine/render/VulkanRenderer.*`: Vulkan setup, swapchain, dynamic rendering, shadow pass, model pass, debug UI rendering, descriptor resources.
- `src/engine/render/GltfMesh.*`: small custom GLTF/GLB loader, static mesh loading, skinned mesh loading, animation sampling/skinning.
- `src/engine/render/ImageData.*`: texture loading through WIC.
- `src/engine/ui/Ui.*`: very small in-game immediate UI used for the quit confirmation, including crude bitmap-glyph text.
- `shaders/`: GLSL shader sources compiled to SPIR-V by CMake.
- `levels/`: source `.scr` level files copied into `build/assets/levels`.
- `assets/`: source KayKit asset packs.

## Implemented Tile Types

Tile character mappings are defined in `src/engine/TileTypes.hpp`.

```text
' '  Air
'.'  Ground
'#'  Wall
'E'  End
'P'  Pressure plate
'C'  Player start
'R'  Rock / movable block
'I'  Ice / movable ice block and ice floor
'W'  Water
'L'  Ladder
'^'  Conveyor up
'v'  Conveyor down
'>'  Conveyor right
'<'  Conveyor left
```

Important tile behavior:

- `Player`, `Rock`, and `Ice` are stored as movable entities rather than static level cells. When parsed, their underlying static level tile becomes Air.
- `Ground` and `Wall` are solid blocks.
- `Water` supports entities but can also be filled/occupied by fallen entities.
- `End` and `PressurePlate` are surface entities rendered as thin tiles.
- `Ladder` and conveyors allow entities in their own cell.
- Conveyors are passable cells that apply automatic movement.

## Level File Format

Level screens are plain text `.scr` files under `levels/levelN/screenM.scr`.

The modern format uses sequential layers:

```text
@layer 0
.........
.........
.........

@layer 1
#########
#   C   #
#   E   #
#########
```

Rules:

- Layer headers must be exactly sequential starting with `@layer 0`.
- Empty lines between layers are allowed.
- All layers are normalized to the max width/height found in the file; missing cells become Air.
- Single-layer legacy files without `@layer` are still accepted.
- There must be exactly one player start tile `C`.
- Ladders are validated at load time: every `L` must be adjacent to a Ground tile `.` on the same layer.

Typical layer usage:

- Layer 0: floor/support tiles such as Ground or Water.
- Layer 1: gameplay layer with player, walls, goals, plates, rocks, ladders, conveyors.
- Higher layers: elevated floors/walls/entities.

Entity support/walkability:

- An entity cell must be in bounds.
- The cell itself must allow entities.
- The cell below must exist and support entities.
- Support currently comes from solid blocks or Water.

## Gameplay Features

Core movement (discrete step system):

- Game time advances in discrete world steps (`rules::step`); every entity
  moves at most one tile per step, and all entities move simultaneously
  (player can walk while ice slides and conveyors carry rocks).
- Ice sliding is momentum stored in `GameState` (`playerSliding`,
  `Movable::sliding`): one tile per step until blocked, fallen, or off
  slippery ground. Slide momentum overrides player input.
- Steps last `config::stepDurationSeconds` (debug-adjustable); all entities
  interpolate across the same step duration, so chained steps animate as
  continuous motion. Per-step movement rates other than one tile are a
  planned extension.
- WASD moves the player (one tile per step; held keys step repeatedly).
- `Z` undoes one step; undoing pauses pending world motion until the next
  input-driven step. `R` restarts.
- Movement history stores one record per step for undo.

Goals and pressure plates:

- `P` pressure plates are tracked separately.
- The end is considered unlocked when all pressure plates are occupied by a movable or player state as implemented by `isEndUnlocked()`.
- Entering an unlocked `E` advances to the next screen or level.

Rocks:

- `R` is pushable.
- Pushing checks whether the rock can move into the next cell.
- Rocks participate in sliding/falling/water behavior.

Ice:

- `I` can be a movable ice block.
- Ice floor/sliding logic causes player and movable blocks to slide until a stop condition.
- Ice rendering uses a translucent/blurred look through the renderer's scene-color sampling path.

Water and falling:

- Entities can fall through unsupported cells.
- Water can catch/fill with fallen entities.
- Water rendering has depressed height and edge faces where adjacent water is open.
- Moving out of water is handled specially to render transitions.

Ladders:

- `L` must be placed next to `Ground` on the same level.
- Ladder rendering is placeholder geometry: two thick brown rungs in the upper/lower third of the block, attached to the neighboring ground.
- Climbing begins when the player is on the ladder tile and moves toward the ground tile the ladder is attached to.
- Climbing is blocked if the destination/attached tile is occupied.

Conveyors:

- `^`, `v`, `>`, `<` represent conveyor directions.
- Conveyors use the KayKit Platformer `conveyor_4x4x1_blue` GLTF asset.
- Conveyors move every entity standing on them one tile per world step, resolved inside `rules::step` together with all other movement (player input overrides the belt under the player). Conveyed movables get the usual slide/fall/water treatment. Conveyors never push one entity into another; blocked entities stay put.
- Belt surfaces scroll one texture cycle per step, matching rider speed.
- Conveyor-started movement uses the conveyor interval as animation duration, so chained conveyor motion appears continuous.
- Conveyor rendering uses primitive material texture indices from the GLTF so the blue body, dark belt, and white arrows show correctly.

## Level Editor

The editor is built with ImGui and is enabled in Debug builds. It is not polished product UI.

Editor capabilities:

- Load/save `.scr` files.
- New/resize documents.
- Add/delete layers.
- Paint tile types from a palette.
- Delete tiles.
- Undo editor operations.
- Play draft and return to current screen.
- Browse levels/screens under the configured root.
- Add screens before/after existing screens.
- Add levels before/after existing levels.
- Soft-delete levels into a Deleted tab.
- Restore or permanently delete soft-deleted content.
- Mirror edited source levels into runtime build assets for testing.

Important editor behavior:

- The active layer is shown with lower layers underneath.
- "Lock edits to current layer" changes paint targeting behavior.
- Clicking usually adds above; `R + click` replaces; `D + click` deletes.
- Deletion preview now dithers the selected tile instead of hiding it completely.
- Addition preview also uses dithered preview geometry.
- Picking logic was fixed so top/side wall faces are selected more reliably instead of accidentally selecting the ground below.
- Placing a ladder in the editor validates same-layer adjacent Ground.

Rough editor areas:

- The UI is dense and debug-tool-like.
- Text/layout is pure ImGui and not designed as final player-facing tooling.
- File browser workflows work, but need UX hardening and safety review.
- Editor/render picking has had several bugs and should be tested whenever camera, board projection, or layer behavior changes.

## Rendering And Assets

Renderer:

- Vulkan 1.4, dynamic rendering, synchronization2, extended dynamic state, graphics pipeline libraries.
- Uses SDL3 window/Vulkan integration.
- Has a shadow pass and scene pass.
- Supports MSAA modes, wireframe, line width controls, lighting controls, grid overlay, and render stats in Debug UI.
- Renders simple tile faces procedurally and GLTF models for certain tiles/entities.

Model assets currently used:

- KayKit Block Bits 1.0:
  - `bricks_A` for wall/ground-style blocks.
  - `stone` for rocks.
  - `water` for water.
  - `glass` for ice.
- KayKit Adventurers 2.0:
  - `Rogue.glb`
  - `rogue_texture.png`
  - animation clips from `Animations/gltf/Rig_Medium`.
- KayKit Platformer Pack 1.0:
  - `conveyor_4x4x1_blue.gltf`
  - `conveyor_4x4x1_blue.bin`
  - `platformer_texture.png`
  - `threads.png`

Asset path decisions:

- Current source asset folders use names with spaces:
  - `assets/KayKit Block Bits 1.0`
  - `assets/KayKit Adventurers 2.0`
  - `assets/KayKit Platformer Pack 1.0`
- The old folder names `KayKit_BlockBits_1.0_FREE` and `KayKit_Adventurers_2.0_FREE` were replaced in `CMakeLists.txt`.

CMake asset pipeline:

- Shaders are compiled from `shaders/*.glsl` to `build/assets/shaders/*.spv`.
- Levels are copied from `levels/` to `build/assets/levels`.
- Only selected model files are copied into `build/assets/models`; adding a new model usually requires adding copy commands and dependencies in `CMakeLists.txt`.

GLTF loader notes:

- `GltfMesh.*` is a small custom loader, not a general-purpose robust GLTF implementation.
- It supports enough JSON parsing, buffers, accessors, nodes, skins, and animations for the current assets.
- Static model vertices include `textureIndex`; conveyors opt into `usePrimitiveMaterialTextures`.
- The conveyor texture mapping currently relies on primitive material indices mapping to shader sampler choices.
- If adding complex GLTF assets, consider switching to a proven GLTF library or broadening loader support carefully.

Shader notes:

- `model.vert.glsl` accepts position, normal, UV, and texture index.
- `triangle.frag.glsl` samples:
  - shadow map
  - resolved scene color for blur/translucency
  - rogue texture
  - platformer texture
  - platformer thread texture
- Push constants carry transform, lighting, grid, material, and texture options.

## UI And Text Rendering Rough State

There are two UI systems:

- ImGui Debug UI/editor in Debug builds.
- A tiny custom `UiContext` for in-game overlays such as quit confirmation.

The custom UI/text is intentionally primitive:

- Text is rendered with a tiny hard-coded 5x7-ish glyph set.
- Only a small subset of letters exists.
- No font asset pipeline.
- No layout engine.
- No wrapping, kerning, localization, dynamic sizing, or high-quality button styling.
- Text rendering should be considered placeholder.

If making player-facing menus, pause screens, settings, or polished editor UI, plan to replace or expand this system.

## Recent Work Summary

Major recent additions and fixes:

- Added layered levels with `@layer N` sections.
- Added level editor/document browser and draft play flow.
- Added Debug UI controls for rendering, lighting, tile scale, grid, and conveyor rate.
- Added KayKit GLTF asset pipeline for blocks, Rogue character, animations, and conveyors.
- Added animated Rogue player rendering.
- Added rocks/movables, undo, restart, pressure plates, and screen progression.
- Added water/falling behavior and water edge rendering.
- Added ice sliding and translucent/blurred ice rendering.
- Added ladder tile `L`.
  - Placement/load validation requires adjacent same-layer Ground.
  - Placeholder rung rendering was made thicker/larger.
  - Climb behavior was corrected to start from the ladder tile and move toward attached ground.
  - Climbing is blocked when the attached tile is occupied.
- Fixed editor deletion picking so elevated/wall tiles are selected more accurately.
- Changed deletion preview to dither the selected tile instead of hiding it.
- Fixed addition preview picking for adding on top of top-wall faces.
- Added conveyor tiles `^`, `v`, `>`, `<`.
  - Uses Platformer Pack conveyor asset.
  - Adds adjustable conveyor movement rate.
  - Conveyors move all entities on them (player and movables) via `rules::applyConveyorStep`; they never push one entity into another.
  - Conveyor animation duration now matches the conveyor interval for continuous motion.
  - Conveyor belt surfaces are animated by scrolling the thread texture in sync with the movement rate.
  - Fixed conveyor GLTF material rendering by carrying primitive material texture indices through the mesh pipeline.
- Renamed asset paths in `CMakeLists.txt` from old `_FREE` folder names to the current space-containing KayKit folder names.

## Current Codebase State

At the time this handoff was created:

- The live checkout had a clean `git status`.
- `cmake --build build --config Debug` had passed after the latest changes.
- `HANDOFF.md` was added after that verification, so run another build if desired after reviewing this file.

Known useful verification commands:

```powershell
git status --short
rg -n "KayKit_Adventurers_2\.0_FREE|KayKit_BlockBits_1\.0_FREE" .
cmake --build build --config Debug
.\build\Debug\sokoban.exe
```

The `rg` command above should return no matches.

## Important Design Decisions

- Keep gameplay rules in the headless `Rules` module as pure functions of `(Level, GameState)`; `Application` owns input, animation, and presentation only, and the renderer receives a render-frame description rather than owning game rules.
- When changing or adding mechanics, implement them in `Rules.cpp` and add cases to `tests/RulesTests.cpp`; the tests compile without SDL/Vulkan so they can run anywhere.
- Store `Player`, `Rock`, and movable `Ice` as dynamic entities extracted from level data rather than static cells.
- Use character-driven tile definitions as the single source of truth for level parsing/editor palette.
- Use layered `.scr` text files instead of a binary or JSON format for now.
- Keep CMake asset copying explicit so the runtime build directory contains only the assets actually needed.
- Use ImGui only for debug/editor tooling, not final player-facing UI.
- Preserve existing code style and avoid broad abstractions unless a mechanic really needs them.

## Likely Next Improvements

High-value gameplay/editor work:

- Extend `tests/RulesTests.cpp` (movement/push/slide/fall/water/ladder/conveyor basics exist) with level parsing, ladder validation, undo round-trips, and editor document operations.
- Add more Sokoban mechanics only after hardening interactions among existing ones.
- Revisit conveyor edge cases if needed (e.g. conveyor loops/cycles do not rotate; entities in a full cycle stay put).
- Revisit the exact semantics of water/fallen entities and ice sliding edge cases.
- Add better level progression metadata, names, and completion tracking.

Rendering/assets:

- Replace the custom GLTF parsing with a robust library if assets get more complex.
- Generalize material/texture binding instead of hard-coding rogue/platformer samplers.
- Add asset manifests to avoid growing `CMakeLists.txt` copy commands forever.
- Improve visual consistency between procedural tiles and GLTF assets.
- Verify model orientation/scale whenever a new asset is added.

UI:

- Replace placeholder `UiContext` text with real font rendering.
- Add real menu/settings/pause UI.
- Make level editor layout more deliberate and less debug-panel-like.
- Add user-facing explanations/tooltips only where they help, not as clutter.

Engineering:

- Gameplay rules now live in the headless `Rules` module with tests; `Application.cpp` still owns rendering-layout, editor, and UI responsibilities that could be split further.
- Add save format/versioning if level files evolve.
- Review asset licensing/readme files before distribution.

## Practical Tips For The Next Agent

- Prefer `rg` for searches.
- Build with `cmake --build build --config Debug`.
- Check `git status --short` before and after edits; the user may have local changes.
- For manual edits, keep changes small and use existing patterns.
- When adding a tile:
  - Update `TileType` and `tileTypeDefinitionTable`.
  - Update helper predicates in `TileTypes.cpp`.
  - Update parser/render/gameplay/editor behavior as needed.
  - Add CMake asset copies if a new model/texture is needed.
  - Add editor preview/rendering support.
  - Verify level serialization still maps one-to-one.
- When adding a model:
  - Copy model, bin, and textures through `sokoban_assets`.
  - Add a `RenderModel` enum if needed.
  - Load/upload mesh in `VulkanRenderer::createModelResources`.
  - Return it from `meshForModel`.
  - Set per-tile transform/scale/rotation in `Application`.
  - Add descriptor/shader support if the model needs textures/materials beyond current bindings.
