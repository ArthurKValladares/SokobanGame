# Render evidence — 75% scale, 4x MSAA

- Device: NVIDIA GeForce RTX 4060 Laptop GPU (discrete)
- Evidence location: level 5, screen 5
- Developer workspace: visible
- CPU profiler: enabled
- Effect fixture: none; 0 retained particles
- Authored water: enabled
- Water reflections: enabled
- Swapchain: 2880x1800
- Present mode: Mailbox
- Scene target: 2160x1350
- SSAO target: 1080x675
- Atmosphere target: 540x338
- Atmosphere coverage: 1 media, 2916000 / 2916000 full-resolution pixels (100.0% after scissoring)
- Ambient occlusion: enabled
- Evidence water fixture: disabled
- Main-scene translucency: present
- SSAO color snapshot: exercised
- MSAA samples: 4
- Scene depth: 32 bits
- Draw calls: 73
- Triangles: 22406
- Prepared particles: 16
- Render passes: 11
- Point-shadow faces: 0 / 0 in range; 0 culled (0 face draws avoided)
- Point-shadow cube faces: 0 rendered, 0 reused
- Point-shadow quad submissions: 0 draws for 0 instances
- Point-shadow models: 0 / 0 in range; 0 culled
- Persistent renderables: 289 (visible 289, culled 0; bounds reused 289, rebuilt 0)
- Main-scene frustum culling: enabled
- Parallel scene preparation: enabled
- Point-shadow range/cache optimizations: enabled
- Recorder scratch reuse: enabled; 0 capacity growths this frame; 5120 bytes of model-recording capacity
- Asset scheduling: average 0.024 ms, p95 0.045 ms, maximum 0.059 ms (120 samples)
- Frame-fence wait: average 0.081 ms, p95 0.149 ms, maximum 0.241 ms (120 samples)
- Asset maintenance: average 0.405 ms, p95 0.552 ms, maximum 0.846 ms (120 samples)
- Image acquisition: average 0.020 ms, p95 0.041 ms, maximum 0.093 ms (120 samples)
- Command recording: average 1.888 ms, p95 2.724 ms, maximum 4.099 ms (120 samples)
- Submit/present: average 0.205 ms, p95 0.506 ms, maximum 0.756 ms (120 samples)
-   Recorder setup: average 0.072 ms, p95 0.114 ms, maximum 0.415 ms (120 samples)
-   Game command recording: average 1.313 ms, p95 2.025 ms, maximum 3.157 ms (120 samples)
-     Shadow command recording: average 0.453 ms, p95 0.608 ms, maximum 1.146 ms (120 samples)
-     Scene command recording: average 0.854 ms, p95 1.392 ms, maximum 2.619 ms (120 samples)
-   SSAO command recording: average 0.195 ms, p95 0.389 ms, maximum 0.740 ms (120 samples)
-   Atmosphere command recording: average 0.082 ms, p95 0.145 ms, maximum 0.432 ms (120 samples)
-   Preview command recording: unavailable
-   Output/UI command recording: average 0.252 ms, p95 0.546 ms, maximum 0.929 ms (120 samples)
- Asset publication events: average 0.520 ms, p95 1.202 ms, maximum 1.917 ms (54 samples)
- GPU shadows: average 0.053 ms, p95 0.062 ms, maximum 0.121 ms (120 samples)
- GPU scene color/depth: average 2.582 ms, p95 2.769 ms, maximum 3.587 ms (120 samples)
-   GPU scene raster/resolve: average 0.448 ms, p95 0.566 ms, maximum 1.261 ms (120 samples)
-     GPU scene surfaces: average 0.173 ms, p95 0.265 ms, maximum 0.799 ms (120 samples)
-     GPU scene models: average 0.061 ms, p95 0.080 ms, maximum 0.220 ms (120 samples)
-   GPU scene depth publish: average 0.001 ms, p95 0.001 ms, maximum 0.001 ms (120 samples)
-   GPU scene translucency: average 2.133 ms, p95 2.233 ms, maximum 2.324 ms (120 samples)
-   GPU mirror continuation: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU SSAO: average 0.332 ms, p95 0.345 ms, maximum 0.346 ms (120 samples)
-   GPU SSAO scene snapshot: average 0.059 ms, p95 0.062 ms, maximum 0.062 ms (120 samples)
-   GPU SSAO occlusion: average 0.132 ms, p95 0.137 ms, maximum 0.143 ms (120 samples)
-   GPU SSAO composite: average 0.141 ms, p95 0.146 ms, maximum 0.147 ms (120 samples)
- GPU volumetric atmosphere: average 0.235 ms, p95 0.247 ms, maximum 0.249 ms (120 samples)
-   GPU global atmosphere: average 0.235 ms, p95 0.247 ms, maximum 0.249 ms (120 samples)
-     GPU ray integration: average 0.061 ms, p95 0.065 ms, maximum 0.066 ms (120 samples)
-     GPU depth-aware composite: average 0.173 ms, p95 0.181 ms, maximum 0.182 ms (120 samples)
-   GPU bounded fog volumes: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU output/UI: average 0.422 ms, p95 0.445 ms, maximum 0.489 ms (120 samples)
- Asset publications: 54 across 54 frames
- Texture uploads: 25 submitted, 25 completed, 0 in flight
- Scene preparation: average 1.405 ms, p95 1.720 ms, maximum 2.672 ms (120 samples)
- Application frame (including profiler, excluding frame cap): average 5.610 ms, p95 7.092 ms, maximum 8.075 ms (120 samples)
- Frame interval (including pacing): average 5.613 ms, p95 7.093 ms, maximum 8.077 ms (120 samples)
- Frame pacing: average 0.001 ms, p95 0.003 ms, maximum 0.008 ms (120 samples)
- Application update: average 0.041 ms, p95 0.080 ms, maximum 0.152 ms (120 samples)
- Application UI: average 0.548 ms, p95 1.018 ms, maximum 1.790 ms (120 samples)
- Application frame build/prepare: average 2.030 ms, p95 2.469 ms, maximum 3.326 ms (120 samples)
- CPU frame: average 2.678 ms, p95 3.751 ms, maximum 5.152 ms (120 samples)
- GPU frame: average 3.628 ms, p95 3.853 ms, maximum 4.724 ms (120 samples)
- Process resident memory: 582.680 MiB (peak 591.633 MiB)
- GPU allocation memory: 611.518 MiB in 94 allocations; 678.156 MiB reserved in blocks
- Scene image: `scene-scale-75-msaa-4.png`
- Filtered SSAO image: `occlusion-scale-75-msaa-4.png`

