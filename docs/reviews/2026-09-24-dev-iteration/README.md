# Sokoban 3D developer-iteration review

Reviewed September 24, 2026, at commit `889ff0aed817aa9ff52019e50e09e1b114085855`.

This review measures the edit → build → run → reach-the-thing-you-changed loop and recommends changes to shorten it. It covers build speed, live reload, the level editor, and the content pipeline. The previous reviews looked for defects; this one looks for time lost on each iteration. The HANDOFF rule still applies: every recommendation below cites a measurement or a concrete source path. No code was changed.

## Implementation status

Packet A was implemented on September 24, 2026, and verified on Linux.
[The packet A evidence note](evidence/packet-a.md) has the details.

- **DI-01:**
  - The BC7 encoder is optimized in every configuration.
  - Development stages skip unchanged content, take textures from a
    per-configuration cache, and encode cache misses in parallel.
  - The shipping presets still stage from scratch.
- **DI-02:** `dev` and `dev-all` presets (Ninja, Debug, MSVC) were added, and
  `/MP` is on for the Visual Studio generators.
- **DI-03:**
  - Suites link into the shared runners `sokoban_tests` and
    `sokoban_vulkan_tests`; `player_profile` stays standalone.
  - The 80 CTest names are unchanged.
  - Sanitizer builds keep one executable per suite because of linker memory.

In the 2-core Linux measurement environment:

| Scenario | Before | After |
| --- | ---: | ---: |
| No-op build | 25.2 s | 0.1 s |
| One-line `Rules.cpp` edit, build the game | 29.3 s | 5.0 s |
| One-line `Rules.cpp` edit, rebuild everything | 61.6 s | 8.5 s |
| Executables relinked by a core change | 78 | 5 |

The Windows/MSVC acceptance timings in DI-01 to DI-03 have not been recorded
yet.

Packet B was implemented on September 25, 2026, and verified on Linux.
[The packet B evidence note](evidence/packet-b.md) has the details.

- **DI-07:** saving a shader recompiles it with the build's `glslc` and
  flags, publishes it into the staged tree, and rebuilds pipelines in the
  running game. Errors keep the last good shaders and are shown over the
  game.
- **DI-08:** the `SOKOBAN_TUNABLE_*` declarations and the Tuning tab let
  values change live, and Save writes them back into the header. Fog of war
  is migrated; water and lighting are not yet.
- **DI-09:** the `--continue`, `--title`, `--level/--screen` and `--edit`
  launch options, plus a Debug session file that resumes the active slot
  and the editor document on launch.

## Assessment

The biggest cost is not compilation. **Every Debug build of `sokoban` spends about 29 seconds re-encoding every texture to BC7, even when nothing changed.** The local build tree shows 27.6 s of BC7 encoding on the last Debug build. A cloud reproduction shows a no-op build taking 25.2 s, all of it in `sokoban_content`. This one step turns a one-line shader or constant tweak into a half-minute wait before the game can even start.

The second cost is restarting. Shaders, tuning constants, source textures, manifest edits, and externally edited levels all need a rebuild plus a restart today. The game then boots to the title screen, and you navigate back to where you were. The renderer already has most of what live reload needs: a pipeline-only swap with fence-safe retirement (`VulkanRenderer.cpp:1633-1644`), `updateTexture`, compressed-artifact invalidation, and live-tunable settings structs. What is missing is the trigger and the persistence.

The third cost is link fan-out. Any `sokoban_core` change relinks 78 executables: the game, the content tool, and 76 test programs. On the local Windows tree those test programs occupy 3.7 GB of PDBs, and the documented loop builds all of them.

The level editor is functional and well tested, but a few basics are missing that most tile editors have: drag-painting, redo, save/play shortcuts, and a fast play→edit round trip. Changes to gameplay rules have no automatic check against existing levels.

### What an edit costs today, and what it could cost

