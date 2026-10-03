# Render evidence — 100% scale, 4x MSAA

- Device: NVIDIA GeForce RTX 4060 Laptop GPU (discrete)
- Evidence location: level 5, screen 3
- Developer workspace: hidden
- CPU profiler: enabled
- Effect fixture: mixed-stress; 4576 retained particles
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
- Draw calls: 132
- Triangles: 179092
- Prepared particles: 4592
- Particle draw calls: 2
- Special-surface coverage: 0 water faces, 1608 energy faces, 66 energy models, 32 blurred ice models
- Render passes: 11
- Point-shadow faces: 0 / 0 in range; 0 culled (0 face draws avoided)
- Point-shadow cube faces: 0 rendered, 0 reused
- Point-shadow quad submissions: 0 draws for 0 instances
- Point-shadow models: 0 / 0 in range; 0 culled
- Persistent renderables: 916 (visible 916, culled 0; bounds reused 916, rebuilt 0)
- Main-scene frustum culling: enabled
- Parallel scene preparation: enabled
- Point-shadow range/cache optimizations: enabled
- Recorder scratch reuse: enabled; 0 capacity growths this frame; 64112 bytes of model-recording capacity
- Asset scheduling: average 0.684 ms, p95 0.835 ms, maximum 1.163 ms (120 samples)
- Frame-fence wait: average 0.115 ms, p95 0.193 ms, maximum 0.337 ms (120 samples)
- Asset maintenance: average 0.499 ms, p95 0.755 ms, maximum 1.015 ms (120 samples)
- Image acquisition: average 0.031 ms, p95 0.051 ms, maximum 0.097 ms (120 samples)
- Command recording: average 5.077 ms, p95 7.107 ms, maximum 7.791 ms (120 samples)
- Submit/present: average 0.235 ms, p95 0.421 ms, maximum 0.948 ms (120 samples)
-   Recorder setup: average 0.106 ms, p95 0.164 ms, maximum 0.381 ms (120 samples)
-   Game command recording: average 4.431 ms, p95 6.535 ms, maximum 7.221 ms (120 samples)
-     Shadow command recording: average 1.175 ms, p95 1.433 ms, maximum 1.756 ms (120 samples)
-     Scene command recording: average 3.248 ms, p95 5.307 ms, maximum 5.958 ms (120 samples)
-   SSAO command recording: average 0.207 ms, p95 0.344 ms, maximum 0.852 ms (120 samples)
-   Atmosphere command recording: average 0.077 ms, p95 0.149 ms, maximum 0.229 ms (120 samples)
-   Preview command recording: unavailable
-   Output/UI command recording: average 0.179 ms, p95 0.258 ms, maximum 0.535 ms (120 samples)
- Asset publication events: average 0.716 ms, p95 1.467 ms, maximum 1.574 ms (54 samples)
- GPU shadows: average 0.113 ms, p95 0.116 ms, maximum 0.129 ms (120 samples)
- GPU scene color/depth: average 3.326 ms, p95 3.680 ms, maximum 4.382 ms (120 samples)
-   GPU scene raster/resolve: average 1.234 ms, p95 1.462 ms, maximum 1.686 ms (120 samples)
-     GPU scene surfaces: average 0.396 ms, p95 0.506 ms, maximum 0.660 ms (120 samples)
-     GPU scene models: average 0.388 ms, p95 0.481 ms, maximum 0.575 ms (120 samples)
-   GPU scene depth publish: average 0.001 ms, p95 0.001 ms, maximum 0.001 ms (120 samples)
-   GPU scene translucency: average 2.092 ms, p95 2.375 ms, maximum 2.732 ms (120 samples)
-     GPU particles: average 1.073 ms, p95 1.258 ms, maximum 1.365 ms (120 samples)
-   GPU mirror continuation: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU SSAO: average 0.767 ms, p95 0.771 ms, maximum 0.791 ms (120 samples)
-   GPU SSAO scene snapshot: average 0.176 ms, p95 0.179 ms, maximum 0.182 ms (120 samples)
-   GPU SSAO occlusion: average 0.259 ms, p95 0.262 ms, maximum 0.284 ms (120 samples)
-   GPU SSAO composite: average 0.332 ms, p95 0.333 ms, maximum 0.334 ms (120 samples)
- GPU volumetric atmosphere: average 0.376 ms, p95 0.377 ms, maximum 0.396 ms (120 samples)
-   GPU global atmosphere: average 0.376 ms, p95 0.377 ms, maximum 0.395 ms (120 samples)
-     GPU ray integration: average 0.093 ms, p95 0.095 ms, maximum 0.114 ms (120 samples)
-     GPU depth-aware composite: average 0.281 ms, p95 0.283 ms, maximum 0.283 ms (120 samples)
-   GPU bounded fog volumes: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU output/UI: average 0.675 ms, p95 0.680 ms, maximum 0.686 ms (120 samples)
- Asset publications: 54 across 54 frames
- Texture uploads: 25 submitted, 25 completed, 0 in flight
- Scene preparation: average 6.153 ms, p95 6.885 ms, maximum 7.677 ms (120 samples)
- Application frame (including profiler, excluding frame cap): average 14.472 ms, p95 16.758 ms, maximum 18.267 ms (120 samples)
- Frame interval (including pacing): average 14.447 ms, p95 16.796 ms, maximum 18.273 ms (120 samples)
- Frame pacing: average 0.002 ms, p95 0.003 ms, maximum 0.036 ms (120 samples)
- Application update: average 0.064 ms, p95 0.124 ms, maximum 0.240 ms (120 samples)
- Application UI: average 0.191 ms, p95 0.306 ms, maximum 0.769 ms (120 samples)
- Application frame build/prepare: average 7.125 ms, p95 8.922 ms, maximum 10.379 ms (120 samples)
- CPU frame: average 6.702 ms, p95 8.714 ms, maximum 9.444 ms (120 samples)
- GPU frame: average 5.261 ms, p95 5.614 ms, maximum 6.326 ms (120 samples)
- Process resident memory: 560.605 MiB (peak 561.332 MiB)
- GPU allocation memory: 660.518 MiB in 50 allocations; 684.438 MiB reserved in blocks
- Scene image: `scene-scale-100-msaa-4.png`
- Filtered SSAO image: `occlusion-scale-100-msaa-4.png`

Simulation: fixed 1/60 s steps. The final two images share the same simulation state and differ by the SSAO composite debug selector.

## Ranked optimization candidates

1. **Command recording is a CPU hot phase** (high, score 191.8)
   - Evidence: Command recording is a CPU hot phase: 7.11 ms (81.6% of the renderer CPU frame).
   - Next experiment: Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost.
2. **Scene preparation is a CPU hot phase** (high, score 187.2)
   - Evidence: Scene preparation is a CPU hot phase: 6.89 ms (79.0% of the renderer CPU frame).
   - Next experiment: Profile culling, face generation, depth sorting, bounds-cache misses, and parallel chunk balance.
3. **Main scene rendering is the dominant GPU pass** (high, score 152.9)
   - Evidence: Main scene rendering is the dominant GPU pass: 3.68 ms (65.6% of the GPU frame).
   - Next experiment: Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture.
4. **A CPU scope dominates exclusive frame time** (high, score 116.7)
   - Evidence: `Renderer.Prepare scene` accounts for 5.82 ms exclusive (38.6%) across 1 call(s).
   - Next experiment: Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.
