# Water cell feature cache — October 5, 2026

Caching the water shader's static cellular features reduces the level 5,
screen 5 translucency pass by **24–25%**, and the complete GPU frame by
**9–11%**, in the paired captures below. Ripple animation, analytic projected
caustic derivatives, reflections, filtering, render scale and MSAA settings
retain their existing calculations and defaults.

## Change

Each ripple cell has a static feature point, axis, aspect and distance weight.
Previously every water fragment generated those values for nine neighbors,
for both ripple layers and again for projected caustics. A compute shader now
generates two 128×128 feature windows around the current primary and secondary
pattern domains. The fragment shader loads these features from a storage
buffer, then evaluates the existing animated field and distance gradients.

The windows move in 32-cell steps. Each frame in flight owns its buffer, updated
after its fence and synchronized from compute writes to fragment reads. A
pipeline reload invalidates both windows; swapchain resources own and retire
the buffers with their descriptor sets. Total cache storage for two frames is
2,097,216 bytes (about 2 MiB). The ordinary frozen and 420-frame animated
captures each require two dispatches, one per frame buffer.

Cells outside a window use the shared procedural generator. This preserves
the unbounded field for editor previews, extreme tuning and large views.
Nonfinite or extreme domain centers disable the window; graphics queues
without compute support use procedural generation. The diagnostic
`--disable-water-cell-cache` specializes the fragment pipeline to remove cache
lookups and bounds checks, providing an unpenalized procedural control.

The shader catalog, CMake shader compilation, staging and developer hot reload
now accept compute shaders. Binding 16 holds the cache. The total scene set
uses four storage buffers, within Vulkan's minimum supported limit.

## Paired measurements

Windows/MSVC RelWithDebInfo (`dev-fast`), NVIDIA GeForce RTX 4060 Laptop GPU,
1280×720 scene and swapchain, 100% scale, 4× MSAA, D32 depth, Mailbox
presentation, reflections and AO enabled, CPU profiler enabled and developer
workspace hidden. Screen indices are zero-based. Each capture renders 420
frames and retains the final 120 samples; screenshots are outside the timing
window. Animated captures advance by a fixed 1/60 s per frame.

For each simulation mode, run order was disabled A, enabled A, enabled B,
disabled B. Individual reports and before/after hardware snapshots are saved
in [evidence](evidence/).

| Capture | Translucency average / p95 ms | GPU frame average / p95 ms |
|---|---:|---:|
| [Frozen disabled A](evidence/frozen-off-a.md) | 0.927 / 1.138 | 1.635 / 1.790 |
| [Frozen enabled A](evidence/frozen-on-a.md) | 0.695 / 0.720 | 1.459 / 1.496 |
| [Frozen enabled B](evidence/frozen-on-b.md) | 0.684 / 0.695 | 1.446 / 1.473 |
| [Frozen disabled B](evidence/frozen-off-b.md) | 0.921 / 1.130 | 1.635 / 1.783 |
| [Animated disabled A](evidence/animated-off-a.md) | 0.922 / 1.139 | 1.633 / 1.786 |
| [Animated enabled A](evidence/animated-on-a.md) | 0.710 / 0.737 | 1.483 / 1.506 |
| [Animated enabled B](evidence/animated-on-b.md) | 0.705 / 0.726 | 1.479 / 1.497 |
| [Animated disabled B](evidence/animated-off-b.md) | 0.934 / 1.146 | 1.622 / 1.728 |

Mean frozen translucency falls from 0.924 to 0.690 ms (25.4%), and GPU frame
time from 1.635 to 1.453 ms (11.2%). Animated translucency falls from 0.928 to
0.708 ms (23.8%), and GPU frame time from 1.628 to 1.481 ms (9.0%). The original
shader's separate frozen/animated baselines were 0.920/0.911 ms translucency
and 1.640/1.656 ms GPU frame, consistent with the procedural controls.

Post-run snapshots report 2580 MHz SM clocks and 68–78°C. Memory clocks are
8001 MHz except one snapshot at 7001 MHz. These snapshots and the balanced
ordering reduce drift concerns, but do not establish constant clocks within
every frame. This is one GPU at a lower resolution than the October 3
2880×1800 investigation; do not extrapolate the percentages to other hardware
or resolutions.

## Visual and correctness checks

Full-resolution RGB comparisons against the original shader and the final
procedural control show only small floating-point differences:

| Simulation | Changed pixels out of 921,600 | Maximum channel difference |
|---|---:|---:|
| Frozen | 84 | 3/255; only one pixel exceeds 1/255 |
| Animated | 78 | 1/255 |

The animated image was also inspected visually. Local screenshots, amplified
difference images and traces remain under `out/water-cell-cache/`; numerical
comparison results are retained in [image-comparison.txt](evidence/image-comparison.txt).

Unit coverage checks both rotated domains, negative coordinates, window
margin, amortized rebuilds, tuning changes and invalid/extreme inputs. GPU ABI
reflection checks binding 16's header offsets, integer types and feature
stride in both the fragment and compute modules. All 27 compiled modules pass
`spirv-val --target-env vulkan1.3`. Full test and runtime validation results
are retained alongside the measurements.

The optimized editor and Release configurations each pass all 95 CTest suites.
A required-validation Debug run completes 12,000 animated frames with the
Level Editor visible, including a live compute-shader reload and ten cache
rebuilds as the domains drift. There are no Vulkan API validation errors. The
loader reports a pre-existing missing Epic overlay manifest; existing unused
vertex-output warnings also remain. These environmental/interface diagnostics
are recorded in [validation-drift-reload.log](evidence/validation-drift-reload.log).

## Reproduce

```powershell
cmake --build --preset dev-fast-all
ctest --preset dev-fast
out/dev-fast/RelWithDebInfo/sokoban.exe --smoke-frames 420 `
  --save-directory out/water-on-profile --evidence-output out/water-on `
  --evidence-level 5 --evidence-screen 5 --evidence-animate --evidence-disable-vsync
out/dev-fast/RelWithDebInfo/sokoban.exe --smoke-frames 420 `
  --save-directory out/water-off-profile --evidence-output out/water-off `
  --evidence-level 5 --evidence-screen 5 --evidence-animate --evidence-disable-vsync `
  --disable-water-cell-cache
```

Repeat in reverse order and compare hardware snapshots. The full
`tools/RunPerformanceSuites.ps1` matrix now includes a screen 5 cache-disabled
control. The live profiler and evidence reports expose cache bytes and
rebuilds, making repeated dispatches visible during future tuning.

The next useful performance check is this same A/B comparison on an integrated
GPU and at native high-DPI resolution. Any further shader optimization should
use those measurements to decide between memory access, animated warping and
reflection traversal.