| Edit | Today (Debug, Visual Studio) | After the recommendations |
| --- | --- | --- |
| Nothing (rebuild and launch) | ~29 s content staging, then launch → title → Continue | < 1 s, launch straight into the last location |
| One shader line (`atmosphere.frag.glsl`) | 1 `glslc` (0.1 s) + ~29 s staging + restart + navigate | Saved file visible in the running game in < 1 s |
| One fog/water/lighting value | 4–112 TU recompile, 78 relinks with `ALL_BUILD`, ~29 s staging, restart, then copy tuned values back into a header by hand | Drag a slider; the value persists and ships |
| One `Rules.cpp` line | 1 TU + 78 relinks (`ALL_BUILD`) + ~29 s staging + restart | 1 TU + 2–3 links + restart into the same screen |
| Paint a 10-tile wall | 10 clicks, 10 undo records, no redo | One drag, one undo record, redo available |
| Test the level you are editing | Button, then Esc → modal → Stop Testing | One key each way |

The "after" column combines targets from the acceptance criteria below. It is not a measured result.

## Scope, method, and confidence

- **Build and content:** the working tree was copied into a 2-core Linux container and built with clang 18, Ninja, `-ftime-trace`, full Vulkan configuration, and Debug. Per-file rebuild fan-out comes from the dependency graph and is generator-independent. Absolute times do not transfer to MSVC. The counts do, and the ratios are indicative. PCH and unity experiments were applied only to that copy.
- **Local Windows evidence:** file timestamps and sizes were read from `build/` on the development machine and `.vcxproj` settings were inspected. No Windows build was run, so no MSVC wall-clock timing is claimed except what the timestamps show.
- **Live reload, editor, and content:** source tracing of `Application`, `ApplicationTools`, `LevelEditor`, `LevelEditorDebugUi`, `InputRouter`, `VulkanRenderer`, `VulkanPipelineFactory`, `ContentPipeline`, the manifest and decoration tooling, and recent commit history.
- **Not exercised:** the running game on a GPU, including startup time, frame time in Debug, and pipeline-recreation cost; the Visual Studio IDE; MSVC Hot Reload.

Evidence labels:

- **Measured:** timed or counted in the reproduction, or read from local build artifacts.
- **Source-confirmed:** the code path establishes the behavior; not timed.
- **Estimate:** an expected effect, to be confirmed by the acceptance measurement.

Priorities:

- **P1:** saves the most time per iteration relative to effort.
- **P2:** clear payoff, more work or narrower use.
- **P3:** worthwhile after P1/P2, or a spike to answer a question first.

Supporting data: [build impact](evidence/build-impact.md), [content staging](evidence/content-staging.md), and the [fan-out script](evidence/measure-header-impact.sh).

## Recommendations

### Build and content pipeline

#### DI-01 — Make content staging incremental and cached

**P1 · Measured · Content pipeline**

**Location:** `CMakeLists.txt:695-707` (`sokoban_content ALL`, no outputs); `ContentPipeline.cpp:1286-1380` (`stageContent`); `CMakeLists.txt:130-144` (`sokoban_bc7enc16`, no optimization override).

`sokoban_content` has no declared output, so it runs on every build. Each run deletes the staging tree, copies all 293 files, BC7-encodes all 62 textures on one thread, validates, and swaps directories. In Debug the encoder is unoptimized.

| Content tool | Time |
| --- | ---: |
| Debug (today) | 24.8 s (cloud) / 27.6 s encode window (local Windows) |
| Debug, only `bc7enc16` at `-O2` | 11.2 s |
| Release | 6.9 s (cloud) / 6.0 s (local Windows) |
| Inventory and validation only | 0.1 s |

Almost all of the time is BC7 encoding of textures that have not changed.

**Change:**

1. Compile `sokoban_bc7enc16` optimized in every configuration (`/O2`, or `-O2` on GCC/Clang). This one line takes Debug from 24.8 s to 11.2 s and is safe to land first.
2. Cache compressed artifacts outside the output tree, e.g. `${CMAKE_BINARY_DIR}/content-cache/`. Key them by a hash of the source bytes, the `TextureInterpretation`, and an encoder-version constant. On a hit, copy or hardlink the cached KTX2 file instead of encoding.
3. Encode cache misses in parallel. `TaskSystem` already exists; each texture is independent.
4. Give the target a stamp output and a depfile, or `DEPENDS` on a manifest-derived file list, so a no-op build skips the tool entirely. If that is awkward with glTF-discovered dependencies, have the tool compare the new inventory and hashes against the previous `content.index` plus the cache, and exit early without touching the output tree.
5. Replace the full delete-copy-swap with a sync: copy changed files and delete removed ones. Keep the current atomic swap for the shipping preset, where the package gate depends on it.

