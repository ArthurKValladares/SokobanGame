# Packet D implementation evidence

Implemented September 25, 2026, on top of packet C and the rebindable
editor shortcuts. Verified in the same 2-core Linux container as the earlier
packets (GCC 13 and clang 18, Debug and Release, with lavapipe under Xvfb).
No Windows, MSVC or GPU run was possible from there.

## What changed

- **DI-05, header fan-out.**
  - `render/WaterGeometry.hpp` holds the four water constants that
    geometry and gameplay presentation read (depth below ground, exterior
    margin, drowned-player depth). `WaterConfig.hpp` includes it, and the
    files that only needed those constants include it instead.
  - `RenderTypes.hpp` no longer includes `WaterConfig.hpp` or
    `LevelCatalog.hpp`. `WaterRendering` members are zero-initialized, and
    `RenderFrameData::defaultWaterRendering()` in the new `RenderTypes.cpp`
    fills in the `config::water*` defaults. `PresentationSettings`, the
    Water tab's Reset button and the evidence capture use it.
  - `LevelLocation` moved to `engine/LevelLocation.hpp`, which has no
    `<filesystem>`. `Level.hpp`, `CampaignSession.hpp`, `PlayerProfile.hpp`,
    `SaveSlotManager.hpp`, `SplatPainter.hpp` and
    `LevelAssetAssociations.hpp` include it instead of `LevelCatalog.hpp`.
  - The review's third point (`nlohmann/json.hpp` in
    `PlayerProfileMigrations.hpp`) went away with that file when profile
    migrations were removed.
- **DI-04, precompiled headers.**
  - `SOKOBAN_PRECOMPILED_HEADERS` (default `ON`) and
    `sokoban_enable_precompiled_headers(target [extra headers])`. The list
    is 21 standard headers plus `Math.hpp`; the renderer and the game add
    `<vulkan/vulkan.h>`. It is applied to `sokoban_core`, `sokoban_ui`,
    `sokoban_render_vulkan` and `sokoban`.
  - `MiniaudioImpl.cpp`, `VulkanMemoryAllocatorImplementation.cpp` and the
    ImGui sources skip it. `bc7enc16.c` is C, and the PCH is CXX-only.
  - clang-tidy builds switch it off automatically.
  - The shared test runners do not use it. Every suite source is compiled
    with `-Dmain=sokobanTestSuite_<name>`, and GCC refuses a PCH built
    without that define ("not used because `main' is defined"), which
    `-Winvalid-pch` under warnings-as-errors turns into a build failure.
    A per-suite PCH would cost more than it saves.
  - Not done: the optional unity-build fixes.
- **DI-14, recorded solutions.**
  - `engine/Solution.{hpp,cpp}`: the text format, `levelDigest`, a
    `Driver` that applies one input and waits until the hero and every
    slide, conveyor and enemy reaction has settled, `record` and `replay`.
  - `GameplaySession` gains `PlayerInput`, `inputLog()` (the inputs that
    started actions since the last restart or restore) and `resetToState`
    for the tools.
  - `tests/SolutionReplayTests.cpp` (`solution_replay`): format round trip,
    digest behavior, failure messages, and a replay of every file in
    `solutions/` against the screen it matches.
  - `tools/SolveLevel.cpp` (`sokoban_solve_level`) searches for solutions
    over the real rules through the same driver. Walks are collapsed: one
    search step is a push, a hero cycle or an interaction from anywhere
    the heroes can walk to. `--best-first` orders by moves plus uncovered
    plates and distance to an End.
  - In Debug, every solve of a campaign screen or an editor draft is
    recorded automatically on a worker thread through
    `solution::reconcileStore` (`engine/SolutionStore.hpp`). It keeps the
    shortest run per screen as `solutions/level<L>-screen<S>.solution`,
    keeps unsaved-draft runs in `solutions/drafts/` until the draft is
    saved, and parks a screen's old recording there when the screen is
    edited. The first version had a **Save Solution** button instead; it
    was hard to find, so the button was replaced by automatic saving.
  - Deviations from the review: files live in the repository's
    `solutions/` directory, not next to the `.scr`, because the content
    pipeline rejects unexpected files in `levels/levelN/`. They are matched
    by content digest, so renumbering screens does not orphan them. The
    missing-solution warning is printed by the test rather than by the
    content pipeline. The optional in-editor replay button was not built.
