# Sokoban 3D Project Handoff

This file is intended for another coding agent picking up work on the project. It summarizes the current shape of the game, the codebase, level format, asset pipeline, implemented mechanics, recent decisions, and rough areas that still need attention.

## Repository Note

Treat the local checkout as the canonical project location:

```text
C:\Users\arthu\Documents\Projects\Sokoban Game
```

This handoff reflects the post-hardening baseline at commit `b287ebc4`
(`remove unneeded file`). The latest verified `out/visual-studio`
configurations are Debug and Release: all 59 CTest suites pass in each. P4-7
clean-machine/GPU-driver acceptance was manually verified by the project owner
on 2026-08-25. Treat the release-validation scripts and matrix as mandatory
again for every new release artifact or supported-driver change.

## Project Idea

This is a small C++20 Sokoban-like 3D puzzle game. It uses SDL3 for
platform/window/input and a Vulkan 1.3 release renderer. The game is tile/grid
based, but rendered as an isometric-ish 3D board with GLTF assets, lighting,
shadows, animated character movement, and an in-game ImGui level editor in
Debug builds.

## Current Shipping Baseline

The original engine review's Phase 0 through Phase 4 plan is complete. The
important operational state for the next agent is:

- Profiles are format 26. Their settings model contains only audio, video, and
  input; the old incomplete accessibility block, including reduced motion, was
  deliberately removed. Format-25 profiles discard that obsolete block during
  migration.
- VSync defaults to FIFO. Players can choose a foreground frame cap and may
  allow tearing; the renderer selects only modes advertised by the surface.
  Unfocused windows pace to 20 FPS and minimized/background windows to 5 FPS;
  simulation timing clamps stalls and suspends safely.
- The release device contract is Vulkan 1.3 with dynamic rendering,
  synchronization2, cube-map arrays, extended dynamic state, and required
  descriptor capacity. Wireframe and wide lines are optional developer
  capabilities. Unsupported hardware and device/surface loss have actionable
  diagnostics.
- Shipping uses the editor-free `shipping` CMake preset with LTO and separate
  Runtime/Symbols ZIPs. `shipping-installer` creates the per-user Inno Setup
  installer; `shipping-signed` signs and verifies the executable and installer
  when certificate tooling is available.
- `packaging/ValidateShippingPackage.ps1` validates a Runtime ZIP or installed
  runtime tree. `packaging/CollectReleaseValidationEvidence.ps1` collects the
  automated result, GPU/driver/monitor/Vulkan/DirectX evidence, hashes,
  signatures, log tail, and manual acceptance answers into a ZIP. Full usage
  and the hardware matrix live in `packaging/ReleaseValidation.md`.
- Asset publication is non-blocking and budgeted; static geometry uses
  device-local suballocation plus mapped upload rings; skinning is GPU-based;
  repeated opaque draws are sorted/instanced; telemetry exposes residency and
  CPU/GPU frame-time summaries.

The core loop is classic Sokoban-inspired:

- The player moves on a layered grid.
- Mirror interactions can create multiple synchronized player instances.
- Movable objects can be pushed.
- Immobile enemies attack adjacent players and can themselves be pushed by
  movables.
- The goal/end unlocks when all pressure plates are occupied.
- Levels can contain multiple screens and multiple vertical layers.
- Additional mechanics include ice/sliding, water/falling, ladders,
  conveyors, and directional mirrors.

## Build And Run

Main dependencies:

- CMake 3.25+
- Vulkan SDK 1.3+ with `glslc`
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
cmake -S . -B out\visual-studio -G "Visual Studio 17 2022" -A x64
cmake --build out\visual-studio --config Debug
.\out\visual-studio\Debug\sokoban.exe
```

Every normal build runs the manifest-driven content pipeline and stages a
versioned `assets/` tree beside the executable. The game never reads models,
audio, shaders, or levels from the source checkout at runtime. To validate and
refresh content explicitly after editing `assets/manifest.json` or a level:

```powershell
cmake --build out\visual-studio --config Debug --target sokoban_content
```

Shipping Runtime/Symbols ZIP packaging:

```powershell
cmake --preset shipping
cmake --build --preset shipping
cpack --preset shipping
```

This emits separate Runtime and Symbols ZIPs in `out/shipping`; give players
only the Runtime ZIP and retain Symbols for crash-dump symbolication. The
runtime contains `sokoban.exe`, executable-relative `assets/`, and dependency
licenses. MSVC builds use the static C/C++ runtime, so the ZIP does not require
a separately installed Visual C++ Redistributable.

Every test executable is declared with the `sokoban_add_test()` helper at the
top of the `SOKOBAN_BUILD_TESTS` block (NAME/TARGET/SOURCES, optional LIBS,
ASSET_DIR for the `SOKOBAN_TEST_ASSET_DIR` define, ASSETS_ENV for the
`SOKOBAN_ASSETS` run env). It applies the shared `src` include path and the
per-compiler warning set (`/W4 /permissive-` on MSVC, `-Wall -Wextra
-Wpedantic` otherwise) and registers the CTest case, so adding a suite is one
call, not a copy-pasted block. Production sources compile once into
`sokoban_core`, `sokoban_ui`, or `sokoban_render_vulkan`; tests link the owning
library and compile only their test `.cpp`. The helper rejects `src/` entries
at configure time to prevent target-definition drift from returning.

Command-line flags (all builds unless noted):

```powershell
.\sokoban.exe --smoke-frames 240 --require-validation --save-directory C:\temp\smoke
```

- `--smoke-frames <n>` starts a new game, renders `n` frames through the
  ordinary loop, and exits. It starts a game deliberately: the title screen
  draws no world (`buildRenderFrame` returns an empty `RenderFrameData` while
  none is loaded), so a title-only run records no tiles, models, shadows or
  SSAO and would prove almost nothing.
- `--require-validation` refuses to run unless `VK_LAYER_KHRONOS_validation`
  actually loaded, and exits 4 if it did not. Without it a validation gate is
  theatre - a missing layer, a stale `VK_LAYER_PATH`, a Release build and a
  genuinely clean frame all report zero errors.
- `--save-directory <path>` roots saves, settings and the pipeline cache
  somewhere other than the preference path, so a smoke run cannot write into a
  real player profile.
- `--bake-tile-thumbnails` is Debug-only and rejected with exit 2 elsewhere.
- Exit codes: 0 clean, 1 fatal error, 2 bad arguments, 3 validation reported
  at least one error, 4 validation was required but inactive.

Parsing lives in the header-only `src/engine/CommandLineOptions.hpp` and is
covered by `tests/CommandLineOptionsTests.cpp` (ctest name `command_line`).
It is deliberately strict - a missing value, a non-numeric or zero count,
trailing garbage, and unknown flags are all rejected - because a CI gate is
only as trustworthy as the flag that starts it.

Continuous integration (`.github/workflows/required-tests.yml`):

- The Linux `quality` matrix runs Debug/Release tests, ASan+UBSan, clang-tidy
  and the profile fuzzer, as before.
- The Debug entry additionally builds `SOKOBAN_BUILD_VULKAN_SMOKE_TESTS=ON`
  and runs a **Render frames under validation** step: lavapipe pinned through
  `VK_DRIVER_FILES`, validation layers located explicitly, `xvfb-run`, and
  `--smoke-frames`/`--require-validation`. Before this, `mesa-vulkan-drivers`
  and `vulkan-validationlayers` were installed by CI and nothing used them -
  the Vulkan smoke test defaults to OFF and had never been switched on.
- A `windows` job builds Debug and Release with MSVC and runs ctest. It is the
  only job that uses the compiler releases actually ship with. It configures
  `SOKOBAN_BUILD_VULKAN_SMOKE_TESTS=OFF` on purpose: hosted Windows runners
  have no Vulkan ICD, so that test could only fail for reasons unrelated to
  the change under test. Device coverage is the Linux lavapipe job's job.
- `VULKAN_SDK_WINDOWS_SHA256` starts empty. The first Windows run fails and
  prints the hash it downloaded; verify that download, paste the hash in, and
  the installer is pinned the same way the Linux tarball already is.

Headless rules tests (no SDL/Vulkan needed at runtime; built by default via `SOKOBAN_BUILD_TESTS`):

```powershell
cmake --build out\visual-studio --config Debug --target sokoban_rules_tests
.\out\visual-studio\Debug\sokoban_rules_tests.exe
```

Level parser/serializer tests cover legacy and layered files, malformed input,
ragged-layer normalization, CRLF loading, entity extraction, and ladder
validation:

```powershell
cmake --build out\visual-studio --config Debug --target sokoban_level_tests
.\out\visual-studio\Debug\sokoban_level_tests.exe
```

Headless gameplay-session tests cover command buffering, action timing,
push metadata, restart, undo, automatic-motion pausing, solution move counts,
checkpoint restore, invalid/impossible history rejection, and per-screen undo
reset:

```powershell
cmake --build out\visual-studio --config Debug --target sokoban_gameplay_session_tests
.\out\visual-studio\Debug\sokoban_gameplay_session_tests.exe
```

Player-profile tests cover format-26 round trips and the complete format-1
through format-26 migration chain, exact active-screen/undo checkpoints,
stable entity IDs, generic presentation transactions, completion bests,
normalization, atomic writes,
asynchronous save coalescing/shutdown flushing, prior-save backups,
corrupt-save archival, backup recovery, and double-corruption default recovery:

```powershell
cmake --build out\visual-studio --config Debug --target sokoban_profile_tests
.\out\visual-studio\Debug\sokoban_profile_tests.exe
```

Input tests cover keyboard and gamepad action mapping, remapping, button edges,
stick direction thresholds, invalid-binding diagnostics, and raw SDL event
capture for the remapping UI:

```powershell
cmake --build out\visual-studio --config Debug --target sokoban_input_tests
.\out\visual-studio\Debug\sokoban_input_tests.exe
```

Image-data tests cover RGBA decoding, concurrent worker-style loads with
byte-identical results, and contextual diagnostics for missing or invalid
files:

```powershell
cmake --build out\visual-studio --config Debug --target sokoban_image_data_tests
.\out\visual-studio\Debug\sokoban_image_data_tests.exe
```

UI tests cover TTF atlas generation, glyph draw data, reusable button/slider/
checkbox interactions, options-page navigation, graphics changes, audio
changes, and quit confirmation:

```powershell
cmake --build out\visual-studio --config Debug --target sokoban_ui_tests
.\out\visual-studio\Debug\sokoban_ui_tests.exe
```

Title-shell tests cover title navigation, new-game confirmation, level/screen
select locking and screen choice, the level-complete overlay, and the pause
menu's title-exit row:

```powershell
cmake --build out\visual-studio --config Debug --target sokoban_title_tests
.\out\visual-studio\Debug\sokoban_title_tests.exe
```

Headless animation-controller tests cover animation selection, deduplication,
crossfades, reverse playback, selected-model preview overrides, and reset:

```powershell
cmake --build out\visual-studio --config Debug --target sokoban_animation_controller_tests
.\out\visual-studio\Debug\sokoban_animation_controller_tests.exe
```

Headless animation-event sequencer tests cover source-time marker evaluation,
global/per-use speed composition, one-shot delivery, frame overshoot, replay,
and reset:

```powershell
cmake --build out\visual-studio --config Debug --target sokoban_animation_event_sequencer_tests
.\out\visual-studio\Debug\sokoban_animation_event_sequencer_tests.exe
```

Headless presentation tests cover mutable settings normalization, lighting/grid
conversion, stable-target entity interpolation, clip/facing behavior, fallen
offsets, generic event-dependency timing and cycle rejection, forward/reverse
transaction sampling, attack-to-death ordering, drowning, isolated
animation-preview stage construction, and gameplay render-frame construction,
including valid mirror beam/ghost previews:

```powershell
cmake --build out\visual-studio --config Debug --target sokoban_presentation_tests
.\out\visual-studio\Debug\sokoban_presentation_tests.exe
```

Headless particle tests cover deterministic burst emission, texture selection,
movement, expansion, alpha fading, expiry, empty definitions, and reset:

```powershell
cmake --build out\visual-studio --config Debug --target sokoban_particle_tests
.\out\visual-studio\Debug\sokoban_particle_tests.exe
```

Headless content-pipeline tests cover manifest/file validation, path
containment, external glTF sidecars, level continuity, staging replacement,
and notice inclusion:

```powershell
cmake --build out\visual-studio --config Debug --target sokoban_content_pipeline_tests
.\out\visual-studio\Debug\sokoban_content_pipeline_tests.exe
```

Debug builds define `SOKOBAN_ENABLE_DEBUG_UI=1`, which enables one ImGui Developer Tools window with Engine, Asset Manifest, Level Editor, and Animation tabs. Animation owns global clip speeds, per-semantic-use clip/speed controls, timeline events, start gates, and a source glTF/GLB preview browser. Timeline Events uses a two-page list/editor workflow: the selected use first shows named events with `Edit` actions plus `Add New Event`; either action opens one focused scrubber with source seconds and normalized percentage, preview visibility/playback/frame-step controls, and the event-name commit action. Timeline Events and the free-form Animation Preview own independent clip, cursor, playback, and visibility sessions; interacting with one makes it the isolated preview-scene owner without mutating the other. Both previews replace the normal game/editor frame with the isolated 3x3 authoring stage. Release builds still compile the headless editor APIs but do not expose the ImGui editor/debug UI or compile source-asset paths into the executable.

## Important Source Map

- `CMakeLists.txt`: defines three reusable static libraries with
  `Sokoban::core`, `Sokoban::ui`, and `Sokoban::render_vulkan` aliases.
  `sokoban_core` owns gameplay, persistence, platform services, content, and
  Vulkan-free render data/preparation; `sokoban_ui` owns player-facing UI plus
  the UI-dependent `InputRouter`/`ShellFlow`; `sokoban_render_vulkan` owns
  Vulkan resources, recording, and the renderer facade. The executable keeps
  only `Application`, `main`, and Debug ImGui adapters as composition code.
  The content tool and every test consume these libraries rather than
  recompiling production translation units.
- `src/main.cpp`: process entry point.
- `src/engine/Application.*`: composition root and external-effect executor for component lifetime, the SDL/UI frame, shell commands, file-backed level loading, and calls into Window/Vulkan/audio/save services. Campaign policy lives in `CampaignSession`, gameplay advancement in `GameplayLoop`, input admission/focus routing in `InputRouter`, profile-to-runtime settings policy in `SettingsCoordinator`, and the timing curve/midpoint contract for world replacement in `LevelTransition`. Entering a selector or completing a puzzle defers the campaign mutation and load until the transition is fully closed; gameplay and selector prompts remain paused until the destination has opened. When `levels/overworld/layout.json` exists it loads `OverworldMap`, configures its real topology/checkpoint identity, installs the living-player screen-cohesion admission rule, commits active-screen metadata after gameplay commits, and feeds `OverworldView` into the frame; otherwise it preserves the legacy `overworld.scr` path. It builds one logical `RenderFrameData` per frame, asks `VulkanRenderer` to prepare it once, and retains the lightweight prepared-frame handle so editor picking can reuse the previous submitted scene. `buildLevelCatalog()` refreshes the campaign's cached screen counts when a screen loads so debug-editor changes remain visible without title queries hitting the filesystem. It no longer owns campaign counters, movement-loop policy, input-routing policy, settings projection/change detection, mutable rendering settings, visual interpolation, debug animation-browser state, or render-frame construction.
- `src/engine/CampaignSession.*`: headless campaign/run state. Owns the cached puzzle catalog/selector targets, overworld topology fingerprint and stable screen catalog, active overworld-screen identity, validated current context, puzzle timing/completion/best policy, and deferred-checkpoint cadence while mutating only the supplied `PlayerProfile`. It rejects stale topology checkpoints, falls back to the authored overworld start, persists typed overworld checkpoints, and exposes shared-player-screen plus committed-transition helpers for composed-map integration. Application performs UI, audio, persistence, and loading effects. Covered by `tests/CampaignSessionTests.cpp`.
- `src/engine/GameplayLoop.*`: headless per-frame bridge between semantic button states, `GameplaySession`, and `GameplayPresentation`. Owns opposing-direction resolution, command queueing, action time consumption, presentation begin/finish calls, and solved-screen/draft outcomes. Covered by `tests/GameplayLoopTests.cpp`.
- `src/engine/InputRouter.*`: testable routing layer between raw `InputState` and context-specific gameplay, title, completion-overlay, options, pointer, and editor frames. Owns SDL event admission around ImGui capture, binding-candidate suppression, modal focus, and draft-exit Back precedence; `Application` only pumps events and executes the resulting intents. Covered by `tests/InputRouterTests.cpp` (`sokoban_input_router_tests`).
- `src/engine/SettingsTypes.*` + `src/engine/SettingsCoordinator.*`: the UI-neutral, authoritative `UserSettings` value (`audio`, `video`, and `input`) plus headless runtime-effect policy. `PlayerProfile` owns one `UserSettings`; menus, persistence, and the coordinator no longer maintain parallel field sets. The coordinator normalizes a replacement value, updates `PresentationSettings`, detects which external systems actually changed, and emits a data-only `SettingsEffects` plan for window, renderer, audio, input, presentation policy, and persistence. `Application` executes that plan without deciding settings policy. Covered by `tests/SettingsCoordinatorTests.cpp` (`sokoban_settings_coordinator_tests`).
- Focused configuration headers replace the former `Config.hpp` umbrella:
  `AudioConfig.hpp`, `GameplayConfig.hpp`, `UserSettingsConfig.hpp`,
  `ui/UiConfig.hpp`, and the render-owned `AnimationConfig.hpp`,
  `CameraConfig.hpp`, `LightingConfig.hpp`, `RendererConfig.hpp`,
  `SceneConfig.hpp`, and `WaterConfig.hpp`. Consumers include only the owner
  they use; foundational `RenderTypes`, `PresentationSettings`,
  `PlayerProfile`, and `VulkanRenderer` headers no longer transitively expose
  unrelated art/runtime tuning. The focused files also own normalization
  bounds, video defaults, audio cadence limits, font-atlas sizing, and camera
  fit policy that were previously hard-coded at their call sites.
- `src/engine/PresentationSettings.*`: mutable runtime presentation settings initialized from the immutable defaults in the focused render config headers. Owns lighting, SSAO/shadow tuning, grid appearance, surface geometry, tile scales, normalization, sun-direction conversion, and renderer-facing lighting/grid values.
- `src/engine/EntityId.hpp`: stable runtime identity shared by authoritative
  state, action history, presentation, persistence, and rendering. Every
  production player, movable, and enemy receives a nonzero `EntityId`;
  `EntityTarget` pairs that ID with `EntityKind`. Index-derived fallback IDs
  exist only for transient hand-authored tests/editor previews.
- `src/engine/ActionPresentation.hpp`: immutable, Vulkan-free presentation
  transaction stored on each committed action. It contains independently timed
  `ActionMotionTrack`s and per-target `ActionAnimationTrack`s made of resolved
  clip segments. Playback never reconstructs ordering from before/after state.
- `src/engine/PresentationTransactionBuilder.*`: generic reducer from motion
  and animation intents to a resolved `ActionPresentationTimeline`. It composes
  global/per-use animation speed, resolves named catalog-event dependencies,
  supports arbitrary entity targets, validates references, and rejects cycles
  before marker evaluation. Mechanics may emit domain-specific intents here;
  the resulting transaction contains no combat policy.
- `src/engine/GameplayPresentation.*`: headless sampler for authoritative
  action transactions. It owns rendered positions, clip/fallback clocks,
  looping/crossfade state, player facing, smooth enemy orientation, camera
  pitch, and the world/conveyor clock without mutating `GameState`.
  `buildActionPresentation` is the mechanic-to-intent boundary; `seekAction`
  is fully generic and samples the same immutable timeline from start to end
  for normal play or end to start for undo. There are no death-, attack-, or
  undo-specific playback flags. Drowning and enemy attacks are merely current
  producers of ordinary motion/animation segments.
- `src/engine/AnimationCatalog.*`: strict, Vulkan-free mapping between stable
  code-owned `AnimationUse` IDs and manifest clips. Format 2 records the source
  duration and global speed of every manifest animation. Every semantic use
  has one independently editable clip binding and speed multiplier, may emit
  uniquely named events at normalized source-clip times, and may wait for a
  named event on another use. Parsing rejects unknown/duplicate/missing uses,
  unknown clips, stale/missing durations, invalid events, missing gate targets,
  and dependency cycles. Canonical JSON saves use `AtomicFile`; content staging
  loads each source glTF/GLB clip to verify its catalog duration before
  packaging `assets/animation_catalog.json`. Covered by
  `sokoban_animation_catalog_tests`, `sokoban_animation_event_sequencer_tests`,
  and render-timing assertions in `sokoban_presentation_tests`.
- `src/engine/AnimationEventSequencer.*`: standalone Vulkan-free evaluator for
  authored events on concrete actor instances. It remains useful for live
  one-shot event consumers and is independently tested, but committed action
  ordering no longer depends on mutable sequencer state: the transaction
  builder resolves those event offsets before playback/undo.
- `src/engine/AnimationCatalogEditor.*`: headless animation authoring document
  and filesystem owner. It loads the authoritative source catalog, tracks
  dirty/status state, validates and atomically saves it, then mirrors the same
  canonical JSON into the active staged runtime assets so a Visual Studio
  relaunch retains tuning without a content rebuild. Persistence and both-file
  reload behavior are covered by `sokoban_animation_catalog_tests`.
- `src/engine/AnimationPreviewScene.*`: Vulkan-free builder for the isolated
  3x3 animation stage. It emits the selected skinned model with a stable
  instance identity; `AnimationPreviewDebugUi` owns the arbitrary source clip
  and timeline, while the renderer only samples the requested model/time.
- `src/engine/ParticleSystem.*`: Vulkan-free reusable particle simulation. Effect definitions provide texture choices, tint, burst count, lifetime/size ranges, spawn radius, and velocity/rotation ranges; live particles own randomized state and emit renderer-facing billboards with smooth lifetime fading. `MirrorParticleEffect.*` resolves the ten code-configured smoke texture names through the manifest and builds the cyan mirror-swap effect. Covered by `sokoban_particle_tests`.
- `src/engine/RenderFrameBuilder.*`: SDL/Vulkan-free construction of gameplay and editor `RenderFrameData`. Owns tile/model mapping, static geometry, procedural open-water planes/shore edges, ladder rungs, editor previews/pick-only cells, dynamic entities, tile scaling, conveyor texture offsets, and optional composed-overworld camera extent/offset plus cell-eligibility and ground-splat-region inputs. The composed runtime uses the eligibility callback to omit distant tiles, local water/ladder geometry, decorations, selectors, and actors outside the settled 3x3 or transitioning source/destination neighborhood union; it resolves stable-ID splat textures for each visible screen into the frame region table. Editor frames can also include neighboring overworld definitions at slot-relative origins as non-pickable context with per-screen splat regions, without changing active-screen camera framing. Water is emitted as `WaterSurface` data rather than a manifest model; filled cells omit the surface. Level-wide water adds a one-cell shoreline ring and four large non-pickable continuation surfaces outside ordinary single-board frames without changing the frame's authored dimensions.
- `src/engine/render/IsoScenePreparer.*`: Vulkan-free once-per-frame scene preparation and picking. Computes top-down, isometric-camera, and shadow layouts; creates one projected/cull-tested face pool; and emits depth-sorted opaque/translucent face indices plus model, shadow, picking, and camera-facing particle billboard lists. Explicit camera extents accept a floating `cameraOffset`, translating both the camera target and fitted footprint without changing zoom. Particles are translucent-only, shadowless, and non-pickable. It fills reusable renderer-owned frame scratch rather than using function-static storage. Covered by `tests/IsoScenePreparerTests.cpp` (`sokoban_iso_scene_preparer_tests`).
- `src/engine/ApplicationDebugUi.*`: Debug-only ImGui adapter for engine statistics and tuning. Edits `PresentationSettings` and calls the public `GameplaySession`/`VulkanRenderer` controls instead of storing application logic.
- `src/engine/DebugUi.*`: Debug-only registry and presentation owner for the single Developer Tools window. Feature adapters register content callbacks as reorderable, scrolling tabs instead of creating independent windows.
- `src/engine/AnimationPreviewDebugUi.*`: Debug-only owner of animation/model
  selection, source-asset scanning, play/loop/speed/scrub/frame-step state,
  preview-scene activation, timeline-marker drawing, and renderer preview
  delegation. Its catalog-event and free-form browser sessions are independent,
  with explicit active-session selection. It can select an exact catalog
  clip/use for event authoring and synchronizes the catalog's stored source
  duration with the loaded clip.
- `src/engine/AnimationCatalogDebugUi.*`: thin Debug-only ImGui adapter for
  live global/per-use tuning, clip rebinding, normalized timeline event
  list/editor navigation, placement, and start-gate selection. Save/reload,
  transactional event rename validation, and dirty state are delegated to
  `AnimationCatalogEditor`; it composes
  `AnimationPreviewDebugUi` into the same Animation tab.
- `src/engine/AudioSystem.*`: miniaudio-backed sound playback behind a pimpl (`EngineHandle`), so no miniaudio types leak into headers. Preloads manifest sound sets from the staged runtime content tree with `MA_SOUND_FLAG_DECODE` into stable `std::vector<ma_sound>` storage. `playOneShot(name)` handles reusable randomized effects; `update(dt, playerWalking, pushingStone)` retains specialized footstep cadence and seamless stone-drag loops with short fades. Music streams one looping track per level with a 600 ms crossfade. Manifest gains remain authored in the Asset Manifest window; profile-backed master, music, and sound-effect bus gains are previewed live and persisted when Debug UI sliders are committed. Audio degrades gracefully to silence if the device or files are missing.
- `src/engine/GameplaySession.*`: headless gameplay orchestration between input and `Rules`. Owns the authoritative `GameState`, buffered move/undo/restart commands, active action timing, action history, a branch-safe undo stack, automatic world steps, the post-undo automatic-motion pause, and solution-move snapshots that restore correctly across undo/restart. An optional runtime-only projected-state admission policy can reject a planned player batch, ambient action, mirror, undo, or restart before scheduling; the composed overworld uses it to prevent living players from splitting across authored screens. Each committed `Action` owns its complete Vulkan-free `ActionPresentationTimeline`; inversion preserves that transaction and runs it backward for its resolved full duration. Its committed-state snapshot/restore API persists exact player/movable/enemy state, stable IDs, presentation ordering, and the usable undo chain; restore rejects disconnected or impossible rules transitions without mutating the live session. `reset` always clears undo state at a loaded-world boundary. Tested by `tests/GameplaySessionTests.cpp` (`sokoban_gameplay_session_tests`).
- `src/engine/InputBindings.*`: platform-neutral semantic action and binding model. Each action owns an ordered list of keyboard, gamepad-button, and signed gamepad-axis bindings, allowing keyboard+D-pad+stick defaults. `assignBinding` removes an identical binding from actions active in the same context, then replaces only the action's bindings of the same kind, so editor-only modifiers can reuse gameplay defaults and a d-pad rebind keeps a stick binding; `bindingDisplayName`/`actionBindingsDisplay` provide UI labels.
- Options > Controls separates remapping into Keyboard and Controller tabs.
  Each tab displays only its device class and rejects capture candidates from
  the other class; reset-to-defaults remains shared. Debug editor bindings are
  keyboard-only and are linked from the Keyboard tab.
- `src/engine/ui/InputPrompts.*` parses the staged Kenney Input Prompts 1.5
  spritesheet metadata and resolves stable SDL bindings to keyboard or
  controller-specific atlas regions. `InputState` exposes SDL3's real gamepad
  type and per-device face-button labels; Xbox, PlayStation, Switch, GameCube,
  and Steam Deck themes are selected without changing persisted generic
  bindings, with a generic theme and text fallback for unknown controls.
  Controls binding rows and the overworld screen-selector prompt render these
  manifest-backed glyphs through `UiDrawKind::TextureImage`.
- `src/engine/Input.*`: SDL3 device owner and action mapper. Tracks raw keyboard/mouse state for editor tooling, hot-plugs gamepads, selects the most recently used controller, normalizes stick axes with threshold/pressed-edge semantics, clears stuck input on focus loss, reports active-device diagnostics, and converts raw SDL events into typed remapping candidates. `InputRouter` controls event admission and distributes its state to active consumers. Covered by `tests/InputTests.cpp` (`sokoban_input_tests`).
- `src/engine/PlayerProfile.*` + `src/engine/PlayerProfileCodec.cpp`:
  current format-26 player progress model plus one owned `UserSettings` value.
  Forward JSON patches (`migrate1to2` through `migrate25to26`) feed one strict
  current-format parse. Format 9 made progress/settings independently optional
  for split slot and shared-settings files; format 10 added Mirror bindings,
  format 11 restored the intended `Z` Undo / `F` Mirror defaults, and format 12
  added the remappable Show Top-Down View action. Formats 13-15 introduced AO
  strength, multi-player checkpoints, enemies, and explicit death causes;
  format 16 added presentation data to history, and format 17 added stable
  entity IDs plus generic motion/animation tracks while preserving compatible
  format-16 checkpoints and undo history. Format 18 introduced per-screen and
  overworld progress, format 19 added the three editor tile bindings, and
  format 20 replaced the untyped single-overworld snapshot with a topology-
  fingerprinted checkpoint carrying a stable active overworld-screen ID,
  format 21 added the screen-preview binding, and format 22 retired the
  dedicated Mirror action in favor of Confirm / Interact and removed Return
  from that action's keyboard default. Formats 23-24 corrected top-down and
  overworld-map input defaults, format 25 added safe tearing/frame-cap
  defaults, and format 26 removed the obsolete accessibility block.
  Stores exact active-screen
  gameplay/undo state including action presentation transactions, progress,
  bests, reached screens, typed input bindings,
  audio/video/input settings, and normalized display/render settings.
  Covered with `SaveStore` by `tests/PlayerProfileTests.cpp`.
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
- `src/engine/SaveSlotManager.*`: headless owner of the save-slot lifecycle - the per-slot progress stores, the shared `settings.json` store, the `active-slot.txt` marker, slot summaries (progress-based emptiness, completed flags), switching (flush, channel repoint, marker write, settings carry-over), and deletion (drain-then-remove so in-flight writes cannot resurrect a deleted save). Non-active slot summaries are decoded once and cached, invalidated only by switch/delete (the active slot is summarized live), so a title open no longer re-parses the other slots' JSON. `Application` owns the live `PlayerProfile` and gameplay consequences; every disk decision lives here. Tested by `tests/SaveSlotManagerTests.cpp` (`sokoban_save_slot_tests`), including the fresh-install-writes-nothing guarantee, pre-split settings migration, the reset-profile-reads-empty regression, and summary-cache invalidation.
- `src/engine/SaveStore.*`: profile persistence rooted at SDL's platform-appropriate `SDL_GetPrefPath`. A `fileStem` constructor parameter names the slot's files (slot 1 keeps the historical `profile` stem so pre-slot saves remain valid; slots 2/3 use `profile-slot2/3`), and corrupt-archive detection derives its prefixes from those names so slots never interfere. Writes validated JSON through same-directory temporary replacement, keeps the previous valid primary as `<stem>.backup.json`, migrates old versions, archives corrupt primary/backup files for diagnosis, recovers from backup, and restores defaults when both copies are unusable.
- `src/engine/AsyncSaveStore.*`: single serialized persistence worker serving one or more independent channels (each its own `SaveStore` + pending profile + per-channel deadline/diagnostics). The channel-less overloads target channel 0, preserving the original single-store API; `addChannel` and `replaceChannel` (drain-then-repoint, used on slot switch) support several destinations on one thread, so `SaveSlotManager` runs its progress and settings stores without a thread each. Deferred requests coalesce per channel over a configurable window, while JSON encoding, backup rotation, and atomic filesystem replacement happen off the game thread. Screen transitions and committed settings request immediate saves; clean shutdown flushes every channel. Multi-channel behavior is covered in `tests/PlayerProfileTests.cpp`.
- `src/engine/Rules.*`: headless gameplay rules engine. `GameState` owns
  stable-ID vectors of players, movables, and enemies. `rules::step` advances
  the whole world one discrete step through the file-local
  `MicroStepResolver`, which handles every surviving player with shared input,
  movable momentum/conveyors, collision conflicts, pushing, falls, enemy
  pushing, and post-move attacks. Mirrors can duplicate a player when equally
  near valid mirrors reflect it simultaneously; copies receive new IDs and
  participate in all later input, collision, death, mirror, save, and
  completion rules. Enemies are immobile unless pushed by a movable, kill
  orthogonally adjacent players, may fall, and block occupancy. Completion
  requires every player copy to stand on an end while all plates are occupied
  and no player is dead. No SDL/Vulkan/rendering dependencies; tested by
  `tests/RulesTests.cpp`.
- `src/engine/Level.*`: level file parsing, serialization, layered grid storage, optional `@water N` metadata, manifest-named mesh decorations with full affine authoring transforms, walkability/support rules, and player/movable/enemy extraction. Air on the configured water layer resolves to Water at runtime while the authored tile remains queryable. Decorations remain outside the rules grid and camera bounds. Tested by `tests/LevelTests.cpp` (`sokoban_level_tests`).
- `src/engine/OverworldMap.*` + `src/engine/OverworldView.*`: multi-screen overworld runtime foundation. `OverworldMap` strictly loads/canonically writes format-3 `overworld/layout.json`, gives component screens stable IDs and spatial slots, composes separately authored definitions into one normal rules space, translates decorations and compound selector identities, requires exactly one authored Player tile across all components, derives the initial screen from that tile, exposes global/local ownership and 3x3-neighborhood queries, fingerprints layout/content, and enforces assigned-target validity plus the rule that one puzzle level's selectors live in one overworld screen. Unassigned selectors and incomplete selector coverage are valid inert authoring states, allowing flags orphaned by level/screen changes to survive the build and be repaired after startup; campaign completion remains false until every catalog screen is both assigned and solved. Screen placement has no reachability requirement; ordinary gameplay rules alone decide whether a boundary move succeeds. Composition also accepts a complete in-memory layout/component override for editor drafts, including definitions with no file yet. `OverworldView` computes living-player action admission, active-screen-only camera framing, source/destination neighborhood union, and camera translation synchronized to player presentation. Production content now uses the composed path with stable screen ID 1; the legacy root-file loader remains only as a compatibility fallback. Covered by `tests/OverworldMapTests.cpp` (`sokoban_overworld_map_tests`).
- `src/engine/Log.*` + `src/engine/LogQueue.*`: categorized asynchronous logging with Debug/Info/Warning/Error levels. RAII `Message` objects format only their payload on the producer, then enqueue timestamp/category/message records into a bounded 4,096-entry queue; one writer owns stderr, the append-only file, timestamp formatting, and flushing. Normal traffic flushes every second, errors flush immediately, explicit `flush`/`shutdown` drain and join at process exit, and errors displace the oldest lower-severity entry when a full queue permits. Overflow is aggregated into synthetic `[WARN] [LOG] Dropped N...` records and exposed through queue/write/flush/drop/per-category diagnostics in Debug UI. Output is `[HH:MM:SS.mmm] [LEVEL] [CATEGORY] message`. `Application` adds `log.txt` beside profiles and Debug builds admit Debug messages. Covered by `tests/LogTests.cpp` (`sokoban_log_tests`) including bounded policy, concurrent producers, periodic/error flushing, category output, filtering, and shutdown/reset behavior.
- `src/engine/TaskSystem.*`: standard-library-only worker pool for task-based parallelism. `taskSystem().enqueue(fn)` returns a future (exceptions propagate on get); `parallelFor(count, minChunk, fn(begin, end))` runs chunked loops with the calling thread participating. Tasks must not block on other tasks (no dependency graph yet). Used by GLTF vertex skinning (`skinWithPoses`) and lazy CPU-side model/texture/animation preparation in `VulkanModelResources`; Vulkan publication stays on the render thread. Tested by `tests/TaskSystemTests.cpp` (`sokoban_task_tests`).
- `src/engine/TileTypes.*`: tile enum, character mapping, colors, helper predicates such as `tileTypeAllowsEntity`.
- `src/engine/LevelEditor.*`: headless single-document editor model and command API. Owns document state/history, water-layer selection and layer-index maintenance, tile and mesh-decoration commands, decoration selection/transform validation, draft construction, level load/save, source/runtime mirroring, browser enumeration, puzzle screen/level renumbering, soft-delete/restore, guarded permanent deletion, and the neighboring-overworld-context visibility setting. Layout-referenced overworld components are recognized by path rather than filename: their dimensions are locked, End tiles are blocked, Player is authored normally, puzzle-level selector ownership is enforced, saves validate exactly one Player across the complete composed map transactionally, and draft play merges the unsaved component with the complete in-memory topology draft. Ordinary edge-tile edits open or close implicit seams. Map and layer insertion shift authored decoration coordinates in the same undoable transaction. It has no SDL, Vulkan, or ImGui dependency and is tested by `tests/LevelEditorTests.cpp` (`sokoban_level_editor_tests`).
- `src/engine/OverworldMapEditor.*`: headless project-level topology editor, separate from `LevelEditor` and shared by the ImGui panel. Owns the parsed layout/component draft, stable screen selection/identity, immediate neighboring-screen creation, screen moves, soft delete/restore, undo/redo, complete draft overrides, and rollback-capable source/runtime save through `LevelProjectStore`. New screens are an unobstructed Ground floor with Air above. The editor does not analyze how screen edges are traversed and allows isolated or blocked screens. Covered by `tests/OverworldMapEditorTests.cpp` (`sokoban_overworld_map_editor_tests`).
- `src/engine/DecorationMeshCatalog.*`: headless source-tree `.gltf`/`.glb` discovery for Debug authoring. It matches relative source paths to manifest model entries and sorts registered assets first. Release application paths neither scan nor embed the source asset root.
- `src/engine/DecorationAssetRegistry.*`: headless first-use registration for arbitrary catalog meshes. It validates asset-relative paths and external glTF URIs, mirrors the mesh plus buffer/image dependencies into staged runtime assets, generates a unique readable model name, resolves a single external glTF base-color atlas, reuses textures by normalized path or registers a new texture, and atomically saves the source/staged manifests before appending live texture/model ids. Generated decorations set `preserveSourceScale`, retaining authored units and origin instead of normalizing into a tile; old generated entries are upgraded when re-registered. The renderer grows texture slots before model slots. Multi-atlas and embedded-image materials still require explicit manifest authoring. Covered with the catalog by `tests/DecorationMeshCatalogTests.cpp` (`sokoban_decoration_mesh_catalog_tests`).
- `src/engine/LevelEditorDebugUi.*`: Debug-only ImGui adapter coordinating `LevelEditor` and the shared `OverworldMapEditor`. Its Overworld tab shows a scrollable spatial slot canvas with screen cards, directional `+N`/`+E`/`+S`/`+W` controls that immediately create all-Ground neighbors, selector counts, open/move/delete/restore controls, topology undo/redo, movement guidance, and validated save. While editing a component, **Show Neighboring Screens** displays cardinal and diagonal neighbors as read-only scene context. The tile palette exposes Player for overworld components and complete-map validation enforces uniqueness. The adapter owns widget buffers and presentation only; every state transition and filesystem action is delegated to a headless model.
- `src/engine/render/RenderTypes.hpp`: renderer-facing frame contract and model/animation enums, independent of the Vulkan facade. `WaterSurface` carries world bounds, lowered elevation, tint/opacity, and editor-preview state; model instances may carry an authored translation/pivot, XYZ rotation, and non-uniform scale; the frame carries one shared water animation clock so adjacent cells remain phase-continuous. Composed-overworld frames also carry a bounded ground-splat region table (global bounds plus resolved textures) and stable screen-ID texture naming helpers.
- `src/engine/render/RenderResolution.*`: Vulkan-free internal-resolution policy. Validates the supported 100/75/67/50/25 presets, clamps custom percentages to 25-100, and computes rounded, non-zero scene extents; 67% deliberately means exact two-thirds so 3840x2160 becomes 2560x1440. Covered by `sokoban_vulkan_device_selection_tests` alongside the headless device-selection policy.
- `src/engine/AssetManifest.*`: runtime asset manifest - the single source of truth for models, textures, animations, asset-backed tile visuals (model + render scale per tile type), sounds, and music. Procedural Water and Ladder rendering are deliberate code-owned exceptions. Parses the versioned `assets/manifest.json` with nlohmann/json, rejects malformed JSON, wrong types, missing/unknown properties, unsupported format versions, and invalid material combinations, then performs domain validation (unique names/tiles/roles, resolvable textures/models, exactly one skinned `role: "player"` model, all five player animation roles, paired optional enemy model/attack roles, texture count <= `maxModelTextures`). `RenderModel`/`RenderAnimation` are runtime ids (index+1 into the ordered JSON arrays; 0 = cube/none) defined in `RenderTypes.hpp`. Adding an asset, tile visual, or sound is a JSON edit plus rebuilding `sokoban_content` and relaunching - no CMake, enum, or renderer change. Headless; tested by `tests/AssetManifestTests.cpp` (`sokoban_asset_manifest_tests`).
- `src/engine/AssetManifestEditor.*`: headless editable manifest document. Loads the strict runtime model, exposes typed add/update/remove/reorder commands for every manifest section, tracks dirty/status state, serializes canonical JSON, validates through `AssetManifest`, and uses temporary/backup replacement so an invalid or failed save leaves the source manifest intact. Tested by `tests/AssetManifestEditorTests.cpp` (`sokoban_asset_manifest_editor_tests`).
- `src/engine/AssetManifestDebugUi.*`: Debug-only ImGui adapter for `AssetManifestEditor`. Provides the Asset Manifest tab and owns only widget/modal presentation; all document and filesystem behavior stays reusable by a future non-debug editor UI.
- `src/engine/ContentPipeline.*` + `tools/ContentTool.cpp`: headless production-content inventory, validation, and staging. Resolves manifest model, skinned-attachment, texture, animation, and audio references plus external `.gltf` URIs, rejects missing/escaping paths, parses every playable level, rejects decoration model names absent from the manifest, requires contiguous level/screen indices, verifies all compiled shaders, includes nearby asset notices, excludes `levels/Deleted`, and atomically replaces the output with only reachable files. When `levels/overworld/layout.json` exists it validates the composed topology, production selector coverage/ownership, component decorations, and stages the layout plus every referenced screen; the legacy root file is accepted only when no layout exists. Safety checks resolve the nearest existing ancestor before appending missing output components, avoiding Windows `weakly_canonical` stalls on a new nested staging path without weakening junction/symlink containment checks. Writes `content.index` with format/game version, file count, sizes, and paths. Tested by `tests/ContentPipelineTests.cpp` (`sokoban_content_pipeline_tests`).
- `src/engine/LevelProjectStore.*`: rollback-capable editor transaction boundary. It clones the level tree, applies one mutation, validates contiguous puzzle content plus a structural `OverworldMap` and its selector catalog, builds a runtime mirror, then swaps both roots with recoverable backups. Composed runtime mirrors include only `layout.json` and referenced active component files, so source-only `overworld/Deleted` screens cannot leak into a runnable build. Puzzle renumbering in `LevelEditor` uses the same transaction to rewrite selectors across every referenced overworld component. Covered by `tests/LevelProjectStoreTests.cpp` and the topology/editor suites.
- `src/engine/RuntimeContent.*`: resolves the read-only `assets/` directory beside the executable through `SDL_GetBasePath` and rejects missing, corrupt, unsupported, or game-version-mismatched `content.index` files. `Application`, `VulkanRenderer`, and `AudioSystem` all use this one runtime root.
- `src/engine/render/RenderAssetRequirements.*`: Vulkan-free model/animation/texture requirement sets plus shared tile-to-model mapping. Computes requirements from a loaded `Level` for prefetching or from `RenderFrameData` as a draw-time safety net. Every level decoration contributes its manifest model, so current/next-level prefetch covers authored scenery. Mirror-bearing levels preload the ten smoke sprites; active particles explicitly require their selected textures. Procedural water is excluded, so no obsolete water mesh can enter lazy loading. Tested by `tests/AssetRequirementsTests.cpp` (`sokoban_asset_requirements_tests`).
- `src/engine/render/AnimationController.*`: Vulkan-free owner of gameplay animation clips, per-actor clip selection, preview overrides, deduplication, and crossfade state. It emits immutable skinning requests and is tested by `tests/AnimationControllerTests.cpp` (`sokoban_animation_controller_tests`).
- Non-looping render tiles carry an independent fallback clock. Death and
  enemy-attack clips can therefore use one effective speed while their
  dead-idle/enemy-idle fallback uses another, without coupling the two poses.
- `src/engine/render/SkinnedMeshUpdater.*`: owns each animated actor's skinned source mesh and dynamic Vulkan vertex/index buffers. It consumes `AnimationController` requests, performs CPU skinning/blending (including named-node static attachments), and uploads changed vertices.
- `src/engine/render/VulkanModelResources.*`: owns lazy per-asset load states, TaskSystem futures, static model meshes, texture images/samplers, manifest material bindings, and failure retention. CPU parsing/decoding runs on workers; completed results are published to Vulkan on the render thread. It orchestrates `AnimationController` and `SkinnedMeshUpdater` while exposing lightweight mesh/material/texture views and loading statistics to the renderer.
- `src/engine/render/VulkanSsaoPass.*`: owns the scene-sized R8 ambient-occlusion target and sampler, plus depth/AO transitions and the fullscreen AO/composite recording sequence. Pipelines and scene descriptors are passed in as non-owning handles.
- `src/engine/render/VulkanShadowPass.*`: owns the fixed-size shadow depth image, sampler, and image-layout state. It records pass setup/transitions while `VulkanSceneRecorder` supplies the scene-specific shadow draw traversal between `begin` and `end`.
- `src/engine/render/VulkanSwapchainResources.*`: owns the native-resolution swapchain/image views, scaled scene color/MSAA/depth/resolve-depth attachments, acquire/present calls, resize lifecycle, frame attachment transitions, the ice-blur scene-color copy, and the final linear upscale into the swapchain before native-resolution player/debug UI. Replacement construction passes the active swapchain through `oldSwapchain`, allowing resize/settings resources to be built before the old bundle is retired. Profile VSync selects guaranteed FIFO; disabled VSync prefers mailbox, then immediate, then FIFO fallback.
- `src/engine/render/VulkanPipelineFactory.*`: owns the shared pipeline layout and all scene, procedural-water, model, UI, shadow, SSAO, composite, and visualization pipelines. Shader-module loading and graphics-pipeline construction no longer live in `VulkanRenderer`.
- `src/engine/render/VulkanSceneDescriptors.*`: owns the scene descriptor-set layout, pool, set, and bindings for shadow, copied scene color, model textures, sampled scene depth, and SSAO. Resize/MSAA changes update the same set with new attachment views.
- `src/engine/render/VulkanUiResources.*`: owns the one-time R8 font-atlas
  upload and sampler used by the player-facing overlay shader.
- `src/engine/render/VulkanResourceUtils.*`: exception-safe shared Vulkan image allocation, image-view creation, memory-type selection, and destruction used by the focused resource owners. `VulkanRenderConstants.hpp` holds the shared 256-byte push-constant contract.
- `src/engine/render/VulkanDeviceContext.*`: RAII owner of the Vulkan instance, SDL surface, selected physical/logical device, graphics/present queues, queue-family capabilities, and graphics command pool. It also owns device suitability checks and sample/line capability queries.
- `src/engine/render/RendererReconfiguration.*`: Vulkan-free settings transaction planner. Repeated MSAA, render-scale, wireframe, and resize requests coalesce into one immutable plan; requested settings become active only after replacement resource construction succeeds. Covered by `tests/RendererReconfigurationTests.cpp` (`sokoban_renderer_reconfiguration_tests`).
- `src/engine/render/FrameResourceTracker.*`: Vulkan-free frame-slot/generation tracker used to retain superseded render-resource bundles until every graphics fence that references them has completed. Covered by `tests/FrameResourceTrackerTests.cpp` (`sokoban_frame_resource_tracker_tests`).
- `src/engine/render/VulkanSceneRecorder.*`: non-owning command encoder for shadow, opaque, translucent, SSAO/composite, world-transition, upscale, game UI, and Debug UI work. The world-transition pass copies the resolved scene, applies the fullscreen pixel-blur/cover shader, and runs before native-resolution player or Debug UI so those surfaces remain crisp. Water shares the translucent scene-color snapshot with ice, binds its dedicated pipeline while traversing the depth-sorted face pool, and composites refracted opaque color without writing depth. The recorder owns pass ordering, barriers, draw traversal, pipeline/descriptor binding, push constants, and per-recording render statistics.
- `src/engine/render/VulkanRenderer.*`: top-level frame orchestrator for synchronization, asset publication, descriptor refresh, swapchain recovery, submission, and presentation. Settings writes are queued and coalesced at the frame boundary: full render bundles are transactionally replaced for MSAA/render-scale/swapchain changes, pipeline-only bundles for wireframe changes, and old generations are destroyed after their frame fences complete. Swapchain retirement additionally waits only the presentation queue because graphics fences do not cover presentation completion; settings paths never call `vkDeviceWaitIdle`. Its checked `PreparedFrame` handle refers to one of two renderer-owned CPU scratch slots, retaining capacity between uses and rejecting stale generations. Device, resource, pass, pipeline, descriptor, model, animation, projection/picking, and command-recording ownership is delegated to focused components.
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
- `src/engine/ui/OptionsMenu.*`: pure options reducer, state-only controller,
  declarative row model, and stateless `OptionsMenuView`. `optionsMenuRows`
  describes each page as typed button/choice/toggle/slider/binding rows;
  drawing consumes those rows and emits semantic intents without mutating
  settings or deciding policy. `reduceOptionsMenu` takes immutable
  `OptionsMenuState`/`UserSettings` values and returns the next state plus at
  most one `OptionsAction`; `options::SettingsChanged` carries the complete
  replacement `UserSettings` through `ShellFlow::ApplySettings`, so
  `Application` never reads settings back from the menu. Custom render-scale
  dragging is reducer-owned preview state and commits once on release.
  Direct reducer, row-composition, binding, navigation, and rendered-control
  coverage lives in `tests/UiTests.cpp`. `open(allowTitleExit)` adds an
  "Exit To Title" row only when opened as the in-game pause menu, and
  `allowLevelSelect` adds a
  "Level Select" row (pause context only; `Application` passes it once every
  level on disk has a completion record, so it is permanent for that save
  and cleared by New Game). The Controls page lists the gameplay
  remappable gameplay actions (menu navigation is deliberately fixed) with
  press-to-rebind capture: `capturingBinding()` tells `InputRouter` to return
  binding candidates for `provideBindingCandidate` and suppress raw key/pad
  navigation meanwhile (keys bound to MenuBack still pass
  so Escape cancels; Start cancels directly). Escape/Start are never bindable,
  duplicates are stolen from actions in the same input context, and Reset To
  Defaults restores `defaultInputBindings()`. Debug builds add an Editor
  Controls subpage for Replace Tile, Delete Tile, and Move Tile.
- `src/engine/ui/TitleScreen.*`: headless fullscreen title-screen state
  (Main with Continue/New Game/Options/Quit and a destructive-action New Game
  confirmation). The world is not loaded while the main menu is up; only the
  Continue/New Game results make `Application` load it. A level/screen-select
  page is opened as a standalone flow from the pause/options or completion
  UI once shell policy allows level selection. The caller supplies
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
'W'  Water (legacy explicit tile)
'L'  Ladder
'^'  Conveyor up
'v'  Conveyor down
'>'  Conveyor right
'<'  Conveyor left
'1'  Mirror north-west
'2'  Mirror north-east
'3'  Mirror south-west
'4'  Mirror south-east
'D'  Decorative block (renders only; no gameplay/camera semantics)
'N'  Enemy start
```