**Acceptance:**

- A no-op `cmake --build --target sokoban` spends < 1 s in content staging on the development machine.
- Changing one texture re-encodes exactly that texture.
- Changing only a `.scr` re-encodes nothing.
- A cold-cache stage produces a `content.index` and file set byte-identical to today's output.
- The shipping package gate and `content_pipeline` tests pass unchanged.
- An editor-painted texture still invalidates correctly. The cache key is content-based, so a repainted splat map misses the cache.

#### DI-02 — Add a developer build preset that builds only what you run

**P1 · Measured + Source-confirmed · Build**

**Location:** `CMakePresets.json` (no development preset); `HANDOFF.md` "Build and validation" (the documented loop builds `ALL_BUILD`); `build/*.vcxproj` (Visual Studio generator, no `MultiProcessorCompilation`).

The documented loop is `cmake --build build --config Debug`. That builds the game, the content tool, every test executable, and the content step. The local projects come from the Visual Studio generator. None of the 95 generated `.vcxproj` files requests `/MP`. Unless the machine enables MSBuild's multi-tool task scheduling globally, files within one project, such as the 90 in `sokoban_core`, compile in a single `cl.exe` process. The `/FS` comment at `CMakeLists.txt:17` assumes `/MP` may be in use, but nothing turns it on.

**Change:** add a `dev` configure preset:

- Ninja generator with MSVC from the VS developer environment. Visual Studio's "Open Folder" uses CMake presets natively.
- `CMAKE_BUILD_TYPE=Debug`.
- A `dev` build preset whose `targets` is `sokoban`.

Add a `dev-tests` build preset that builds the test targets and a matching test preset. Ninja parallelizes every translation unit across targets and has a much cheaper up-to-date check than MSBuild over 95 projects. If the Visual Studio generator stays, add `/MP` to Debug compile options for MSVC. `/FS` is already present. Update README and HANDOFF to describe the two loops: run the game, and run the tests.

**Acceptance:** before and after timings on the development machine for (a) a no-op build, (b) touching `Rules.cpp`, and (c) touching `WaterConfig.hpp`, recorded under `evidence/`. `ctest --preset dev-tests` runs the full registry. CI is unchanged.

#### DI-03 — Stop relinking 76 test programs for every core change

**P1 · Measured · Build**

**Location:** `CMakeLists.txt:779-806` (`sokoban_add_test`: one executable per suite); `tests/TestHarness.hpp` (each suite owns `main()`).

Touching any `sokoban_core` source relinks `sokoban`, `sokoban_content_tool`, and 76 test executables. In the cloud build those links summed to 15.7 s. On Windows the output is heavier. `build/Debug` holds 79 test executables (359 MB) and their PDBs (3,664 MB), all rewritten when core changes and `ALL_BUILD` runs. DI-02 keeps tests out of the inner loop. This item makes the test loop itself cheap.

**Change:** link suites into a small number of runner executables grouped by dependency: `sokoban_core_tests`, `sokoban_ui_tests`, and `sokoban_vulkan_tests`.

- Rename each suite's `main()` to a registered entry point. A `SOKOBAN_TEST_SUITE(name)` macro in `TestHarness.hpp` can do this mechanically.
- Reset `failures` and `checks` per suite.
- Register each suite as its own CTest test (`add_test(NAME rules COMMAND sokoban_core_tests --suite rules)`), so names, labels, timeouts, and `-R` filtering are unchanged.
- Carry per-suite compile definitions (`SOKOBAN_TEST_ASSET_DIR`, `SOKOBAN_ENABLE_TEST_HOOKS`, shader directories) as source-file properties.
- Carry per-suite environment variables (`ASSETS_ENV`) as test properties.

