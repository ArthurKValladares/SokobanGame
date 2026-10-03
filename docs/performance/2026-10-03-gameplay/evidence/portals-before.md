# Render evidence — 100% scale, 4x MSAA

- Device: NVIDIA GeForce RTX 4060 Laptop GPU (discrete)
- Evidence location: level 5, screen 4
- Developer workspace: visible
- CPU profiler: enabled
- Effect fixture: portals; 896 retained particles
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
- Draw calls: 1147
- Triangles: 14902
- Prepared particles: 1120
- Render passes: 11
- Point-shadow faces: 0 / 0 in range; 0 culled (0 face draws avoided)
- Point-shadow cube faces: 0 rendered, 0 reused
- Point-shadow quad submissions: 0 draws for 0 instances
- Point-shadow models: 0 / 0 in range; 0 culled
- Persistent renderables: 120 (visible 120, culled 0; bounds reused 120, rebuilt 0)
- Main-scene frustum culling: enabled
- Parallel scene preparation: enabled
- Point-shadow range/cache optimizations: enabled
- Recorder scratch reuse: enabled; 0 capacity growths this frame; 3224 bytes of model-recording capacity
- Asset scheduling: average 0.173 ms, p95 0.231 ms, maximum 0.322 ms (120 samples)
- Frame-fence wait: average 0.083 ms, p95 0.224 ms, maximum 0.334 ms (120 samples)
- Asset maintenance: average 0.308 ms, p95 0.664 ms, maximum 1.035 ms (120 samples)
- Image acquisition: average 0.025 ms, p95 0.077 ms, maximum 0.131 ms (120 samples)
- Command recording: average 4.037 ms, p95 5.847 ms, maximum 6.530 ms (120 samples)
- Submit/present: average 0.237 ms, p95 0.602 ms, maximum 1.270 ms (120 samples)
-   Recorder setup: average 0.086 ms, p95 0.211 ms, maximum 0.839 ms (120 samples)
-   Game command recording: average 3.393 ms, p95 4.720 ms, maximum 5.743 ms (120 samples)
-     Shadow command recording: average 0.432 ms, p95 0.787 ms, maximum 1.052 ms (120 samples)
-     Scene command recording: average 2.952 ms, p95 4.183 ms, maximum 5.229 ms (120 samples)
-   SSAO command recording: average 0.229 ms, p95 0.520 ms, maximum 0.917 ms (120 samples)
-   Atmosphere command recording: average 0.078 ms, p95 0.151 ms, maximum 0.349 ms (120 samples)
-   Preview command recording: unavailable
-   Output/UI command recording: average 0.264 ms, p95 0.737 ms, maximum 0.927 ms (120 samples)
- Asset publication events: average 0.604 ms, p95 1.617 ms, maximum 2.042 ms (54 samples)
- GPU shadows: average 0.041 ms, p95 0.050 ms, maximum 0.056 ms (120 samples)
- GPU scene color/depth: average 1.285 ms, p95 1.437 ms, maximum 1.533 ms (120 samples)
-   GPU scene raster/resolve: average 0.765 ms, p95 0.866 ms, maximum 0.955 ms (120 samples)
-     GPU scene surfaces: average 0.307 ms, p95 0.403 ms, maximum 0.484 ms (120 samples)
-     GPU scene models: average 0.074 ms, p95 0.082 ms, maximum 0.101 ms (120 samples)
-   GPU scene depth publish: average 0.001 ms, p95 0.001 ms, maximum 0.001 ms (120 samples)
-   GPU scene translucency: average 0.520 ms, p95 0.552 ms, maximum 0.608 ms (120 samples)
-   GPU mirror continuation: average 0.000 ms, p95 0.000 ms, maximum 0.001 ms (120 samples)
- GPU SSAO: average 0.784 ms, p95 0.790 ms, maximum 0.791 ms (120 samples)
-   GPU SSAO scene snapshot: average 0.177 ms, p95 0.179 ms, maximum 0.180 ms (120 samples)
-   GPU SSAO occlusion: average 0.283 ms, p95 0.288 ms, maximum 0.289 ms (120 samples)
-   GPU SSAO composite: average 0.325 ms, p95 0.326 ms, maximum 0.327 ms (120 samples)
- GPU volumetric atmosphere: average 0.381 ms, p95 0.383 ms, maximum 0.397 ms (120 samples)
-   GPU global atmosphere: average 0.381 ms, p95 0.383 ms, maximum 0.397 ms (120 samples)
-     GPU ray integration: average 0.102 ms, p95 0.104 ms, maximum 0.119 ms (120 samples)
-     GPU depth-aware composite: average 0.278 ms, p95 0.279 ms, maximum 0.281 ms (120 samples)
-   GPU bounded fog volumes: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU output/UI: average 0.817 ms, p95 0.846 ms, maximum 0.903 ms (120 samples)
- Asset publications: 54 across 54 frames
- Texture uploads: 25 submitted, 25 completed, 0 in flight
- Scene preparation: average 1.203 ms, p95 1.514 ms, maximum 2.355 ms (120 samples)
- Application frame (including profiler, excluding frame cap): average 7.651 ms, p95 9.447 ms, maximum 10.335 ms (120 samples)
- Frame interval (including pacing): average 7.658 ms, p95 9.450 ms, maximum 10.338 ms (120 samples)
- Frame pacing: average 0.002 ms, p95 0.003 ms, maximum 0.008 ms (120 samples)
- Application update: average 0.048 ms, p95 0.081 ms, maximum 0.184 ms (120 samples)
- Application UI: average 0.553 ms, p95 1.026 ms, maximum 2.517 ms (120 samples)
- Application frame build/prepare: average 1.788 ms, p95 2.206 ms, maximum 3.117 ms (120 samples)
- CPU frame: average 4.920 ms, p95 6.844 ms, maximum 7.572 ms (120 samples)
- GPU frame: average 3.311 ms, p95 3.476 ms, maximum 3.569 ms (120 samples)
- Process resident memory: 576.461 MiB (peak 576.461 MiB)
- GPU allocation memory: 785.643 MiB in 94 allocations; 876.438 MiB reserved in blocks
- Scene image: `scene-scale-100-msaa-4.png`
- Filtered SSAO image: `occlusion-scale-100-msaa-4.png`