Important tile behavior:

- `Player`, `Rock`, `Ice`, and `Enemy` starts are extracted into dynamic
  runtime entities. Their underlying static level tile becomes Air.
- `Ground` and `Wall` are solid blocks.
- `Water` supports entities but can also be filled/occupied by fallen entities.
- `End` and `PressurePlate` are surface entities rendered as thin tiles.
- `Ladder` and conveyors allow entities in their own cell.
- Conveyors are passable cells that apply automatic movement.
- Mirrors are non-passable static cells and use the `pictureframe_large_A`
  KayKit Furniture Bits model in four rotations plus a model-space 45-degree
  counter-clockwise correction for the asset's authored forward axis.
- `Decorative Block` renders as world geometry but behaves exactly like Air
  for gameplay, support, water-grid bounds, and camera fit.
- `Enemy` uses the skinned Barbarian model and is an editor-placeable start
  marker rather than a persistent static tile.

## Level File Format

Level screens are plain text `.scr` files under `levels/levelN/screenM.scr`.

The modern format uses sequential layers, may declare one water layer, and may
declare any number of manifest-backed mesh decorations:

```text
@water 0
@decoration {"model":"Tree","position":[4.5,2.5,1.0],"rotation":[0.0,0.0,30.0],"scale":[1.0,1.0,1.25]}

@layer 0
.........
.... ....
.........

@layer 1
#########
#   C   #
#   E   #
#########
```

