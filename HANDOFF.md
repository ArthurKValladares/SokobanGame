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
- nlohmann/json 3.11.3 is pinned as a vendored single header in
  `third_party/nlohmann`; it parses the runtime asset manifest without any
  configure-time downloads.
- stb_image 2.30 and stb_truetype are pinned in `third_party/stb`; texture
  files are decoded without platform APIs and the player UI builds a real TTF
  atlas from the staged, OFL-licensed Karla font.

Common commands:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\sokoban.exe
```

Every normal build runs the manifest-driven content pipeline and stages a
versioned `assets/` tree beside the executable. The game never reads models,
audio, shaders, or levels from the source checkout at runtime. To validate and
refresh content explicitly after editing `assets/manifest.json` or a level:

```powershell
cmake --build build --config Debug --target sokoban_content
```

Release install and ZIP packaging:

```powershell
cmake --build build --config Release
cmake --install build --config Release --prefix build\install
cmake --build build --config Release --target package
```

The install contains `sokoban.exe`, its executable-relative `assets/` tree,
and dependency licenses. MSVC builds use the static C/C++ runtime so the ZIP
does not require a separately installed Visual C++ Redistributable.

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
push metadata, restart, undo, automatic-motion pausing, solution move counts,
checkpoint restore, invalid/impossible history rejection, and per-screen undo
reset:

```powershell
cmake --build build --config Debug --target sokoban_gameplay_session_tests
.\build\Debug\sokoban_gameplay_session_tests.exe
```

Player-profile tests cover format-7 round trips, format-1/2/3/4/5/6 migration, exact
active-screen/undo checkpoints, completion bests, normalization, atomic writes,
asynchronous save coalescing/shutdown flushing, prior-save backups,
corrupt-save archival, backup recovery, and double-corruption default recovery:

```powershell
cmake --build build --config Debug --target sokoban_profile_tests
.\build\Debug\sokoban_profile_tests.exe
```

Input tests cover keyboard and gamepad action mapping, remapping, button edges,
stick direction thresholds, invalid-binding diagnostics, and raw SDL event
capture for the remapping UI:

```powershell
cmake --build build --config Debug --target sokoban_input_tests
.\build\Debug\sokoban_input_tests.exe
```

Image-data tests cover RGBA decoding, concurrent worker-style loads with
byte-identical results, and contextual diagnostics for missing or invalid
files:

```powershell
cmake --build build --config Debug --target sokoban_image_data_tests
.\build\Debug\sokoban_image_data_tests.exe
```

UI tests cover TTF atlas generation, glyph draw data, reusable button/slider/
checkbox interactions, options-page navigation, graphics changes, audio
changes, and quit confirmation:

```powershell
cmake --build build --config Debug --target sokoban_ui_tests
.\build\Debug\sokoban_ui_tests.exe
```

Title-shell tests cover title navigation, new-game confirmation, level/screen
select locking and screen choice, the level-complete overlay, and the pause
menu's title-exit row:

```powershell
cmake --build build --config Debug --target sokoban_title_tests
.\build\Debug\sokoban_title_tests.exe
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

Headless content-pipeline tests cover manifest/file validation, path
containment, external glTF sidecars, level continuity, staging replacement,
and notice inclusion:

```powershell
cmake --build build --config Debug --target sokoban_content_pipeline_tests
.\build\Debug\sokoban_content_pipeline_tests.exe
```

Debug builds define `SOKOBAN_ENABLE_DEBUG_UI=1`, which enables one ImGui Developer Tools window with Engine, Asset Manifest, Level Editor, and Animation Preview tabs. Animation Preview browses source glTF/GLB content as an authoring tool and plays any clip on the player model with play/pause/scrub/speed controls, overriding gameplay animation while active. Release builds still compile the headless editor APIs but do not expose the ImGui editor/debug UI or compile source-asset paths into the executable.

## Important Source Map

