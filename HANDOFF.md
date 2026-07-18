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
- miniaudio is vendored in `third_party/miniaudio` (header-only; compiled once in `src/engine/MiniaudioImpl.cpp` together with its bundled `extras/stb_vorbis.c` for OGG decoding — that TU builds with warnings disabled and nothing else may define `MINIAUDIO_IMPLEMENTATION`).

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

Level parser/serializer tests cover legacy and layered files, malformed input,
ragged-layer normalization, CRLF loading, entity extraction, and ladder
validation:

```powershell
cmake --build build --config Debug --target sokoban_level_tests
.\build\Debug\sokoban_level_tests.exe
```

Headless gameplay-session tests cover command buffering, action timing,
push metadata, restart, undo, and automatic-motion pausing:

```powershell
cmake --build build --config Debug --target sokoban_gameplay_session_tests
.\build\Debug\sokoban_gameplay_session_tests.exe
```

Headless animation-controller tests cover Rogue animation selection,
deduplication, crossfades, reverse playback, preview overrides, and reset:

```powershell
cmake --build build --config Debug --target sokoban_animation_controller_tests
.\build\Debug\sokoban_animation_controller_tests.exe
```

Headless presentation tests cover mutable settings normalization, lighting/grid
conversion, entity interpolation, clip/facing behavior, fallen offsets, and
gameplay render-frame construction:

```powershell
cmake --build build --config Debug --target sokoban_presentation_tests
.\build\Debug\sokoban_presentation_tests.exe
```

Debug builds define `SOKOBAN_ENABLE_DEBUG_UI=1`, which enables ImGui engine controls, the `LevelEditorDebugUi` adapter, and an Animation Preview window (browses every glTF/GLB under the source `assets/` tree and plays any clip on the player model with play/pause/scrub/speed controls, overriding gameplay animation while active). Release builds still compile the headless `LevelEditor` API but do not expose the ImGui editor/debug UI.

## Important Source Map