Rules:

- Layer headers must be exactly sequential starting with `@layer 0`.
- `@water N` is optional, must appear before `@layer 0`, and must refer to an
  existing layer.
- `@decoration` is optional and repeatable. Its compact JSON object requires a
  non-empty manifest model name plus finite three-component `position`,
  `rotation`, and `scale` arrays. Rotation is XYZ Euler degrees and every scale
  component must be positive. Decoration metadata must appear before
  `@layer 0`; using metadata requires the modern layered format.
- Decorations render as non-pickable model instances but never enter the tile
  grid, gameplay rules, support/occupancy queries, water-grid bounds, or camera
  fit. The content pipeline rejects unknown model names before packaging.
- Every authored Air cell on the water layer resolves to Water. Solid or other
  explicit tiles on that layer remain unchanged.
- Empty lines between layers are allowed.
- All layers are normalized to the max width/height found in the file; missing cells become Air.
- Single-layer legacy files without `@layer` are still accepted when they do
  not use metadata.
- Explicit `W` tiles remain loadable for backward compatibility, but the
  editor authors new water through the layer setting.
- There must be exactly one player start tile `C`.
- Ladders are validated at load time: every `L` must be adjacent to a Ground tile `.` on the same layer.

Typical layer usage:

- Layer 0: floor/support tiles such as Ground, optionally with `@water 0`
  filling the Air between and beyond them.
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
  next input-driven step. `R` restarts, Space interacts (including activating
  mirrors), and holding `T` animates the camera to a straight-down pitch. These
  gameplay bindings are loaded from `PlayerProfile::settings.input`. Gamepads
  use D-pad or left stick for movement, south/A for interactions, west/X for
  undo, north/Y for restart, and Start for menu/back; the top-down action is
  remappable but has no default gamepad binding.
- `GameplaySession` stores one action record per completed step for undo; the
  authoritative state commits only after `Application` finishes animating the
  action.

Goals and pressure plates:

- `P` pressure plates are tracked separately.
- The end is considered unlocked when all pressure plates are occupied by a movable or player state as implemented by `isEndUnlocked()`.
- Every living player copy must stand on an `E`, every plate must be occupied,
  and no player may be dead before the screen advances.

Players and enemies:

- A screen starts with one authored `C`, but mirror activation may create
  additional player instances when a player is reflected by multiple equally
  near valid mirrors. Every copy receives the same movement input and may be
  duplicated again.
- Every player instance faces the direction of the last movement input,
  including instances that input did not move — a copy blocked by a wall still
  turns. The copies are one character in several places, so they share one
  facing; this is intended and is pinned by
  `playerCopiesShareTheInputFacing` in `tests/PresentationTests.cpp`. Facing
  driven by a belt or slide rather than by input is a separate case and applies
  only to the entities that moved.
- `N` creates an immobile enemy. Enemies kill any living player entering an
  orthogonally adjacent tile; diagonals do not count. Movables can push enemies
  when the resulting destination/fall is valid.
- Enemies use the Barbarian skinned model, the shared idle clip, and smoothly
  slerp toward the nearest player. Attacks use the catalog's `enemy.attack`
  binding (currently Rig_Medium Animation 14). `axe_1handed.gltf` is attached
  to `handslot.r`, receives a local half-turn, and inherits the complete
  animated hand transform during idle, attack, and blends.
- The default catalog places `attack-connected` at 90% of `enemy.attack` and
  gates `player.death` on that event. This ordering is authored data, not an
  undo special case.

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
- An optional `@water N` level directive turns all Air on layer N into Water.
  The renderer continues that water beyond all four board edges, but exterior
  surfaces are non-pickable and excluded from authored dimensions, camera fit,
  level bounds, and gameplay.
- Open water renders as a slightly transparent plane at
  `ground top - waterDepthBelowGround`; a warped cellular field produces
  small, rounded, irregular honeycomb-like ripple cells, with a rotated muted
  translucent cyan layer beneath the bright crests and subtle scene-color
  refraction. Beneath those ripples, broad warped value noise divides the water
  body approximately evenly between configurable dark and light tones. Every
  field is evaluated in world space so it remains seamless across water
  surfaces. A separate subtle animated line follows every integer tile border,
  with configurable width, color/opacity, warp amplitude/frequency, and speed;
  it is fully visible inside authored board bounds and fades smoothly through
  nearby generated exterior water. Shoreline foam is layered over it.
  Ground-colored edge faces remain around exposed shorelines. Water does not
  cast shadows or require a GLTF/model asset.
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

Mirrors:

- `1`, `2`, `3`, and `4` pair the north-west, north-east, south-west, and
  south-east orthogonal rays. Reflection is bidirectional between each pair;
  distance from the mirror is preserved.
- Mirror activation uses the contextual Confirm / Interact action, defaulting
  to keyboard Space and gamepad South. Undo defaults to keyboard `Z` and
  gamepad West. Profile migrations retire the old dedicated Mirror binding.
- `rules::activateMirrors` is pure and transactional. It reflects the player,
  rocks, and ice; the nearest entity occludes entities behind it; chains may
  use each mirror once; ambiguous, obstructed, unsupported, and overlapping
  outcomes reject the entire activation. A valid destination over Water still
  runs normal fall/death/fill rules.
- If multiple equally near mirrors produce distinct valid destinations for a
  player, the original moves to one destination and stable-ID copies are
  created at the others. Rocks and ice retain all-or-nothing non-duplicating
  behavior.
- `rules::previewMirrorActivation` is the shared source for both activation
  and rendering. It returns the exact committed candidate state, affected
  entity identities, fall outcomes, and every input/output beam leg in a
  chained reflection; the visualization cannot disagree with gameplay rules.
- While gameplay is idle and the player is alive, `RenderFrameBuilder`
  deduplicates shared beam legs, emits configurable cyan core/halo prisms, and
  adds translucent destination copies for every affected player/rock/ice.
  During an active action it evaluates both endpoint states, interpolates
  beam paths and ghost destinations only when both endpoints use the same
  mirror chain and the entity remains collinear on the incoming ray. The first
  leg is anchored to the rendered entity only in that case. Sideways exits and
  transitions between different mirrors retain the previous straight geometry
  but quickly smooth-fade the beam and ghost over the configurable initial
  fraction of the action, avoiding bent rays, stale full-strength previews, and
  geometry passing behind a different mirror. Invalid transactions still emit
  no preview.
- Successful forward mirror activations emit
  `GameplayLoop::UpdateResult::mirrorActivated`; `Application` maps that event
  to the manifest-backed `mirror-swap` one-shot set using Kenney's
  `Woosh/woosh1.ogg`. Rejected activations and previews stay silent.
  `AudioSystem::playOneShot` supports arbitrary non-reserved manifest sound
  sets while footsteps and stone dragging keep their cadence/loop handling.
- The same successful event carries every affected entity's final cell.
  `Application` emits a configurable cyan smoke burst above each destination;
  each burst randomizes seven expanding/rising billboards across Kenney
  `smoke_01.png` through `smoke_10.png`. Effects reset at screen boundaries,
  pause with gameplay, and never enter authoritative rules/save state. The
  whole effect can be resized without changing its variation ranges through
  `config::mirrorSwapSmokeScale` in `ParticleConfig.hpp`. Mirror smoke opts
  into the particle system's explicit `drawOnTop` mode, which keeps ordinary
  particles depth-tested while rendering the swap burst over its entity.
  Procedural textured quads use material mode `5` and pass a one-based
  descriptor handle in `textureOptions.y`; model material mode `1` retains its
  descriptor index in `materialOptions.z`. Keeping these paths explicit avoids
  colliding with `model.vert.glsl`, where `textureOptions.y` stores rotation.
- Mirror preview geometry carries `RenderSurfaceEffect::MirrorEnergy`.
  `IsoScenePreparer` routes it exclusively to translucent face/model lists
  and excludes it from picking and shadow passes. Dedicated procedural-face
  and model Vulkan pipelines share `mirror_energy.frag.glsl`, which preserves
  enough model texture detail to identify the ghost while adding an emissive
  rim, pulse, and moving scanline. Ghost model draws back-face cull and write
  depth for their own translucent shell, preventing interior/back triangles
  from blending over the visible surface; the recorder restores non-writing,
  double-sided state afterward. `MirrorConfig.hpp` owns beam dimensions,
  colors/alpha, ghost tint, rim, pulse, scanline, and texture-detail tuning.
- Activation is an instantaneous normal `GameplaySession` action. It does not
  increase the walking move count, is undoable, and survives exact checkpoint
  save/restore validation.
- The editor palette comes from tile definitions, so all four orientations can
  be painted, undone, saved, and loaded without ImGui-owned editor policy.

## Level Editor

The editor logic is available through the headless `LevelEditor` API. Debug builds expose it through `LevelEditorDebugUi`, an ImGui adapter that is not polished product UI. A future in-game editor can use the same commands without depending on ImGui.

Editor capabilities:

- Load/save `.scr` files.
- New/resize documents.
- Insert layers above or below the active layer, and delete layers.
- Paint tile types from a palette.
- Browse any source `.gltf`/`.glb`; first selection registers it automatically,
  then place it as a decoration.
- Select, translate, rotate, non-uniformly scale, reset, duplicate, and delete
  mesh decorations through undoable headless editor commands.
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
- Clicking usually adds above; `R + click` replaces; `D + click` deletes; hold
  `M` while clicking a source and destination to move a tile in one undoable
  edit. The same binding moves overworld screen-selector flags without changing
  their stable IDs or targets, and both move endpoints are dithered. The Level
  Editor panel documents these controls using their live
  bindings, and Debug builds expose their remapping under
  Options > Controls > Editor Controls.
- Deletion preview dithers the selected tile while replacing its normal draw
  with an invisible pick-only proxy. Discarded pixels therefore reveal the
  actual scene instead of an identical copy of the tile, while top/side hover
  selection remains stable.
- Addition preview also uses dithered preview geometry.
- Painting one cell beyond any document edge grows every layer in the same
  undoable transaction; north/west growth shifts existing authored cells.
- Picking logic was fixed so top/side wall faces are selected more reliably instead of accidentally selecting the ground below.
- Placing a ladder in the editor validates same-layer adjacent Ground.
- The Debug mesh catalog scans source `assets/` recursively for `.gltf` and
  `.glb`. Selecting an unregistered file generates a `Decoration_<stem>` model
  name (with a uniqueness suffix when needed), adds a default static/untextured
  model to both manifests, copies the mesh and external glTF dependencies into
  staged assets, grows the renderer's model slots, and refreshes the catalog.
  The screen still serializes only the stable generated manifest name, so
  shipping content never depends on a source filesystem path. Material mode,
  textures, aspect preservation, and other model metadata can be refined later
  in the Asset Manifest tab. Release builds do not scan source assets.
- New decorations snap their pivot to the top surface under the mouse. Their
  initial position uses the tile center in X/Y and the resolved top elevation
  in Z; transform controls can then move them freely. Decorations remain
  separate from tile picking and do not change camera framing.

Rough editor areas:

- The UI is dense and debug-tool-like.
- Text/layout is pure ImGui and not designed as final player-facing tooling.
- File browser workflows work, but need UX hardening and safety review.
- Editor/render picking has had several bugs and should be tested whenever camera, board projection, or layer behavior changes.

## Rendering And Assets

Renderer:

- Vulkan 1.3, dynamic rendering, synchronization2, cube-map arrays, and
  extended dynamic state. Wireframe and wide lines remain optional
  developer-only features.
