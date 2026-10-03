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
- Draw calls: 28
- Triangles: 14902
- Prepared particles: 1120
- Particle draw calls: 1
- Special-surface coverage: 0 water faces, 0 energy faces, 0 energy models, 1 blurred ice models
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
- Asset scheduling: average 0.189 ms, p95 0.237 ms, maximum 0.300 ms (120 samples)
- Frame-fence wait: average 0.087 ms, p95 0.132 ms, maximum 0.744 ms (120 samples)
- Asset maintenance: average 0.290 ms, p95 0.425 ms, maximum 1.016 ms (120 samples)
- Image acquisition: average 0.022 ms, p95 0.044 ms, maximum 0.148 ms (120 samples)
- Command recording: average 2.340 ms, p95 3.465 ms, maximum 4.255 ms (120 samples)
- Submit/present: average 0.206 ms, p95 0.395 ms, maximum 0.795 ms (120 samples)
-   Recorder setup: average 0.078 ms, p95 0.134 ms, maximum 0.272 ms (120 samples)
-   Game command recording: average 1.778 ms, p95 2.659 ms, maximum 3.518 ms (120 samples)
-     Shadow command recording: average 0.419 ms, p95 0.627 ms, maximum 0.711 ms (120 samples)
-     Scene command recording: average 1.352 ms, p95 2.192 ms, maximum 3.059 ms (120 samples)
-   SSAO command recording: average 0.193 ms, p95 0.295 ms, maximum 0.558 ms (120 samples)
-   Atmosphere command recording: average 0.071 ms, p95 0.111 ms, maximum 0.136 ms (120 samples)
-   Preview command recording: unavailable
-   Output/UI command recording: average 0.226 ms, p95 0.365 ms, maximum 0.427 ms (120 samples)
- Asset publication events: average 0.490 ms, p95 1.267 ms, maximum 1.572 ms (54 samples)
- GPU shadows: average 0.044 ms, p95 0.050 ms, maximum 0.081 ms (120 samples)
- GPU scene color/depth: average 1.303 ms, p95 1.445 ms, maximum 1.594 ms (120 samples)
-   GPU scene raster/resolve: average 0.790 ms, p95 0.937 ms, maximum 1.070 ms (120 samples)
-     GPU scene surfaces: average 0.323 ms, p95 0.454 ms, maximum 0.581 ms (120 samples)
-     GPU scene models: average 0.077 ms, p95 0.084 ms, maximum 0.108 ms (120 samples)
-   GPU scene depth publish: average 0.001 ms, p95 0.001 ms, maximum 0.001 ms (120 samples)
-   GPU scene translucency: average 0.513 ms, p95 0.529 ms, maximum 0.552 ms (120 samples)
-     GPU particles: average 0.010 ms, p95 0.013 ms, maximum 0.018 ms (120 samples)
-   GPU mirror continuation: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU SSAO: average 0.800 ms, p95 0.818 ms, maximum 0.826 ms (120 samples)
-   GPU SSAO scene snapshot: average 0.177 ms, p95 0.180 ms, maximum 0.185 ms (120 samples)
-   GPU SSAO occlusion: average 0.294 ms, p95 0.307 ms, maximum 0.314 ms (120 samples)
-   GPU SSAO composite: average 0.329 ms, p95 0.334 ms, maximum 0.336 ms (120 samples)
- GPU volumetric atmosphere: average 0.400 ms, p95 0.418 ms, maximum 0.428 ms (120 samples)
-   GPU global atmosphere: average 0.400 ms, p95 0.418 ms, maximum 0.428 ms (120 samples)
-     GPU ray integration: average 0.107 ms, p95 0.114 ms, maximum 0.121 ms (120 samples)
-     GPU depth-aware composite: average 0.292 ms, p95 0.305 ms, maximum 0.312 ms (120 samples)
-   GPU bounded fog volumes: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU output/UI: average 0.830 ms, p95 0.851 ms, maximum 0.899 ms (120 samples)
- Asset publications: 54 across 54 frames
- Texture uploads: 25 submitted, 25 completed, 0 in flight
- Scene preparation: average 1.235 ms, p95 1.601 ms, maximum 2.219 ms (120 samples)
- Application frame (including profiler, excluding frame cap): average 5.949 ms, p95 7.288 ms, maximum 8.045 ms (120 samples)
- Frame interval (including pacing): average 5.956 ms, p95 7.291 ms, maximum 8.048 ms (120 samples)
- Frame pacing: average 0.001 ms, p95 0.002 ms, maximum 0.009 ms (120 samples)
- Application update: average 0.046 ms, p95 0.074 ms, maximum 0.186 ms (120 samples)
- Application UI: average 0.525 ms, p95 0.840 ms, maximum 1.129 ms (120 samples)
- Application frame build/prepare: average 1.853 ms, p95 2.358 ms, maximum 3.141 ms (120 samples)
- CPU frame: average 3.193 ms, p95 4.370 ms, maximum 5.125 ms (120 samples)
- GPU frame: average 3.380 ms, p95 3.550 ms, maximum 3.708 ms (120 samples)
- Process resident memory: 576.523 MiB (peak 576.523 MiB)
- GPU allocation memory: 785.643 MiB in 94 allocations; 876.438 MiB reserved in blocks
- Scene image: `scene-scale-100-msaa-4.png`
- Filtered SSAO image: `occlusion-scale-100-msaa-4.png`

Simulation: fixed 1/60 s steps. The final two images share the same simulation state and differ by the SSAO composite debug selector.

## Ranked optimization candidates

1. **Command recording is a CPU hot phase** (high, score 187.7)
   - Evidence: Command recording is a CPU hot phase: 3.46 ms (79.3% of the renderer CPU frame).
   - Next experiment: Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost.
2. **Main scene rendering is the dominant GPU pass** (high, score 113.1)
   - Evidence: Main scene rendering is the dominant GPU pass: 1.44 ms (40.7% of the GPU frame).
   - Next experiment: Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture.
3. **Scene preparation is a CPU hot phase** (high, score 110.9)
   - Evidence: Scene preparation is a CPU hot phase: 1.60 ms (36.6% of the renderer CPU frame).
   - Next experiment: Profile culling, face generation, depth sorting, bounds-cache misses, and parallel chunk balance.
4. **Output and UI are a dominant GPU pass** (medium, score 86.4)
   - Evidence: Output and UI are a dominant GPU pass: 0.85 ms (24.0% of the GPU frame).
   - Next experiment: Inspect fullscreen bandwidth, tone mapping, UI overdraw, and final target transitions.
5. **SSAO is a dominant GPU pass** (medium, score 84.9)
   - Evidence: SSAO is a dominant GPU pass: 0.82 ms (23.0% of the GPU frame).
   - Next experiment: Measure occlusion resolution, sample count, snapshot bandwidth, and composite cost separately.
6. **A CPU scope dominates exclusive frame time** (medium, score 82.2)
   - Evidence: `Renderer.Prepare scene` accounts for 1.23 ms exclusive (17.0%) across 1 call(s).
   - Next experiment: Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.