Keep `sokoban_add_test`'s guard against compiling production sources. The runner separates suites by process when CTest runs them individually, so isolation is preserved.

**Acceptance:**

- The CTest registry keeps the same 80 names.
- `ctest -R rules` runs only the rules suite.
- Touching `Rules.cpp` relinks at most the game, the content tool, and three runners.
- A failing suite still reports its test name and line.
- Sanitizer, fuzz, and headless presets still work.

#### DI-04 — Precompiled headers for the first-party libraries

**P2 · Measured · Build**

**Location:** `CMakeLists.txt` (`sokoban_core`, `sokoban_ui`, `sokoban_render_vulkan`, `sokoban`, and the DI-03 runners).

Parsing dominates compilation: 88% of first-party compile time is frontend. The heaviest headers are standard ones (`<filesystem>`, `<cmath>`, `<chrono>`, `<string>`, `<format>`) plus `Math.hpp` and `RenderTypes.hpp`. A PCH of 20 standard headers plus `engine/Math.hpp` built cleanly with no source changes and cut a clean core+ui build from 66.2 s to 40.7 s (−39%). A unity build was faster (30.3 s) but broke on three anonymous-namespace collisions, and it penalizes incremental edits.

**Change:** add `target_precompile_headers` with a shared standard-library list per library. Exclude `MiniaudioImpl.cpp`, `VulkanMemoryAllocatorImplementation.cpp`, `bc7enc16.c`, and the ImGui sources. Add `<vulkan/vulkan.h>` for `sokoban_render_vulkan` and the application. Do not put first-party config or tuning headers in the PCH, or every tuning edit rebuilds the PCH. Optionally fix the three unity collisions (`lowercase`, `alignUp`, and the `json` alias) and offer `CMAKE_UNITY_BUILD` as a CI-only switch.

**Acceptance:** clean Debug and Release warnings-as-errors builds pass on MSVC, GCC, and Clang, and so do the headless, sanitizer, and clang-tidy jobs. Clang-tidy may need `SKIP_PRECOMPILE_HEADERS` on the tidy build. Record a clean-build and a `Math.hpp`-touch timing before and after on the development machine.

#### DI-05 — Keep tuning headers from fanning out

**P2 · Measured · Build**

**Location:** `render/RenderTypes.hpp:7` (includes `WaterConfig.hpp`) and `:454-463` (default member initializers); `RenderTypes.hpp:3` (includes `LevelCatalog.hpp`, which pulls `<filesystem>` into 112 TUs).

Editing one water constant recompiles 112 TUs, because `RenderTypes.hpp` is widely included and default-initializes `WaterRendering` from `config::` constants. Fog (4 TUs) and particles (6) are well contained. Water is the outlier. `LevelCatalog.hpp` in `RenderTypes.hpp` makes `<filesystem>` the second-heaviest header in the build.

**Change:**

- Move the `WaterRendering` defaults into a `.cpp` (`defaultWaterRendering()`), used by `PresentationSettings` and the Reset button, so `RenderTypes.hpp` no longer includes `WaterConfig.hpp`.
- Include only what `RenderTypes.hpp` needs from `LevelCatalog.hpp`. It likely needs `LevelLocation`; move that into a small header without `<filesystem>`.
- Move `nlohmann/json.hpp` out of `PlayerProfileMigrations.hpp`, the only first-party header that includes it.

DI-08 reduces how often these headers are edited at all. This item makes the remaining edits cheap.

**Acceptance:** touching `WaterConfig.hpp` recompiles ≤ 5 TUs, and touching `LevelCatalog.hpp` recompiles fewer than half its current 122 TUs, both measured with the fan-out script.

#### DI-06 — Compiler cache and embedded debug info

**P3 · Source-confirmed · Build**

**Location:** `CMakeLists.txt:9-22` and `:101-113`; `.github/workflows/required-tests.yml` (no compiler cache).

No compiler launcher is configured locally or in CI. For MSVC, `sccache` needs `/Z7` (embedded) debug information. Embedded debug info also removes the shared-PDB serialization that `/FS` exists to tolerate.