Simulation: fixed 1/60 s steps. The final two images share the same simulation state and differ by the SSAO composite debug selector.

## Ranked optimization candidates

1. **Command recording is a CPU hot phase** (high, score 175.7)
   - Evidence: Command recording is a CPU hot phase: 2.72 ms (72.6% of the renderer CPU frame).
   - Next experiment: Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost.
2. **Main scene rendering is the dominant GPU pass** (high, score 163.0)
   - Evidence: Main scene rendering is the dominant GPU pass: 2.77 ms (71.9% of the GPU frame).
   - Next experiment: Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture.
3. **Scene preparation is a CPU hot phase** (high, score 127.6)
   - Evidence: Scene preparation is a CPU hot phase: 1.72 ms (45.9% of the renderer CPU frame).
   - Next experiment: Profile culling, face generation, depth sorting, bounds-cache misses, and parallel chunk balance.
4. **A CPU scope dominates exclusive frame time** (medium, score 85.3)
   - Evidence: `Application.UI` accounts for 1.36 ms exclusive (18.9%) across 1 call(s).
   - Next experiment: Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.
5. **CPU frame pacing has a long tail** (medium, score 58.0)
   - Evidence: CPU standard deviation is 0.67 ms, p95 is 3.75 ms, and p99 is 4.89 ms.
   - Next experiment: Inspect the slowest trace frames for waits, asset publication, allocator growth, and uneven task chunks instead of optimizing only the average.