- Uses SDL3 window/Vulkan integration.
- **The GPU has a camera** (review item C1). This is the fact most of the
  renderer's shape now follows from, and it is worth reading before changing
  anything in `shaders/` or `VulkanSceneRecorder`.
  - `SceneFrameUniform` (set 0, binding 7, GLSL block `SceneFrame`, instance
    `frame`) carries `clipFromWorld`, `shadowFromWorld` and the camera's world
    position, alongside the point lights. It is written once per frame per
    descriptor set by `VulkanSceneDescriptors::updateFrame`.
  - `isoClipFromWorld` and `shadowClipFromWorld` build those two matrices from
    the prepared layouts. They are **exact** equivalents of
    `projectIsoPointToClip` and `projectShadowPoint`, which still exist for
    picking and camera fitting, and `IsoScenePreparerTests` pins each pair
    across a grid of points and camera states. The two forms differ only where
    the scalar ones clamp - view-space z behind the camera, and shadow depth
    outside the sun's range - because a matrix cannot clamp. The shaders clamp
    the shadow depth on the line after the multiply; do not delete that.
  - The isometric projection is an *off-axis* perspective:
    `mat4PerspectiveOffCenter` folds the board's `fitScale` and
    `projectedCenter` shear into the matrix, so framing behaves exactly as it
    did. Its depth row is the ordinary Vulkan one. `mat4View` builds the view
    from the layout's basis and assumes the renderer's conventions: **+z is
    forward** and there is no Y flip, because the scene pass viewport already
    has a negative height.
  - Vertices reach the GPU in **world space**: tile quads as four world corners
    in push constants, models as `worldFromModel` in the instance buffer or
    push block. `PreparedIsoFace` keeps the projected corners too, under
    `vertices`, because picking works in screen space; `worldVertices` is what
    is drawn.
  - **`triangle.vert` serves two spaces, and a quad has to say which.** The
    scene's quads are world space; the UI, the top-down 2D board and its grid
    overlay are authored directly in clip space and have no world position.
    The w of each corner in `TilePushConstants::vertices` carries the answer -
    `worldSpaceQuad` (1) or `clipSpaceQuad` (0) - and `drawFace` takes a
    `clipSpace` flag for the callers that need it. Getting this wrong is not
    subtle and does not look like a transform bug: every clip-space quad lands
    off screen at once, so the window comes up empty and the game reads as
    hung. It cost one grey-screen launch to find. The tidier end state is a
    separate screen-space vertex shader and pipeline, which belongs with F4's
    shader split rather than here.
  - Particles were the other quad still arriving pre-projected and are world
    space now like everything else. The billboard is still built from the
    camera basis; the corners it produces are ordinary world points.
  - `worldFromSunShadow` is gone from all four shaders. World position is an
    interpolated vertex output now instead of being reconstructed by inverting
    the sun's frustum, and the four `sunShadow*` uniform fields that existed
    only to feed it are gone with it.
  - `triangle.frag` and `mirror_energy.frag` derive `viewDirection` from the
    camera position and the fragment's world position. It used to be
    `normalize(vec3(0.0, 0.25881904, 0.9659258))`, compiled in - **specular
    highlights will not look identical**, because they were previously correct
    only for one fixed camera angle.
  - The shadow pass is the deliberate exception: it has one camera per sun and
    six per point light, so no single uniform could serve it. Its pipelines
    still receive CPU-projected clip-space corners, in the push block's first
    slot. Nothing in a shadow pass asks where it is in the world.
  - **A draw's parameters live in a storage buffer, not push constants**
    (review item T1, step one). `GpuDrawInstance` in
    `VulkanRenderConstants.hpp` is the whole per-draw block; a scene draw
    calls `writeDrawInstance`, gets back an index, and passes it as
    `firstInstance`, so every stage reads its parameters through
    `gl_InstanceIndex`. There used to be one
    `vkCmdPushConstants(256)` + `vkCmdDraw(6)` per quad, and a board of any
    size is on the order of a thousand quads.
    - One struct now serves quads, models and the shadow pipelines. What used
      to be a separate `GpuModelInstance` is gone: a model's `worldFromModel`
      is the same four columns a quad uses for its corners, and its rotation
      was already duplicated in `normalAndAmbientRed.xyz`.
    - The fragment stage reads the same entry through a `flat` varying at
      location 7. Each fragment shader aliases it as `draw`, so `pc.color`
      became `draw.color` and nothing else moved.
    - Two exceptions, both deliberate. **Shadow pipelines** still receive the
      block as push constants: they are not instanced, they have a camera per
      pass, and nothing there reads material state. **`skinned_model.vert`**
      gets its index pushed as `DrawInstanceIndexPushConstants`, because its
      `gl_InstanceIndex` is already spoken for by the skinning palette.
    - Binding 10's stage flags now include the fragment stage. Missing that
      is a validation error, not a silent wrong result.
    - Consecutive faces sharing a pipeline are now drawn as **one instanced
      draw** (`drawQuadRun`). A pipeline change is the only thing that can
      end a run, because everything that used to differ per draw lives in the
      instance entry - which is why the entries a run covers are always a
      contiguous span. There is a defensive check for that contiguity: if
      anything ever allocates an entry mid-loop, the run breaks cleanly
      rather than drawing the wrong face.
    - The run is flushed before the model pass and before the UI, so nothing
      reorders across them. Draw order within a run is entry order, which is
      the sorted face order - the ordering invariants above are untouched.
    - `stats_.drawCalls` counts real draws again rather than quads;
      `visibleFaces`, `vertices` and `triangles` still count quads, so the
      ratio between them is now a live measure of how well runs are merging.
    - The UI, the 2D board and particles still draw one quad at a time. They
      go through the same path (`drawQuadRun(..., 1)`) and could be batched
      the same way; they are simply not where the draw calls were.
    - `maxDrawInstancesPerFrame` is `tileCapacity * 4`. If "Draw instance
      buffer is exhausted" ever appears, that is the knob.
  - Push constants are no longer full. The 64-byte second slot used to be a
    shadow-space copy of the corners - the frame's sun transform restated once
    per face - and is now `passData`, free unless a pass claims it. Water is
    the one claimant, for its border and ripple parameters. It had already
    been squatting there.
  - One consequence worth knowing: tile faces used to be pushed pre-divided
    with `w = 1`, so their attributes interpolated affinely. They go through a
    real projection now, so interpolation is perspective-correct. Ground splat
    UVs and grid lines on large quads will shift slightly, and the ground
    splat shader's "global origin" - `min` over `pc.vertices[i].xy` - is
    genuinely a world coordinate now rather than a normalised device one.
- Has a shadow pass and scene pass.
- The opaque and translucent scene passes use different pipelines and
  different sort orders, and both halves matter:
  - Opaque geometry binds blend-disabled twins (`sceneOpaque`,
    `groundSplatOpaque`, `modelOpaque`, `skinnedModelOpaque`) and
    `IsoScenePreparer` sorts `opaqueFaceIndices` **nearest first**, so the
    depth test rejects occluded fragments before shading and the blend unit
    never reads the colour attachment back.
  - Translucent geometry keeps the blended pipelines and stays sorted
    **farthest first**. Reversing that order is a correctness bug, not a
    performance regression. `IsoScenePreparerTests` pins each direction
    separately.
  - A face in the opaque list may still be blended, and **blended faces
    write depth**. Drawn front to back, such a face writes depth before the
    geometry behind it is drawn, so it blends against the background instead
    of against that geometry - a hole, not a tint. `IsoScenePreparer`
    therefore partitions `opaqueFaceIndices` on `color.w >= 1.0f`: a
    nearest-first fully opaque prefix, then a farthest-first blended tail,
    with `scene.opaqueBlendedFirst` marking the boundary. That is the same
    predicate the recorder picks its pipeline with, so the tail is also one
    uninterrupted run of blended draws and costs one fewer pipeline bind
    than the interleaved list did.
  - **The depth range must cover every drawn tile, not only the ones that
    frame the camera.** This is the invariant the opaque order depends on,
    and it is worth understanding because violating it produced two
    different bugs that both looked like something else.
    `projectIsoPoint` **clamps** z to `[0, 1]` rather than clipping - the
    quads are projected on the CPU and pushed as clip-space vertices, so
    there is no near/far clipping to rely on. A surface past the far plane
    therefore does not disappear: it arrives at exactly `z = 1.0`, sharing
    that depth with every other such surface, and the depth test can no
    longer order them at all.
    - `calculateIsoLayout` used to derive near and far from the camera-fit
      geometry alone. With an authored `cameraExtent` - which every gameplay
      frame sets - that meant the eight corners of the extent box plus one
      tile of padding, and any tile outside it landed on the far plane.
    - Symptom one: flipping the opaque sort turned the far row inside out.
      All those faces tie at 1.0, `LESS_OR_EQUAL` gives a tie to whichever
      drew last, so back-to-front showed the nearest of them and
      front-to-back showed the farthest. It reads exactly like a back-face
      culling bug and is not - the culling toggle made no difference.
    - Symptom two: `VK_COMPARE_OP_LESS` was tried as the fix for symptom
      one, and it made the whole far row *vanish*, because the depth buffer
      clears to exactly 1.0. A tile type change in that row brought it back,
      since `includeCameraCell` then extended the extent past it.
    - The fix separates the two questions. The on-screen fit stays authored:
      only tiles with `affectsCameraFit` participate, and only when there is
      no explicit `cameraExtent`, so a decoration still cannot zoom the
      board out. The depth range walks **every** tile and iso face
      unconditionally. Nothing clamps, no two surfaces share `z = 1.0`, and
      the order is free again. `IsoScenePreparerTests` pins it: no drawable
      vertex may sit on either plane, the fit must stay identical to a board
      containing only the framed rows, and the depth range must reach past
      them.
    - The pass stays on `LESS_OR_EQUAL` throughout. `LESS` is the textbook
      op for a front-to-back opaque pass, but here its failure mode is
      silent disappearance rather than a wrong winner, and with the range
      fixed it buys nothing.
  - Debug UI > Engine has a "Sort Opaque Front To Back" checkbox. Turning it
    off restores one painter-ordered list, which is the pre-Phase-0
    behaviour. Together with "Cull Model Back Faces" it separates an
    ordering problem from a winding one in one click, which is the whole
    reason both exist - the first bug here was reported as culling and was
    not.
  - A face in the opaque list may still carry a sub-1.0 alpha - the editor's
    ladder-rung preview does - so the pipeline is chosen per face from
    `face.color.w`, not per pass. Editor dither previews are `discard`, not
    blending, and stay on the opaque pipeline.
- Model draws cull back faces (`VK_CULL_MODE_BACK_BIT`) with
  **`VK_FRONT_FACE_CLOCKWISE`**; tile quads do neither.
  - The clockwise front face is not a quirk of the assets. glTF authors front
    faces counter-clockwise and nothing from object space to clip space
    reverses that - the camera basis is right-handed, `projectIsoPointToClip`
    applies no sign flip, and decoration scale is clamped positive. The scene
    pass viewport is what reverses it: it has a **negative height** to put +Y
    up in Vulkan's Y-down NDC, and a negative viewport height flips the
    winding the rasterizer sees. Culling BACK against the pass-level
    COUNTER_CLOCKWISE therefore removes the outside of every mesh and keeps
    the inside - models render hollow. If the projection or the viewport sign
    ever changes, this constant changes with it.
  - Tile quads stay `CULL_MODE_NONE`: they are already rejected on the CPU by
    `faceVisible`, and their winding after CPU projection is not guaranteed.
  - Mirror ghosts cull back faces too, and are the one model kind that culls
    in the *translucent* pass. Before the winding fix they culled BACK under
    the inverted convention, which meant the effect had always been drawn
    inside-out; that was a bug, not a style. `mirror_energy.frag.glsl` needed
    no retuning because its rim term uses
    `1 - abs(dot(normal, viewDirection))`, which is sign-independent - the
    surface being shaded changed, the rim response did not. Ghosts write
    depth, so that depth now comes from the near surface rather than the far
    one.
  - Blur-behind ice is *not* culled: it is the other occupant of the
    translucent list and its look depends on what its own back faces
    contribute, which is a visual decision rather than a free win.
  - Debug UI > Engine has a "Cull Model Back Faces" checkbox, because a model
    wound the other way disappears rather than degrading. It gates every
    culled model draw including ghosts, so it is a true escape hatch, and
    culling is forced off while wireframe is on.
- The `renderFinished` semaphore is per swapchain **image**, not per
  frame-in-flight (`SwapchainPresentSemaphores`). A present carries no fence,
  so nothing else proves the presentation engine has stopped waiting on it;
  with `minImageCount + 1` images and two frames in flight, the old
  per-frame-slot semaphore could be re-signalled under a queued present. The
  set is owned by `RenderResourceSet` and retired with its swapchain, which
  is what makes the present-queue wait in `destroyCompletedRetirements()` the
  guarantee that destroying it is safe.
- `copyResolvedSceneDepth` runs only when `VulkanSsaoPass::samplesSceneDepth`
  says the occlusion pass will read it. The sampled depth image has exactly
  one reader; with AO off that copy was a full render-extent `vkCmdCopyImage`
  plus four barriers per frame that nothing consumed. Both the copy and the
  pass key off that one predicate so they cannot drift.
- Texture samplers use anisotropic filtering at the device maximum when
  `samplerAnisotropy` is available (`VulkanDeviceContext::maxSamplerAnisotropy`
  returns 1.0 - the "off" value - when it is not, so callers need no branch).
  It is applied only to mipped, linear-filtered textures, so the pixel-art
  atlases are unaffected. Like wireframe, it is an optional tier feature: a
  device without it still passes the release contract.