**Change:** in the `dev` preset, set `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded` and `CMAKE_CXX_COMPILER_LAUNCHER=sccache` when sccache is found. In CI, cache ccache/sccache directories keyed by compiler and preset. Keep the shipping preset's `ProgramDatabase` contract unchanged.

**Acceptance:** a branch switch and back rebuilds mostly from cache (hit rate reported). Shipping PDB separation still passes the package gate.

### Live reload

#### DI-07 — Hot-reload shaders

**P1 · Source-confirmed + Measured · Renderer**

**Location:** `VulkanPipelineFactory.cpp:184-186` (modules read from `assetRoot/shaders` at pipeline creation); `VulkanRenderer.cpp:1633-1644` (the pipeline-only reconfiguration branch: build replacements, retire old pipelines after their frames complete); `CMakeLists.txt:264-298` (glslc flags).

Four of the last five commits changed shaders (`atmosphere.frag.glsl` three times, and `ssao*.frag.glsl`). Each iteration today is 0.1 s of `glslc`, then ~29 s of content staging, then restart and navigation. The renderer already knows how to replace every pipeline without a device wait. The wireframe toggle uses that path.

**Change (Debug and developer builds only):**

- Pass the shader source directory, `GLSLC_EXECUTABLE`, and the exact glslc flags to the application as compile definitions. Generate them from the same CMake list so they cannot drift.
- Poll mtimes of `shaders/*.glsl` and `shaders/include/*.glsl` about twice a second. On change, compile the affected modules to a dev shader directory. An include change recompiles every module, as CMake does.
- On success, point `VulkanPipelineFactory` at that directory and enqueue a pipeline-only reconfiguration (a `shaderRevision` field in the reconfiguration plan).
- On failure, keep the current pipelines and show the glslc diagnostics in the Log tab and as a one-line overlay.
- Add an "F6: reload shaders" binding for manual use.

The next normal build still stages the compiled modules, so a restart keeps the change.

**Acceptance:**

- Saving `atmosphere.frag.glsl` changes the running image within 1 s.
- A syntax error leaves the previous image running and reports file:line.
- 50 consecutive reloads run under validation without errors or leaked pipelines. Check `pipelineRebuilds_` and the retirement count.
- `gpu_abi` still runs against the build-tree modules.

#### DI-08 — Live, persistent tuning for `config::` constants

**P1 · Source-confirmed · Tooling**

**Location:** 240 `inline constexpr` values across 13 `*Config.hpp` headers (`FogOfWarConfig.hpp` 17, `LightingConfig.hpp` 50, `WaterConfig.hpp` 44, `ParticleConfig.hpp` 44, …); `ApplicationDebugUi.cpp:186-195` (water: "Live renderer tuning; reset restores WaterConfig defaults."); `SettingsCoordinator.cpp:109-118` (only AO and exposure are persisted).

The three most recent commits all tune fog. Each one edits `FogOfWarConfig.hpp` and threads the value through `VulkanSceneRecorder.cpp`, then rebuilds and restarts. Fog-of-war has no ImGui controls. Water, lighting, and atmosphere do, but those edits are lost on exit: `PresentationSettings::water` and most of `lighting` are never persisted. A good value found with a slider has to be copied into a header by hand and rebuilt.

**Change:** add a small typed tuning registry.

- A `SOKOBAN_TUNABLE(group, name, type, default, min, max)` declaration compiles to a `constexpr` in shipping builds. In Debug and developer builds it reads a registry slot.
- The registry drives an auto-generated ImGui panel per group, with sliders, color pickers, and reset-to-default.
- Values persist to a checked-in `assets/tuning.json`. The shipping build compiles it in, for example via a configure-time generated header, so what you tuned is what ships.
- Migrate fog-of-war first, then water and lighting. Their existing `PresentationSettings` defaults become registry defaults.
- Values that feed push constants or uniforms need no pipeline rebuild. Values that change pipeline state or sample counts (such as `fogOfWarSampleCount`) are marked `restart`/`rebuild` and are not live.

**Acceptance:**

