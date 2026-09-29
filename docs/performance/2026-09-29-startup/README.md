# Startup optimization evidence

Captured 2026-09-29 on an NVIDIA GeForce RTX 4060 Laptop GPU from the Release
build. Timings are diagnostic measurements for this machine, not correctness
thresholds.

## Changes

- Audio device creation and sound loading now run concurrently with renderer
  construction.
- Shadow, UI, and model-resource setup now overlap the initial swapchain call.
- The persisted fullscreen/windowed mode is applied before Vulkan creates its
  first swapchain, avoiding a redundant pre-frame swapchain generation.
- Startup logs now report application construction and first-frame time plus
  Vulkan device, swapchain, render-resource, pipeline, renderer, and audio
  subphases.
- `RunPerformanceSuites.ps1` now records a matched cold/warm application pair
  with Markdown and JSON output.

## Measurements

The warm-cache comparison reuses one isolated save directory and discards the
first process run. The pre-change median was approximately 1,668 ms from the
session-start log to the completed gameplay frame. Five post-change samples
reported first-frame times of 1,390.6, 1,527.8, 1,361.2, 1,489.3, and 1,488.8
ms: a 1,488.8 ms median, about 10.7 percent lower.

Fresh per-run save directories, which deliberately remove the game pipeline
cache, measured a 2,249.6 ms median after discarding the first process run.
That result is close to the approximately 2,307.5 ms pre-change observation;
cold startup remains dominated by serialized driver work and the difference
is not large enough to claim beyond run-to-run variance.

The new subphase data explains the limit. On warm launches, the first
`vkCreateSwapchainKHR` path generally costs 645–731 ms, Vulkan instance/device
construction varies by roughly 143–291 ms, and cached graphics-pipeline
creation costs roughly 28–42 ms. Without the game pipeline cache, graphics
pipeline creation costs roughly 700–781 ms in addition to the initial
swapchain. The overlapped renderer prerequisites finish before they are needed
(`prerequisite-wait` is typically 3–8 us), and the audio future is also ready
before Application joins it.

## Reproduction

```powershell
cmake --build out\visual-studio --config Release --target sokoban sokoban_vulkan_tests --parallel
.\tools\RunPerformanceSuites.ps1 -BuildDirectory out\visual-studio -Configuration Release -Quick
```

Open `performance-results/Release/comprehensive/startup/startup-report.md` for
the matched process timings and the linked logs for the detailed phase lines.