- `src/main.cpp`: process entry point.
- `src/engine/Application.*`: top-level coordinator for the SDL event loop, input translation, level/screen progression, gameplay-session scheduling, editor picking/painting, modal flow, and component lifetime. It no longer owns mutable rendering settings, visual interpolation, debug animation-browser state, or render-frame construction.
- `src/engine/PresentationSettings.*`: mutable runtime presentation settings initialized from the immutable defaults in `Config.hpp`. Owns lighting, SSAO/shadow tuning, grid appearance, surface geometry, tile scales, normalization, sun-direction conversion, and renderer-facing lighting/grid values.
- `src/engine/GameplayPresentation.*`: headless presentation state derived from `GameplaySession::Action` snapshots. Owns player/movable interpolation, fallen render offsets, player clip/facing/playback state, and the shared world/conveyor animation clock without mutating authoritative gameplay state.
- `src/engine/RenderFrameBuilder.*`: SDL/Vulkan-free construction of gameplay and editor `RenderFrameData`. Owns tile/model mapping, static geometry, water edges, ladder rungs, editor previews/pick-only cells, dynamic entities, tile scaling, and conveyor texture offsets.
- `src/engine/ApplicationDebugUi.*`: Debug-only ImGui adapter for engine statistics and tuning. Edits `PresentationSettings` and calls the public `GameplaySession`/`VulkanRenderer` controls instead of storing application logic.
- `src/engine/AnimationPreviewDebugUi.*`: Debug-only owner of animation asset scanning, clip selection, preview playback state, and renderer preview delegation.
- `src/engine/AudioSystem.*`: miniaudio-backed sound playback behind a pimpl (`EngineHandle`), so no miniaudio types leak into headers. Preloads the manifest's `footsteps` and `stone-drag` sound sets straight from the source assets tree (no copying) with `MA_SOUND_FLAG_DECODE` into never-reallocated `std::vector<ma_sound>`s. `update(dt, playerWalking, pushingStone)` drives the pure `FootstepCadence` struct (first step fires immediately on walk start, then one per `footstepIntervalSeconds`, burst-capped with debt drop after hitches) and plays a random non-repeating footstep. It also edge-detects `pushingStone` (from `player().movingClip == RoguePush && motion.moving`, so undoing a push drags too): push start picks a random non-repeating `stoneDragLoop1..4.ogg` and plays it as a seamless miniaudio loop with a 15 ms fade-in; push end fades it out over 40 ms. The loop assets live in `assets/custom/audio/` and are generated from the Kenney Foley `stoneDrag*` samples by `tools/make_drag_loops.py` (ffmpeg required): silence-trimmed, tail crossfaded into the head for a continuous seam, plus a 1.5 ms edge ramp because Vorbis does not decode file edges exactly (raw whole-file looping would dip/click). Dialog/editor paths call `update(dt, false, false)` so an Escape mid-push stops the loop. `playMusicForLevel(level)` streams (`MA_SOUND_FLAG_STREAM`) one looping per-level soundtrack with a 600 ms crossfade on switch; the level-to-track mapping lives in the manifest's `music <level>` sections and is applied in `loadCurrentScreen`, where re-requesting the playing level is a no-op so screens within a level keep the music running. Per-sound volumes relative to master (`config::musicVolume/footstepVolume/stoneDragVolume`) are tunable in Debug UI > Audio alongside the master volume and footstep interval; the drag and music volumes apply live to the playing loop. Degrades gracefully (`available()` false, silent) if the device or files are missing. `Application::update` feeds it `presentation_.player().motion.moving` on the normal gameplay path only, so dialogs/editor are silent. Defaults come from `config::masterVolume` / `config::footstepIntervalSeconds`; both are runtime-tunable in Debug UI > Audio (tune Footstep Interval there to match the walk animation).
- `src/engine/GameplaySession.*`: headless per-screen gameplay orchestration between input and `Rules`. Owns the authoritative `GameState`, buffered move/undo/restart commands, active action timing, action history, a branch-safe undo stack, automatic world steps, and the post-undo automatic-motion pause. Emits `Action` snapshots for `Application` to animate. Tested by `tests/GameplaySessionTests.cpp` (`sokoban_gameplay_session_tests`).
- `src/engine/Rules.*`: headless gameplay rules engine. `GameState` (player + movables + fallen flags + slide momentum) plus pure functions in `sokoban::rules` — `step` advances the whole world one discrete step (simultaneous one-tile moves, pushes, ladder climbs, momentum, falls, water, conveyors); `hasPendingMotion` reports whether the world would keep moving without input; queries cover conveyors, unfilled water, pressure plates, and end unlock. No SDL/Vulkan/rendering dependencies; tested by `tests/RulesTests.cpp`.
- `src/engine/Level.*`: level file parsing, serialization, layered grid storage, walkability/support rules, player/movable extraction. Tested by `tests/LevelTests.cpp` (`sokoban_level_tests`).
- `src/engine/TaskSystem.*`: standard-library-only worker pool for task-based parallelism. `taskSystem().enqueue(fn)` returns a future (exceptions propagate on get); `parallelFor(count, minChunk, fn(begin, end))` runs chunked loops with the calling thread participating. Tasks must not block on other tasks (no dependency graph yet). Used by GLTF vertex skinning (`skinWithPoses`) and lazy CPU-side model/texture/animation preparation in `VulkanModelResources`; Vulkan publication stays on the render thread. Tested by `tests/TaskSystemTests.cpp` (`sokoban_task_tests`).
- `src/engine/TileTypes.*`: tile enum, character mapping, colors, helper predicates such as `tileTypeAllowsEntity`.
- `src/engine/LevelEditor.*`: headless editor model and command API. Owns document state/history, tile validation, draft construction, level load/save, source/runtime mirroring, browser enumeration, screen/level renumbering, soft-delete/restore, and guarded permanent deletion. It has no SDL, Vulkan, or ImGui dependency and is tested by `tests/LevelEditorTests.cpp` (`sokoban_level_editor_tests`).
- `src/engine/LevelEditorDebugUi.*`: Debug-only ImGui adapter for `LevelEditor`. Owns widget text buffers and confirmation-modal presentation only; every editor state transition and filesystem action is delegated to the headless API.
- `src/engine/render/RenderTypes.hpp`: renderer-facing frame contract and model/animation enums, independent of the Vulkan facade.
- `src/engine/AssetManifest.*`: runtime asset manifest - the single source of truth for models, textures, animations, tile visuals (model + render scale per tile type), and sounds. Parses `assets/manifest.txt` (plain text, sections start unindented, properties indented, `#` comments, paths relative to the assets root) at startup with line-numbered errors and full validation (unique names, resolvable material textures, exactly one `role player` skinned model, all three `player-idle/move/push` animation roles, texture count <= `maxModelTextures`). `RenderModel`/`RenderAnimation` are now runtime ids (index+1 into the manifest lists; 0 = cube/none) defined in `RenderTypes.hpp`. Adding an asset, tile visual, or sound is a manifest edit plus relaunch - no CMake, enum, or renderer change. Headless; tested by `tests/AssetManifestTests.cpp` (`sokoban_asset_manifest_tests`).
- `src/engine/render/RenderAssetRequirements.*`: Vulkan-free model/animation requirement sets plus shared tile-to-model mapping. Computes requirements from a loaded `Level` for prefetching or from `RenderFrameData` as a draw-time safety net. Tested by `tests/AssetRequirementsTests.cpp` (`sokoban_asset_requirements_tests`).
- `src/engine/render/AnimationController.*`: Vulkan-free owner of gameplay animation clips, Rogue clip selection, preview overrides, deduplication, and crossfade state. It emits immutable skinning requests and is tested by `tests/AnimationControllerTests.cpp` (`sokoban_animation_controller_tests`).
- `src/engine/render/SkinnedMeshUpdater.*`: owns the Rogue's skinned source mesh and dynamic Vulkan vertex/index buffers. It consumes `AnimationController` requests, performs CPU skinning/blending, and uploads changed vertices.
- `src/engine/render/VulkanModelResources.*`: owns lazy per-asset load states, TaskSystem futures, static model meshes, texture images/samplers, manifest material bindings, and failure retention. CPU parsing/decoding runs on workers; completed results are published to Vulkan on the render thread. It orchestrates `AnimationController` and `SkinnedMeshUpdater` while exposing lightweight mesh/material/texture views and loading statistics to the renderer.
- `src/engine/render/VulkanSsaoPass.*`: owns the swapchain-sized R8 ambient-occlusion target and sampler, plus depth/AO transitions and the fullscreen AO/composite recording sequence. Pipelines and scene descriptors are passed in as non-owning handles.
- `src/engine/render/VulkanShadowPass.*`: owns the fixed-size shadow depth image, sampler, and image-layout state. It records pass setup/transitions while `VulkanRenderer` supplies the scene-specific shadow draw traversal between `begin` and `end`.
- `src/engine/render/VulkanSwapchainResources.*`: owns the swapchain, image views, MSAA color attachment, scene depth/resolve-depth, scene-color sampling target, acquire/present calls, resize lifecycle, frame attachment transitions, and the ice-blur scene-color copy.
- `src/engine/render/VulkanPipelineFactory.*`: owns the shared pipeline layout and all scene, model, UI, shadow, SSAO, composite, and visualization pipelines. Shader-module loading and graphics-pipeline construction no longer live in `VulkanRenderer`.
- `src/engine/render/VulkanSceneDescriptors.*`: owns the scene descriptor-set layout, pool, set, and bindings for shadow, copied scene color, model textures, sampled scene depth, and SSAO. Resize/MSAA changes update the same set with new attachment views.
- `src/engine/render/VulkanResourceUtils.*`: exception-safe shared Vulkan image allocation, image-view creation, memory-type selection, and destruction used by the focused resource owners. `VulkanRenderConstants.hpp` holds the shared 256-byte push-constant contract.
- `src/engine/render/VulkanRenderer.*`: top-level Vulkan instance/device/queue and frame orchestration, command buffers/synchronization, debug UI, camera/projection calculations, and scene draw traversal. Resource, pass, pipeline, descriptor, model, and animation ownership is delegated to the focused components above.
- `src/engine/render/GltfMesh.*`: small custom GLTF/GLB loader, static mesh loading, skinned mesh loading, animation sampling/skinning. `skinGltfMeshBlended` skins with a pose blended between two clips; `SkinnedMeshUpdater` uses it for the player crossfades requested by `AnimationController` over `config::playerAnimationFadeSeconds`.
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
  moves at most its per-step rate in tiles, and all entities move
  simultaneously (player can walk while ice slides and conveyors carry rocks).