- Every fog-of-war value is editable live and survives a restart.
- A shipping build's compiled values equal `tuning.json`, checked by a test.
- The Release/shipping build contains no registry lookups for these values; inspect the recorder's disassembly or use a `static_assert` on the `constexpr` path.
- Editing a tunable's value no longer recompiles anything.

#### DI-09 — Start where you left off

**P2 · Source-confirmed · Application**

**Location:** `Application.cpp:225` (always `openTitleScreen()`); `CommandLineOptions.hpp` (no location or editor options); `ApplicationTools::initialize` (the editor opens the campaign's current screen, not the last edited document).

Every restart returns to the title screen, the Continue flow, and then the editor must be re-pointed at the document you were editing.

**Change:**

- Add developer command-line options: `--continue` (load the active slot and skip the title), `--level N --screen M`, `--overworld`, and `--edit <path>` (open that document in the editor view).
- Persist a small developer session file next to `imgui.ini` with the last edited document, tool, active layer, and editing-vs-playing view. Restore it in Debug by default.
- Document a `launch.vs.json` entry with `--continue` in the README, since `.vs/` is ignored.

**Acceptance:** launching with `--edit levels/level3/screen2.scr` shows that document in the editor with no menu interaction. A plain Debug launch restores the last session. Release and shipping ignore the session file.

#### DI-10 — Reload source assets and levels without restarting

**P2 · Source-confirmed · Content/Editor**

**Location:** `Application::screenPath` (reads the staged runtime tree); `AssetManifestDebugUi.cpp:176-177` with `AssetManifestEditor::save` (writes source and runtime manifests and refreshes the index, but nothing applies the saved document to `Application::assetManifest_`); `DecorationAssetRegistry.cpp:334` ("Restart the editor to reload the upgraded model."); `VulkanRenderer::updateTexture` and `invalidateCompressedTextureArtifactsForSource` (already used by the splat painter).

The game runs from the staged tree. A `.scr` edited in a text editor, a PNG re-exported from an image editor, or a manifest field changed in the Asset Manifest tab does nothing until the next build (~29 s today) and a restart.

**Change (Debug and developer builds):** a polling watcher over source `levels/`, the manifest, the animation catalog, and manifest-referenced texture paths.

- **Levels:** mirror the changed file into the runtime tree (the project store already does this for editor saves) and reload the current screen if it is the one that changed and the editor has no dirty draft for it.
- **Textures:** copy, invalidate prepared artifacts, and call `updateTexture`. This follows the path the splat painter already uses.
- **Manifest:** apply non-structural fields live (tile visuals, material behavior, sound volume). Show "restart required" in the Asset Manifest tab for structural changes instead of saving silently.
- **Models:** replacing an existing model's geometry is the most expensive piece, because `VulkanModelResources` publication and retirement would have to support replacement. Per the HANDOFF guidance on that class, leave it until the other three are in use.

**Acceptance:** each asset kind has a test covering its reload path, headless where possible. A texture overwrite shows in the next frame, a `.scr` edit reloads the screen, and a manifest volume change is audible immediately, all without restart and without validation errors.

#### DI-11 — Spike: C++ hot reload and an optimized tools configuration

**P3 · Source-confirmed · Build**

**Location:** `CMakeLists.txt:525` (developer tools compile only in the `Debug` configuration); `CMakeLists.txt:16-19` (`/experimental:deterministic`, `/pathmap`); local Debug uses `ProgramDatabase` (`/Zi`) and `/RTC1`.

The editor and all tuning UI exist only in unoptimized Debug builds with `/RTC1`. The content tool alone runs 3.6× slower in Debug than in Release. MSVC Hot Reload (Edit and Continue) needs `/ZI` and incremental linking. It may conflict with `/experimental:deterministic` and `/pathmap`, which the project sets for every configuration.

**Change:** time-boxed spikes:

- (a) allow developer tools in `RelWithDebInfo` by extending the generator expression at `:525` and the `SOKOBAN_SOURCE_*` definitions, giving an optimized "develop" configuration;
- (b) in the `dev` preset only, try `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=EditAndContinue` without the determinism flags, and record which edits Hot Reload can apply to `GameplayPresentation.cpp` and `Rules.cpp` while the game runs.

**Acceptance:** a short note under `evidence/` with the result and cost of each spike. Adopt (a) if the editor works unchanged. Adopt (b) only if it applies function-body edits reliably.

### Level editor

#### DI-12 — Drag painting with one undo record per stroke, plus redo

**P1 · Source-confirmed · Editor**

**Location:** `InputRouter.hpp:59-61` ("Tile placement wants this, so one click places one tile"); `ApplicationTools.cpp:718-747` (paint and erase only on `primaryPressed`); `LevelEditor.hpp:179` (`tryUndoEdit` only; no redo); `OverworldMapEditor.hpp:70` (the topology editor already has `redo()`).

Painting a ten-tile wall takes ten clicks and creates ten undo records. There is no redo, so an accidental undo is lost work.

**Change:**

- Add a stroke session to `LevelEditor` modeled on `beginSelectedDecorationTransform`/`endSelectedDecorationTransform`: capture one snapshot at mouse-down, apply `paintCell` or `eraseCell` for each newly entered cell while the button is held (the resolved target, deduplicated per stroke), and record one `EditActionRecord` on release.
- Shift constrains the stroke to a line.
- An optional rectangle mode fills the drag's bounding box on the active layer.
- Add a redo stack, cleared on any new edit and remapped with `editHistory_` in `applyScreenIdentityRemaps` (`LevelEditor.cpp:2740-2753`) and in the draft cache.

**Acceptance:**

- A drag paints every crossed cell once.
- One undo removes the whole stroke; redo restores it.
- Redo survives switching documents through the draft cache, and a structural renumber (the existing CQ-01 identity tests extended with redo).
- Single-click behavior is unchanged.
- Move, replace, and delete modifiers compose with drag.

#### DI-13 — Keyboard shortcuts and a one-key play/edit round trip

**P1 · Source-confirmed · Editor**

**Location:** `LevelEditorDebugUi.cpp:161-185` (Load, Save, Play Draft, and Return are buttons only); `InputBindings.hpp:23-25` (the only editor actions are the Replace/Delete/Move modifiers); `ApplicationTools.cpp:469-497` (leaving a draft always opens a "Stop testing?" modal); the layer is chosen only through a slider.

The playtest loop is: click Play Draft, play, press Esc, click Stop Testing. Saving and changing layers need the mouse.

**Change:** add remappable editor actions to the existing Editor Controls page:

- Save: Ctrl+S.
- Play/stop draft: F5. Stop returns to the editor immediately. Keep the confirmation only when Esc is pressed during a draft that has been solved, or drop it.
- Redo: Ctrl+Y or Ctrl+Shift+Z.
- Layer up/down: PageUp/PageDown.
- Eyedropper: Alt+click selects the hovered tile type.
- Recent tiles: 1–9 select from a most-recently-used strip.
- Tools: Tab cycles Tiles, Decorations, and Selectors.
- Toggle layer lock.

Suppress these while an ImGui text field has focus. Add "play from cursor": start the draft with the active hero on the hovered cell, which skips walking to the area under test.

**Acceptance:**

- Every action appears in Options > Controls > Editor Controls and can be rebound.
- F5 toggles play and edit without a modal, and preserves the editor tool, layer, and selection.
- Play-from-cursor rejects invalid cells with a status message.
- Existing `input_router` and `level_editor` tests are extended for the new actions.

#### DI-14 — Record solutions and replay them in CI

**P2 · Source-confirmed · Content/Test**

**Location:** `levels/` (18 puzzle screens and 4 overworld screens); the tests that read levels exercise the catalog, project store, and editor, not solvability. Recent commits change core rules: enemies as pushable blocks, the bard aura, walking into water, the witch.

A rule change can make an existing puzzle unsolvable, or trivially solvable, without any test failing. Today the only check is replaying levels by hand.

**Change:**

- When a draft or campaign screen is solved in a Debug build, offer "Save solution". It writes the move sequence, including hero-cycle and interact actions, to `screenN.solution` next to the `.scr`.
- Add a headless CTest suite that loads each screen that has a solution, replays it through `GameplaySession`, and asserts the solved state. On failure it reports the screen, the step index, and the first diverging entity.
- The content pipeline may warn about screens that have no solution. It should not fail on them.
- Optional: a "Replay solution" button in the editor for watching the recorded run.

**Acceptance:** all current puzzle screens have recorded solutions. The suite runs in under 5 s. Deliberately breaking a rule, for example making rocks unpushable, fails the suite with a readable message.

#### DI-15 — Editor view and library quality-of-life

**P3 · Source-confirmed · Editor**

**Location:** `RenderFrameBuilderEditor.cpp:143-155` (the editor camera always fits the gameplay extent); `LevelEditorDebugUi.cpp:528-560` (the decoration library is a text list; tiles have baked thumbnails through `VulkanThumbnailPass`).

**Change:**

- Add editor camera controls: scroll to zoom, middle-drag to pan, Q/E to rotate 90°, and a top-down toggle. These affect only the editor view and never persisted camera framing.
- Add lazily baked mesh thumbnails for decoration library entries, using the existing thumbnail pass and cached under the build tree.
- Keep the text filter.

**Acceptance:**

- The camera resets when switching documents.
- Picking remains correct at every zoom and rotation (extend `ground_pick` tests).
- Thumbnails bake off the critical path and never block a frame for more than one thumbnail.

## Implementation order

1. **Packet A — stop paying for nothing:**
   - DI-01 step 1 (optimized encoder), then the remaining DI-01 steps.
   - DI-02 (dev preset).
   - DI-03 (test runners).

   Record before and after timings on the development machine. The expected result is a Debug no-op build dropping from ~29 s to about 1 s, and a core `.cpp` edit relinking 2–5 executables instead of 78. This is an estimate until measured.
2. **Packet B — stop restarting:** DI-07 (shaders), DI-08 (tuning registry, fog first), DI-09 (start where you left off).
3. **Packet C — editor basics:** DI-12 (drag, stroke undo, redo), DI-13 (shortcuts, F5 round trip, play from cursor).
4. **Packet D — deeper speedups and safety:** DI-04 (PCH), DI-05 (header fan-out), DI-14 (solution replay), DI-10 (asset and level watcher).
5. **Packet E — spikes and polish:** DI-11, DI-06, DI-15.

Packets A and B are independent and can proceed in parallel. DI-08 reduces the value of DI-05 for tuning headers but not for `RenderTypes.hpp`, `Math.hpp`, or `TileTypes.hpp`.

## Validation performed

| Check | Result | Evidence |
| --- | --- | --- |
| Full Debug configure and build, clang 18 + Ninja, all targets | Built. The first `sokoban_content` run failed only because the measurement copy lacked vendor notice files; it passed once they were copied | [build impact](evidence/build-impact.md) |
| Content staging: Debug, Debug with optimized encoder, Release, validate-only | 24.8 s / 11.2 s / 6.9 s / 0.1 s | [content staging](evidence/content-staging.md) |
| No-op and incremental wall-clock scenarios | 25.2 s no-op; 29–35 s per small edit, ~25 s of it content | [build impact](evidence/build-impact.md) |
| Per-file rebuild fan-out (17 files) | 1–147 TUs; 78 relinks for any core change | [build impact](evidence/build-impact.md), [script](evidence/measure-header-impact.sh) |
| `-ftime-trace` aggregate over 221 TUs | 88% frontend; top headers and instantiations listed | [build impact](evidence/build-impact.md) |
| PCH and unity experiments (core+ui clean) | 66.2 → 40.7 s (PCH); 30.3 s with 3 collisions (unity) | [build impact](evidence/build-impact.md) |
| Local Windows build artifacts (timestamps, sizes, `.vcxproj` settings) | 27.6 s Debug BC7 window; 3.7 GB of test PDBs; no `/MP` | [content staging](evidence/content-staging.md) |

Not run: any Windows or MSVC build, the game on a GPU, the Visual Studio IDE, or Hot Reload. MSVC timings for DI-02, DI-03, and DI-04 are part of their acceptance criteria, not claims of this review.
