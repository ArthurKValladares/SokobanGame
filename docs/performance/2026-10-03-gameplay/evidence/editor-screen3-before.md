# Render evidence — 100% scale, 4x MSAA

- Device: NVIDIA GeForce RTX 4060 Laptop GPU (discrete)
- Evidence location: level 5, screen 3
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
- Draw calls: 35
- Triangles: 39608
- Prepared particles: 16
- Particle draw calls: 1
- Special-surface coverage: 0 water faces, 90 energy faces, 2 energy models, 0 blurred ice models
- Render passes: 11
- Point-shadow faces: 0 / 0 in range; 0 culled (0 face draws avoided)
- Point-shadow cube faces: 0 rendered, 0 reused
- Point-shadow quad submissions: 0 draws for 0 instances
- Point-shadow models: 0 / 0 in range; 0 culled
- Persistent renderables: 180 (visible 180, culled 0; bounds reused 180, rebuilt 0)
- Main-scene frustum culling: enabled
- Parallel scene preparation: enabled
- Point-shadow range/cache optimizations: enabled
- Recorder scratch reuse: enabled; 0 capacity growths this frame; 16688 bytes of model-recording capacity
- Asset scheduling: average 0.035 ms, p95 0.050 ms, maximum 0.076 ms (120 samples)
- Frame-fence wait: average 0.125 ms, p95 0.209 ms, maximum 0.270 ms (120 samples)
- Asset maintenance: average 0.536 ms, p95 0.701 ms, maximum 0.811 ms (120 samples)
- Image acquisition: average 0.032 ms, p95 0.054 ms, maximum 0.064 ms (120 samples)
- Command recording: average 2.773 ms, p95 3.777 ms, maximum 4.305 ms (120 samples)
- Submit/present: average 0.361 ms, p95 0.606 ms, maximum 0.747 ms (120 samples)
-   Recorder setup: average 0.111 ms, p95 0.184 ms, maximum 0.208 ms (120 samples)
-   Game command recording: average 1.549 ms, p95 2.131 ms, maximum 2.468 ms (120 samples)
-     Shadow command recording: average 0.690 ms, p95 0.962 ms, maximum 1.098 ms (120 samples)
-     Scene command recording: average 0.850 ms, p95 1.185 ms, maximum 1.597 ms (120 samples)
-   SSAO command recording: average 0.226 ms, p95 0.332 ms, maximum 0.536 ms (120 samples)
-   Atmosphere command recording: average 0.091 ms, p95 0.134 ms, maximum 0.211 ms (120 samples)
-   Preview command recording: unavailable
-   Output/UI command recording: average 0.778 ms, p95 1.223 ms, maximum 1.784 ms (120 samples)
- Asset publication events: average 0.636 ms, p95 1.152 ms, maximum 1.744 ms (54 samples)
- GPU shadows: average 0.097 ms, p95 0.108 ms, maximum 0.112 ms (120 samples)
- GPU scene color/depth: average 16.630 ms, p95 28.592 ms, maximum 29.193 ms (120 samples)
-   GPU scene raster/resolve: average 10.341 ms, p95 14.917 ms, maximum 15.059 ms (120 samples)
-     GPU scene surfaces: average 0.439 ms, p95 0.516 ms, maximum 0.593 ms (120 samples)
-     GPU scene models: average 4.927 ms, p95 7.297 ms, maximum 7.430 ms (120 samples)
-   GPU scene depth publish: average 0.001 ms, p95 0.002 ms, maximum 0.002 ms (120 samples)
-   GPU scene translucency: average 6.289 ms, p95 14.011 ms, maximum 14.346 ms (120 samples)
-     GPU particles: average 0.057 ms, p95 0.003 ms, maximum 6.666 ms (120 samples)
-   GPU mirror continuation: average 0.000 ms, p95 0.000 ms, maximum 0.001 ms (120 samples)
- GPU SSAO: average 12.765 ms, p95 21.331 ms, maximum 21.838 ms (120 samples)
-   GPU SSAO scene snapshot: average 3.344 ms, p95 7.203 ms, maximum 7.280 ms (120 samples)
-   GPU SSAO occlusion: average 3.096 ms, p95 7.388 ms, maximum 7.454 ms (120 samples)
-   GPU SSAO composite: average 6.325 ms, p95 13.991 ms, maximum 14.293 ms (120 samples)
- GPU volumetric atmosphere: average 5.367 ms, p95 7.735 ms, maximum 14.316 ms (120 samples)
-   GPU global atmosphere: average 5.367 ms, p95 7.735 ms, maximum 14.316 ms (120 samples)
-     GPU ray integration: average 0.433 ms, p95 0.384 ms, maximum 7.172 ms (120 samples)
-     GPU depth-aware composite: average 4.694 ms, p95 7.541 ms, maximum 7.611 ms (120 samples)
-   GPU bounded fog volumes: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU output/UI: average 15.259 ms, p95 28.177 ms, maximum 28.724 ms (120 samples)
- Asset publications: 54 across 54 frames
- Texture uploads: 25 submitted, 25 completed, 0 in flight
- Scene preparation: average 1.602 ms, p95 1.925 ms, maximum 2.034 ms (120 samples)
- Application frame (including profiler, excluding frame cap): average 171.343 ms, p95 194.930 ms, maximum 212.708 ms (120 samples)
- Frame interval (including pacing): average 171.296 ms, p95 194.935 ms, maximum 212.716 ms (120 samples)
- Frame pacing: average 0.002 ms, p95 0.003 ms, maximum 0.013 ms (120 samples)
- Application update: average 0.070 ms, p95 0.122 ms, maximum 0.175 ms (120 samples)
- Application UI: average 163.654 ms, p95 186.159 ms, maximum 202.136 ms (120 samples)
- Application frame build/prepare: average 2.385 ms, p95 2.893 ms, maximum 3.515 ms (120 samples)
- CPU frame: average 3.946 ms, p95 5.084 ms, maximum 5.654 ms (120 samples)
- GPU frame: average 50.269 ms, p95 78.769 ms, maximum 79.536 ms (120 samples)
- Process resident memory: 581.520 MiB (peak 581.566 MiB)
- GPU allocation memory: 785.643 MiB in 94 allocations; 876.438 MiB reserved in blocks
- Scene image: `scene-scale-100-msaa-4.png`
- Filtered SSAO image: `occlusion-scale-100-msaa-4.png`

