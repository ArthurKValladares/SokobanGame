# Render evidence — 100% scale, 4x MSAA

- Device: NVIDIA GeForce RTX 4060 Laptop GPU (discrete)
- Evidence location: level 5, screen 3
- Developer workspace: visible
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
- Asset scheduling: average 0.774 ms, p95 0.937 ms, maximum 1.286 ms (120 samples)
- Frame-fence wait: average 0.160 ms, p95 0.412 ms, maximum 0.505 ms (120 samples)
- Asset maintenance: average 0.608 ms, p95 1.341 ms, maximum 1.960 ms (120 samples)
- Image acquisition: average 0.040 ms, p95 0.116 ms, maximum 0.249 ms (120 samples)
- Command recording: average 7.747 ms, p95 9.453 ms, maximum 11.098 ms (120 samples)
- Submit/present: average 0.299 ms, p95 0.824 ms, maximum 1.356 ms (120 samples)
-   Recorder setup: average 0.129 ms, p95 0.372 ms, maximum 0.736 ms (120 samples)
-   Game command recording: average 6.907 ms, p95 8.455 ms, maximum 10.233 ms (120 samples)
-     Shadow command recording: average 1.260 ms, p95 1.608 ms, maximum 2.490 ms (120 samples)
-     Scene command recording: average 5.638 ms, p95 7.250 ms, maximum 8.793 ms (120 samples)
-   SSAO command recording: average 0.249 ms, p95 0.649 ms, maximum 0.842 ms (120 samples)
-   Atmosphere command recording: average 0.087 ms, p95 0.198 ms, maximum 0.352 ms (120 samples)
-   Preview command recording: unavailable
-   Output/UI command recording: average 0.292 ms, p95 0.736 ms, maximum 1.137 ms (120 samples)
- Asset publication events: average 0.756 ms, p95 1.917 ms, maximum 4.278 ms (54 samples)
- GPU shadows: average 0.114 ms, p95 0.122 ms, maximum 0.146 ms (120 samples)
- GPU scene color/depth: average 3.406 ms, p95 4.049 ms, maximum 4.496 ms (120 samples)
-   GPU scene raster/resolve: average 1.249 ms, p95 1.423 ms, maximum 1.896 ms (120 samples)
-     GPU scene surfaces: average 0.404 ms, p95 0.560 ms, maximum 0.772 ms (120 samples)
-     GPU scene models: average 0.394 ms, p95 0.497 ms, maximum 0.659 ms (120 samples)
-   GPU scene depth publish: average 0.001 ms, p95 0.001 ms, maximum 0.001 ms (120 samples)
-   GPU scene translucency: average 2.156 ms, p95 2.677 ms, maximum 2.942 ms (120 samples)
-     GPU particles: average 1.115 ms, p95 1.357 ms, maximum 1.478 ms (120 samples)
-   GPU mirror continuation: average 0.000 ms, p95 0.000 ms, maximum 0.001 ms (120 samples)
- GPU SSAO: average 0.766 ms, p95 0.771 ms, maximum 0.795 ms (120 samples)
-   GPU SSAO scene snapshot: average 0.174 ms, p95 0.177 ms, maximum 0.181 ms (120 samples)
-   GPU SSAO occlusion: average 0.259 ms, p95 0.263 ms, maximum 0.290 ms (120 samples)
-   GPU SSAO composite: average 0.333 ms, p95 0.334 ms, maximum 0.335 ms (120 samples)
- GPU volumetric atmosphere: average 0.375 ms, p95 0.377 ms, maximum 0.402 ms (120 samples)
-   GPU global atmosphere: average 0.375 ms, p95 0.377 ms, maximum 0.402 ms (120 samples)
-     GPU ray integration: average 0.094 ms, p95 0.096 ms, maximum 0.122 ms (120 samples)
-     GPU depth-aware composite: average 0.280 ms, p95 0.281 ms, maximum 0.282 ms (120 samples)
-   GPU bounded fog volumes: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU output/UI: average 0.827 ms, p95 0.862 ms, maximum 0.877 ms (120 samples)
- Asset publications: 54 across 54 frames
- Texture uploads: 25 submitted, 25 completed, 0 in flight
- Scene preparation: average 6.500 ms, p95 8.170 ms, maximum 9.583 ms (120 samples)
- Application frame (including profiler, excluding frame cap): average 18.403 ms, p95 21.137 ms, maximum 24.051 ms (120 samples)
- Frame interval (including pacing): average 18.389 ms, p95 21.140 ms, maximum 24.054 ms (120 samples)
- Frame pacing: average 0.002 ms, p95 0.004 ms, maximum 0.011 ms (120 samples)
- Application update: average 0.070 ms, p95 0.114 ms, maximum 0.286 ms (120 samples)
- Application UI: average 0.721 ms, p95 1.453 ms, maximum 3.882 ms (120 samples)
- Application frame build/prepare: average 7.426 ms, p95 9.262 ms, maximum 11.175 ms (120 samples)
- CPU frame: average 9.712 ms, p95 11.384 ms, maximum 12.845 ms (120 samples)
- GPU frame: average 5.492 ms, p95 6.128 ms, maximum 6.578 ms (120 samples)
- Process resident memory: 579.754 MiB (peak 579.754 MiB)
- GPU allocation memory: 785.643 MiB in 94 allocations; 876.438 MiB reserved in blocks
- Scene image: `scene-scale-100-msaa-4.png`
- Filtered SSAO image: `occlusion-scale-100-msaa-4.png`

Simulation: fixed 1/60 s steps. The final two images share the same simulation state and differ by the SSAO composite debug selector.

## Ranked optimization candidates

1. **Command recording is a CPU hot phase** (high, score 194.5)
   - Evidence: Command recording is a CPU hot phase: 9.45 ms (83.0% of the renderer CPU frame).
   - Next experiment: Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost.
2. **Scene preparation is a CPU hot phase** (high, score 174.2)
   - Evidence: Scene preparation is a CPU hot phase: 8.17 ms (71.8% of the renderer CPU frame).
   - Next experiment: Profile culling, face generation, depth sorting, bounds-cache misses, and parallel chunk balance.
3. **Main scene rendering is the dominant GPU pass** (high, score 153.7)
   - Evidence: Main scene rendering is the dominant GPU pass: 4.05 ms (66.1% of the GPU frame).
   - Next experiment: Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture.
4. **A CPU scope dominates exclusive frame time** (high, score 102.7)
   - Evidence: `Renderer.Prepare scene` accounts for 5.47 ms exclusive (29.8%) across 1 call(s).
   - Next experiment: Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.
