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

The first pass identified these targets. The follow-up below addresses the
browser and particle costs and makes smaller water improvements; the procedural
water field remains the main GPU target.

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

## Follow-up: browser, particle preparation, and water

The browser now retains separate active/deleted listings instead of enumerating
directories and parsing metadata on every draw. Save, project transactions,
root changes, and reinitialization invalidate both snapshots immediately.
Invalidation preserves the current vectors so a UI command cannot invalidate
its own iteration. External additions, metadata edits, and deletions appear
within a 500 ms refresh interval. The browser, selector assignments, selector
labels, and editor frame builder share these snapshots. Structural commands
continue to use fresh filesystem reads.

Headless tests exercise immediate add, rename, screen deletion, level deletion,
restore, permanent deletion, new-screen save (including a stale mirror), root
change, and reinitialization. They also verify metadata/screen edits and level
directory additions/deletions at the refresh boundary,
without sleeping or changing the semantics of the fresh collection API.

| Follow-up Debug capture, 240 frames | Before avg / p95 ms | After avg / p95 ms |
|---|---:|---:|
| [Screen 3 Level Editor UI](evidence/followup/editor-after.md) | 3.175 / 3.584 | 1.388 / 2.085 |
| Screen 3 application frame | 8.352 / 9.800 | 6.165 / 7.321 |
| [Mixed effects command recording](evidence/followup/particles-final.md) | 8.093 / 10.097 | 5.072 / 7.335 |
| Mixed effects application frame | 17.200 / 20.441 | 14.179 / 16.749 |

These are new before/after captures of the already-fixed game, rather than
comparisons with the original 170–240 ms editor regression. Their baselines
are archived [for the editor](evidence/followup/editor-before.md) and
[for mixed effects](evidence/followup/particles-before.md). Hardware state still
affects absolute frame times, so use the CPU scopes and submission changes to
interpret the improvement.

The [trace scope summary](evidence/followup/scope-summary.json) shows the browser
draw dropping from 1.898 to 0.173 ms. The 240-frame post-cache trace contains
six directory scans instead of one per frame. The [Debug CPU benchmark](evidence/followup/cpu-browser-debug.md)
measures a fresh scan at 1.348 ms, versus 0.006 ms for **128** cached reads;
the [Release benchmark](evidence/followup/cpu-editor-release.md) records 0.496 ms
and 0.002 ms respectively. Cached access and filesystem refresh are now
separate benchmark cases in both quick and full suites.

Particle draw batching had left a redundant `vkCmdSetPrimitiveTopology` call
for every particle. The recorder now sets topology once for the run, retains
instance/draw statistics, and skips quad-length/nine-slice preparation for
textures without sliced borders. The 4,592-particle mixed case still uses
132 total draws and two particle draws. Removing these redundant commands
is particularly useful with Debug Vulkan validation enabled.

Billboard preparation now skips projection of zero alignment vectors, computes
sine/cosine only when the rotation is needed, and reuses camera-basis scales
and the billboard diagonal. Sorting uses the owned contiguous particle storage
with the same comparator. New preparation/sort scopes distinguish projection
from ordering. In matched intermediate/final traces, the particle task,
including sorting, drops from 3.503 to 2.488 ms. Whole-scene preparation remains
near 6 ms because this task overlaps other scene work; its savings should not
be added directly to application-frame savings. The final mixed scene PNG
is **pixel identical** to the follow-up baseline.

Water's exterior continuation strips previously overlapped at the corners:
left/right strips extended over the entire visible Y range, while top/bottom
strips also extended across X. Top/bottom strips now keep their central X
span, leaving the corners to the side strips. A geometry regression checks
non-overlapping rectangles and exactly one covering strip at each exterior
corner. This fixes redundant shading but did not produce a substantial timing
gain at the measured screen 5 camera.

Reflection tracing transforms the surface origin and ray direction once into
homogeneous clip coordinates. March and refinement steps evaluate this line
directly instead of multiplying the camera matrix for every sample. The ray
steps, depth tests, quality settings, and reflection appearance are retained.
Two comparisons showed small translucency savings: 4.439 → 4.369 ms, then
4.391 → 4.331 ms in a shader-only repeat with the same geometry. This is about
1–2%, close enough to mobile-GPU variation that it is **not a claim of a large
water speedup**. The corresponding GPU clocks also differed (2415 versus
2445 MHz), enough to account for much of that difference. Treat this as a
reduction in repeated shader work, with an inconclusive measured speedup.
Shader-only repeat metrics are archived
[before](evidence/followup/water-old-repeat1.md) and
[after](evidence/followup/water-new-repeat1.md).
The [before hardware snapshot](evidence/followup/water-old-repeat-hardware.txt)
and [after snapshot](evidence/followup/water-new-repeat-hardware.txt) record the
clocks and no active thermal slowdown at the end of that pair.

The normal screen 5 shader comparison changes 47 of 5,184,000 pixels, with a
maximum five-value difference in an 8-bit channel. The boundary-crossing water
fixture at 75% scale/1x MSAA changes four pixels, maximum four values. Visual
inspection found no seam or missing water coverage. Removing corner overlap
alone changes 8,318 pixels by at most one value. Captures remain in
`out/performance-followup`; decoded-image comparison results are archived in
[image comparisons](evidence/followup/image-comparisons.json).

Branchless cellular minima, forced loop unrolling, and explicit early fragment
tests did not yield useful gains in the measured case and were removed.
Water still spends roughly 4.3 ms in translucency at full resolution. The
larger next experiment is caching the static cellular feature data or
evaluating the animated field once for reuse, with explicit handling of
world coordinates, animation, projected caustics, and pixel footprints.
Render scale, MSAA, and water/reflection defaults have not been reduced.

The final Debug/Release builds pass the same 14 relevant CTests,
including the warm zero-allocation scene regression. All six effect families
also complete the full 40-sample [Release CPU suite](evidence/followup/cpu-effects-release.md).
The final water SPIR-V passes `spirv-val`. Debug captures with required
validation cover authored water at 4x MSAA, a boundary-crossing fixture at
75% scale/8x MSAA, and the mixed effect stress. They report no Vulkan usage
errors; the loader still reports this machine's stale external Epic overlay
JSON registration. Test logs are archived for
[Debug](evidence/followup/tests-debug.txt) and
[Release](evidence/followup/tests-release.txt).

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
