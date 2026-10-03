# Gameplay performance investigation — October 3, 2026

The reported Debug slowdown with the **Level Editor** open is reproducible.
The main cause was repeated overworld-layout file loading and JSON parsing
from the tile palette. That path is now fixed. Particle submission also had a
separate scaling problem, which is now batched. The largest remaining scene
GPU target is water on level 5, screen 5.

## Method and limits

Measurements use an NVIDIA GeForce RTX 4060 Laptop GPU at 2880×1800,
100% render scale, 4x MSAA, D32 scene depth, and Mailbox presentation unless
the case says otherwise. Puzzle cases advance simulation by fixed 1/60 s
steps, use isolated save/preferences directories, warm up before measurement,
and retain the final 120 frame samples. Screens are zero-based, matching the
game's command-line indices. Full captures render 420 frames; quick captures
render 240. Screenshots and encoding are outside the timing window.

These are animated stationary-screen captures, rather than an input replay
of a complete playthrough. The source investigation identifies the current
costly path; it does not establish the first historical commit responsible
for the reported regression.

The machine also entered **software thermal slowdown at 87–88°C** during
sustained runs. The recorded [driver observation](evidence/hardware-thermal-observation.txt)
confirms this. Consequently, later GPU timings and whole-frame improvements
include hardware-state variation. Editor CPU scopes and submission counts
identify the code problems independently. Do not interpret the lower GPU
times after the editor fix as a shader optimization.

The full matrix also recorded one 121.8-second **swapchain image-acquisition
stall** in the witch fixture. Its UI still averaged 4.578 ms; the stall
inflates application/renderer averages rather than demonstrating expensive
witch simulation or shader work. The capture does not establish whether
window visibility or driver/presentation scheduling caused that stall.
Treat the full matrix as coverage and phase evidence, and repeat unstable
timing cases with stable window visibility and hardware state.

## Main regression: Level Editor palette

`LevelEditorDebugUi::drawTilePalette` called `editingOverworld()` up to three
times for each tile definition. `editingOverworld()` called
`overworldScreenIdForPath`, which reopened and parsed `overworld/layout.json`
for both source roots, then normalized and compared every screen path.
Puzzle documents paid this cost despite being outside the overworld.

The palette section took approximately 176 ms per frame in the detailed
capture. Directory enumeration accounted for only about 2.6 ms. Hiding the
workspace reduced screen 3's average application frame to 9.677 ms in the
paired pre-fix control, matching the reported F11 improvement.

The fix rejects paths outside the composed overworld before reading a layout
and evaluates the palette's mode once per draw. It retains live layout reads
for actual overworld documents, so topology edits still take effect without
introducing a stale document cache.

| Debug, Level Editor selected | Application avg / p95 ms | UI avg / p95 ms |
|---|---:|---:|
| [Screen 3 before](evidence/editor-screen3-before.md) | 171.343 / 194.930 | 163.654 / 186.159 |
| [Screen 3 after](evidence/editor-screen3-after.md) | 14.244 / 18.275 | 5.728 / 7.568 |
| [Screen 5 before](evidence/editor-screen5-before.md) | 243.361 / 374.036 | 232.392 / 355.378 |
| [Screen 5 after](evidence/editor-screen5-after.md) | 33.560 / 41.197 | 5.299 / 6.661 |

The post-fix screen 3 trace contains four overworld-identity queries per
frame, approximately 0.61 ms combined, and **no overworld-layout parsing**.
The complete palette costs approximately 0.45 ms. The remaining browser scan
costs approximately 3.26 ms per frame and dominates editor UI work.

Workspace captures now explicitly select **Level Editor**. Merely showing
the workspace was insufficient: its default selected tab had much lower CPU
cost and missed this regression. New panel, palette, browser, identity, and
layout-read CPU scopes make this distinction visible in application traces.
The CPU suite also benchmarks repeated puzzle classification and browser scans.

## Intermittent effects: particle submission

The previous scene recorder issued one quad draw per particle. Textures and
nine-slice parameters were already stored per instance, so particles could
share a draw while retaining their existing alpha-blend order.

The recorder now batches consecutive particle instances and splits at depth
mode changes or nonconsecutive buffer entries. Buffer exhaustion retains the
existing discard behavior. Prepared ordering, depth behavior, and appearance
are preserved.

| Debug effect workload | Total draws before → after | Command recording avg ms before → after | Application avg / p95 ms before → after |
|---|---:|---:|---:|
| [Mixed, 32 emitters, 4,592 particles](evidence/mixed-after.md) | 4,722 → 132 | 15.004 → 7.747 | 26.120 / 29.444 → 18.403 / 21.137 |
| [Portal stress, 1,120 particles](evidence/portals-after.md) | 1,147 → 28 | 4.037 → 2.340 | 7.651 / 9.447 → 5.949 / 7.288 |

The mixed case submits particles in two draws; the portal case needs one.
The normal-scene PNGs are **pixel identical** before and after batching in
both cases. These comparisons precede the Level Editor fix and use the
workspace's default tab; they isolate particle submission. The stress case
is deliberately much heavier than ordinary gameplay and is not a claim of
a 30% improvement across all screens.

## Remaining optimization priorities