- **DI-10, source watcher.**
  - `SourceWatcher` polls size and modification time. It watches the
    source `levels/` tree (`.scr`, `.json`), the manifest, the animation
    catalog and every texture the manifest names.
  - `Application::serviceSourceWatcher` runs every 500 ms in Debug
    developer builds, after shader hot reload, and never in smoke or
    evidence runs.
  - Levels: mirrored into the staged tree and the content index refreshed.
    The editor reloads the file if it is the open, unmodified document
    (`LevelEditor::reloadFromDisk`), and the current puzzle screen restarts
    from the new layout. The screen's stale checkpoint is dropped first, so
    the restart does not log a failed restore.
  - Textures: mirrored, prepared artifacts invalidated, and
    `VulkanRenderer::updateTexture` called with the new pixels.
  - Manifest: `AssetManifest::adoptLiveFields` applies tile scales and
    sound-set and music volumes, then the game reapplies tile scales and
    volumes. Any other difference leaves the running manifest alone and
    shows "needs a restart" in the Asset Manifest tab. Reverting the edit
    clears the message.
  - Animation catalog: reloaded unless the Animation tab has unsaved edits.
  - Models are not reloaded, as the review advised.

## Measurements

### DI-05 fan-out

GCC Debug tree, the review's `measure-header-impact.sh` method (touch one
file, dry-run a copy of `build.ninja` without the regeneration edge):

| Header touched | TUs recompiled before | After |
| --- | ---: | ---: |
| `render/WaterConfig.hpp` | 111 | 5 |
| `LevelCatalog.hpp` | 126 | 14 |
| `render/RenderTypes.hpp` | 111 | 112 |
| `Math.hpp` | 149 | 150 |
| `render/WaterGeometry.hpp` (new) | n/a | 10 |
| `LevelLocation.hpp` (new) | n/a | 127 |

Both acceptance targets are met: at most 5 TUs for `WaterConfig.hpp`, and
fewer than half of the previous count for `LevelCatalog.hpp`. The "before"
counts are from this tree before the change, so they differ slightly from
the review's (the codebase grew in packets A to C). `LevelLocation.hpp`
reaches as many TUs as `LevelCatalog.hpp` used to, but it is a
ten-line struct that should rarely change.

### DI-04 build time

First-party `sokoban_core` plus `sokoban_ui` rebuilt from scratch (object
files and PCH deleted, third-party libraries already built), Debug,
warnings as errors, `-j2`, two rounds each:

| Compiler | PCH off | PCH on | Change |
| --- | ---: | ---: | ---: |
| clang 18 | 121.2 s, 116.6 s | 68.6 s, 70.2 s | −41% |
| GCC 13 | 128.2 s, 125.6 s | 114.1 s, 113.1 s | −10% |

Touching `Math.hpp` and rebuilding core plus ui with clang took 65.8 s with
the PCH (the PCH is rebuilt, then every TU) and 93.5 s without it.

The absolute times are higher than the review's 66.2 s baseline. The cause
was not investigated (the container's speed varies between sessions, and
the code has grown since). The relative clang gain matches the review's
−39%.

GCC gains little: its `.gch` files are large and loading one per TU costs
much of what it saves. clang's PCH is cheaper to load, which matches the
review's clang experiment. MSVC was not measured; its acceptance timing is
still open.

## Live checks

Run in the Debug game under Xvfb with `--level 0 --screen 0`. Every source
file was restored afterwards, and `git status` shows `levels/` and
`assets/` clean.

| Edit made outside the game | Log line | Result on screen |
| --- | --- | --- |
| Add a rock to `levels/level0/screen0.scr` | "Reloaded screen0.scr in the editor after it changed on disk." then "Reloaded level 0 screen 0 after its source changed." | The rock appears and the hero restarts. No checkpoint warning after the fix above. |
| Tint `rogue_texture.png` red | "Reloaded texture Rogue" | The Rogue turns red in the next frames. Restoring the file restores it. |
| Change the footsteps volume in `manifest.json` | "Applied manifest.json live (tile scales and volumes)." | Not audible in the container (no audio device). |
| Remove one footstep sound from its set | "manifest.json changed in a way that needs a restart ..." | Message shown in the Asset Manifest tab. |
| Solve level 0 screen 0 with its recording removed | "Saved the 2-step solution of level0/screen0 as level0-screen0.solution." | File recreated, identical to the removed one. |
| Solve it again in 4 inputs | "Solved level0/screen0 in 4 inputs; kept the stored 2-step level0-screen0.solution." | File unchanged. |

