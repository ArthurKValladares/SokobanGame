# Render evidence — 100% scale, 4x MSAA

- Device: NVIDIA GeForce RTX 4060 Laptop GPU (discrete)
- Evidence location: level 5, screen 5
- Developer workspace: visible
- CPU profiler: enabled
- Effect fixture: none; 0 retained particles
- Authored water: enabled
- Water reflections: enabled
- Swapchain: 2880x1800
- Present mode: Mailbox
- Scene target: 2880x1800
- SSAO target: 1440x900
- Atmosphere target: 720x450
- Atmosphere coverage: 1 media, 5184000 / 5184000 full-resolution pixels (100.0% after scissoring)
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
- Asset scheduling: average 0.028 ms, p95 0.054 ms, maximum 0.073 ms (120 samples)
- Frame-fence wait: average 1.182 ms, p95 2.290 ms, maximum 2.536 ms (120 samples)
- Asset maintenance: average 0.476 ms, p95 0.741 ms, maximum 1.055 ms (120 samples)
- Image acquisition: average 0.024 ms, p95 0.067 ms, maximum 0.122 ms (120 samples)
- Command recording: average 2.189 ms, p95 3.294 ms, maximum 5.221 ms (120 samples)
- Submit/present: average 0.290 ms, p95 0.525 ms, maximum 1.052 ms (120 samples)
-   Recorder setup: average 0.089 ms, p95 0.193 ms, maximum 0.332 ms (120 samples)
-   Game command recording: average 1.506 ms, p95 2.403 ms, maximum 3.411 ms (120 samples)
-     Shadow command recording: average 0.543 ms, p95 0.843 ms, maximum 1.172 ms (120 samples)
-     Scene command recording: average 0.956 ms, p95 1.650 ms, maximum 2.527 ms (120 samples)
-   SSAO command recording: average 0.214 ms, p95 0.329 ms, maximum 0.527 ms (120 samples)
-   Atmosphere command recording: average 0.086 ms, p95 0.135 ms, maximum 0.258 ms (120 samples)
-   Preview command recording: unavailable
-   Output/UI command recording: average 0.290 ms, p95 0.495 ms, maximum 0.939 ms (120 samples)
- Asset publication events: average 0.519 ms, p95 0.915 ms, maximum 2.781 ms (54 samples)
- GPU shadows: average 0.053 ms, p95 0.060 ms, maximum 0.075 ms (120 samples)
- GPU scene color/depth: average 5.594 ms, p95 5.851 ms, maximum 6.005 ms (120 samples)
-   GPU scene raster/resolve: average 1.135 ms, p95 1.283 ms, maximum 1.422 ms (120 samples)
-     GPU scene surfaces: average 0.277 ms, p95 0.414 ms, maximum 0.511 ms (120 samples)
-     GPU scene models: average 0.419 ms, p95 0.452 ms, maximum 0.477 ms (120 samples)
-   GPU scene depth publish: average 0.001 ms, p95 0.001 ms, maximum 0.001 ms (120 samples)
-   GPU scene translucency: average 4.458 ms, p95 4.611 ms, maximum 4.832 ms (120 samples)
-   GPU mirror continuation: average 0.000 ms, p95 0.000 ms, maximum 0.001 ms (120 samples)
- GPU SSAO: average 0.770 ms, p95 0.784 ms, maximum 0.788 ms (120 samples)
-   GPU SSAO scene snapshot: average 0.198 ms, p95 0.201 ms, maximum 0.202 ms (120 samples)
-   GPU SSAO occlusion: average 0.231 ms, p95 0.244 ms, maximum 0.249 ms (120 samples)
-   GPU SSAO composite: average 0.341 ms, p95 0.343 ms, maximum 0.343 ms (120 samples)
- GPU volumetric atmosphere: average 0.400 ms, p95 0.408 ms, maximum 0.410 ms (120 samples)
-   GPU global atmosphere: average 0.400 ms, p95 0.408 ms, maximum 0.410 ms (120 samples)
-     GPU ray integration: average 0.092 ms, p95 0.093 ms, maximum 0.093 ms (120 samples)
-     GPU depth-aware composite: average 0.307 ms, p95 0.314 ms, maximum 0.316 ms (120 samples)
-   GPU bounded fog volumes: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU output/UI: average 0.874 ms, p95 0.912 ms, maximum 0.939 ms (120 samples)
- Asset publications: 54 across 54 frames
- Texture uploads: 25 submitted, 25 completed, 0 in flight
- Scene preparation: average 1.597 ms, p95 2.106 ms, maximum 2.464 ms (120 samples)
- Application frame (including profiler, excluding frame cap): average 7.719 ms, p95 9.393 ms, maximum 10.818 ms (120 samples)
- Frame interval (including pacing): average 7.720 ms, p95 9.397 ms, maximum 10.822 ms (120 samples)
- Frame pacing: average 0.002 ms, p95 0.003 ms, maximum 0.032 ms (120 samples)
- Application update: average 0.054 ms, p95 0.097 ms, maximum 0.144 ms (120 samples)
- Application UI: average 0.682 ms, p95 1.224 ms, maximum 1.779 ms (120 samples)
- Application frame build/prepare: average 2.330 ms, p95 2.978 ms, maximum 3.562 ms (120 samples)
- CPU frame: average 4.251 ms, p95 6.367 ms, maximum 7.229 ms (120 samples)
- GPU frame: average 7.695 ms, p95 7.949 ms, maximum 8.108 ms (120 samples)
- Process resident memory: 582.863 MiB (peak 589.676 MiB)
- GPU allocation memory: 785.643 MiB in 94 allocations; 876.438 MiB reserved in blocks
- Scene image: `scene-scale-100-msaa-4.png`
- Filtered SSAO image: `occlusion-scale-100-msaa-4.png`

Simulation: fixed 1/60 s steps. The final two images share the same simulation state and differ by the SSAO composite debug selector.

## Ranked optimization candidates

1. **Main scene rendering is the dominant GPU pass** (high, score 165.8)
   - Evidence: Main scene rendering is the dominant GPU pass: 5.85 ms (73.6% of the GPU frame).
   - Next experiment: Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture.
2. **Command recording is a CPU hot phase** (high, score 138.1)
   - Evidence: Command recording is a CPU hot phase: 3.29 ms (51.7% of the renderer CPU frame).
   - Next experiment: Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost.
3. **Scene preparation is a CPU hot phase** (high, score 104.5)
   - Evidence: Scene preparation is a CPU hot phase: 2.11 ms (33.1% of the renderer CPU frame).
   - Next experiment: Profile culling, face generation, depth sorting, bounds-cache misses, and parallel chunk balance.
4. **A CPU scope dominates exclusive frame time** (medium, score 88.3)
   - Evidence: `Renderer.Prepare scene` accounts for 1.35 ms exclusive (20.8%) across 1 call(s).
   - Next experiment: Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.
5. **CPU frame pacing has a long tail** (medium, score 58.0)
   - Evidence: CPU standard deviation is 1.08 ms, p95 is 6.37 ms, and p99 is 6.87 ms.
   - Next experiment: Inspect the slowest trace frames for waits, asset publication, allocator growth, and uneven task chunks instead of optimizing only the average.