Simulation: fixed 1/60 s steps. The final two images share the same simulation state and differ by the SSAO composite debug selector.

## Ranked optimization candidates

1. **Command recording is a CPU hot phase** (high, score 198.8)
   - Evidence: Command recording is a CPU hot phase: 5.85 ms (85.4% of the renderer CPU frame).
   - Next experiment: Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost.
2. **Main scene rendering is the dominant GPU pass** (high, score 114.1)
   - Evidence: Main scene rendering is the dominant GPU pass: 1.44 ms (41.3% of the GPU frame).
   - Next experiment: Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture.
3. **A CPU scope dominates exclusive frame time** (high, score 103.4)
   - Evidence: `Renderer.Record scene pass` accounts for 2.76 ms exclusive (30.2%) across 2 call(s).
   - Next experiment: Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.
4. **Output and UI are a dominant GPU pass** (medium, score 86.9)
   - Evidence: Output and UI are a dominant GPU pass: 0.85 ms (24.3% of the GPU frame).
   - Next experiment: Inspect fullscreen bandwidth, tone mapping, UI overdraw, and final target transitions.
5. **Scene preparation is a CPU hot phase** (medium, score 84.8)
   - Evidence: Scene preparation is a CPU hot phase: 1.51 ms (22.1% of the renderer CPU frame).
   - Next experiment: Profile culling, face generation, depth sorting, bounds-cache misses, and parallel chunk balance.
6. **SSAO is a dominant GPU pass** (medium, score 84.3)
   - Evidence: SSAO is a dominant GPU pass: 0.79 ms (22.7% of the GPU frame).
   - Next experiment: Measure occlusion resolution, sample count, snapshot bandwidth, and composite cost separately.
7. **Draw submission is unusually fine grained** (medium, score 73.5)
   - Evidence: 1147 draws submit 14902 triangles (13.0 triangles/draw).
   - Next experiment: Prioritize instancing, compatible material sorting, and eliminating tiny or duplicate submissions; verify that draw count and command-recording time fall together.
8. **CPU frame pacing has a long tail** (medium, score 58.0)
   - Evidence: CPU standard deviation is 1.03 ms, p95 is 6.84 ms, and p99 is 7.56 ms.
   - Next experiment: Inspect the slowest trace frames for waits, asset publication, allocator growth, and uneven task chunks instead of optimizing only the average.
