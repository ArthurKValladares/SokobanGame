# Render evidence — 100% scale, 4x MSAA

- Device: NVIDIA GeForce RTX 4060 Laptop GPU (discrete)
- Evidence location: level 5, screen 5
- Developer workspace: visible (Level Editor)
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
- Draw calls: 58
- Triangles: 22406
- Prepared particles: 16
- Particle draw calls: 1
- Special-surface coverage: 92 water faces, 84 energy faces, 0 energy models, 0 blurred ice models
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
- Asset scheduling: average 0.040 ms, p95 0.066 ms, maximum 0.143 ms (120 samples)
- Frame-fence wait: average 18.838 ms, p95 26.305 ms, maximum 29.046 ms (120 samples)
- Asset maintenance: average 0.806 ms, p95 1.233 ms, maximum 1.538 ms (120 samples)
- Image acquisition: average 0.050 ms, p95 0.078 ms, maximum 0.191 ms (120 samples)
- Command recording: average 3.886 ms, p95 5.098 ms, maximum 5.669 ms (120 samples)
- Submit/present: average 0.475 ms, p95 0.734 ms, maximum 0.930 ms (120 samples)
-   Recorder setup: average 0.167 ms, p95 0.281 ms, maximum 0.389 ms (120 samples)
-   Game command recording: average 2.185 ms, p95 3.033 ms, maximum 3.603 ms (120 samples)
-     Shadow command recording: average 0.793 ms, p95 1.136 ms, maximum 1.442 ms (120 samples)
-     Scene command recording: average 1.377 ms, p95 1.979 ms, maximum 2.558 ms (120 samples)
-   SSAO command recording: average 0.327 ms, p95 0.479 ms, maximum 1.145 ms (120 samples)
-   Atmosphere command recording: average 0.129 ms, p95 0.195 ms, maximum 0.506 ms (120 samples)
-   Preview command recording: unavailable
-   Output/UI command recording: average 0.989 ms, p95 1.423 ms, maximum 1.864 ms (120 samples)
- Asset publication events: average 0.804 ms, p95 1.608 ms, maximum 2.360 ms (54 samples)
- GPU shadows: average 0.779 ms, p95 7.219 ms, maximum 7.345 ms (120 samples)
- GPU scene color/depth: average 24.481 ms, p95 30.171 ms, maximum 30.543 ms (120 samples)
-   GPU scene raster/resolve: average 6.132 ms, p95 12.499 ms, maximum 12.804 ms (120 samples)
-     GPU scene surfaces: average 0.387 ms, p95 0.432 ms, maximum 0.442 ms (120 samples)
-     GPU scene models: average 3.397 ms, p95 7.313 ms, maximum 7.422 ms (120 samples)
-   GPU scene depth publish: average 0.001 ms, p95 0.001 ms, maximum 0.002 ms (120 samples)
-   GPU scene translucency: average 18.349 ms, p95 21.644 ms, maximum 24.736 ms (120 samples)
-     GPU particles: average 0.146 ms, p95 0.305 ms, maximum 3.540 ms (120 samples)
-   GPU mirror continuation: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU SSAO: average 1.283 ms, p95 1.562 ms, maximum 5.088 ms (120 samples)
-   GPU SSAO scene snapshot: average 0.403 ms, p95 0.557 ms, maximum 3.540 ms (120 samples)
-   GPU SSAO occlusion: average 0.376 ms, p95 0.605 ms, maximum 4.009 ms (120 samples)
-   GPU SSAO composite: average 0.504 ms, p95 0.765 ms, maximum 0.802 ms (120 samples)
- GPU volumetric atmosphere: average 0.592 ms, p95 0.603 ms, maximum 0.884 ms (120 samples)
-   GPU global atmosphere: average 0.592 ms, p95 0.603 ms, maximum 0.884 ms (120 samples)
-     GPU ray integration: average 0.134 ms, p95 0.141 ms, maximum 0.165 ms (120 samples)
-     GPU depth-aware composite: average 0.455 ms, p95 0.461 ms, maximum 0.743 ms (120 samples)
-   GPU bounded fog volumes: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU output/UI: average 5.883 ms, p95 8.718 ms, maximum 12.134 ms (120 samples)
- Asset publications: 54 across 54 frames
- Texture uploads: 25 submitted, 25 completed, 0 in flight
- Scene preparation: average 2.072 ms, p95 2.665 ms, maximum 3.033 ms (120 samples)
- Application frame (including profiler, excluding frame cap): average 33.560 ms, p95 41.197 ms, maximum 42.546 ms (120 samples)
- Frame interval (including pacing): average 33.556 ms, p95 41.201 ms, maximum 42.550 ms (120 samples)
- Frame pacing: average 0.002 ms, p95 0.004 ms, maximum 0.006 ms (120 samples)
- Application update: average 0.084 ms, p95 0.154 ms, maximum 0.432 ms (120 samples)
- Application UI: average 5.299 ms, p95 6.661 ms, maximum 7.820 ms (120 samples)
- Application frame build/prepare: average 3.168 ms, p95 3.977 ms, maximum 4.945 ms (120 samples)
- CPU frame: average 24.193 ms, p95 31.680 ms, maximum 33.627 ms (120 samples)
- GPU frame: average 33.219 ms, p95 40.568 ms, maximum 40.924 ms (120 samples)
- Process resident memory: 582.457 MiB (peak 589.500 MiB)
- GPU allocation memory: 785.643 MiB in 94 allocations; 876.438 MiB reserved in blocks
- Scene image: `scene-scale-100-msaa-4.png`
- Filtered SSAO image: `occlusion-scale-100-msaa-4.png`

Simulation: fixed 1/60 s steps. The final two images share the same simulation state and differ by the SSAO composite debug selector.

## Ranked optimization candidates

1. **Main scene rendering is the dominant GPU pass** (high, score 187.0)
   - Evidence: Main scene rendering is the dominant GPU pass: 30.17 ms (74.4% of the GPU frame).
   - Next experiment: Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture.
2. **GPU frame time exceeds the target** (high, score 150.2)
   - Evidence: GPU p95/latest is 40.57 ms against a 16.67 ms budget; renderer CPU is 31.68 ms
   - Next experiment: Optimize the dominant GPU pass first, then verify at the same resolution, render scale, and MSAA setting.
3. **Command recording is a CPU hot phase** (high, score 94.0)
   - Evidence: Command recording is a CPU hot phase: 5.10 ms (16.1% of the renderer CPU frame).
   - Next experiment: Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost.
4. **A CPU scope dominates exclusive frame time** (medium, score 86.4)
   - Evidence: `Editor.Scan level directories` accounts for 4.22 ms exclusive (19.6%) across 1 call(s).
   - Next experiment: Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.