- Supports MSAA modes (default is MSAA 4x, automatically falling back to the highest count the device's color+depth framebuffers support; the Debug UI combo shows the requested mode, Rendering Stats shows the active sample count), internal render-scale presets of 100%, 75%, 67% (exact two-thirds), 50%, and 25%, plus custom percentages from 25-100%, wireframe, line width controls, lighting controls, grid overlay, and render stats in Debug UI. The 3D scene renders into scaled offscreen attachments and is linearly upscaled to the native swapchain before player/debug UI, so a 4K window can render the scene at exact 1440p or 1080p while UI remains crisp.
- MSAA, render-scale, wireframe, and swapchain requests are coalesced and applied once at a frame boundary. Replacement resources are constructed before publication, superseded resource generations remain alive until all referencing frame fences complete, and swapchain retirement uses a presentation-queue wait instead of stopping the whole device. Rendering Stats exposes replacement count, pending state, retired-bundle count, and presentation-retirement waits.
- Screen-space ambient occlusion (SSAO) applies to all geometry, tiles and GLTF models alike. `VulkanSwapchainResources` provides sampled or resolved scene depth, and `VulkanSsaoPass` records the fullscreen depth-only AO pass (12-tap golden-angle spiral, range falloff to avoid halos, `config::ssaoRadiusPixels/DepthRange`) into an R8 target before multiply-compositing the blurred result onto the lit image. Descriptor bindings 5 (scene depth) and 6 (AO) live in `VulkanSceneDescriptors`. Toggle, strength, and raw-AO visualization are mutable `PresentationSettings` edited by Debug UI > Lighting.
- Renders simple tile faces procedurally and GLTF models for certain tiles/entities.
- `IsoScenePreparer` fills two renderer-owned `PreparedFrameScratch` slots. Their vectors are cleared without releasing capacity, so current rendering and previous-frame editor picking can coexist without function-static mutable state or repeated large allocations.

Model assets currently used:

- KayKit Block Bits 1.0:
  - `bricks_A` for wall/ground-style blocks.
  - `stone` for rocks.
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
- The reachable Kenney Input Prompts spritesheets, XML atlas metadata, and CC0
  license are staged explicitly; the remainder of the vendor pack stays out
  of packaged builds.
- Shaders compile with `MODEL_TEXTURE_COUNT` read out of
  `sokoban::maxModelTextures` (`AssetManifest.hpp`) at configure time, so the
  two cannot drift; descriptor writes pad the texture array with a fallback
  texture, so the manifest can define up to that many textures without shader
  or pipeline changes. Growing it is a one-line edit to that constant followed
  by a CMake re-configure (which happens automatically - the header is in
  `CMAKE_CONFIGURE_DEPENDS`).

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
  independent TaskSystem jobs. Manifest models referenced by mesh decorations
  are collected here exactly like tile/entity models, so scenery needed by an
  upcoming screen is already preparing before the transition.
- `Application` builds one `RenderFrameData` and calls
  `VulkanRenderer::prepareFrame` once per frame. The resulting camera/shadow
  layouts, projected faces, depth-sorted pass lists, model lists, and shadow
  geometry are shared by every pass; editor picking uses the previous prepared
  frame instead of rebuilding the current scene. Prepared frames are checked
  handles over two renderer-owned reusable scratch slots; stale generations
  fail explicitly. `drawFrame` verifies exact
  asset requirements and publishes at most one completed preload per frame.
  Loaded assets remain cached for the process lifetime; there is no eviction
  policy yet.
- Static/skinned Vulkan buffers, texture images, upload command buffers, queue
  submission, and descriptor updates all happen on the render thread. Texture
  uploads use the existing graphics queue and command pool and wait for that
  queue before replacing shared descriptors. There are deliberately no worker
  command pools, dedicated transfer queues, or concurrent Vulkan uploads.
- Background failures are retained in the asset slot, counted in Debug UI, and
  reported without interrupting the current level. If that asset later becomes
  required, `ensureAssets` throws a contextual path/kind error.
- Debug UI > Rendering Stats reports the scene-preparation count and prepared
  iso/shadow/model counts alongside loaded/pending model, texture, and
  animation counts plus failures. A submitted frame reports exactly one scene
  preparation. Unrequested assets are the difference between total, loaded,
  pending, and failed counts.

GLTF loader notes:

- `GltfMesh.*` is a small custom loader, not a general-purpose robust GLTF implementation.
- It supports enough JSON parsing, buffers, accessors, nodes, skins, and animations for the current assets.
- Static model vertices include an integer, one-based `textureIndex` containing
  the final global descriptor-array slot and integer `materialFlags` containing
  resolved per-material behavior. Models using
  `{ "mode": "primitive-materials", "slots": [...] }` resolve every glTF
  material slot from a manifest texture name independently while loading.
  There is no base-index-plus-offset or source-slot behavior contract, so
  inserting or reordering unrelated textures cannot silently change a model.
- Lazy asset requirements request exactly the named textures used by each
  primitive-texture model. Missing, empty, or unknown mappings fail manifest
  validation, while a glTF primitive whose material slot has no mapping fails
  model loading with the offending slot number.
- Skinned model manifest entries may declare `attachments` containing a static
  glTF `path` and skeleton `node`. Attachment geometry is loaded in source
  scale with its owning model, resolved to the named node once, and transformed
  by that node's sampled global matrix during every skinning/blending update.
  It is merged into the actor's dynamic mesh and inherits the actor material,
  guaranteeing held items follow idle, attack, and crossfade poses. The
  Attachments may request a local `rotateHalfTurn` before node placement. The
  Barbarian binds `axe_1handed.gltf` to `handslot.r` with that half-turn so the
  axe faces forward while still inheriting the complete animated hand pose.
- If adding complex GLTF assets, consider switching to a proven GLTF library or broadening loader support carefully.

Shader notes:

- `model.vert.glsl` accepts position, normal, UV, final texture index, and
  material flags. Descriptor handles and flags use flat integer varyings.
- `triangle.frag.glsl` samples the shadow map, resolved scene color, and a
  model texture descriptor array (`MODEL_TEXTURE_COUNT`, padded with a
  fallback texture). That count is compiled into every shader from
  `CMakeLists.txt`, which reads it out of `sokoban::maxModelTextures` in
  `AssetManifest.hpp`; the manifest rejects more textures than the cap, and
  device selection rejects GPUs that cannot bind that many sampled images.
- Each manifest model declares material mode `none`, `texture` with one named
  texture, or `primitive-materials` with one record per glTF material slot.
  Every record owns its texture and optional behavior such as `scrollV`;
  mappings become final descriptor indices and vertex flags during loading.
  The conveyor is therefore ordinary material data rather than a hard-coded
  model, descriptor index, or source-slot convention.
- Push constants carry transform, lighting, grid, material, and texture options.
- `ground_splat.frag.glsl` blends two ground textures through a splat map on
  ground tile tops. It reuses the standard tile lighting, shadowing, grid
  overlay, and editor-preview dither so splatted ground matches neighbouring
  surfaces exactly. `TilePushConstants` is already at the guaranteed 256-byte
  limit, so the pass reuses slots the ground path leaves free rather than
  growing the block: `materialOptions.x` carries splat-region-local origin X
  (opaque ground never blurs) and `textureOptions` carries the three one-based
  texture handles plus splat-region-local origin Y. Global material UVs are
  derived from the face vertices. Handles are one-based because zero
  means "unresolved", which falls back to the flat tile color.

Ground splatting:

- Textures are generated, not authored: `python tools/make_ground_textures.py`
  writes seamless tiling `ground_grass.png`, `ground_rock.png`, the greyscale
  `ground_splat.png`, one `ground_splat_level<N>_screen<M>.png` per puzzle
  screen, and one `ground_splat_overworld_<ID>.png` per composed-overworld
  screen
  into `assets/custom/textures/`. The script uses
  only the standard library (PNGs are encoded by hand with `zlib`), is
  deterministic for a fixed seed, and documents its knobs (resolution, layer
  colors, noise octaves/frequency, splat patch scale and contrast) at the top.
  Seamlessness matters because the UVs wrap across tiles - any seam would show
  as a grid line across the board.
- The manifest declares them as ordinary textures (`GroundGrass`,
  `GroundRock`, `GroundSplatMap`; names live in `RenderTypes.hpp` as
  `groundSplat*TextureName` so the builder and the requirement planner cannot
  drift). They are optional: `RenderFrameBuilder` resolves them with the
  non-throwing `AssetManifest::findTextureIdByName`, and when any is missing
  the ground falls back to the flat tile color and the standard pipeline, so
  a trimmed manifest still renders.
- Resolving an id is not enough to make a texture usable: textures are only
  uploaded into the descriptor array when something *requires* them, so
  `RenderAssetRequirements` requires all three both per level (preload) and
  per frame (draw-time safety net). Skipping that step leaves those slots
  holding the 1x1 white fallback and the ground renders flat - the failure
  mode is silent, so `tests/AssetRequirementsTests.cpp` locks it with a
  regression case.
- `RenderFrameBuilder` marks Ground tiles with
  `RenderSurfaceEffect::GroundSplat` (both the gameplay and editor paths, so
  the editor previews the real look). `IsoScenePreparer` maps that to
  `PreparedSurfaceMaterial::GroundSplat` for the upward-facing top face only -
  block sides keep the flat tile material - and `VulkanSceneRecorder` binds
  the dedicated pipeline and calls `drawGroundSplatFace`.
- The two material layers use world-grid UVs: face-local coordinates are
  scaled by the face's size in tiles and offset by the minimum global face
  vertex, so the grain is continuous across adjacent tiles and stable as the
  camera moves, rather than stamping the same texture per tile.
  `GROUND_UV_TILES` (repeat span, in tiles) is a shader constant.
- The splat map is the opposite: it does **not** tile. One map covers one
  screen's board exactly, which is what makes it paintable - a repeating map
  would echo every brush stroke across the board. Its coverage is not pushed
  per face (the 256-byte push-constant block is completely full); instead the
  shader divides `textureSize()` by `GROUND_SPLAT_TEXELS_PER_TILE` to recover
  the board size. That constant must stay equal to
  `SplatCanvas::texelsPerTile` and `SPLAT_TEXELS_PER_TILE` in
  `tools/make_ground_textures.py`, or every stroke lands offset and rescaled.
- Texture sampling is three independent manifest options, because the ground
  needs three different combinations:
  - `"tiling": true` - repeat addressing instead of clamp-to-edge. The
    material layers need it (their UVs leave 0..1); the splat map must not
    have it.
  - `"filter": "linear"` - interpolate instead of point sample. All three
    ground textures want it; the pixel-art atlases do not.
  - `"colorSpace": "linear"` - upload as UNORM instead of SRGB. The splat map
    needs it: it is weight data, and an sRGB decode turns a painted 0.5 into
    a 0.21 blend weight.
  All three default to the plain-colour-atlas behaviour, so every other
  texture is unaffected, and `AssetManifestEditor` re-serializes them so a
  save from the in-game manifest editor cannot silently change how the ground
  is sampled.

Per-screen splat maps:

- Every screen gets its own blend map, so screens differ in where rock shows
  through the grass - not just levels. The convention is the texture name
  `GroundSplatMap<level>_<screen>` (`groundSplatMapTextureNameForScreen` in
  `RenderTypes.hpp`), backed by
  `custom/textures/ground_splat_level<N>_screen<M>.png`. Both indices are
  zero-based, matching `levels/level<N>/screen<M>.scr`. The separator matters:
  without it, level 1 screen 23 and level 12 screen 3 would collide on the
  same name.
- A screen with no map of its own falls back to the shared `GroundSplatMap`.
  That fallback is deliberate: adding a screen and forgetting to regenerate
  gives the old look rather than untextured ground. It is also what the level
  editor uses, since a document belongs to no screen.
- `groundSplatTexturesForScreen` in `RenderTypes.hpp` is the single definition
  of that selection-and-fallback rule. `RenderFrameBuilder` and
  `RenderAssetRequirements` both go through it, because a preloader that
  fetched a different map than the drawing code would leave the screen
  sampling an unpublished slot - the same silent flat-ground failure as
  forgetting to require the texture at all.
- Composed-overworld screens use their stable IDs instead of puzzle indices:
  `GroundSplatMapOverworld<ID>` and
  `custom/textures/ground_splat_overworld_<ID>.png`, defined by
  `groundSplatMapTextureNameForOverworldScreen` and
  `groundSplatMapAssetPathForOverworldScreen`. Moving a screen to a different
  layout slot therefore does not rename or repaint it.
- `Application` passes every visible `OverworldView` screen as a global-bounds
  region to `RenderFrameBuilder`. The resulting fixed-capacity region table
  selects textures per ground face in `VulkanSceneRecorder`; the recorder
  subtracts the region origin only for splat lookup, while the shader retains
  global grass/rock UVs. `RenderAssetRequirements` includes every region's
  textures so newly visible maps cannot resolve to an unpublished descriptor.
- `tools/make_ground_textures.py` discovers IDs and component files through
  `levels/overworld/layout.json`, generates board-sized maps, rejects missing
  manifest entries, and includes overworld files in stale-map pruning. The
  production migration currently declares screen ID 1 with a 288x224 map for
  its 9x7 board.
- The location reaches the builder as `GameplayInput::levelLocation` and the
  planner as the optional third argument to `renderAssetRequirementsForLevel`,
  both `std::optional<LevelLocation>` and both defaulting to unset, i.e. the
  shared map.
- Adding a screen: the level editor's "Create Splat Map" button does the whole
  job in-game - writes a blank board-sized map into both asset trees,
  registers the texture in the live manifest, persists it to the source and
  staged `manifest.json`, and drops straight into paint mode. No generator run
  and no restart.
- Or from the command line: create `levels/level<N>/screen<M>.scr`, re-run
  `python tools/make_ground_textures.py` (it discovers screens from the tree),
  and add the matching `GroundSplatMap<N>_<M>` manifest entry. The script
  exits non-zero and names the missing entry if you skip that last step,
  because a missing entry is otherwise silent - the screen just renders with
  the shared map.
- **The generator never overwrites an existing splat map.** Once a map exists
  it may have been painted, and painted bytes are indistinguishable from
  generated ones, so re-running the script only fills in missing maps.
  `--force` regenerates everything (destroying painted work) and `--prune`
  deletes maps whose screen is gone; both are opt-in for the same reason.
- Runtime texture registration: `AssetManifest::addTexture` appends to the
  live manifest, which is safe because ids are indices and appending never
  disturbs an existing one. Anything holding parallel per-texture state has to
  grow with it - `VulkanModelResources::syncManifestTextures` does that for
  the GPU slots, and the renderer refreshes descriptors. The entry must also
  be written to both `manifest.json` copies or it is gone on restart.
- The file name lives in two languages, so
  `groundSplatMapAssetPathForScreen` (C++) and the generator's f-string
  (Python) must agree; `AssetManifestTests` pins the shipped manifest's paths
  against the C++ helper.
- Maps are board-sized: `boardTiles * 32` pixels, so brush detail is the same
  on every screen. The generator reads board dimensions straight out of the
  `.scr`, mirroring `Level::loadFromLayers` (tallest layer, longest row).
- Budget: each screen costs one slot in the model texture descriptor array,
  sized by `sokoban::maxModelTextures` (currently 64, 35 in use). That
  constant is the single source of truth: `CMakeLists.txt` parses it out of
  `AssetManifest.hpp` at configure time and compiles every shader with
  `MODEL_TEXTURE_COUNT` set to it, so growing the array is a one-line header
  edit plus a re-configure. Overflowing throws while parsing the manifest, and
  `VulkanDeviceContext::isDeviceSuitable` rejects any device whose
  `maxPerStageDescriptorSampledImages` cannot hold the array, so both failure
  modes are loud. Far past that point the right fix is a layered texture
  indexed by screen, not a bigger array.

Painting splat maps in the editor (Debug builds only):

- The Level Editor panel has a "Ground Paint" section. "Paint Ground" opens
  the splat map for the document being edited; from then on the pointer paints
  instead of placing tiles, and Ctrl+Z undoes strokes rather than tile edits.
- Three constraints make painting dead-silent if they are not respected, and
  all three were bugs in the first version - worth knowing before touching
  this code:
  - **Editor input only exists in document-editing mode.** `InputRouter` fills
    `frame.editor` only when `RoutingContext::editorEditing` is set, and
    `Application::update` only calls `updateEditorPainting` when
    `levelEditor_.editingDocument()`. Opening a paint session therefore also
    switches the editor into that view; from the "current screen" view there
    is no editor pointer input at all, so nothing happens and nothing reports
    an error.
  - **Painting must not use the tile pick list.** `scene.pickFaceIndices`
    excludes editor previews, and every layer except the active one is a
    preview. Ground normally lives on layer 0 while the active layer is above
    it, so keying off that list makes the brush dead in the common case.
    `pickGroundPoint` scans all faces and filters by material instead.
  - **Strokes key off the held button, not the pressed edge.**
    `EditorInput::primaryPressed` is true only on the frame the button goes
    down (correct for placing one tile per click); `primaryDown` is the held
    state. Driving strokes from the edge ends them a frame after they start,
    so dragging is impossible.
- **Use `loadedDocumentPath()`, never `documentPath()`,** to work out which
  screen a document is. `documentPath()` is the browser *selection* and moves
  on a single click without loading anything; `loadedDocumentPath()` is where
  the in-memory document actually came from, and is empty for an unsaved new
  document. Keying the splat map off the selection meant clicking a screen in
  the browser swapped the rendered map onto the loaded screen, and "Paint
  Ground" opened the selected screen's map while the view showed the loaded
  one (which at startup is whatever screen the save file is on).
- A board resized in the editor takes its splat map with it:
  `SplatCanvas::resizeToBoard` grows or crops anchored at the board origin, on
  open and mid-session (`SplatPainter::followBoardResize`). Without it the map
  keeps covering the old extent and the extra tiles repeat the clamped edge,
  because coverage comes from the texture's dimensions. A resize drops paint
  undo history, since those snapshots are sized for the old canvas, and it is
  the one case where `updateTexture` recreates the image rather than writing
  in place - so it reports `descriptorsChanged` and the renderer refreshes
  descriptor sets.
- `pickGroundPoint` returns a `Vec3`: x/y are the board tile position, z is the
  world height of the surface that was hit. Painting only uses x/y, but the
  preview ring is drawn by projecting world points, so it needs the real
  height. Ground is often not at z=0 - editor previews are nudged up slightly,
  and raised blocks are a whole unit higher - and assuming a height puts the
  ring below the paint, by more the further the surface is from the camera.
- The pieces, and why they are split this way:
  - `SplatCanvas` (`src/engine/SplatCanvas.*`) - the weight buffer and the
    round brush (radius in board tiles, hardness, opacity, black/white).
    Entirely headless, which is where nearly all of the behaviour is pinned
    down: `tests/SplatCanvasTests.cpp`.
  - `SplatPainter` (`src/engine/SplatPainter.*`) - one paint session: which
    screen, load/save, stroke-level undo, dirty tracking. Puzzle documents
    derive `GroundSplatMap<level>_<screen>` from their path; composed
    overworld documents pass `GroundSplatMapOverworld<ID>` explicitly, so the
    same painter follows stable topology identity. Also headless;
    `tests/SplatPainterTests.cpp`.
  - `IsoScenePreparer::pickGroundPoint` - pointer to a continuous world-tile
    position. Unlike `pickGridCell` it resolves *within* a tile and is
    perspective-correct via the stored per-corner clip w.
    `tests/GroundPickTests.cpp` round-trips world -> pixel -> world.
  - `VulkanModelResources::updateTexture` - re-uploads painted pixels into the
    existing image, so the view, sampler and every descriptor pointing at it
    stay valid and no descriptor rewrite is needed.
  - `encodeGrayscalePng` (`src/engine/render/PngWriter.*`) - saving. The engine
    otherwise only reads images; this is a small self-contained encoder
    (fixed-Huffman deflate, adaptive filters) rather than a vendored
    dependency, verified by round-tripping through stb_image in
    `tests/PngWriterTests.cpp`.
- Brush radius is in **board tiles**, not pixels or texels, so a brush keeps
  its physical size on the ground whatever the board size or camera distance.
- A stroke is one press-drag-release and one undo step, however many pointer
  samples it contains. `stampLine` interpolates between samples so a fast drag
  paints a continuous line rather than a row of discs. Strokes that change
  nothing record no undo step, so Ctrl+Z never appears to do nothing.
- **Within a stroke the canvas accumulates coverage, not colour.** Each texel
  keeps the strongest `falloff * opacity` it has seen and is composited once
  against the state the stroke started from - never against its own output.
  This matters because the pointer is sampled every frame: holding a click for
  8 frames re-stamps the same spot 8 times, and compositing repeatedly drives
  `1 - (1 - f)^N` toward full. That turned every soft brush into a hard disc
  (all hardness settings looked alike after one click) while opacity merely
  saturated. It also keeps overlapping passes of one drag from painting a
  darker streak where stamps pile up. Separate strokes still build up, which
  is how repeated dabs are expected to darken.
- The outer texel of the brush always feathers slightly, even at hardness 1,
  so a hard edge is smooth rather than stair-stepped. One consequence: a
  repeated identical stroke is not a whole-stamp no-op, because the partially
  covered rim keeps creeping toward the target. The covered interior is
  saturated after the first stroke.
- Saving is explicit ("Save Map"), and writes to the source `assets/` tree and
  mirrors into the staged tree beside the executable, so a painted map is both
  committed and live without re-running the content pipeline.
- The brush preview is a disc of concentric rings whose per-vertex alpha comes
  from `SplatCanvas::coverageAt` - the same function stamping uses - so it
  shows hardness and opacity rather than merely outlining them. Drawing it
  from an independently written profile would be worse than having no preview:
  it would look authoritative and be wrong, so `SplatCanvasTests` pins
  `coverageAt` against what stamping actually writes.
- Every preview vertex is a world point projected individually through the
  *previous* frame's camera - the same one that produced the brush position -
  and written into ImGui's background draw list. It deliberately needs no
  shader or descriptor changes; the tile push-constant block has no room left.
  The indices are written by hand, so the fill is skipped rather than wrapped
  if the draw list is near the 16-bit `ImDrawIdx` limit.
- Re-uploads are throttled by comparing `SplatPainter::revision()` against the
  last uploaded value, so a stroke that changes nothing does not stall the
  device (the upload path does a full `vkDeviceWaitIdle`, which is acceptable
  only because it is editor-only and skipped when idle).
- The session closes itself if the document changes underneath it, so loading
  another screen from the file browser cannot paint screen A's map while
  showing screen B's board.

Tile palette thumbnails:

- The level editor's palette shows a picture of each tile, and those pictures
  are **baked offline by screenshotting the real game render**. Run:

  ```powershell
  .\out\visual-studio\Debug\sokoban.exe --bake-tile-thumbnails
  ```

  It renders each tile through the normal frame path - same shaders, lighting,
  shadows, SSAO and MSAA - captures the result, and writes
  `assets/custom/thumbnails/tile_<name>.png` into both the source and staged
  trees, then exits. Re-run it after changing tile models, materials or
  lighting.

  The same work is on a "Re-bake Tile Pictures" button in the Level Editor
  panel, which is usually easier: the command line flag is easy to forget and
  awkward to pass when launching from an IDE. Both paths log which mode the
  process started in, because "I passed the flag and it just opened the game"
  is otherwise indistinguishable from the flag never arriving.
- The button only *requests* a bake; it runs between frames in `run()`. The
  callback fires inside the debug UI's ImGui frame, and the bake begins ImGui
  and UI frames of its own - nesting them trips ImGui's frame-scope assertion.
- The bake begins an empty ImGui frame per captured frame and never draws the
  debug UI into it. That is not cosmetic: the debug UI is recorded into the
  same pass that resolves into the image being captured, so any panel left
  open would be baked into the thumbnails. It also gives the `ImGui::Render()`
  inside `drawFrame` a matching `NewFrame`.
- Consequences worth keeping: a thumbnail cannot drift from how the board
  actually looks, because it *is* how the board looks; and the files can be
  opened and checked outside the game.
- `TileThumbnailBake` owns the scene and the crop rectangle, so the bake and
  its tests agree on where the tile lands. It is headless and tested
  (`tests/TileThumbnailBakeTests.cpp`); the rendering itself is the game's own.
- **The subject stands on a 3x3 bed of neutral grey ground, and the whole bed
  drives the camera fit.** That is not only for looks: fitting the camera to
  the subject alone framed every tile differently - a flat tile filled the
  view while a tall one was pushed back - so no two thumbnails shared a scale.
  With a fixed bed every tile gets an identical camera, and the crop comes out
  the same rectangle for all of them. The bed also gives shadows and ambient
  occlusion somewhere to land, so tiles read as sitting on ground rather than
  floating in a void.
- The bed is drawn as plain cubes rather than Ground tiles, so a screen's
  splat map cannot change what the thumbnails look like. Ground itself
  replaces the centre bed cell instead of stacking on it, which would z-fight.
- **The subject tile is built by `tileVisual` (`RenderFrameBuilder.hpp`), which
  the editor also uses.** That function is the single definition of how a tile
  type looks - footprint, height, colour, model, rotation, manifest scale. It
  exists because the bake originally restated those rules and quietly dropped
  branches: conveyors are neither a surface entity nor a solid block, so they
  baked at height 0, and their rotation lives in
  `rules::conveyorDirectionForTile` rather than `mirrorOrientationQuarterTurns`,
  so all four directions baked as one identical flat picture. Anything that
  needs to draw a lone tile should call `tileVisual` rather than re-deriving it.
  `tests/TileThumbnailBakeTests.cpp` pins the bake's subject to `tileVisual`
  field by field, so a future copy of the rules fails the suite.
- **Settings must be passed in** (the live `PresentationSettings`). They carry
  both the lighting - `RenderFrameData::Lighting`'s defaults have shadows *and*
  ambient occlusion switched off, so a bake using them silently produces flat,
  contact-less pictures - and the per-tile manifest scales that `tileVisual`
  applies.
- **The bake uses a long lens** (`RenderFrameData::cameraDistanceMultiplier`,
  4x). Camera distance is derived from the size of the area being framed, so a
  camera fitted to a 3x3 bed sits roughly four times closer than one fitted to
  a board, and the perspective was strong enough to see - a tile's vertical
  edges splayed by nearly 4% of the picture width, which reads as the tile
  leaning or bulging. The multiplier pulls the camera back while the fit
  rescales to compensate, so the subject stays the same size on screen and only
  the divergence changes; it is a lens choice, not a zoom. 4x puts the splay
  between what a 9-wide and a 13-wide board produce. Much further would
  approach an orthographic view, which is *less* like the game.
  `tests/TileThumbnailBakeTests.cpp` measures the splay directly rather than
  asserting the constant, and checks that removing the multiplier breaks it.
- The crop is derived by projecting the centre cell through the frame's own
  camera rather than taking a fixed fraction of the extent, so it follows the
  subject if the camera or the bed ever changes.
- **The content pipeline stages thumbnails explicitly**
  (`ContentPipeline::addTileThumbnails`). Nothing in the manifest names them -
  they are editor pictures, not assets the game loads - and staging wipes the
  output root and copies only what it was told about. The `sokoban_content`
  target is `ALL`, so it re-stages on every build: a bake wrote into both the
  source tree and the runtime root, so the palette looked right until the next
  build removed the staged copies, and the editor then showed coloured squares
  on every launch while the files sat untouched in `assets/custom/thumbnails`.
  Missing thumbnails are skipped rather than fatal, since before the first bake
  there is nothing to copy.
- **Baked thumbnails upload as `VK_FORMAT_R8G8B8A8_SRGB`.** The capture holds
  display-ready, sRGB-*encoded* bytes, which is why the PNGs look right in an
  image viewer. The swapchain is sRGB, so the hardware encodes linear->sRGB
  when ImGui writes. Uploading as UNORM hands the shader those already-encoded
  bytes as if they were linear and they get encoded a second time - byte 60
  displays as 133, byte 200 as 229 - which is the washed-out, overexposed look
  the palette had. SRGB decodes on sample so the write round-trips exactly, and
  it matches how the game uploads its own colour textures. Note this assumes an
  sRGB swapchain; `chooseSurfaceFormat` prefers one but falls back to
  `formats.front()`, and on that fallback every colour texture in the game
  would be wrong together, not just thumbnails.
- `VulkanFrameCapture` reads a region of the resolved scene colour image back
  to the CPU. That image is already single-sampled and `TRANSFER_SRC`, so the
  capture needs no changes to the render targets - it just moves the layout to
  transfer-source and puts it back. Swapchain formats are often BGRA, so the
  channel order is fixed up during the copy out.
- `VulkanThumbnailPass` now only *loads* those PNGs and hands them to ImGui.
  An earlier version rendered the models here in a bespoke pipeline; it had no
  shadows or SSAO and its material handling had to be kept in step with
  `triangle.frag.glsl` by hand, and it looked wrong. Loading a screenshot is
  both simpler and exact by construction.
- Thumbnails are loaded lazily and cached, including the misses, so a tile with
  no baked file does not hit the filesystem every frame. A missing file just
  means the palette keeps its colour swatch for that tile.
- The images are uploaded as UNORM, not SRGB: a capture already holds the
  shaded, display-ready pixels the game presented, and decoding them again
  would wash the palette out relative to the board.

## Building the render layer outside Visual Studio

Most of `src/engine/render` and the debug UI can be type-checked on a machine
without the Vulkan SDK, which matters because these files are otherwise only
ever compiled by a full Windows build:

- Vulkan headers are vendored inside SDL at
  `third_party/SDL/src/video/khronos` (add that directory to the include path).
  It predates `VK_API_VERSION_1_4`, so define it to `VK_API_VERSION_1_3` for a
  syntax-only check.
- ImGui needs `third_party/imgui`, `third_party/imgui/backends` and
  `third_party/imgui/misc/cpp` on the include path.
- `Application.cpp` additionally wants `SOKOBAN_SOURCE_ASSET_DIR`,
  `SOKOBAN_SOURCE_LEVEL_DIR`, `SOKOBAN_GAME_VERSION` and
  `SOKOBAN_ENABLE_DEBUG_UI=1`.
- A few headers use backslash include paths (`engine\Level.hpp`), which MSVC
  accepts and other compilers do not; a shim directory containing files with
  those literal names works around it.

Note that MSVC also accepts a nested type's default member initializers inside
a default argument of the enclosing class, which GCC and Clang reject - see
`VulkanModelResources::createTextureBlocking`, where the sampling argument is
passed explicitly for that reason.

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
  (audio/video/input) are shared across slots in a
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
- Semantic W/S or D-pad/stick navigation, Space or controller South
  confirmation, and Escape/Start back navigation.

It still lacks wrapping, kerning, localization, accessibility-driven scaling,
scroll containers, and constraint-based responsive reflow; add those within
these focused modules rather than moving player UI into Debug-only ImGui.

## Recent Work Summary

Major recent additions and fixes:

- Replaced mechanic-specific animation rewind logic with a generic immutable
  presentation transaction. Stable `EntityId`/`EntityTarget` values now bind
  motion and animation tracks to actors across state copies, save/load, and
  undo. `PresentationTransactionBuilder` resolves catalog marker dependencies
  and clip speeds once; normal play and undo seek the same data in opposite
  directions. Renderer-facing state no longer interprets attack, death, or
  undo policy. Profile format 17 persists the new IDs/tracks and migrates
  compatible format-16 checkpoints. Generic timing, cycle rejection, actor
  routing, partial forward playback, and partial/full reverse playback are
  covered by presentation/profile/session tests.
- Added the animation catalog and authoring workflow: every manifest clip has
  a global speed, each semantic use has its own clip/speed override, uses may
  expose named normalized timeline events, and dependent uses may start at a
  source event. The Animation tab persists edits atomically and provides an
  isolated model+clip preview scene with play/step/loop/scrub controls. Event
  editing uses a list page plus one focused seconds/percentage timeline; event
  and free-form preview sessions are independent.
- Added multi-player mirror duplication and immobile Barbarian enemies.
  Player copies share movement input, can be copied again, and must all reach
  ends. Enemies face the nearest player with smooth slerp, attack adjacent
  players, can be pushed by movables, and carry a hand-node-attached axe.
  Enemy attack and player death ordering is authored through the animation
  catalog's `attack-connected` event.
- Added `Solve Current Screen` to the Debug Engine tab. It updates the current
  screen completion flow without pretending to solve an entire multi-screen
  level.

- Fixed mesh-decoration imports that rendered white and tile-fitted. Static
  mesh loading now has an opt-in `preserveSourceScale` path that performs only
  the engine coordinate-system conversion, decoration instances use the
  authored origin as their pivot, and the importer discovers and binds a
  single external glTF base-color atlas while reusing existing texture paths.
  Existing Furniture Bits and Platformer decoration entries were migrated.
  Manifest, importer, real-mesh scale, and presentation-pivot regressions cover
  the complete path.
- Added a live mirror activation preview backed by the exact pure rules
  transaction. Valid idle reflections now show a two-layer animated energy
  beam through every chained mirror leg and a translucent textured ghost of
  each affected entity at its final destination, including submerged
  destinations. The dedicated mirror-energy face/model pipelines are
  translucent, shadowless, and non-pickable; the content pipeline packages the
  new shader. Rules, presentation, scene-preparation, and packaging regressions
  cover chain paths, final state agreement, frame emission, pass ownership, and
  shader staging.
- Added subtle animated water tile-border lines inspired by the supplied
  Order of the Sinking Star reference. The shader warps both axes of an
  integer world-space grid with independent two-frequency waves, keeping tile
  boundaries gently ripple-shaped while remaining phase-continuous across
  adjacent cells and large exterior water surfaces. `WaterConfig.hpp` owns
  border color/opacity, width, warp amplitude, frequency, speed, and exterior
  fade distance. Authored board dimensions attenuate the mask by radial
  distance outside the board, so generated ocean tiles lose the grid without a
  rectangular cutoff. Water-only unused shadow-transform push slots carry
  those controls without extending the Vulkan push-constant layout; shoreline
  foam remains the top layer.
- Replaced the high-fan-out `Config.hpp` with focused gameplay, audio, user
  settings, UI, animation, camera, lighting, renderer, scene, and water config
  headers. Removed configuration includes from foundational renderer/profile
  headers where possible, retained aggregate settings compatibility, and made
  normalization and Debug UI use the same configurable bounds. Changing water
  art tuning now rebuilds only direct water consumers instead of the broader
  settings, profile, application, and renderer-interface graph. An incremental
  MSBuild check after touching only `WaterConfig.hpp` compiled
  `GameplayPresentation.cpp`, `RenderFrameBuilder.cpp`,
  `VulkanSceneRecorder.cpp`, and `PresentationTests.cpp` only.
- Changed the rotated secondary ripple from a dark-blue blend, which became
  indistinguishable from the two-tone body's dark regions, to its own
  configurable muted cyan tint and low opacity. It now reads as a translucent
  echo of the primary crest, matching the reference while remaining below the
  crisp white layer. The secondary field now has an independent configurable
  thickness scale and favors its narrow crest over its halo, preventing it
  from reading as a second set of heavy bands. `WaterConfig.hpp` also exposes
  the shared ripple crest width, halo width, and primary crest/halo strengths.
- Added a separate low-frequency two-tone field to the water body, matching the
  reference's broad alternating color regions beneath the ripple network. A
  dominant coarse value-noise octave plus light detail is thresholded around
  its midpoint for a roughly even split and gently warped/drifted in world
  space. `WaterConfig.hpp` exposes tone frequency, dark/light multipliers,
  transition width, and animation speed; previously unused water push-constant
  fields carry them to the shader.
- Replaced the open-water sine contour field, whose lines could connect into
  map-spanning paths, with a warped cellular ridge field. Nearest/second-nearest
  feature distances produce small closed irregular cells; a strongly jittered
  triangular lattice, asymmetric displacement, randomly oriented anisotropic
  distance metrics, per-cell weights, cell-scale nested warping, and varying
  crest width prevent a polygonal or overly regular pattern. The centered
  lattice search keeps its candidate set stable across cell boundaries. The
  final coordinate pass adds crossed, nested sub-cell waves with controlled
  lateral displacement, making each crest meander without folding the field
  into disconnected rings. Bright crests use a narrow, tightly antialiased
  core with only a faint supporting halo, avoiding cloudy gradients and white
  blooms where several boundaries meet. The secondary translucent layer
  evaluates the same field after a 90-degree rotation. The implementation uses a nine-sample
  neighborhood and arithmetic hash rather than an exact 34-sample Voronoi edge
  search, keeping fullscreen fragment cost bounded.
- Replaced authored per-cell water for shipped levels with optional
  `@water N` level metadata. Air on that layer now resolves to Water while
  explicit solid cells preserve island/shore shapes. `Level::Definition`
  parses and serializes the metadata; `LevelEditor` owns toggling, undo,
  insertion/deletion index maintenance, draft construction, and persistence,
  with ImGui reduced to a thin checkbox adapter. Rendering keeps interactive
  water inside the board and adds a non-pickable exterior ring plus four
  world-space continuation strips, so water fills the viewport without
  changing camera fit or gameplay bounds. Shipped level 2 water screens were
  migrated, and parser/editor/presentation/scene-preparer regressions cover
  the contract. Exterior neighbors bypass the bounded legacy edge-face gate,
  so no bank is drawn at the authored rectangle. Prepared water faces also
  retain camera-depth clip W per vertex; the water draw reconstructs clip-space
  positions from it so shader coordinates interpolate perspective-correctly
  across cell, ring, and large continuation quads without ripple phase seams.
- Added three save slots: slot-stemmed `SaveStore`/`AsyncSaveStore` files
  (existing saves become slot 1), an `active-slot.txt` marker, and a
  title-screen Save Slots page with per-slot summaries, inline confirmed
  deletion, and a choose-a-slot flow for the first New Game. Settings moved
  out of the slots into a shared `settings.json`
  (`PlayerProfile::settingsOnly`/`adoptSettingsFrom`), bootstrapped from the
  pre-split save. Covered by store-isolation, settings-split, and
  slots-page tests.
- Added the Options > Controls input-remapping page: gameplay actions
  render their current keyboard/pad bindings, confirm starts a raw-event
  capture (fed from `InputState::bindingCandidate` in the SDL loop with
  navigation suppressed), same-kind bindings are replaced while other-device
  bindings survive, duplicates are stolen from actions in the same context,
  Escape/Start
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
- Added Vulkan-free `IsoScenePreparer` and made `PreparedFrame` the
  renderer entry contract. Camera/shadow layout, face projection/culling,
  opaque/translucent sorting, model categorization, shadow geometry, and pick
  geometry are prepared once and shared across all passes; Application also
  stopped rebuilding the editor frame separately for picking.
- Extracted `VulkanDeviceContext` as the RAII owner of instance, surface,
  device, queues, capabilities, and command pool. Extracted
  `VulkanSceneRecorder` as the owner of pass/draw command encoding and render
  statistics. `VulkanRenderer` now coordinates frame synchronization, asset
  publication, descriptor refresh, submission, and presentation, while two
  checked CPU scratch slots retain prepared-scene vector capacity.
- Replaced settings-time `vkDeviceWaitIdle` rebuilds with a Vulkan-free,
  coalescing reconfiguration transaction and frame-generation retirement.
  MSAA/render-scale/resize construct complete replacement render bundles,
  wireframe constructs only a replacement pipeline bundle, and superseded
  resources survive until their exact graphics-frame users complete. Debug
  diagnostics expose pending, retired, replacement, and present-wait counts.
- Unified persisted and menu-facing settings into one `UserSettings` value
  owned by `PlayerProfile`. Replaced the stateful settings copy in
  `OptionsMenu` with a pure reducer, state-only controller, declarative row
  descriptions, and a stateless view that emits semantic intents. Changed
  settings now travel through the shell command as data instead of requiring
  Application to read a menu snapshot and map it back into the profile.
- Replaced per-message mutex-held disk writes/flushes with a bounded
  multi-producer logging queue and one sink-owning writer. Added semantic
  categories, periodic and immediate-error flushing, explicit process-exit
  draining, error-preserving overflow policy, synthetic drop reports, Debug UI
  queue/drop diagnostics, and a focused concurrent logger regression suite.
- Replaced per-test production-source lists with reusable `sokoban_core`,
  `sokoban_ui`, and `sokoban_render_vulkan` static libraries. The game,
  content tool, and all test executables now share one set of production
  compile definitions and link dependencies; tests compile only their own
  source, and the CMake helper rejects future `src/` entries.
- Split application presentation/configuration responsibilities into
  `PresentationSettings`, `GameplayPresentation`, and `RenderFrameBuilder`,
  with `ApplicationDebugUi` and `AnimationPreviewDebugUi` as adapters.
  `Application.cpp` is now roughly 910 lines and concentrates on the event
  loop, component composition, external effects, editor interaction, and
  lifecycle orchestration; campaign, gameplay-loop, input-routing, and
  settings policy are testable components.
- Split headless gameplay orchestration out of `Application` into
  `GameplaySession` and `GameplayLoop`; `InputRouter` now translates SDL-backed
  state into focused consumer frames. State/history/undo/restart/action timing
  and routing semantics are covered by dedicated test executables.
- Added layered levels with `@layer N` sections.
- Added level editor/document browser and draft play flow.
- Added Debug UI controls for rendering, lighting, runtime audio, grid, and conveyor rate. Authored tile scales and sound/music volumes live in the dedicated Asset Manifest window.
- Added KayKit GLTF asset pipeline for blocks, Rogue character, animations, and conveyors.
- Added animated Rogue player rendering.
- Added rocks/movables, undo, restart, pressure plates, and screen progression.
- Added water/falling behavior and water edge rendering.
- Drowned players now remain a dynamic rendered entity instead of becoming a
  static replacement tile: they descend one full cell to the basin floor while
  the procedural water surface remains above them. Entering the dead state
  records a `player-death` segment whose completion use is
  `player-dead-idle`. Restored dead checkpoints start directly in the dead idle
  pose. Undo samples that same descent/death transaction backward, including
  its clip clock and movement ordering; there is no drowned-player undo branch
  or mutable death-transition flag. The shipped roles use
  `Rig_Medium_General.glb` Animation 3 (`Death_B`, manifest clip 3) and
  Animation 4 (`Death_B_Pose`, manifest clip 4). Manifest clip values use the
  one-based numbering shown by asset tools. Both clips participate in lazy
  level preloading and frame fallback requirements.
- Replaced the KayKit water mesh with Vulkan-free procedural water-surface
  frame data and a dedicated translucent Vulkan shader. Open cells now render
  lowered, world-space phase-continuous ripple planes with subtle refraction.
  The surface uses an irregular weighted cellular field with nested
  world-space warping, producing rounded animated cells with defined pale
  crests. A second evaluation is rotated 90 degrees and phase-offset, then
  composited as a muted translucent cyan echo beneath the primary layer.
  Broad warped noise independently divides the body between two tones. Water
  surfaces carry dynamic four-edge and four-corner shoreline masks; gameplay
  recomputes them from static Ground/Wall cells and water currently filled by
  fallen rocks or ice. The shader uses them to add two continuous animated foam
  levels around the resulting shoreline island. Diagonal-only contact emits a
  rounded quarter-circle cap that shares crest phases with its adjoining edge
  directions; adjacent filled cells suppress caps at their internal seam. The
  configurable near level fills solid white from its moving crest to the tile
  boundary, while a configurable farther band can overlap it and has separate
  configurable opacity and thickness. Filled cells omit the plane, shoreline
  edges remain, editor preview/picking still work, and the water GLTF is no
  longer requested or staged.
- Added ice sliding and translucent/blurred ice rendering.
- Added ladder tile `L`.
  - Placement/load validation requires adjacent same-layer Ground.
  - Placeholder rung rendering was made thicker/larger.
  - Climb behavior was corrected to start from the ladder tile and move toward attached ground.
  - Climbing is blocked when the attached tile is occupied.
- Fixed editor deletion picking so elevated/wall tiles are selected more accurately.
- Changed deletion preview to dither the selected tile instead of hiding it.
  The original tile now remains only as pick geometry during deletion preview,
  preventing the ordinary tile underneath from visually filling every dither
  hole while preserving stable hover selection.
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

- `b287ebc4` is the handoff's baseline commit. It follows the complete
  engine-hardening and productization program; check `git status --short`
  before making changes because release-evidence and documentation work may be
  intentionally uncommitted.
- The full Debug and Release builds pass from `out/visual-studio`. The current
  Runtime ZIP package gate passed with 198 indexed assets (10,059,860 bytes)
  and a packaged Vulkan launch. The project owner also completed the external
  clean-machine/GPU-driver acceptance. Repeat that evidence collection for
  every release artifact or driver-support revision.
- All 59 CTest suites pass in both Debug and Release, including real-font/options UI, concurrent texture decoding,
  input/gamepad mapping, profile
  migration/recovery, gameplay
  move-count semantics, mandatory validation of the shipped manifest,
  manifest-editor save semantics, drowned-player rendering/death-animation
  transitions, mirror transaction/visualization ownership, enemies/multiple
  players, animation catalog events, generic presentation dependency/cycle
  handling, forward/reverse transaction playback, and the `content_pipeline`
  suite.
- `cmake --preset shipping`, `cmake --build --preset shipping`, and
  `cpack --preset shipping` produce separate
  `Sokoban3D-0.1.0-Windows-x64-Runtime.zip` and `-Symbols.zip` artifacts in
  `out/shipping`. The runtime stage contains 198 reachable files
  (10,059,860 bytes), not complete source vendor packs. Use
  `packaging/ReleaseValidation.md` and its evidence collector before shipping.
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

## Math And Geometry

`src/engine/Math.hpp` is the engine's linear algebra and the only definition
of it. `src/engine/Geometry.hpp` adds the bounding volumes on top. Both are
header-only and pinned by `tests/MathTests.cpp` and `tests/GeometryTests.cpp`
(ctest names `math` and `geometry`).

Conventions, all of which are silent when violated:

- **Matrices are column-major**, `values[column * 4 + row]`, so a `Mat4`
  uploads to GLSL without transposing. This matches the layout the glTF loader
  already produced.
- **`a * b` applies b first.** A model-view-projection is
  `projection * view * model`. Quaternion composition follows the same rule.
- **Quaternions are (x, y, z, w)**, w last, matching glTF.
- **Every type is an aggregate** with no user-declared constructors, because
  brace initialization is used at hundreds of call sites.
- `Frustum` extraction assumes **Vulkan's 0..z..w clip volume**: the near
  plane is row 2 alone, not row2 + row3. The OpenGL form puts the near plane
  in the wrong place and the symptom looks like something else entirely.
- Frustum plane names follow clip space, not the screen. The scene pass draws
  through a negative-height viewport, so clip-space "top" appears at the
  bottom of the window.

Before this module the engine had the same operations written out separately
in about a dozen translation units, including **three quaternion slerps** and
**two identity matrices**. Two differences were behavioural rather than
cosmetic, and both are preserved deliberately rather than merged away:

- `normalize(Vec3)` existed twice with different epsilons (1e-6 and 1e-4) and
  different degenerate results (`{0,0,1}` and `{0,0,0}`). The shared
  `normalize` returns the zero vector; a caller that needs a specific axis
  back says so through `normalizeOr(v, fallback)`. The glTF loader's vertex
  normals use `normalizeOr(v, {0,0,1})` because that is what its own copy
  returned. The threshold is now uniformly 1e-6 on length, so vectors between
  1e-6 and 1e-4 long normalize where `IsoScenePreparer` previously zeroed
  them - in this codebase that range means "degenerate either way".
- `slerp` existed twice, once clamping `t` and once not. The shared one does
  **not** clamp, because that is the mathematical operation.
  `GameplayPresentation` clamps at its call site, visibly, so the enemy-facing
  blend keeps exactly the guarantee it used to make for itself.

`add`/`subtract`/`multiply` exist as named forms beside the operators. That is
not an accident: rewriting several hundred existing call sites to reach one
definition would have been a large silent-transcription risk in projection
code for a cosmetic gain. New code should prefer the operators. Both spellings
resolve to the same definition, which is the point.

`Mat4` no longer lives in `GltfMesh.hpp`. Rotations that are rotations are
now `Quat`: `SkeletonNode::rotation`, `NodePose::rotation`, and
`GameplayPresentation`'s actor `orientation`. `AnimationKeyframes::values`
stays `Vec4` because that buffer is untyped four-float data - translation,
scale and rotation all live in it - and is interpreted as a quaternion only on
the branch that knows it is one.

The camera's own matrices (perspective, look-at) are deliberately *not* here.
Choosing a depth range and a handedness belongs with the camera, and that
decision lands with the real-camera work rather than being guessed at now.

## Important Design Decisions

- Keep gameplay rules in the headless `Rules` module as pure functions of `(Level, GameState)`. `GameplaySession` owns command/state/history orchestration, `GameplayPresentation` owns visual interpolation/animation state, `InputRouter` owns consumer focus and semantic routing, `Application` pumps SDL and coordinates component lifetime, and the renderer receives a render-frame description rather than owning game rules.
- Give every authoritative dynamic entity a stable `EntityId`. Action history,
  persistence, presentation, and rendering must address `EntityTarget`s rather
  than relying on vector position; mirrors can add players and future mechanics
  may reorder or remove actors.
- Record presentation ordering once on the forward action as an immutable
  `ActionPresentationTimeline`. New mechanics should emit generic motion and
  animation intents, using named catalog events for dependencies. Do not add
  mechanic-specific flags or reconstruction branches to undo, the sampler, or
  the renderer; undo seeks the recorded transaction backward.
- When changing or adding mechanics, implement them in `Rules.cpp` and add cases to `tests/RulesTests.cpp`; the tests compile without SDL/Vulkan so they can run anywhere.
- Store player starts, enemies, rocks, and movable ice as dynamic entities
  extracted from level data rather than static cells.
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
- Keep compile-time constants/defaults in their owning focused config header;
  do not recreate an umbrella include. Keep mutable presentation tuning in
  `PresentationSettings`, authoritative gameplay tuning/state in
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

- Isometric camera tuning lives in `CameraConfig.hpp`. `cameraPitchDegrees`
  controls the angle away from straight down, `cameraYawDegrees` controls the
  heading around the board, and FOV, board-relative distance, and final
  fit/zoom have adjacent controls. The current default pitch is `30` degrees
  and the default yaw is `0` degrees.
- Water overlay visibility is independently tunable in `WaterConfig.hpp`
  through `waterPrimaryRippleOpacity`, `waterSecondaryRippleOpacity`,
  `waterPrimaryShorelineOpacity`, `waterSecondaryShorelineOpacity`, and
  `waterGridLineOpacity`. These multiply final blend weights without changing
  the procedural masks, widths, or animation.

- Add an explicit cache budget/eviction policy if the manifest grows enough
  for lifetime caching to become expensive.
- Add timing/history diagnostics for blocking `ensureAssets` calls and
  background CPU preparation if asset stalls become difficult to reproduce.
- Replace the custom GLTF parsing with a robust library if assets get more complex.
- Improve visual consistency between procedural tiles and GLTF assets.
- Verify model orientation/scale whenever a new asset is added.
- Keep `VulkanRenderer` as the frame orchestrator,
  `VulkanDeviceContext` as the device-lifetime root, `IsoScenePreparer` as the
  Vulkan-free projection/picking boundary, and `VulkanSceneRecorder` as the
  command-encoding owner. Add a dedicated synchronization owner only if frame
  pacing or additional queues make the current two-frame orchestration grow.
- If frequent live window-mode changes make swapchain retirement measurable,
  enable present-id/present-wait or swapchain-maintenance present fences and
  replace the conservative presentation-queue retirement wait with per-present
  completion tracking.

Audio:

- Add sounds for more events (undo, restart, level completion, falling into
  water, conveyor hum) as manifest sound sets.
- Consider a mixer section in the manifest if compile-time audio defaults and
  bus policy should move out of `AudioConfig.hpp`.

UI:

- Make level editor layout more deliberate and less debug-panel-like.
- Add user-facing explanations/tooltips only where they help, not as clutter.

Engineering:

- `Decorative Block` is a first-class tile (`D`) for authored world geometry
  that has no gameplay semantics. Entities, movement, falling, mirrors, and
  occupancy treat it like air, while presentation renders it as a full-height
  model and includes it in normal lighting/shadows. It is available in the
  level-editor palette and asset manifest. Render frames carry a gameplay-only
  camera extent calculated from authored gameplay tiles and immutable player
  and movable starts. Decorative tiles and runtime entity positions are
  excluded. When an explicit camera extent is present, `IsoScenePreparer`
  treats it as authoritative for target, distance, projected center, and fit;
  transient models, mirror ghosts, beams, and other procedural faces cannot
  move or zoom the camera during play. The authored volume's normalized depth
  range has a configurable guard band (`cameraDepthPaddingTiles` in
  `CameraConfig.hpp`) because GLTF meshes can protrude past their logical tile
  transform; this prevents near/far clipping without changing framing.
- Render frames also carry stable, origin-aware water-grid bounds calculated
  from authored gameplay-relevant tiles. The water shader uses these bounds
  instead of the serialized level dimensions, so air and decorative scenery
  cannot extend the tile-border visualization. Runtime movement must not make
  either the water grid or camera framing grow or shift.
- Editor frames provide a symmetric, invisible one-cell picking border around
  the document. Painting there automatically grows every layer; north/west
  growth prepends rows or columns and shifts all authored tiles together,
  while south/east growth appends them. The resize and paint are one undoable
  document transaction and updates the requested dimensions. Unlike manual
  resize, paint-driven growth initializes every new cell as air and an ordinary
  exterior click targets layer 0, so placing decoration creates only that one
  decorative tile. Camera extents are origin-aware and exclude both air and
  decoration, preventing sparse north/west or south/east growth from changing
  gameplay framing. Replace mode and layer lock retain their explicit behavior.
- The editor exposes `+ Layer Below` and `+ Layer Above` as separate headless
  document commands. Both select the inserted air layer and are undoable;
  insertion below shifts existing layers and renumbers the configured water
  layer when necessary.
- Editor hover previews are layered over stable underlying pick geometry.
  Addition/replacement previews retain the ordinary underlying tile or
  empty-cell pick surface. Deletion previews replace the ordinary tile draw
  with a shape-equivalent `pickOnly` proxy so dithering reveals the real scene
  without destabilizing selection. Never remove the underlying pick geometry:
  previews are intentionally non-pickable, and doing so creates a
  frame-to-frame loop where selection alternates between adjacent cells or
  selected/unselected.
- Invisible editor pick surfaces for air and the one-cell expansion border are
  projected at the top of their logical cell (`z + 1`), not its base. This
  keeps the pick quad coincident with a full-height preview block's top under
  perspective; using `z` creates parallax that grows toward the far side of
  the board. `IsoScenePreparerTests` covers the projected-face invariant.
- Player-profile format 11 migrated only the exact accidental format-10
  defaults (`Mirror=Z`, `Undo=X`); format 22 later retired the Mirror action
  and consolidated mirror activation under Confirm / Interact (Space).
- `Show Top-Down View` is a remappable hold action, defaulted to `T`. It is
  routed only while gameplay is active and smoothly moves the regular 3D
  camera pitch between `config::cameraPitchDegrees` and zero; it does not use
  the legacy `TopDown2D` renderer path. The smoothstep transition duration is
  `config::cameraPitchTransitionSeconds` (0.15 seconds by default), and the
  zero-pitch camera basis has an explicit non-degenerate orientation.
  Player-profile format 12 adds the binding to existing profiles while
  preserving the one-control/one-action invariant.
  The controls menu now uses compact rows with separate fitted label/binding
  columns, preventing long gamepad binding strings from overlapping or
  escaping narrow 580x718-class layouts.

- Gameplay rules, gameplay/campaign orchestration, input routing, settings
  policy, presentation, render-frame construction, and editor document behavior
  live in focused `Rules`, `GameplaySession`, `GameplayLoop`, `CampaignSession`,
  `InputRouter`, `SettingsCoordinator`, `GameplayPresentation`,
  `RenderFrameBuilder`, and `LevelEditor` components with tests. `Application`
  remains the lifecycle/composition root and executes platform, rendering,
  audio, filesystem, UI, and persistence effects requested by those systems.
- The `TaskSystem` now handles lazy CPU asset preparation as well as skinning.
  Grow it by moving more independent CPU work onto tasks (render-frame building,
  animation updates) and eventually adding task dependencies/graphs when
  systems need ordering. Keep Vulkan publication render-thread-owned.
- Add save format/versioning if level files evolve.
- Review asset licensing/readme files before distribution.

## Practical Tips For The Next Agent

- Prefer `rg` for searches.
- Build with `cmake --build out\visual-studio --config Debug`.
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
    `{ "mode": "primitive-materials", "slots": [{ "texture":
    "<Material0>" }, { "texture": "<Material1>", "scrollV": true }] }`.
    The array position is the glTF material slot; each texture name and behavior
    resolves independently of global descriptor ordering.
