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
- Asset scheduling: average 0.657 ms, p95 0.770 ms, maximum 0.888 ms (120 samples)
- Frame-fence wait: average 0.125 ms, p95 0.246 ms, maximum 0.484 ms (120 samples)
- Asset maintenance: average 0.507 ms, p95 0.663 ms, maximum 1.224 ms (120 samples)
- Image acquisition: average 0.032 ms, p95 0.050 ms, maximum 0.140 ms (120 samples)
- Command recording: average 5.072 ms, p95 7.335 ms, maximum 7.945 ms (120 samples)
- Submit/present: average 0.225 ms, p95 0.341 ms, maximum 0.966 ms (120 samples)
-   Recorder setup: average 0.109 ms, p95 0.252 ms, maximum 0.325 ms (120 samples)
-   Game command recording: average 4.448 ms, p95 6.622 ms, maximum 7.196 ms (120 samples)
-     Shadow command recording: average 1.207 ms, p95 1.624 ms, maximum 2.091 ms (120 samples)
-     Scene command recording: average 3.233 ms, p95 5.391 ms, maximum 6.000 ms (120 samples)
-   SSAO command recording: average 0.192 ms, p95 0.290 ms, maximum 0.596 ms (120 samples)
-   Atmosphere command recording: average 0.073 ms, p95 0.122 ms, maximum 0.236 ms (120 samples)
-   Preview command recording: unavailable
-   Output/UI command recording: average 0.174 ms, p95 0.271 ms, maximum 0.539 ms (120 samples)
- Asset publication events: average 0.624 ms, p95 1.504 ms, maximum 1.829 ms (54 samples)
- GPU shadows: average 0.114 ms, p95 0.127 ms, maximum 0.142 ms (120 samples)
- GPU scene color/depth: average 3.291 ms, p95 3.683 ms, maximum 4.054 ms (120 samples)
-   GPU scene raster/resolve: average 1.219 ms, p95 1.327 ms, maximum 1.743 ms (120 samples)
-     GPU scene surfaces: average 0.389 ms, p95 0.454 ms, maximum 0.715 ms (120 samples)
-     GPU scene models: average 0.381 ms, p95 0.429 ms, maximum 0.613 ms (120 samples)
-   GPU scene depth publish: average 0.001 ms, p95 0.001 ms, maximum 0.001 ms (120 samples)
-   GPU scene translucency: average 2.072 ms, p95 2.310 ms, maximum 2.726 ms (120 samples)
-     GPU particles: average 1.070 ms, p95 1.202 ms, maximum 1.397 ms (120 samples)
-   GPU mirror continuation: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU SSAO: average 0.764 ms, p95 0.769 ms, maximum 0.771 ms (120 samples)
-   GPU SSAO scene snapshot: average 0.174 ms, p95 0.178 ms, maximum 0.179 ms (120 samples)
-   GPU SSAO occlusion: average 0.258 ms, p95 0.262 ms, maximum 0.264 ms (120 samples)
-   GPU SSAO composite: average 0.333 ms, p95 0.334 ms, maximum 0.334 ms (120 samples)
- GPU volumetric atmosphere: average 0.373 ms, p95 0.376 ms, maximum 0.381 ms (120 samples)
-   GPU global atmosphere: average 0.373 ms, p95 0.376 ms, maximum 0.381 ms (120 samples)
-     GPU ray integration: average 0.093 ms, p95 0.095 ms, maximum 0.100 ms (120 samples)
-     GPU depth-aware composite: average 0.279 ms, p95 0.281 ms, maximum 0.282 ms (120 samples)
-   GPU bounded fog volumes: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU output/UI: average 0.675 ms, p95 0.681 ms, maximum 0.684 ms (120 samples)
- Asset publications: 54 across 54 frames
- Texture uploads: 25 submitted, 25 completed, 0 in flight
- Scene preparation: average 6.096 ms, p95 6.906 ms, maximum 8.915 ms (120 samples)
- Application frame (including profiler, excluding frame cap): average 14.179 ms, p95 16.749 ms, maximum 17.570 ms (120 samples)
- Frame interval (including pacing): average 14.174 ms, p95 16.755 ms, maximum 17.573 ms (120 samples)
- Frame pacing: average 0.001 ms, p95 0.002 ms, maximum 0.007 ms (120 samples)
- Application update: average 0.058 ms, p95 0.083 ms, maximum 0.306 ms (120 samples)
- Application UI: average 0.172 ms, p95 0.254 ms, maximum 0.573 ms (120 samples)
- Application frame build/prepare: average 6.901 ms, p95 7.955 ms, maximum 10.513 ms (120 samples)
- CPU frame: average 6.678 ms, p95 8.895 ms, maximum 9.652 ms (120 samples)
- GPU frame: average 5.222 ms, p95 5.634 ms, maximum 5.988 ms (120 samples)
- Process resident memory: 566.633 MiB (peak 568.000 MiB)
- GPU allocation memory: 660.518 MiB in 50 allocations; 684.438 MiB reserved in blocks
- Scene image: `scene-scale-100-msaa-4.png`
- Filtered SSAO image: `occlusion-scale-100-msaa-4.png`

Simulation: fixed 1/60 s steps. The final two images share the same simulation state and differ by the SSAO composite debug selector.

## Ranked optimization candidates

1. **Command recording is a CPU hot phase** (high, score 193.4)
   - Evidence: Command recording is a CPU hot phase: 7.34 ms (82.5% of the renderer CPU frame).
   - Next experiment: Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost.
2. **Scene preparation is a CPU hot phase** (high, score 184.7)
   - Evidence: Scene preparation is a CPU hot phase: 6.91 ms (77.6% of the renderer CPU frame).
   - Next experiment: Profile culling, face generation, depth sorting, bounds-cache misses, and parallel chunk balance.
3. **Main scene rendering is the dominant GPU pass** (high, score 152.6)
   - Evidence: Main scene rendering is the dominant GPU pass: 3.68 ms (65.4% of the GPU frame).
   - Next experiment: Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture.
4. **A CPU scope dominates exclusive frame time** (high, score 118.6)
   - Evidence: `Renderer.Prepare scene` accounts for 6.29 ms exclusive (39.8%) across 1 call(s).
   - Next experiment: Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.