- `src/main.cpp`: process entry point.
- `src/engine/Application.*`: top-level coordinator for the SDL event loop, profile-backed input translation, level/screen progression and completion recording, gameplay-session scheduling, editor picking/painting, and component lifetime. Shell/menu routing is not decided here: menu results and Back presses become `ShellFlow` events, and Application only executes the returned commands (each command handler is a thin call into an existing helper). It no longer owns mutable rendering settings, visual interpolation, debug animation-browser state, or render-frame construction.
- `src/engine/PresentationSettings.*`: mutable runtime presentation settings initialized from the immutable defaults in `Config.hpp`. Owns lighting, SSAO/shadow tuning, grid appearance, surface geometry, tile scales, normalization, sun-direction conversion, and renderer-facing lighting/grid values.
- `src/engine/GameplayPresentation.*`: headless presentation state derived from `GameplaySession::Action` snapshots. Owns player/movable interpolation, fallen render offsets, player clip/facing/playback state, and the shared world/conveyor animation clock without mutating authoritative gameplay state.
- `src/engine/RenderFrameBuilder.*`: SDL/Vulkan-free construction of gameplay and editor `RenderFrameData`. Owns tile/model mapping, static geometry, water edges, ladder rungs, editor previews/pick-only cells, dynamic entities, tile scaling, and conveyor texture offsets.
- `src/engine/ApplicationDebugUi.*`: Debug-only ImGui adapter for engine statistics and tuning. Edits `PresentationSettings` and calls the public `GameplaySession`/`VulkanRenderer` controls instead of storing application logic.
- `src/engine/DebugUi.*`: Debug-only registry and presentation owner for the single Developer Tools window. Feature adapters register content callbacks as reorderable, scrolling tabs instead of creating independent windows.
- `src/engine/AnimationPreviewDebugUi.*`: Debug-only owner of animation asset scanning, clip selection, preview playback state, and renderer preview delegation.
- `src/engine/AudioSystem.*`: miniaudio-backed sound playback behind a pimpl (`EngineHandle`), so no miniaudio types leak into headers. Preloads the manifest's `footsteps` and `stone-drag` sound sets from the staged runtime content tree with `MA_SOUND_FLAG_DECODE` into never-reallocated `std::vector<ma_sound>`s. `update(dt, playerWalking, pushingStone)` drives the pure `FootstepCadence` struct and plays randomized non-repeating footsteps. Stone dragging uses randomized seamless loops with short fades; music streams one looping track per level with a 600 ms crossfade. Manifest gains remain authored in the Asset Manifest window; profile-backed master, music, and sound-effect bus gains are previewed live and persisted when Debug UI sliders are committed. Audio degrades gracefully (`available()` false, silent) if the device or files are missing.
- `src/engine/GameplaySession.*`: headless per-screen gameplay orchestration between input and `Rules`. Owns the authoritative `GameState`, buffered move/undo/restart commands, active action timing, action history, a branch-safe undo stack, automatic world steps, the post-undo automatic-motion pause, and solution-move snapshots that restore correctly across undo/restart. Its committed-state snapshot/restore API persists the exact player/movable state and usable undo chain; restore rejects disconnected or impossible rules transitions without mutating the live session. `reset` always clears undo state at a screen boundary. Emits `Action` snapshots for `Application` to animate. Tested by `tests/GameplaySessionTests.cpp` (`sokoban_gameplay_session_tests`).
- `src/engine/InputBindings.*`: platform-neutral semantic action and binding model. Each action owns an ordered list of keyboard, gamepad-button, and signed gamepad-axis bindings, allowing keyboard+D-pad+stick defaults. `assignBinding` implements remapping semantics (removes the identical binding from every action, then replaces only the action's bindings of the same kind, so a d-pad rebind keeps a stick binding); `bindingDisplayName`/`actionBindingsDisplay` provide UI labels.
- `src/engine/Input.*`: SDL3 device owner and action mapper. Tracks raw keyboard/mouse state for editor tooling, hot-plugs gamepads, selects the most recently used controller, normalizes stick axes with threshold/pressed-edge semantics, clears stuck input on focus loss, reports active-device diagnostics, and converts raw SDL events into typed remapping candidates consumed by the Options > Controls page. `Application` consumes semantic actions only for gameplay. Covered by `tests/InputTests.cpp` (`sokoban_input_tests`).
- `src/engine/PlayerProfile.*` + `src/engine/PlayerProfileCodec.cpp`: current format-9 player progress/settings model (PlayerProfile.cpp, ~150 lines of model methods) with the strict JSON codec and migrations split into PlayerProfileCodec.cpp. Migrations are forward JSON patches (migrate1to2 .. migrate8to9) applied in sequence, then ONE strict current-format parse validates the migrated document - patches only move fields and add defaults, and unknown keys survive them, so schema strictness holds without per-format parsers. Format 9 makes the top-level progress/settings sections each optional: `serialize(ProfileSections)` writes progress-only slot files and a settings-only shared settings.json (section choice flows through `SaveStore`/`AsyncSaveStore` constructor parameters set by `SaveSlotManager`), so slot files no longer carry ignored settings copies. Stores unlocked/current level and screen, an active-screen committed gameplay checkpoint with the multi-screen level move/time counters, per-level completion, per-level `reachedScreens` counts (max screen entered + 1, feeding level-select unlocking), optional move/time bests, typed keyboard/controller action bindings, audio/video/accessibility settings, window mode/size, MSAA, AO, preset/custom internal render-scale state, and bounded normalization. `recordReachedScreen` tracks entry, `resetProgress` implements New Game (clears progress/records, keeps every setting), and `recordLevelCompletion(..., recordBests)` skips best records for runs that started past the first screen. Headless and covered with `SaveStore` by `tests/PlayerProfileTests.cpp` (`sokoban_profile_tests`).
- `src/engine/Flow.hpp`: minimal generic state-machine toolkit shared by UI
  flows and available to future gameplay flows. `flow::Machine<Derived,
  State, Event, Command, Facts>` (CRTP) owns a State and turns events plus a
  caller-supplied facts snapshot into ordered command lists via the derived
  `reduce()`; `flow::Overloaded` is the std::visit overload helper. Reducers
  are pure apart from their own state - they never touch live systems - so
  every transition is unit-testable. Conventions are documented in the
  header (intent-named variant structs; empty emission is meaningful).
- `src/engine/ShellFlow.*`: the game shell's flow built on `flow::Machine`.
  Events are Back presses, window close requests, and the three menus'
  single-action variants; facts are a six-bool snapshot (game loaded, which menus are open,
  title page, game completed); commands are intent structs (`SwitchSlot`,
  `StartLevel`, `OpenOptions{pause, allowLevelSelect}`, ...) executed by
  `Application::executeShellCommand`'s visitor. Owns every menu-precedence
  and context rule: Options-over-overlay-over-title Back routing, the
  overlay swallowing Back, pause vs. title Options context, level-select
  gating, the Continue load-if-needed pair, and the no-saves
  SwitchSlot+StartNewGame chain. Tested by `tests/ShellFlowTests.cpp`
  (`sokoban_shell_flow_tests`).
- `src/engine/SaveSlotManager.*`: headless owner of the save-slot lifecycle - the per-slot progress stores, the shared `settings.json` store, the `active-slot.txt` marker, slot summaries (progress-based emptiness, completed flags), switching (flush, store swap, marker write, settings carry-over), and deletion (drain-then-remove so in-flight writes cannot resurrect a deleted save). `Application` owns the live `PlayerProfile` and gameplay consequences; every disk decision lives here. Tested by `tests/SaveSlotManagerTests.cpp` (`sokoban_save_slot_tests`), including the fresh-install-writes-nothing guarantee, pre-split settings migration, and the reset-profile-reads-empty regression.
- `src/engine/SaveStore.*`: profile persistence rooted at SDL's platform-appropriate `SDL_GetPrefPath`. A `fileStem` constructor parameter names the slot's files (slot 1 keeps the historical `profile` stem so pre-slot saves remain valid; slots 2/3 use `profile-slot2/3`), and corrupt-archive detection derives its prefixes from those names so slots never interfere. Writes validated JSON through same-directory temporary replacement, keeps the previous valid primary as `<stem>.backup.json`, migrates old versions, archives corrupt primary/backup files for diagnosis, recovers from backup, and restores defaults when both copies are unusable.
- `src/engine/AsyncSaveStore.*`: dedicated serialized persistence worker around `SaveStore`. Deferred requests coalesce over a configurable window, while JSON encoding, backup rotation, and atomic filesystem replacement always happen off the game thread. Screen transitions and committed settings request immediate worker saves; clean shutdown flushes the newest pending profile. Diagnostics expose request/write/coalescing counts and pending/writing state.
- `src/engine/Rules.*`: headless gameplay rules engine. `GameState` (player + movables + fallen flags + slide momentum) plus pure functions in `sokoban::rules` — `step` advances the whole world one discrete step by delegating to the file-local `MicroStepResolver`, which treats the player and movables as one uniform entity array (movables first, player last, preserving historical resolution order) and runs four named phases per micro-step: deriveIntents (momentum/input/belt against the source's budget), markContested (simultaneous same-destination intents all lose), resolveMoves (multi-pass so vacated cells can be entered; direct input may push a resolved blocker), and settleBlocked (mutually blocked slides cancel). Player and movable falls share one predicate-parameterized `fallTarget` walk differing only in who occupies the cell below. `hasPendingMotion` reports whether the world would keep moving without input; queries cover conveyors, unfilled water, pressure plates, and end unlock. No SDL/Vulkan/rendering dependencies; tested by `tests/RulesTests.cpp`.
- `src/engine/Level.*`: level file parsing, serialization, layered grid storage, walkability/support rules, player/movable extraction. Tested by `tests/LevelTests.cpp` (`sokoban_level_tests`).
- `src/engine/TaskSystem.*`: standard-library-only worker pool for task-based parallelism. `taskSystem().enqueue(fn)` returns a future (exceptions propagate on get); `parallelFor(count, minChunk, fn(begin, end))` runs chunked loops with the calling thread participating. Tasks must not block on other tasks (no dependency graph yet). Used by GLTF vertex skinning (`skinWithPoses`) and lazy CPU-side model/texture/animation preparation in `VulkanModelResources`; Vulkan publication stays on the render thread. Tested by `tests/TaskSystemTests.cpp` (`sokoban_task_tests`).
- `src/engine/TileTypes.*`: tile enum, character mapping, colors, helper predicates such as `tileTypeAllowsEntity`.
- `src/engine/LevelEditor.*`: headless editor model and command API. Owns document state/history, tile validation, draft construction, level load/save, source/runtime mirroring, browser enumeration, screen/level renumbering, soft-delete/restore, and guarded permanent deletion. It has no SDL, Vulkan, or ImGui dependency and is tested by `tests/LevelEditorTests.cpp` (`sokoban_level_editor_tests`).
- `src/engine/LevelEditorDebugUi.*`: Debug-only ImGui adapter for `LevelEditor`. Owns widget text buffers and confirmation-modal presentation only; every editor state transition and filesystem action is delegated to the headless API.
- `src/engine/render/RenderTypes.hpp`: renderer-facing frame contract and model/animation enums, independent of the Vulkan facade.
- `src/engine/render/RenderResolution.*`: Vulkan-free internal-resolution policy. Validates the supported 100/75/67/50/25 presets, clamps custom percentages to 25-100, and computes rounded, non-zero scene extents; 67% deliberately means exact two-thirds so 3840x2160 becomes 2560x1440. Covered by `sokoban_vulkan_device_selection_tests` alongside the headless device-selection policy.
- `src/engine/AssetManifest.*`: runtime asset manifest - the single source of truth for models, textures, animations, tile visuals (model + render scale per tile type), sounds, and music. Parses the versioned `assets/manifest.json` with nlohmann/json, rejects malformed JSON, wrong types, missing/unknown properties, unsupported format versions, and invalid material combinations, then performs domain validation (unique names/tiles/roles, resolvable textures/models, exactly one `role: "player"` skinned model, all three player animation roles, texture count <= `maxModelTextures`). `RenderModel`/`RenderAnimation` are runtime ids (index+1 into the ordered JSON arrays; 0 = cube/none) defined in `RenderTypes.hpp`. Adding an asset, tile visual, or sound is a JSON edit plus rebuilding `sokoban_content` and relaunching - no CMake, enum, or renderer change. Headless; tested by `tests/AssetManifestTests.cpp` (`sokoban_asset_manifest_tests`).
- `src/engine/AssetManifestEditor.*`: headless editable manifest document. Loads the strict runtime model, exposes typed add/update/remove/reorder commands for every manifest section, tracks dirty/status state, serializes canonical JSON, validates through `AssetManifest`, and uses temporary/backup replacement so an invalid or failed save leaves the source manifest intact. Tested by `tests/AssetManifestEditorTests.cpp` (`sokoban_asset_manifest_editor_tests`).
- `src/engine/AssetManifestDebugUi.*`: Debug-only ImGui adapter for `AssetManifestEditor`. Provides the Asset Manifest tab and owns only widget/modal presentation; all document and filesystem behavior stays reusable by a future non-debug editor UI.
- `src/engine/ContentPipeline.*` + `tools/ContentTool.cpp`: headless production-content inventory, validation, and staging. Resolves manifest references and external `.gltf` URIs, rejects missing/escaping paths, parses every playable level, requires contiguous level/screen indices, verifies all compiled shaders, includes nearby asset notices, excludes `levels/Deleted`, and atomically replaces the output with only reachable files. Writes `content.index` with format/game version, file count, sizes, and paths. Tested by `tests/ContentPipelineTests.cpp` (`sokoban_content_pipeline_tests`).
- `src/engine/RuntimeContent.*`: resolves the read-only `assets/` directory beside the executable through `SDL_GetBasePath` and rejects missing, corrupt, unsupported, or game-version-mismatched `content.index` files. `Application`, `VulkanRenderer`, and `AudioSystem` all use this one runtime root.
- `src/engine/render/RenderAssetRequirements.*`: Vulkan-free model/animation requirement sets plus shared tile-to-model mapping. Computes requirements from a loaded `Level` for prefetching or from `RenderFrameData` as a draw-time safety net. Tested by `tests/AssetRequirementsTests.cpp` (`sokoban_asset_requirements_tests`).
- `src/engine/render/AnimationController.*`: Vulkan-free owner of gameplay animation clips, Rogue clip selection, preview overrides, deduplication, and crossfade state. It emits immutable skinning requests and is tested by `tests/AnimationControllerTests.cpp` (`sokoban_animation_controller_tests`).
- `src/engine/render/SkinnedMeshUpdater.*`: owns the Rogue's skinned source mesh and dynamic Vulkan vertex/index buffers. It consumes `AnimationController` requests, performs CPU skinning/blending, and uploads changed vertices.
- `src/engine/render/VulkanModelResources.*`: owns lazy per-asset load states, TaskSystem futures, static model meshes, texture images/samplers, manifest material bindings, and failure retention. CPU parsing/decoding runs on workers; completed results are published to Vulkan on the render thread. It orchestrates `AnimationController` and `SkinnedMeshUpdater` while exposing lightweight mesh/material/texture views and loading statistics to the renderer.
- `src/engine/render/VulkanSsaoPass.*`: owns the scene-sized R8 ambient-occlusion target and sampler, plus depth/AO transitions and the fullscreen AO/composite recording sequence. Pipelines and scene descriptors are passed in as non-owning handles.
- `src/engine/render/VulkanShadowPass.*`: owns the fixed-size shadow depth image, sampler, and image-layout state. It records pass setup/transitions while `VulkanRenderer` supplies the scene-specific shadow draw traversal between `begin` and `end`.
- `src/engine/render/VulkanSwapchainResources.*`: owns the native-resolution swapchain/image views, scaled scene color/MSAA/depth/resolve-depth attachments, acquire/present calls, resize lifecycle, frame attachment transitions, the ice-blur scene-color copy, and the final linear upscale into the swapchain before native-resolution player/debug UI. Profile VSync selects guaranteed FIFO; disabled VSync prefers mailbox, then immediate, then FIFO fallback.
- `src/engine/render/VulkanPipelineFactory.*`: owns the shared pipeline layout and all scene, model, UI, shadow, SSAO, composite, and visualization pipelines. Shader-module loading and graphics-pipeline construction no longer live in `VulkanRenderer`.
- `src/engine/render/VulkanSceneDescriptors.*`: owns the scene descriptor-set layout, pool, set, and bindings for shadow, copied scene color, model textures, sampled scene depth, and SSAO. Resize/MSAA changes update the same set with new attachment views.
- `src/engine/render/VulkanUiResources.*`: owns the one-time R8 font-atlas
  upload and sampler used by the player-facing overlay shader.
- `src/engine/render/VulkanResourceUtils.*`: exception-safe shared Vulkan image allocation, image-view creation, memory-type selection, and destruction used by the focused resource owners. `VulkanRenderConstants.hpp` holds the shared 256-byte push-constant contract.
- `src/engine/render/VulkanRenderer.*`: top-level Vulkan instance/device/queue and frame orchestration, command buffers/synchronization, debug UI, camera/projection calculations, and scene draw traversal. Resource, pass, pipeline, descriptor, model, and animation ownership is delegated to the focused components above.
- `src/engine/render/GltfMesh.*`: small custom GLTF/GLB loader, static mesh loading, skinned mesh loading, animation sampling/skinning. `skinGltfMeshBlended` skins with a pose blended between two clips; `SkinnedMeshUpdater` uses it for the player crossfades requested by `AnimationController` over `config::playerAnimationFadeSeconds`.
- `src/engine/render/ImageData.*`: platform-independent texture file loading
  and in-memory RGBA decoding through stb_image. Filesystem ownership stays in
  the engine, preserving native `std::filesystem::path` handling and allowing
  independent background loads without COM or other platform initialization.
- `src/engine/ui/FontAtlas.*`: platform-neutral Karla TTF loading, stb_truetype
  atlas generation, ASCII glyph metrics, and text measurement.
- `src/engine/ui/Ui.*`: immediate draw/input context for solid rectangles,
  textured glyphs, panels, dividers, hit testing, and drag ownership.
- `src/engine/ui/UiControls.*`: reusable styled buttons, sliders, checkboxes,
  segmented selectors, and choice steppers with mouse/focus states. Segmented
  controls consume typed `{ value, label }` choices and bind directly to the
  selected value, avoiding parallel value/label arrays and index plumbing.
- `src/engine/ui/UiLayout.*`: frame-local hierarchical layout tree for nested
  vertical/horizontal flows, content-sized groups, fixed items, padding/gaps,
  weighted flexible space, and overflow diagnostics. Controls still consume
  final `UiRect`s, while callers describe relationships instead of coordinates.
- `src/engine/ui/MenuKit.*`: shared building blocks for the player-facing
  menus. `RowList` builds a frame's focusable rows (conditionally, via
  `addIf`) and owns wrap-around navigation, so hand-maintained row enums and
  shifted-index arithmetic cannot drift from the layout; `MenuPage` is the
  standard header scaffold (padded tree, title, optional subtitle, divider);
  `trailingText`, `formatDuration` (one time format with a tenths style -
  the menus previously had three diverging copies), `centeredPanel`, and
  `centeredColumn` replace per-menu duplicates. Presentation-only; menus own
  their state and actions.
- `src/engine/ui/OptionsMenu.*`: headless menu page/navigation state and
  tree-based composition for Graphics, Audio, and separated quit confirmation.
  Named row enums replace positional focus indexes. A frame emits at most
  one `OptionsAction` (`options::SettingsChanged/Quit/ExitToTitle/
  OpenLevelSelect`); the platform-neutral settings snapshot itself (which
  includes `InputBindings`) is read via `settings()` and applied by
  `Application` to the window, renderer, presentation, audio, input, and
  profile owners. `open(settings, allowTitleExit)` adds an "Exit To Title" row only
  when opened as the in-game pause menu, and `allowLevelSelect` adds a
  "Level Select" row (pause context only; `Application` passes it once every
  level on disk has a completion record, so it is permanent for that save
  and cleared by New Game). The Controls page lists the six
  remappable gameplay actions (menu navigation is deliberately fixed) with
  press-to-rebind capture: `capturingBinding()` tells the caller to feed
  `InputState::bindingCandidate` events into `provideBindingCandidate` and to
  suppress raw key/pad navigation meanwhile (keys bound to MenuBack still pass
  so Escape cancels; Start cancels directly). Escape/Start are never bindable,
  duplicates are stolen from other actions, and Reset To Defaults restores
  `defaultInputBindings()`.
- `src/engine/ui/TitleScreen.*`: headless fullscreen title-screen state
  (Main with Continue/New Game/Options/Quit and a destructive-action New Game
  confirmation). The world is not loaded while the main menu is up; only the
  Continue/New Game results make `Application` load it. A level/screen-select
  page exists but is reachable only through `openLevelSelect` (meant for a
  future in-game entry point, currently unwired): the caller supplies
  `TitleLevelInfo` rows (screen count, unlocked/completed, reached screens,
  bests); locked levels render inert, completed levels expose every screen,
  unfinished levels expose only reached screens, and Left/Right picks the
  starting screen on the focused row. A frame of interaction emits at most
  one `TitleAction` (a variant of intent structs - `title::Continue`,
  `title::NewGameOnSlot{slot}`, `title::StartLevel{level, screen}`, ... -
  so impossible combinations are unrepresentable); `Application` owns what
  each action means. Tested by `tests/TitleScreenTests.cpp`
  (`sokoban_title_tests`). The Save Slots page (third main-menu row, showing
  the active slot number) lists three slots with summaries (Empty / Level N -
  K done / Completed!, plus an active marker); confirming the active slot
  returns to Main, confirming another emits `slotSelected` for the caller.
- `src/engine/ui/LevelCompleteOverlay.*`: headless end-of-level stats panel
  showing moves/time against previous bests with NEW BEST highlighting, and
  Continue ("Next Level") or Title Screen choices. Finishing the final level
  opens its game-complete mode instead: a congratulations screen listing
  every level's best moves/time plus whole-game totals, with Level Select
  and Title Screen actions. A frame emits at most one `OverlayAction`
  (`overlay::Continue/ToTitle/ToLevelSelect`). Also covered by
  `tests/TitleScreenTests.cpp`.
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
- Moves (walks, pushes, slides, conveyor carries) are refused when the
  destination column has nothing that can hold the entity - a solid block,
  another movable, or water somewhere below. Entities can still drop any
  number of layers onto real support, but never rest on air; falls that
  reach the bottom with no support mark the move unsupported and block it
  (`FallResult::supported` in `Rules.cpp`).
- Steps last `config::stepDurationSeconds` (debug-adjustable); all entities
  interpolate across the same step duration, so chained steps animate as
  continuous motion.
- Movement rates are `rules::StepRates` in tiles per step, by movement source
  (player input, slide momentum, conveyors); everything defaults to one.
  Multi-tile rates resolve as repeated simultaneous one-tile micro-steps, so
  fast entities still block, vacate, and push correctly. Rates are adjustable
  in the Debug UI under Tile Geometry > Step Rates.
- WASD moves the player by default (one tile per step; held keys step repeatedly).
- `Z` undoes one step by default; undoing pauses pending world motion until the
  next input-driven step. `R` restarts by default. These six gameplay bindings
  are loaded from `PlayerProfile::input`. Gamepads use D-pad or left stick for
  movement, west/X for undo, north/Y for restart, and Start for menu/back.
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
- Supports MSAA modes (default is MSAA 8x, automatically falling back to the highest count the device's color+depth framebuffers support; the Debug UI combo shows the requested mode, Rendering Stats shows the active sample count), internal render-scale presets of 100%, 75%, 67% (exact two-thirds), 50%, and 25%, plus custom percentages from 25-100%, wireframe, line width controls, lighting controls, grid overlay, and render stats in Debug UI. The 3D scene renders into scaled offscreen attachments and is linearly upscaled to the native swapchain before player/debug UI, so a 4K window can render the scene at exact 1440p or 1080p while UI remains crisp.
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
- Model/texture/animation/sound files load from the staged executable-relative `assets/` tree; paths remain authored relative to source `assets/manifest.json` and `.bin` glTF sidecars are discovered automatically.

CMake asset pipeline:

- Shaders compile into an intermediate generated directory.
- The `sokoban_content_tool` validates source assets, levels, external glTF
  dependencies, and shaders, then stages only reachable files into
  `<executable directory>/assets`. A normal build runs this target automatically.
- Staging writes to a sibling temporary directory and replaces the old content
  tree only after every source validates and copies successfully, preventing
  partial packages and removing stale files from deleted manifest entries.
- Runtime loads only the staged tree and validates `content.index` format and
  game version before reading `manifest.json`.
- CMake `install` and CPack ZIP rules consume this same staged tree and include
  SDL, miniaudio, ImGui, and discovered asset license/readme files.
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
  main thread; model parsing, animation parsing, and stb_image decoding run as
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
- Static model vertices include `textureIndex`; a model uses those primitive
  indices when its JSON material mode is `primitive-texture-index`. The parser
  infers primitive-texture loading from that mode so it cannot disagree with a
  second boolean flag.
- Manifest texture order defines the Vulkan descriptor-array indices. The
  current conveyor asset maps primitive indices 1 and 2 to `Platformer` and
  `PlatformerThread`; this invariant is documented beside the texture list in
  `assets/manifest.json`.
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

## UI And Text Rendering

There are two UI systems:

- ImGui Debug UI/editor in Debug builds.
- A Release-capable custom UI stack for player-facing menus.

The custom UI currently provides:

- A staged Karla TTF with its OFL notice, stb_truetype atlas generation, text
  measurement, and Vulkan glyph sampling.
- Reusable buttons, sliders, checkboxes, segmented controls, choice steppers,
  panels, dividers, mouse interaction, and keyboard/gamepad focus styling.
- Nested row/column layout trees with padding, gaps, content measurement,
  flexible space, and overflow detection; inserting a component shifts later
  siblings automatically and bottom actions remain anchored.
- A pause/options flow with Graphics (MSAA, internal render-scale presets plus
  a persistent Custom checkbox/25-100% slider and resolved pixel dimensions,
  AO, fullscreen/window sizes), Audio
  (live master/music sliders), Controls (press-to-rebind input remapping with
  duplicate stealing and reset-to-defaults, persisted to the profile), an
  Exit To Title entry in the pause context, and a visually separated
  confirmed Quit action.
- A player-facing game shell: the game boots to a fullscreen title screen
  with no world loaded (`Application::gameLoaded_`). The first main-menu row
  is data-driven: "Continue" when the active slot has progress, otherwise
  "New Game" (starting immediately when another save exists, or first asking
  which slot to begin on when no saves exist anywhere; the "Save Slot N" row
  is hidden entirely in that no-saves state). Settings
  (audio/video/input/accessibility) are shared across slots in a
  `settings.json` written through its own `AsyncSaveStore` (same atomic
  write/backup/recovery machinery); it bootstraps from the pre-split
  combined save's settings on first run, and slot files' settings copies are
  ignored on load. "Has a save" means non-empty progress
  (`PlayerProfile::progressEmpty`), not file existence; fresh loads return
  defaults without writing anything, and quitting from the title with no
  progress also writes nothing. Three save slots are available from the title's
  Save Slots page: switching flushes the outgoing slot, swaps the progress
  `AsyncSaveStore` to the new stem, carries the live shared settings over
  the incoming profile, unloads the world, updates `active-slot.txt`, and
  returns to the main menu. Each non-empty slot row has an inline Delete
  button (Right focuses it) behind a confirmation page; deleting the active
  slot resets the live progress and leaves the file absent until play
  resumes. Options/Quit reuse the shared menus, and Exit To
  Title returns to the menu with the world kept loaded for an instant
  Continue. Level completion pauses on a stats overlay (moves/time vs.
  bests, NEW BEST highlighting) before continuing or returning to the title.
  Finishing the final level shows the game-complete screen (all-level bests
  and whole-game totals) whose Level Select action - and, from then on, a
  pause-menu Level Select row - opens the standalone level/screen-select
  page (any unlocked level, any reached screen; such runs skip best
  records). Standalone level select closes on Back straight into the game.
  Escape at the title opens Options; Escape inside title sub-pages backs
  out.
- Semantic W/S or D-pad/stick navigation, Enter/Space or controller South
  confirmation, and Escape/Start back navigation.

It still lacks wrapping, kerning, localization, accessibility-driven scaling,
scroll containers, and constraint-based responsive reflow; add those within
these focused modules rather than moving player UI into Debug-only ImGui.

## Recent Work Summary

Major recent additions and fixes:

- Added three save slots: slot-stemmed `SaveStore`/`AsyncSaveStore` files
  (existing saves become slot 1), an `active-slot.txt` marker, and a
  title-screen Save Slots page with per-slot summaries, inline confirmed
  deletion, and a choose-a-slot flow for the first New Game. Settings moved
  out of the slots into a shared `settings.json`
  (`PlayerProfile::settingsOnly`/`adoptSettingsFrom`), bootstrapped from the
  pre-split save. Covered by store-isolation, settings-split, and
  slots-page tests.
- Added the Options > Controls input-remapping page: the six gameplay actions
  render their current keyboard/pad bindings, confirm starts a raw-event
  capture (fed from `InputState::bindingCandidate` in the SDL loop with
  navigation suppressed), same-kind bindings are replaced while other-device
  bindings survive, duplicates are stolen from other actions, Escape/Start
  cancel and can never be bound, and Reset To Defaults plus profile
  persistence complete the loop. Covered by new `sokoban_ui_tests` cases.
- Added the player-facing game shell: headless `TitleScreen` (main menu,
  destructive New Game confirmation, level/screen select fed by per-level
  `TitleLevelInfo`) and `LevelCompleteOverlay` (moves/time vs. bests) drawn
  through the shared UI stack; `Application` boots into the title over the
  restored scene, intercepts level completion behind the overlay, tracks
  per-level reached screens (profile format 8 with `resetProgress` and
  bests-eligibility for mid-level starts), and the pause menu gained Exit To
  Title. Covered by `sokoban_title_tests` and extended profile tests.
- Added format-5 player persistence under `SDL_GetPrefPath`: current/unlocked
  level, current screen, an exact committed screen state and undo stack,
  per-level completion and best move/time records, audio/video/input/
  accessibility settings, typed keyboard/controller bindings, format-1/2/3/4
  migration, atomic replacement, previous-save
  backups, corrupt-file archival, backup recovery, and default recovery. Level
  completion records successful player moves across multi-screen levels and
  excludes automatic world steps; undo/restart restore count snapshots.
  Committed actions mark the checkpoint dirty, and the latest state is captured
  at most once every two seconds at a committed/idle boundary. Screen entry is
  captured immediately; entering a screen resets its undo stack before that
  checkpoint is queued. Saved
  fullscreen/window size, MSAA, AO, VSync, input bindings, reduced motion, and audio buses are applied
  at runtime. Debug master/music/sound sliders now update the profile instead of
  disappearing at process exit.
- Moved all runtime profile writes off the game thread through `AsyncSaveStore`.
  Actions now only mark the in-memory checkpoint dirty; snapshot copying happens
  at most once every two seconds, while screen transitions and committed settings
  are queued immediately. Writes remain strictly serialized, shutdown flushes
  the newest state, and the Engine window reports save requests, completed
  writes, coalescing, and worker state.
- Added a semantic input abstraction and SDL3 gamepad support. Gameplay reads
  actions instead of SDL controls; defaults combine keyboard, D-pad, and left
  stick, with face buttons for undo/restart and Start for menu/back. Controllers
  hot-plug safely, axis edges use configurable thresholds, profiles persist
  typed multi-device bindings, and raw SDL events are captured as binding
  candidates by the Options > Controls remapping page.
- Added the audio system (miniaudio): randomized non-repeating concrete
  footsteps on a tunable cadence while walking/pushing, a seamlessly looping
  stone-drag sound while a rock is pushed (loop-ready assets generated by
  `tools/make_drag_loops.py`; starts/stops with short fades and survives the
  miniaudio scheduled-stop restart gotcha via
  `ma_sound_reset_stop_time_and_fade`), and one streamed looping soundtrack
  per level with a 600 ms crossfade between levels. Sound files, sets, and
  volumes are manifest-driven; the Asset Manifest window edits authored set and
  track gains, while Debug UI > Audio keeps runtime master/music controls and
  the footstep interval.
- Replaced eager runtime catalog loading with a lazy, TaskSystem-backed asset
  pipeline. The active screen is guaranteed before use, current/next-level
  assets prepare in the background, and completed GPU resources are published
  serially on the render thread at a one-asset-per-frame budget.
- Added headless `RenderAssetRequirements` planning so level prefetch and frame
  fallback use the same tile/model semantics as `RenderFrameBuilder`, with a
  dedicated 26-check regression suite.

- Replaced the CMake-generated asset catalog with a runtime asset manifest
  (`assets/manifest.json` + headless `AssetManifest`): string-named models,
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
  by a dedicated headless test executable.
- Added layered levels with `@layer N` sections.
- Added level editor/document browser and draft play flow.
- Added Debug UI controls for rendering, lighting, runtime audio, grid, and conveyor rate. Authored tile scales and sound/music volumes live in the dedicated Asset Manifest window.
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
  tree. Clean installed Release builds also start and remain healthy without
  access to the source checkout.
- All fifteen CTest suites pass, including real-font/options UI, concurrent texture decoding,
  input/gamepad mapping, profile
  migration/recovery, gameplay
  move-count semantics, mandatory validation of the shipped manifest,
  manifest-editor save semantics, and the `content_pipeline` suite.
- `cmake --build out\visual-studio --config Release --target package` produces
  `out/visual-studio/Sokoban3D-0.1.0-Windows-x64.zip`. The current staged tree
  contains 55 reachable files (about 4 MB) instead of the roughly 236 MB / 5,964
  files in the source vendor packs.
- Migrated the asset manifest from the custom indentation-based format to
  versioned strict JSON parsed by pinned nlohmann/json 3.11.3. The parser now
  rejects unknown schema properties and wrong JSON types before domain
  resolution, and all manifest fixtures/content staging use `manifest.json`.
- Added a dedicated Asset Manifest window that edits every texture, model,
  animation, tile visual, sound set, and music track field. The ImGui adapter
  delegates to a headless, tested document API with validated safe saving;
  manifest-backed tile scale and per-sound-set controls were removed from the
  Engine window.
- Release startup loads `%APPDATA%/Sokoban3D/Sokoban3D/` saves through SDL's
  preference-path API. A genuinely fresh install writes nothing at boot: no
  slot file, settings file, or marker exists until the player starts a game
  (first checkpoint) or changes a setting, so first-run users pick a slot on
  a completely empty slate. Old-format profiles migrate on load; successful
  screen loading writes the initial or restored gameplay checkpoint without
  leaving a temporary replacement file. Corrupt-save recovery
  (`ResetCorrupt`) still writes a fresh default file, since in that case a
  save existed.
- Replaced the Windows WIC/COM texture path with vendored stb_image 2.30 and
  removed `ole32`/`windowscodecs` from the game link. Image files are read with
  `std::filesystem`, decoded from memory, and covered by concurrent-load and
  failure-diagnostic tests. Unix builds also link miniaudio's documented `dl`
  and math dependencies, and package names now reflect the configured CPU
  architecture instead of always claiming x64.
- Replaced the placeholder bitmap-letter quit popup with a modular
  Release-capable options menu. Karla glyphs render from a dedicated Vulkan
  atlas; reusable controls live in `UiControls`; `OptionsMenu` owns only
  page/navigation state; and `Application` applies/persists MSAA, internal
  render scale, AO, fullscreen/window size, master/music volume, and confirmed
  quit requests. Render scale provides 100%, 75%, 67%, 50%, and 25% presets
  plus a persistent 25-100% custom slider; scene attachments are recreated
  without changing the native window/UI resolution. Slider drags update the
  menu immediately but defer the expensive Vulkan recreation until release.
  Options pages now use `UiLayout` trees and named navigation rows instead of
  absolute per-control positions and numeric focus indexes.

Known useful verification commands:

```powershell
git status --short
rg -n "KayKit_Adventurers_2\.0_FREE|KayKit_BlockBits_1\.0_FREE" .
cmake --build out\visual-studio --config Debug
ctest --test-dir out\visual-studio -C Debug --output-on-failure
.\out\visual-studio\Debug\sokoban.exe
cmake --build out\visual-studio --config Release --target package
```

The `rg` command above should return no matches.

## Important Design Decisions

- Keep gameplay rules in the headless `Rules` module as pure functions of `(Level, GameState)`. `GameplaySession` owns command/state/history orchestration, `GameplayPresentation` owns visual interpolation/animation state, `Application` coordinates SDL input and component lifetime, and the renderer receives a render-frame description rather than owning game rules.
- When changing or adding mechanics, implement them in `Rules.cpp` and add cases to `tests/RulesTests.cpp`; the tests compile without SDL/Vulkan so they can run anywhere.
- Store `Player`, `Rock`, and movable `Ice` as dynamic entities extracted from level data rather than static cells.
- Use character-driven tile definitions as the single source of truth for level parsing/editor palette.
- Use layered `.scr` text files instead of a binary or JSON format for now.
- Keep runtime asset selection explicit through `assets/manifest.json`; code
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
- Add level metadata/names (a `.scr` header or sidecar) so the title's level
  select can show real names and par moves instead of "Level N".
- Add a scroll container to `UiLayout` before the level list outgrows the
  level-select panel (it currently sizes for roughly six levels).

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

Audio:

- Add sounds for more events (undo, restart, level completion, falling into
  water, conveyor hum) as manifest sound sets.
- Consider a mixer section in the manifest if global master/music gains should
  move out of `Config.hpp`.

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
  - Add a `{ "tile": "<Name>", "model": "...", "scale": ... }` entry to
    the `tiles` array in `assets/manifest.json` if needed.
  - Add editor preview/rendering support.
  - Verify level serialization still maps one-to-one.
- When adding a model:
  - Add entries to the ordered `models`, `textures`, and/or `animations` arrays
    in `assets/manifest.json` (path, geometry, material, orientation flags) and
    map tiles through the `tiles` array; scale defaults live there too.
  - No enum, CMake, or renderer change is needed; relaunch to apply.
  - Extend material modes only if the model cannot use `{ "mode": "none" }`,
    `{ "mode": "texture", "texture": "<Name>" }`, or
    `{ "mode": "primitive-texture-index", "index": <n> }`.
