# Render evidence — 100% scale, 4x MSAA

- Device: NVIDIA GeForce RTX 4060 Laptop GPU (discrete)
- Evidence location: level 5, screen 5
- Developer workspace: visible
- CPU profiler: enabled
- Effect fixture: none; 0 retained particles
- Authored water: enabled
- Water reflections: disabled
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
- Asset scheduling: average 0.029 ms, p95 0.052 ms, maximum 0.084 ms (120 samples)
- Frame-fence wait: average 0.524 ms, p95 1.442 ms, maximum 1.886 ms (120 samples)
- Asset maintenance: average 0.490 ms, p95 0.887 ms, maximum 1.487 ms (120 samples)
- Image acquisition: average 0.025 ms, p95 0.070 ms, maximum 0.117 ms (120 samples)
- Command recording: average 2.169 ms, p95 3.487 ms, maximum 4.780 ms (120 samples)
- Submit/present: average 0.276 ms, p95 0.667 ms, maximum 0.904 ms (120 samples)
-   Recorder setup: average 0.092 ms, p95 0.189 ms, maximum 0.660 ms (120 samples)
-   Game command recording: average 1.486 ms, p95 2.440 ms, maximum 3.939 ms (120 samples)
-     Shadow command recording: average 0.544 ms, p95 0.916 ms, maximum 1.247 ms (120 samples)
-     Scene command recording: average 0.934 ms, p95 1.429 ms, maximum 2.790 ms (120 samples)
-   SSAO command recording: average 0.210 ms, p95 0.314 ms, maximum 0.561 ms (120 samples)
-   Atmosphere command recording: average 0.083 ms, p95 0.139 ms, maximum 0.247 ms (120 samples)
-   Preview command recording: unavailable
-   Output/UI command recording: average 0.287 ms, p95 0.697 ms, maximum 1.278 ms (120 samples)
- Asset publication events: average 0.547 ms, p95 1.403 ms, maximum 3.424 ms (54 samples)
- GPU shadows: average 0.053 ms, p95 0.069 ms, maximum 0.075 ms (120 samples)
- GPU scene color/depth: average 4.953 ms, p95 5.297 ms, maximum 5.548 ms (120 samples)
-   GPU scene raster/resolve: average 1.112 ms, p95 1.280 ms, maximum 1.367 ms (120 samples)
-     GPU scene surfaces: average 0.284 ms, p95 0.421 ms, maximum 0.478 ms (120 samples)
-     GPU scene models: average 0.407 ms, p95 0.461 ms, maximum 0.480 ms (120 samples)
-   GPU scene depth publish: average 0.001 ms, p95 0.001 ms, maximum 0.001 ms (120 samples)
-   GPU scene translucency: average 3.840 ms, p95 4.073 ms, maximum 4.467 ms (120 samples)
-   GPU mirror continuation: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU SSAO: average 0.761 ms, p95 0.768 ms, maximum 0.791 ms (120 samples)
-   GPU SSAO scene snapshot: average 0.192 ms, p95 0.196 ms, maximum 0.198 ms (120 samples)
-   GPU SSAO occlusion: average 0.240 ms, p95 0.249 ms, maximum 0.270 ms (120 samples)
-   GPU SSAO composite: average 0.329 ms, p95 0.330 ms, maximum 0.339 ms (120 samples)
- GPU volumetric atmosphere: average 0.390 ms, p95 0.395 ms, maximum 0.445 ms (120 samples)
-   GPU global atmosphere: average 0.390 ms, p95 0.395 ms, maximum 0.445 ms (120 samples)
-     GPU ray integration: average 0.085 ms, p95 0.086 ms, maximum 0.111 ms (120 samples)
-     GPU depth-aware composite: average 0.304 ms, p95 0.308 ms, maximum 0.334 ms (120 samples)
-   GPU bounded fog volumes: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU output/UI: average 0.853 ms, p95 0.879 ms, maximum 0.920 ms (120 samples)
- Asset publications: 54 across 54 frames
- Texture uploads: 25 submitted, 25 completed, 0 in flight
- Scene preparation: average 1.671 ms, p95 2.290 ms, maximum 3.297 ms (120 samples)
- Application frame (including profiler, excluding frame cap): average 7.056 ms, p95 9.092 ms, maximum 10.357 ms (120 samples)
- Frame interval (including pacing): average 7.068 ms, p95 9.098 ms, maximum 10.360 ms (120 samples)
- Frame pacing: average 0.002 ms, p95 0.006 ms, maximum 0.011 ms (120 samples)
- Application update: average 0.057 ms, p95 0.126 ms, maximum 0.378 ms (120 samples)
- Application UI: average 0.642 ms, p95 1.068 ms, maximum 1.795 ms (120 samples)
- Application frame build/prepare: average 2.405 ms, p95 3.030 ms, maximum 4.410 ms (120 samples)
- CPU frame: average 3.575 ms, p95 5.441 ms, maximum 7.512 ms (120 samples)
- GPU frame: average 7.016 ms, p95 7.367 ms, maximum 7.591 ms (120 samples)
- Process resident memory: 582.840 MiB (peak 589.797 MiB)
- GPU allocation memory: 785.643 MiB in 94 allocations; 876.438 MiB reserved in blocks
- Scene image: `scene-scale-100-msaa-4.png`
- Filtered SSAO image: `occlusion-scale-100-msaa-4.png`

Simulation: fixed 1/60 s steps. The final two images share the same simulation state and differ by the SSAO composite debug selector.

## Ranked optimization candidates

1. **Main scene rendering is the dominant GPU pass** (high, score 163.0)
   - Evidence: Main scene rendering is the dominant GPU pass: 5.30 ms (71.9% of the GPU frame).
   - Next experiment: Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture.
2. **Command recording is a CPU hot phase** (high, score 160.4)
   - Evidence: Command recording is a CPU hot phase: 3.49 ms (64.1% of the renderer CPU frame).
   - Next experiment: Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost.
3. **Scene preparation is a CPU hot phase** (high, score 120.8)
   - Evidence: Scene preparation is a CPU hot phase: 2.29 ms (42.1% of the renderer CPU frame).
   - Next experiment: Profile culling, face generation, depth sorting, bounds-cache misses, and parallel chunk balance.
4. **A CPU scope dominates exclusive frame time** (medium, score 88.6)
   - Evidence: `Renderer.Prepare scene` accounts for 1.40 ms exclusive (21.0%) across 1 call(s).
   - Next experiment: Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.
5. **Asset maintenance is a CPU hot phase** (medium, score 74.3)
   - Evidence: Asset maintenance is a CPU hot phase: 0.89 ms (16.3% of the renderer CPU frame).
   - Next experiment: Batch publication and reclamation work and move non-critical maintenance away from latency-sensitive frames.
6. **CPU frame pacing has a long tail** (medium, score 58.0)
   - Evidence: CPU standard deviation is 0.99 ms, p95 is 5.44 ms, and p99 is 6.99 ms.
   - Next experiment: Inspect the slowest trace frames for waits, asset publication, allocator growth, and uneven task chunks instead of optimizing only the average.