1. **Water shading and fill cost on level 5, screen 5.** The controlled
   earlier controlled series below attributes approximately 4.5 ms of the GPU
   frame to water. Reflection sampling explains only part of that cost.
   Profile the procedural surface, noise/normal/ripple work, overdraw, and
   multisample cost before changing quality defaults.
2. **Level Editor browser refresh.** It still enumerates directories and
   parses level metadata every frame. Retain a browser snapshot, invalidate
   after project mutations/root changes, and refresh external edits through
   the existing watcher or a bounded refresh interval. Preserve immediate
   feedback after save, rename, add, delete, and restore.
3. **Particle preparation and per-instance recording at high counts.** After
   draw batching, the earlier mixed stress capture still spent 6.500 ms in
   Debug scene preparation and 7.747 ms in recording. Billboard/ribbon
   projection, ordering, and instance construction are the next CPU targets
   if comparable particle counts occur in real play. Ordinary screens need
   much less work.

| Screen 5 control | GPU frame avg ms | Translucency avg ms |
|---|---:|---:|
| [Normal water](evidence/water-baseline.md) | 7.695 | 4.458 |
| [Authored water removed](evidence/water-disabled.md) | 3.203 | 0.482 |
| [Water reflections disabled](evidence/water-no-reflections.md) | 7.016 | 3.840 |
| [75% scene render scale](evidence/water-scale75.md) | 3.628 | 2.133 |

Later, thermally limited full-suite captures also identify screen 5's
translucency as the dominant pass, but their absolute values are unsuitable
for comparison with this earlier series. Hardware throttling is an additional
cause of sustained slowdown; it needs consistent power/thermal conditions
for trustworthy optimization comparisons.

## Profiling and coverage fixes

- Optional Vulkan phases previously prevented *all* GPU timings from being
  collected. Unwritten fog-volume queries cause `VK_NOT_READY` for a whole
  query-pool read even when frame/scene timestamps are ready. Collection now
  uses availability flags and a recorded-phase mask. Absent phases contribute
  zero; valid frame and phase queries are collected without adding a wait.
- Workspace screenshots now use the display image's actual tracked layout.
  The workspace leaves it shader-readable; assuming transfer-source layout
  caused validation errors during capture.
- Reports measure complete application frames, including editor UI, frame
  construction, and profiler processing. Renderer-only CPU time had hidden
  the expensive editor work. Frame intervals and deliberate pacing are
  reported separately; screenshot readback and the final AO control frame
  cannot contaminate the timing window.
- Effects use production mirror/witch burst definitions, turret muzzle,
  ribbon and impact definitions, portal builders, blurred ice, gate/mirror
  energy, and linked-object auras. Six fixtures run individually and together.
  CPU tests measure emission/simulation/export and preparation separately.
  GPU captures retain a seeded visible phase so brief effects cannot disappear
  during warm-up. Coverage checks require particles to be prepared/drawn and
  special shaders to use loaded models and their actual rendering paths.
- A separate GPU particle phase and draw count distinguish particle work from
  other translucency. Quick and full GPU runs exercise all six effect families.
  Full runs cover all seven level 5 screens and water, reflection, scale,
  1x/8x MSAA, and repeated-baseline controls. Debug adds editor-hidden and
  profiler-disabled comparisons.
- NVIDIA hardware snapshots, when the tool is installed, record clocks,
  temperature, power, and throttling reasons before and after each GPU case.

## Reproduction and artifacts

```powershell
cmake --build out\visual-studio --config Debug --target sokoban sokoban_tests sokoban_vulkan_tests sokoban_performance_tests --parallel 8
.\tools\RunPerformanceSuites.ps1 -BuildDirectory out\visual-studio -Configuration Debug -SkipBuild

# Optimized CPU measurements and the shorter GPU/effect matrix:
.\tools\RunPerformanceSuites.ps1 -BuildDirectory out\visual-studio -Configuration Release -Quick

# A single reproduction, using a fresh save directory:
.\out\visual-studio\Debug\sokoban.exe --smoke-frames 420 --save-directory out\perf-state --evidence-output out\perf-editor --evidence-level 5 --evidence-screen 3 --evidence-debug-ui --evidence-animate --evidence-disable-vsync
```

The full local [Debug matrix](../../../out/performance-investigation/comprehensive-debug/index.md)
and [Release matrix](../../../out/performance-investigation/comprehensive-release/index.md)
link numeric reports, startup measurements, scene/AO PNGs, and CPU traces.
The final [optimized CPU report](../../../out/performance-investigation/cpu-release-final/performance-report.md)
includes the new editor identity/browser workloads and 32-emitter effect
preparation with 40 samples per case.
Those generated artifacts are ignored by Git. Selected raw metric snapshots
are archived in this document's `evidence` directory for review.

Validation includes Debug/Release builds, the expanded performance runners,
headless editor/content tests, effect fixtures, particle/scene/ABI/draw-lane
tests, the warm zero-allocation scene-preparation regression, and Vulkan
smoke coverage for a completed frame with deliberately unwritten optional
timestamp queries. Timing values remain diagnostic rather than CTest limits.

The 14 relevant CTests pass in both Debug and Release. The full Debug matrix
completed 32 GPU cases; the Release quick matrix completed 12. Particle
batching's mixed and portal scene images were compared as decoded RGBA arrays
and match exactly. The existing minimum three-frame capture also passes with
the workspace both visible and hidden; it legitimately reports no GPU samples
until an in-flight frame has been reused.