- Ice sliding is momentum stored in `GameState` (`playerSliding`,
  `Movable::sliding`): one tile per step until blocked, fallen, or off
  slippery ground. Slide momentum overrides player input.
- Steps last `config::stepDurationSeconds` (debug-adjustable); all entities
  interpolate across the same step duration, so chained steps animate as
  continuous motion.
- Movement rates are `rules::StepRates` in tiles per step, by movement source
  (player input, slide momentum, conveyors); everything defaults to one.
  Multi-tile rates resolve as repeated simultaneous one-tile micro-steps, so
  fast entities still block, vacate, and push correctly. Rates are adjustable
  in the Debug UI under Tile Geometry > Step Rates.
- WASD moves the player (one tile per step; held keys step repeatedly).
- `Z` undoes one step; undoing pauses pending world motion until the next
  input-driven step. `R` restarts.
- `GameplaySession` stores one action record per completed step for undo; the
  authoritative state commits only after `Application` finishes animating the
  action.

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
- Simultaneous intents that target the same destination all wait; movable storage order never picks a winner. Chains can still advance into cells vacated during the same micro-step.
- Belt surfaces scroll one texture cycle per step, matching rider speed.
- Conveyor-started movement uses the conveyor interval as animation duration, so chained conveyor motion appears continuous.
- Conveyor rendering uses primitive material texture indices from the GLTF so the blue body, dark belt, and white arrows show correctly.

