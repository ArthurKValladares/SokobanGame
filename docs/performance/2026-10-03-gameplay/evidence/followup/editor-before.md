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
- Asset scheduling: average 0.030 ms, p95 0.061 ms, maximum 0.095 ms (120 samples)
- Frame-fence wait: average 0.125 ms, p95 0.334 ms, maximum 0.444 ms (120 samples)
- Asset maintenance: average 0.436 ms, p95 0.556 ms, maximum 1.201 ms (120 samples)
- Image acquisition: average 0.027 ms, p95 0.040 ms, maximum 0.105 ms (120 samples)
- Command recording: average 1.979 ms, p95 2.493 ms, maximum 3.493 ms (120 samples)
- Submit/present: average 0.205 ms, p95 0.241 ms, maximum 0.284 ms (120 samples)
-   Recorder setup: average 0.094 ms, p95 0.126 ms, maximum 0.384 ms (120 samples)
-   Game command recording: average 1.111 ms, p95 1.317 ms, maximum 2.250 ms (120 samples)
-     Shadow command recording: average 0.511 ms, p95 0.613 ms, maximum 1.300 ms (120 samples)
-     Scene command recording: average 0.594 ms, p95 0.696 ms, maximum 1.371 ms (120 samples)
-   SSAO command recording: average 0.161 ms, p95 0.213 ms, maximum 0.275 ms (120 samples)
-   Atmosphere command recording: average 0.065 ms, p95 0.084 ms, maximum 0.124 ms (120 samples)
-   Preview command recording: unavailable
-   Output/UI command recording: average 0.517 ms, p95 0.591 ms, maximum 0.740 ms (120 samples)
- Asset publication events: average 0.554 ms, p95 1.081 ms, maximum 1.402 ms (54 samples)
- GPU shadows: average 0.057 ms, p95 0.057 ms, maximum 0.085 ms (120 samples)
- GPU scene color/depth: average 1.383 ms, p95 1.410 ms, maximum 1.423 ms (120 samples)
-   GPU scene raster/resolve: average 0.887 ms, p95 0.913 ms, maximum 0.929 ms (120 samples)
-     GPU scene surfaces: average 0.297 ms, p95 0.323 ms, maximum 0.328 ms (120 samples)
-     GPU scene models: average 0.184 ms, p95 0.209 ms, maximum 0.215 ms (120 samples)
-   GPU scene depth publish: average 0.001 ms, p95 0.001 ms, maximum 0.001 ms (120 samples)
-   GPU scene translucency: average 0.496 ms, p95 0.500 ms, maximum 0.505 ms (120 samples)
-     GPU particles: average 0.001 ms, p95 0.002 ms, maximum 0.002 ms (120 samples)
-   GPU mirror continuation: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU SSAO: average 0.741 ms, p95 0.745 ms, maximum 0.751 ms (120 samples)
-   GPU SSAO scene snapshot: average 0.172 ms, p95 0.175 ms, maximum 0.178 ms (120 samples)
-   GPU SSAO occlusion: average 0.248 ms, p95 0.252 ms, maximum 0.256 ms (120 samples)
-   GPU SSAO composite: average 0.321 ms, p95 0.323 ms, maximum 0.324 ms (120 samples)
- GPU volumetric atmosphere: average 0.369 ms, p95 0.372 ms, maximum 0.387 ms (120 samples)
-   GPU global atmosphere: average 0.369 ms, p95 0.372 ms, maximum 0.387 ms (120 samples)
-     GPU ray integration: average 0.093 ms, p95 0.095 ms, maximum 0.112 ms (120 samples)
-     GPU depth-aware composite: average 0.276 ms, p95 0.276 ms, maximum 0.279 ms (120 samples)
-   GPU bounded fog volumes: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU output/UI: average 0.991 ms, p95 1.018 ms, maximum 1.042 ms (120 samples)
- Asset publications: 54 across 54 frames
- Texture uploads: 25 submitted, 25 completed, 0 in flight
- Scene preparation: average 1.313 ms, p95 1.762 ms, maximum 2.460 ms (120 samples)
- Application frame (including profiler, excluding frame cap): average 8.352 ms, p95 9.800 ms, maximum 10.333 ms (120 samples)
- Frame interval (including pacing): average 8.360 ms, p95 9.802 ms, maximum 10.335 ms (120 samples)
- Frame pacing: average 0.001 ms, p95 0.001 ms, maximum 0.003 ms (120 samples)
- Application update: average 0.035 ms, p95 0.046 ms, maximum 0.090 ms (120 samples)
- Application UI: average 3.175 ms, p95 3.584 ms, maximum 4.337 ms (120 samples)
- Application frame build/prepare: average 1.962 ms, p95 2.448 ms, maximum 3.115 ms (120 samples)
- CPU frame: average 2.861 ms, p95 3.714 ms, maximum 4.620 ms (120 samples)
- GPU frame: average 3.546 ms, p95 3.587 ms, maximum 3.604 ms (120 samples)
- Process resident memory: 575.887 MiB (peak 577.668 MiB)
- GPU allocation memory: 785.643 MiB in 94 allocations; 876.438 MiB reserved in blocks
- Scene image: `scene-scale-100-msaa-4.png`
- Filtered SSAO image: `occlusion-scale-100-msaa-4.png`

Simulation: fixed 1/60 s steps. The final two images share the same simulation state and differ by the SSAO composite debug selector.

## Ranked optimization candidates

1. **Command recording is a CPU hot phase** (high, score 165.8)
   - Evidence: Command recording is a CPU hot phase: 2.49 ms (67.1% of the renderer CPU frame).
   - Next experiment: Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost.
2. **Scene preparation is a CPU hot phase** (high, score 130.4)
   - Evidence: Scene preparation is a CPU hot phase: 1.76 ms (47.4% of the renderer CPU frame).
   - Next experiment: Profile culling, face generation, depth sorting, bounds-cache misses, and parallel chunk balance.
3. **Main scene rendering is the dominant GPU pass** (high, score 110.9)
   - Evidence: Main scene rendering is the dominant GPU pass: 1.41 ms (39.3% of the GPU frame).
   - Next experiment: Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture.
4. **Output and UI are a dominant GPU pass** (high, score 93.4)
   - Evidence: Output and UI are a dominant GPU pass: 1.02 ms (28.4% of the GPU frame).
   - Next experiment: Inspect fullscreen bandwidth, tone mapping, UI overdraw, and final target transitions.
5. **A CPU scope dominates exclusive frame time** (medium, score 83.3)
   - Evidence: `Editor.Scan level directories` accounts for 1.67 ms exclusive (17.7%) across 1 call(s).
   - Next experiment: Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.