Simulation: fixed 1/60 s steps. The final two images share the same simulation state and differ by the SSAO composite debug selector.

## Ranked optimization candidates

1. **GPU frame time exceeds the target** (high, score 230.4)
   - Evidence: GPU p95/latest is 78.77 ms against a 16.67 ms budget; renderer CPU is 5.08 ms
   - Next experiment: Optimize the dominant GPU pass first, then verify at the same resolution, render scale, and MSAA setting.
2. **A CPU scope dominates exclusive frame time** (high, score 205.1)
   - Evidence: `Editor.Draw panel` accounts for 169.61 ms exclusive (93.8%) across 1 call(s).
   - Next experiment: Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.
3. **Command recording is a CPU hot phase** (high, score 178.7)
   - Evidence: Command recording is a CPU hot phase: 3.78 ms (74.3% of the renderer CPU frame).
   - Next experiment: Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost.
4. **Main scene rendering is the dominant GPU pass** (high, score 126.1)
   - Evidence: Main scene rendering is the dominant GPU pass: 28.59 ms (36.3% of the GPU frame).
   - Next experiment: Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture.
5. **Output and UI are a dominant GPU pass** (high, score 125.2)
   - Evidence: Output and UI are a dominant GPU pass: 28.18 ms (35.8% of the GPU frame).
   - Next experiment: Inspect fullscreen bandwidth, tone mapping, UI overdraw, and final target transitions.
6. **Scene preparation is a CPU hot phase** (high, score 113.1)
   - Evidence: Scene preparation is a CPU hot phase: 1.92 ms (37.9% of the renderer CPU frame).
   - Next experiment: Profile culling, face generation, depth sorting, bounds-cache misses, and parallel chunk balance.
7. **SSAO is a dominant GPU pass** (high, score 111.3)
   - Evidence: SSAO is a dominant GPU pass: 21.33 ms (27.1% of the GPU frame).
   - Next experiment: Measure occlusion resolution, sample count, snapshot bandwidth, and composite cost separately.