## Level Editor

The editor logic is available through the headless `LevelEditor` API. Debug builds expose it through `LevelEditorDebugUi`, an ImGui adapter that is not polished product UI. A future in-game editor can use the same commands without depending on ImGui.

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

- ImGui does not mutate editor document or filesystem state directly. It reads `LevelEditor` state and invokes explicit commands/setters.
- Draft validation and the transition into draft playback are handled by `LevelEditor::beginDraftPlayback`; the UI only forwards the returned level to the application callback.
- Document history, load/save, project renumbering, runtime mirroring, deleted-level restore, and permanent-delete containment are covered by headless tests.
- Undo histories are branch-safe: making a new gameplay move or editor edit after undo does not replay an abandoned action during later undos.
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

- Vulkan 1.4, dynamic rendering, synchronization2, and extended dynamic state.
- Uses SDL3 window/Vulkan integration.
- Has a shadow pass and scene pass.
- Supports MSAA modes (default is MSAA 8x, automatically falling back to the highest count the device's color+depth framebuffers support; the Debug UI combo shows the requested mode, Rendering Stats shows the active sample count), wireframe, line width controls, lighting controls, grid overlay, and render stats in Debug UI.
- Screen-space ambient occlusion (SSAO) applies to all geometry, tiles and GLTF models alike. `VulkanSwapchainResources` provides sampled or resolved scene depth, and `VulkanSsaoPass` records the fullscreen depth-only AO pass (12-tap golden-angle spiral, range falloff to avoid halos, `config::ssaoRadiusPixels/DepthRange`) into an R8 target before multiply-compositing the blurred result onto the lit image. Descriptor bindings 5 (scene depth) and 6 (AO) live in `VulkanSceneDescriptors`. Toggle, strength, and raw-AO visualization are mutable `PresentationSettings` edited by Debug UI > Lighting.
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
- `assets/custom/Rig_Medium_Push.glb`: generated block-pushing walk cycle
  (Walking_B with both arm chains frozen straight forward; the pose is solved
  with forward kinematics against the Rogue skeleton and numerically verified,
  see ARM_TARGETS in the tool to tweak direction/height). Regenerate with
  `python tools/make_push_animation.py`; used as the player's push animation
  (manifest animation `RoguePush`, `role player-push`).
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
- The old folder names `KayKit_BlockBits_1.0_FREE` and `KayKit_Adventurers_2.0_FREE` were replaced in the asset manifest.
- Model/texture/animation/sound files load directly from the source `assets/` tree (paths in `assets/manifest.txt`); `.bin` glTF sidecars resolve naturally next to their `.gltf`.

CMake asset pipeline:

- Shaders are compiled from `shaders/*.glsl` to `build/assets/shaders/*.spv`.
- Levels are copied from `levels/` to `build/assets/levels`.
- Everything else (models, textures, animations, sounds, tile visuals) is
  defined in `assets/manifest.txt` and loaded at runtime straight from the
  source assets tree - CMake is not involved. `AssetManifest::loadFromFile`
  validates the manifest at startup and throws descriptive errors.
- Shaders compile with a fixed `MODEL_TEXTURE_COUNT=16`, which must equal
  `sokoban::maxModelTextures` (`AssetManifest.hpp`); descriptor writes pad the
  texture array with a fallback texture, so the manifest can define up to 16
  textures without shader or pipeline changes.

Runtime lazy asset pipeline:

- Renderer creation allocates only a 1x1 white fallback texture needed to keep
  every descriptor-array slot valid. Catalog models, textures, and animations
  are not loaded up front.
- `Application::applyLevel` computes the active screen's requirements and calls
  `VulkanRenderer::ensureAssets`. Required CPU tasks are scheduled together and
  may execute in parallel; the call blocks only when an asset needed now has
  not finished preparing.
- After a normal screen load, `Application` scans every screen in the current
  level and every screen in the next level, merges their requirements, and
  calls `VulkanRenderer::preloadAssets`. Level files are small and read on the
  main thread; model parsing, animation parsing, and WIC image decoding run as
  independent TaskSystem jobs.
- `VulkanRenderer::drawFrame` verifies the exact frame requirements and then
  publishes at most one completed preload per frame. Loaded assets remain
  cached for the process lifetime; there is no eviction policy yet.
- Static/skinned Vulkan buffers, texture images, upload command buffers, queue
  submission, and descriptor updates all happen on the render thread. Texture
  uploads use the existing graphics queue and command pool and wait for that
  queue before replacing shared descriptors. There are deliberately no worker
  command pools, dedicated transfer queues, or concurrent Vulkan uploads.
- Background failures are retained in the asset slot, counted in Debug UI, and
  reported without interrupting the current level. If that asset later becomes
  required, `ensureAssets` throws a contextual path/kind error.
- Debug UI > Rendering Stats reports loaded/pending model, texture, and
  animation counts plus failures. Unrequested assets are the difference between
  total, loaded, pending, and failed counts.

GLTF loader notes:

- `GltfMesh.*` is a small custom loader, not a general-purpose robust GLTF implementation.
- It supports enough JSON parsing, buffers, accessors, nodes, skins, and animations for the current assets.
- Static model vertices include `textureIndex`; the manifest declares whether a
  model uses those primitive indices (`primitive-textures true`).
- Manifest texture order defines the Vulkan descriptor-array indices. The
  current conveyor asset maps primitive indices 1 and 2 to `Platformer` and
  `PlatformerThread`; this invariant is documented beside the texture list in
  `assets/manifest.txt`.
- If adding complex GLTF assets, consider switching to a proven GLTF library or broadening loader support carefully.

Shader notes:

- `model.vert.glsl` accepts position, normal, UV, and texture index.
- `triangle.frag.glsl` samples the shadow map, resolved scene color, and a
  fixed 16-slot model texture descriptor array (`MODEL_TEXTURE_COUNT`, padded
  with a fallback texture).
- Each manifest model declares `material none`, `material texture <Name>`, or
  `material primitive-texture-index <n>`; draw code passes that mode and
  texture index through push constants instead of checking model names or
  sampler bindings. A `belt-scroll true` model scrolls its UVs with the
  conveyor clock (no hard-coded conveyor special case).
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

- Replaced eager runtime catalog loading with a lazy, TaskSystem-backed asset
  pipeline. The active screen is guaranteed before use, current/next-level
  assets prepare in the background, and completed GPU resources are published
  serially on the render thread at a one-asset-per-frame budget.
- Added headless `RenderAssetRequirements` planning so level prefetch and frame
  fallback use the same tile/model semantics as `RenderFrameBuilder`, with a
  dedicated 26-check regression suite.

- Replaced the CMake-generated asset catalog with a runtime asset manifest
  (`assets/manifest.txt` + headless `AssetManifest`): string-named models,
  textures, animations (with player roles), per-tile visuals, and sound sets;
  `RenderModel`/`RenderAnimation` became runtime ids and assets load directly
  from the source tree.
- Extracted model mesh/texture/animation lifetime management from
  `VulkanRenderer` into `VulkanModelResources`; `VulkanRenderer.cpp` dropped
  from roughly 4,750 lines to roughly 3,800 lines.
- Split Rogue animation and dynamic mesh responsibilities out of
  `VulkanModelResources`: `AnimationController` now owns Vulkan-free clip,
  preview, deduplication, and crossfade semantics, while
  `SkinnedMeshUpdater` owns CPU skinning and the dynamic Vulkan mesh buffers.
- Split the remaining Vulkan renderer responsibilities into
  `VulkanSsaoPass`, `VulkanShadowPass`, `VulkanSwapchainResources`,
  `VulkanPipelineFactory`, and `VulkanSceneDescriptors`, with shared image
  allocation in `VulkanResourceUtils`. `VulkanRenderer.cpp` is now roughly
  2,150 lines and retains orchestration and scene traversal instead of owning
  those resource lifetimes and construction details.
- Split application presentation/configuration responsibilities into
  `PresentationSettings`, `GameplayPresentation`, and `RenderFrameBuilder`,
  with `ApplicationDebugUi` and `AnimationPreviewDebugUi` as adapters.
  `Application.cpp` dropped from roughly 1,900 lines to roughly 620 and now
  concentrates on event-loop, level progression, gameplay scheduling, editor
  interaction, and modal orchestration.
- Split headless gameplay orchestration out of `Application` into
  `GameplaySession`. `Application` now translates SDL controls and animates
  session actions, while state/history/undo/restart/action timing are covered
  by a dedicated 52-check test executable.
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

At the time this handoff was updated:

- Commit `54a5324` contains the lazy asset pipeline,
  `RenderAssetRequirements`, level prefetch integration, diagnostics, and tests
  described above.
- Full Debug and Release builds passed from the clean `out/visual-studio` build
  tree. A Debug Vulkan-validation visual runtime smoke test rendered level 0
  correctly, and the process remained healthy while background tasks completed.
- All nine CTest suites pass, including the new `asset_manifest` suite
  (manifest parsing, validation failures, and the shipped manifest file).

Known useful verification commands:

```powershell
git status --short
rg -n "KayKit_Adventurers_2\.0_FREE|KayKit_BlockBits_1\.0_FREE" .
cmake --build out\visual-studio --config Debug
ctest --test-dir out\visual-studio -C Debug --output-on-failure
.\out\visual-studio\Debug\sokoban.exe
```

The `rg` command above should return no matches.

## Important Design Decisions

- Keep gameplay rules in the headless `Rules` module as pure functions of `(Level, GameState)`. `GameplaySession` owns command/state/history orchestration, `GameplayPresentation` owns visual interpolation/animation state, `Application` coordinates SDL input and component lifetime, and the renderer receives a render-frame description rather than owning game rules.
- When changing or adding mechanics, implement them in `Rules.cpp` and add cases to `tests/RulesTests.cpp`; the tests compile without SDL/Vulkan so they can run anywhere.
- Store `Player`, `Rock`, and movable `Ice` as dynamic entities extracted from level data rather than static cells.
- Use character-driven tile definitions as the single source of truth for level parsing/editor palette.
- Use layered `.scr` text files instead of a binary or JSON format for now.
- Keep runtime asset selection explicit through `assets/manifest.txt`; code
  refers to assets via manifest roles/flags (player model, belt-scroll) or
  tile mappings, never hard-coded names.
- Keep runtime asset requirement planning in the Vulkan-free
  `RenderAssetRequirements` layer. CPU file work may use `TaskSystem`, but all
  Vulkan object creation, upload submission, and descriptor mutation must stay
  on the render thread unless the queue/command-pool architecture is redesigned
  and validated as a separate change.
- Keep editor behavior in the headless `LevelEditor` API. ImGui and any future player-facing editor UI should be adapters that call it rather than owning document or filesystem logic.
- Keep compile-time constants/defaults in `Config.hpp`, mutable presentation
  tuning in `PresentationSettings`, authoritative gameplay tuning/state in
  `GameplaySession`, and renderer-facing frame assembly in `RenderFrameBuilder`.
  UI layers may edit/call those APIs but should not duplicate their state.
- Preserve existing code style and avoid broad abstractions unless a mechanic really needs them.

## Likely Next Improvements

High-value gameplay/editor work:

- Extend parser/rules/editor coverage alongside new mechanics; parser normalization, movement conflicts, command/history boundaries, editor mutation, and core project-filesystem workflows now have dedicated regression cases.
- Add more Sokoban mechanics only after hardening interactions among existing ones.
- Revisit conveyor edge cases if needed (e.g. conveyor loops/cycles do not rotate; entities in a full cycle stay put).
- Revisit the exact semantics of water/fallen entities and ice sliding edge cases.
- Add better level progression metadata, names, and completion tracking.

Rendering/assets:

- Add an explicit cache budget/eviction policy if the manifest grows enough
  for lifetime caching to become expensive.
- Extend manifest material metadata with exact primitive-texture dependency
  masks; `PrimitiveTextureIndex` models currently conservatively request every
  manifest texture.
- Add timing/history diagnostics for blocking `ensureAssets` calls and
  background CPU preparation if asset stalls become difficult to reproduce.
- Replace the custom GLTF parsing with a robust library if assets get more complex.
- Improve visual consistency between procedural tiles and GLTF assets.
- Verify model orientation/scale whenever a new asset is added.
- Keep `VulkanRenderer` as the frame orchestrator. If its remaining scene
  traversal grows, extract a scene recorder or projection/layout component
  around a concrete need rather than moving Vulkan ownership back into it.

UI:

- Replace placeholder `UiContext` text with real font rendering.
- Add real menu/settings/pause UI.
- Make level editor layout more deliberate and less debug-panel-like.
- Add user-facing explanations/tooltips only where they help, not as clutter.

Engineering:

- Gameplay rules, gameplay orchestration, presentation, render-frame
  construction, and editor document behavior now live in headless `Rules`,
  `GameplaySession`, `GameplayPresentation`, `RenderFrameBuilder`, and
  `LevelEditor` components with tests. `Application` still owns SDL input
  translation, editor pointer interaction, level progression, and modal flow;
  extract one of those only when it gains enough independent policy to test.
- The `TaskSystem` now handles lazy CPU asset preparation as well as skinning.
  Grow it by moving more independent CPU work onto tasks (render-frame building,
  animation updates) and eventually adding task dependencies/graphs when
  systems need ordering. Keep Vulkan publication render-thread-owned.
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
  - Add a `tile <Name>` section (model/scale) to `assets/manifest.txt` if needed.
  - Add editor preview/rendering support.
  - Verify level serialization still maps one-to-one.
- When adding a model:
  - Add `model`/`texture`/`animation` sections to `assets/manifest.txt` (path,
    geometry, material, orientation flags) and map tiles to it with `tile`
    sections; scale defaults live there too.
  - No enum, CMake, or renderer change is needed; relaunch to apply.
  - Extend material modes only if the model cannot use `none`,
    `texture <Name>`, or `primitive-texture-index <n>`.
