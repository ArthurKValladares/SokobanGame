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
- Asset scheduling: average 0.670 ms, p95 0.775 ms, maximum 0.913 ms (120 samples)
- Frame-fence wait: average 0.139 ms, p95 0.318 ms, maximum 0.405 ms (120 samples)
- Asset maintenance: average 0.532 ms, p95 0.703 ms, maximum 1.156 ms (120 samples)
- Image acquisition: average 0.035 ms, p95 0.051 ms, maximum 0.097 ms (120 samples)
- Command recording: average 8.093 ms, p95 10.097 ms, maximum 11.205 ms (120 samples)
- Submit/present: average 0.259 ms, p95 0.400 ms, maximum 1.266 ms (120 samples)
-   Recorder setup: average 0.118 ms, p95 0.181 ms, maximum 0.432 ms (120 samples)
-   Game command recording: average 7.345 ms, p95 9.313 ms, maximum 9.890 ms (120 samples)
-     Shadow command recording: average 1.188 ms, p95 1.626 ms, maximum 2.147 ms (120 samples)
-     Scene command recording: average 6.146 ms, p95 8.107 ms, maximum 8.675 ms (120 samples)
-   SSAO command recording: average 0.262 ms, p95 0.719 ms, maximum 0.953 ms (120 samples)
-   Atmosphere command recording: average 0.088 ms, p95 0.239 ms, maximum 0.304 ms (120 samples)
-   Preview command recording: unavailable
-   Output/UI command recording: average 0.206 ms, p95 0.480 ms, maximum 1.196 ms (120 samples)
- Asset publication events: average 0.653 ms, p95 1.448 ms, maximum 1.753 ms (54 samples)
- GPU shadows: average 0.114 ms, p95 0.117 ms, maximum 0.143 ms (120 samples)
- GPU scene color/depth: average 3.291 ms, p95 3.580 ms, maximum 3.932 ms (120 samples)
-   GPU scene raster/resolve: average 1.214 ms, p95 1.298 ms, maximum 1.675 ms (120 samples)
-     GPU scene surfaces: average 0.386 ms, p95 0.433 ms, maximum 0.658 ms (120 samples)
-     GPU scene models: average 0.379 ms, p95 0.420 ms, maximum 0.568 ms (120 samples)
-   GPU scene depth publish: average 0.001 ms, p95 0.001 ms, maximum 0.001 ms (120 samples)
-   GPU scene translucency: average 2.077 ms, p95 2.323 ms, maximum 2.646 ms (120 samples)
-     GPU particles: average 1.071 ms, p95 1.203 ms, maximum 1.362 ms (120 samples)
-   GPU mirror continuation: average 0.000 ms, p95 0.000 ms, maximum 0.001 ms (120 samples)
- GPU SSAO: average 0.765 ms, p95 0.771 ms, maximum 0.794 ms (120 samples)
-   GPU SSAO scene snapshot: average 0.174 ms, p95 0.177 ms, maximum 0.182 ms (120 samples)
-   GPU SSAO occlusion: average 0.258 ms, p95 0.263 ms, maximum 0.286 ms (120 samples)
-   GPU SSAO composite: average 0.333 ms, p95 0.334 ms, maximum 0.335 ms (120 samples)
- GPU volumetric atmosphere: average 0.373 ms, p95 0.375 ms, maximum 0.377 ms (120 samples)
-   GPU global atmosphere: average 0.373 ms, p95 0.375 ms, maximum 0.377 ms (120 samples)
-     GPU ray integration: average 0.093 ms, p95 0.095 ms, maximum 0.096 ms (120 samples)
-     GPU depth-aware composite: average 0.279 ms, p95 0.281 ms, maximum 0.281 ms (120 samples)
-   GPU bounded fog volumes: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU output/UI: average 0.675 ms, p95 0.683 ms, maximum 0.707 ms (120 samples)
- Asset publications: 54 across 54 frames
- Texture uploads: 25 submitted, 25 completed, 0 in flight
- Scene preparation: average 5.870 ms, p95 6.758 ms, maximum 7.852 ms (120 samples)
- Application frame (including profiler, excluding frame cap): average 17.200 ms, p95 20.441 ms, maximum 21.124 ms (120 samples)
- Frame interval (including pacing): average 17.194 ms, p95 20.255 ms, maximum 21.128 ms (120 samples)
- Frame pacing: average 0.002 ms, p95 0.003 ms, maximum 0.005 ms (120 samples)
- Application update: average 0.064 ms, p95 0.099 ms, maximum 0.289 ms (120 samples)
- Application UI: average 0.186 ms, p95 0.292 ms, maximum 0.690 ms (120 samples)
- Application frame build/prepare: average 6.777 ms, p95 8.613 ms, maximum 9.438 ms (120 samples)
- CPU frame: average 9.796 ms, p95 11.986 ms, maximum 13.892 ms (120 samples)
- GPU frame: average 5.223 ms, p95 5.514 ms, maximum 5.858 ms (120 samples)
- Process resident memory: 567.098 MiB (peak 568.613 MiB)
- GPU allocation memory: 660.518 MiB in 50 allocations; 684.438 MiB reserved in blocks
- Scene image: `scene-scale-100-msaa-4.png`
- Filtered SSAO image: `occlusion-scale-100-msaa-4.png`

Simulation: fixed 1/60 s steps. The final two images share the same simulation state and differ by the SSAO composite debug selector.

## Ranked optimization candidates

1. **Command recording is a CPU hot phase** (high, score 196.6)
   - Evidence: Command recording is a CPU hot phase: 10.10 ms (84.2% of the renderer CPU frame).
   - Next experiment: Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost.
2. **Main scene rendering is the dominant GPU pass** (high, score 151.9)
   - Evidence: Main scene rendering is the dominant GPU pass: 3.58 ms (64.9% of the GPU frame).
   - Next experiment: Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture.
3. **Scene preparation is a CPU hot phase** (high, score 146.5)
   - Evidence: Scene preparation is a CPU hot phase: 6.76 ms (56.4% of the renderer CPU frame).
   - Next experiment: Profile culling, face generation, depth sorting, bounds-cache misses, and parallel chunk balance.
4. **A CPU scope dominates exclusive frame time** (high, score 112.4)
   - Evidence: `Renderer.Prepare scene` accounts for 5.65 ms exclusive (35.9%) across 1 call(s).
   - Next experiment: Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.
