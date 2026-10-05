# Render evidence — 100% scale, 4x MSAA

- Device: NVIDIA GeForce RTX 4060 Laptop GPU (discrete)
- Evidence location: level 5, screen 5
- Developer workspace: visible (Level Editor)
- CPU profiler: enabled
- Effect fixture: none; 0 retained particles
- Authored water: enabled
- Water reflections: enabled
- Swapchain: 1280x720
- Present mode: Mailbox
- Scene target: 1280x720
- SSAO target: 640x360
- Atmosphere target: 320x180
- Atmosphere coverage: 1 media, 921600 / 921600 full-resolution pixels (100.0% after scissoring)
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
- Water cell cache: enabled; 2097216 bytes; 10 rebuilds
- Recorder scratch reuse: enabled; 0 capacity growths this frame; 5120 bytes of model-recording capacity
- Asset scheduling: average 0.026 ms, p95 0.035 ms, maximum 0.070 ms (120 samples)
- Frame-fence wait: average 0.079 ms, p95 0.115 ms, maximum 0.224 ms (120 samples)
- Asset maintenance: average 0.399 ms, p95 0.520 ms, maximum 0.569 ms (120 samples)
- Image acquisition: average 0.020 ms, p95 0.030 ms, maximum 0.052 ms (120 samples)
- Command recording: average 1.746 ms, p95 2.295 ms, maximum 2.720 ms (120 samples)
- Submit/present: average 0.175 ms, p95 0.244 ms, maximum 0.326 ms (120 samples)
-   Recorder setup: average 0.078 ms, p95 0.115 ms, maximum 0.405 ms (120 samples)
-   Game command recording: average 1.215 ms, p95 1.587 ms, maximum 2.022 ms (120 samples)
-     Shadow command recording: average 0.448 ms, p95 0.569 ms, maximum 0.669 ms (120 samples)
-     Scene command recording: average 0.761 ms, p95 1.052 ms, maximum 1.346 ms (120 samples)
-   SSAO command recording: average 0.175 ms, p95 0.219 ms, maximum 0.388 ms (120 samples)
-   Atmosphere command recording: average 0.070 ms, p95 0.093 ms, maximum 0.130 ms (120 samples)
-   Preview command recording: unavailable
-   Output/UI command recording: average 0.215 ms, p95 0.287 ms, maximum 0.611 ms (120 samples)
- Asset publication events: average 0.385 ms, p95 0.877 ms, maximum 1.524 ms (54 samples)
- GPU shadows: average 0.068 ms, p95 0.071 ms, maximum 0.072 ms (120 samples)
- GPU scene color/depth: average 0.969 ms, p95 0.992 ms, maximum 1.026 ms (120 samples)
-   GPU scene raster/resolve: average 0.212 ms, p95 0.225 ms, maximum 0.255 ms (120 samples)
-     GPU scene surfaces: average 0.089 ms, p95 0.094 ms, maximum 0.120 ms (120 samples)
-     GPU scene models: average 0.051 ms, p95 0.054 ms, maximum 0.077 ms (120 samples)
-   GPU scene depth publish: average 0.001 ms, p95 0.001 ms, maximum 0.001 ms (120 samples)
-   GPU scene translucency: average 0.755 ms, p95 0.771 ms, maximum 0.790 ms (120 samples)
-     GPU particles: average 0.000 ms, p95 0.001 ms, maximum 0.001 ms (120 samples)
-   GPU mirror continuation: average 0.000 ms, p95 0.000 ms, maximum 0.001 ms (120 samples)
- GPU SSAO: average 0.160 ms, p95 0.164 ms, maximum 0.165 ms (120 samples)
-   GPU SSAO scene snapshot: average 0.029 ms, p95 0.031 ms, maximum 0.031 ms (120 samples)
-   GPU SSAO occlusion: average 0.068 ms, p95 0.070 ms, maximum 0.071 ms (120 samples)
-   GPU SSAO composite: average 0.063 ms, p95 0.065 ms, maximum 0.065 ms (120 samples)
- GPU volumetric atmosphere: average 0.112 ms, p95 0.115 ms, maximum 0.115 ms (120 samples)
-   GPU global atmosphere: average 0.112 ms, p95 0.115 ms, maximum 0.115 ms (120 samples)
-     GPU ray integration: average 0.031 ms, p95 0.032 ms, maximum 0.033 ms (120 samples)
-     GPU depth-aware composite: average 0.080 ms, p95 0.082 ms, maximum 0.082 ms (120 samples)
-   GPU bounded fog volumes: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU output/UI: average 0.155 ms, p95 0.162 ms, maximum 0.174 ms (120 samples)
- Asset publications: 54 across 54 frames
- Texture uploads: 25 submitted, 25 completed, 0 in flight
- Scene preparation: average 1.570 ms, p95 1.943 ms, maximum 2.071 ms (120 samples)
- Application frame (including profiler, excluding frame cap): average 5.679 ms, p95 6.748 ms, maximum 9.358 ms (120 samples)
- Frame interval (including pacing): average 5.682 ms, p95 6.751 ms, maximum 9.362 ms (120 samples)
- Frame pacing: average 0.001 ms, p95 0.002 ms, maximum 0.006 ms (120 samples)
- Application update: average 0.061 ms, p95 0.094 ms, maximum 0.157 ms (120 samples)
- Application UI: average 0.515 ms, p95 0.724 ms, maximum 0.916 ms (120 samples)
- Application frame build/prepare: average 2.255 ms, p95 2.755 ms, maximum 2.883 ms (120 samples)
- CPU frame: average 2.501 ms, p95 3.268 ms, maximum 3.826 ms (120 samples)
- GPU frame: average 1.468 ms, p95 1.504 ms, maximum 1.531 ms (120 samples)
- Process resident memory: 490.758 MiB (peak 490.809 MiB)
- GPU allocation memory: 465.088 MiB in 98 allocations; 539.875 MiB reserved in blocks
- Scene image: `scene-scale-100-msaa-4.png`
- Filtered SSAO image: `occlusion-scale-100-msaa-4.png`

Simulation: fixed 1/60 s steps. The final two images share the same simulation state and differ by the SSAO composite debug selector.

## Ranked optimization candidates

1. **Command recording is a CPU hot phase** (high, score 171.4)
   - Evidence: Command recording is a CPU hot phase: 2.29 ms (70.2% of the renderer CPU frame).
   - Next experiment: Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost.
2. **Main scene rendering is the dominant GPU pass** (high, score 153.6)
   - Evidence: Main scene rendering is the dominant GPU pass: 0.99 ms (66.0% of the GPU frame).
   - Next experiment: Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture.
3. **Scene preparation is a CPU hot phase** (high, score 152.0)
   - Evidence: Scene preparation is a CPU hot phase: 1.94 ms (59.4% of the renderer CPU frame).
   - Next experiment: Profile culling, face generation, depth sorting, bounds-cache misses, and parallel chunk balance.
4. **A CPU scope dominates exclusive frame time** (medium, score 87.6)
   - Evidence: `Renderer.Prepare scene` accounts for 1.31 ms exclusive (20.4%) across 1 call(s).
   - Next experiment: Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.
5. **Asset maintenance is a CPU hot phase** (medium, score 73.6)
   - Evidence: Asset maintenance is a CPU hot phase: 0.52 ms (15.9% of the renderer CPU frame).
   - Next experiment: Batch publication and reclamation work and move non-critical maintenance away from latency-sensitive frames.