The first level check logged "Discarded invalid gameplay checkpoint" before
each reload, because the saved checkpoint described the old layout. The
reload now drops that checkpoint first. (While re-checking, a game stopped
with `SIGTERM` kept running and kept writing to the same log (it did
not exit on `SIGTERM` in that run). That was a harness mistake, not a watcher
bug.)

## Recorded solutions

17 of 18 puzzle screens have a recording, found by `sokoban_solve_level`.
Replaying all 17 takes 0.05 s in a Debug build, well inside the 5 s budget.

**Level 3 screen 2 has no recording.** The search gave up without a solution
twice: breadth-first after 1.5 million positions (36 minutes), and
best-first after 5 million positions (Release build). It has three plates,
one of them on the End's island, both islands cut off by water, and only
three rocks, so a solution probably needs mirror copies of the hero standing
on plates as well as rocks used as bridges. Either the search is too small
or the screen is no longer solvable under the current rules. It has since
been confirmed solvable by hand, so it will be recorded automatically the
next time it is solved in a Debug build. See README.md > Solutions for
ideas on making the solver strong enough for screens like this.

Deliberately breaking a rule (rocks made unpushable in `Rules.cpp`, then
restored) fails the suite with messages such as:

```text
level4/screen0 (level4-screen0.solution): step 2 of 9 (right): player 1 should be at (3, 3, 1) but is at (2, 3, 1)
level4/screen2 (level4-screen2.solution): step 5 of 22 (left): movable 2 should be at (5, 3, 1) but is at (6, 3, 1)
```

Content finding: **level 3 screen 3 could be solved in three moves** by
walking left into one of its two Ends, never using the mirrors. That was a
rule gap, not a level bug: a screen was won when every hero stood on an
End, even with other Ends empty. `rules::isAtUnlockedEnd` now also requires
every End to hold a living hero (a rock on an End does not count), covered
by `RulesTests::testEveryEndMustHoldAHero`. The solver's estimate counts
the heroes still missing for the Ends. Re-solved under the new rule,
level 3 screen 3 takes 17 steps and uses a mirror to make a second hero; no
other recording changed.

## Tests and builds

- GCC Debug (warnings as errors): 85 of 85 tests pass under Xvfb and
  lavapipe.
- clang Debug (warnings as errors): 85 of 85.
- `headless-tests`: 78 of 78.
- GCC Release: 85 of 85.
- New suites: `solution_replay` (format, digest, failure messages, replay of
  all recordings) and `source_watcher` (priming, changes, new files in a
  watched tree, deletions not reported, re-watching). Added cases:
  `AssetManifestTests::testLiveFieldsAdoptOnlyWhenNothingStructuralChanged`
  and `LevelEditorTests::testReloadFromDiskKeepsDraftsAndIgnoresOwnSaves`.
- Follow-up: `SolutionReplayTests::testStoreKeepsOneShortestRecordingPerScreen`
  covers the store (first save, shorter replaces longer, longer is kept
  out, unreplayable runs, a draft waiting in `drafts/`, promotion when the
  draft is saved, the old recording parked and restored when the edit is
  undone, and a no-op pass).
- The PCH changed GCC 13's inlining in Release enough to raise a
  `-Warray-bounds` false positive in `ShaderHotReload.cpp` (a conditional
  string literal pushed into a `std::vector<std::string>`). That line now
  builds a `std::string_view` first, and the warning is gone with and
  without the PCH. CI's Release job uses GCC with warnings as errors, so this
  would have failed there.
- Not from this packet: GCC 13 Release also reports
  `-Wmaybe-uninitialized` for `statusesBeforeBardPulse` in `Rules.cpp`. It
  does so at the review's baseline commit too, without the PCH.
