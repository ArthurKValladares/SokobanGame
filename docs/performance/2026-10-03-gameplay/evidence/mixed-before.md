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
- Draw calls: 4722
- Triangles: 179092
- Prepared particles: 4592
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
- Asset scheduling: average 0.791 ms, p95 1.276 ms, maximum 3.130 ms (120 samples)
- Frame-fence wait: average 0.175 ms, p95 0.502 ms, maximum 0.649 ms (120 samples)
- Asset maintenance: average 0.668 ms, p95 1.531 ms, maximum 2.182 ms (120 samples)
- Image acquisition: average 0.044 ms, p95 0.114 ms, maximum 0.197 ms (120 samples)
- Command recording: average 15.004 ms, p95 17.984 ms, maximum 19.819 ms (120 samples)
- Submit/present: average 0.364 ms, p95 1.014 ms, maximum 1.471 ms (120 samples)
-   Recorder setup: average 0.150 ms, p95 0.376 ms, maximum 0.650 ms (120 samples)
-   Game command recording: average 13.934 ms, p95 16.387 ms, maximum 18.082 ms (120 samples)
-     Shadow command recording: average 1.380 ms, p95 2.434 ms, maximum 3.347 ms (120 samples)
-     Scene command recording: average 12.536 ms, p95 14.609 ms, maximum 16.380 ms (120 samples)
-   SSAO command recording: average 0.417 ms, p95 1.258 ms, maximum 1.463 ms (120 samples)
-   Atmosphere command recording: average 0.122 ms, p95 0.375 ms, maximum 0.503 ms (120 samples)
-   Preview command recording: unavailable
-   Output/UI command recording: average 0.384 ms, p95 1.240 ms, maximum 1.362 ms (120 samples)
- Asset publication events: average 0.949 ms, p95 3.570 ms, maximum 4.276 ms (54 samples)
- GPU shadows: average 0.192 ms, p95 0.199 ms, maximum 0.209 ms (120 samples)
- GPU scene color/depth: average 4.748 ms, p95 5.349 ms, maximum 5.795 ms (120 samples)
-   GPU scene raster/resolve: average 1.660 ms, p95 2.052 ms, maximum 2.206 ms (120 samples)
-     GPU scene surfaces: average 0.558 ms, p95 0.753 ms, maximum 0.864 ms (120 samples)
-     GPU scene models: average 0.579 ms, p95 0.736 ms, maximum 0.857 ms (120 samples)
-   GPU scene depth publish: average 0.001 ms, p95 0.001 ms, maximum 0.001 ms (120 samples)
-   GPU scene translucency: average 3.087 ms, p95 3.583 ms, maximum 3.661 ms (120 samples)
-   GPU mirror continuation: average 0.000 ms, p95 0.000 ms, maximum 0.001 ms (120 samples)
- GPU SSAO: average 1.017 ms, p95 1.033 ms, maximum 1.051 ms (120 samples)
-   GPU SSAO scene snapshot: average 0.181 ms, p95 0.185 ms, maximum 0.187 ms (120 samples)
-   GPU SSAO occlusion: average 0.408 ms, p95 0.418 ms, maximum 0.437 ms (120 samples)
-   GPU SSAO composite: average 0.428 ms, p95 0.435 ms, maximum 0.436 ms (120 samples)
- GPU volumetric atmosphere: average 0.610 ms, p95 0.623 ms, maximum 0.632 ms (120 samples)
-   GPU global atmosphere: average 0.610 ms, p95 0.623 ms, maximum 0.632 ms (120 samples)
-     GPU ray integration: average 0.152 ms, p95 0.156 ms, maximum 0.178 ms (120 samples)
-     GPU depth-aware composite: average 0.456 ms, p95 0.466 ms, maximum 0.467 ms (120 samples)
-   GPU bounded fog volumes: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU output/UI: average 0.996 ms, p95 1.034 ms, maximum 1.055 ms (120 samples)
- Asset publications: 54 across 54 frames
- Texture uploads: 25 submitted, 25 completed, 0 in flight
- Scene preparation: average 6.524 ms, p95 8.469 ms, maximum 9.894 ms (120 samples)
- Application frame (including profiler, excluding frame cap): average 26.120 ms, p95 29.444 ms, maximum 30.700 ms (120 samples)
- Frame interval (including pacing): average 26.119 ms, p95 29.448 ms, maximum 30.704 ms (120 samples)
- Frame pacing: average 0.003 ms, p95 0.008 ms, maximum 0.040 ms (120 samples)
- Application update: average 0.089 ms, p95 0.230 ms, maximum 0.510 ms (120 samples)
- Application UI: average 0.818 ms, p95 2.272 ms, maximum 3.271 ms (120 samples)
- Application frame build/prepare: average 7.491 ms, p95 9.708 ms, maximum 11.729 ms (120 samples)
- CPU frame: average 17.143 ms, p95 20.814 ms, maximum 22.791 ms (120 samples)
- GPU frame: average 7.568 ms, p95 8.160 ms, maximum 8.654 ms (120 samples)
- Process resident memory: 580.848 MiB (peak 580.848 MiB)
- GPU allocation memory: 785.643 MiB in 94 allocations; 876.438 MiB reserved in blocks
- Scene image: `scene-scale-100-msaa-4.png`
- Filtered SSAO image: `occlusion-scale-100-msaa-4.png`

Simulation: fixed 1/60 s steps. The final two images share the same simulation state and differ by the SSAO composite debug selector.

## Ranked optimization candidates

1. **Command recording is a CPU hot phase** (high, score 220.5)
   - Evidence: Command recording is a CPU hot phase: 17.98 ms (86.4% of the renderer CPU frame).
   - Next experiment: Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost.
2. **Main scene rendering is the dominant GPU pass** (high, score 152.9)
   - Evidence: Main scene rendering is the dominant GPU pass: 5.35 ms (65.6% of the GPU frame).
   - Next experiment: Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture.
3. **Scene preparation is a CPU hot phase** (high, score 138.2)
   - Evidence: Scene preparation is a CPU hot phase: 8.47 ms (40.7% of the renderer CPU frame).
   - Next experiment: Profile culling, face generation, depth sorting, bounds-cache misses, and parallel chunk balance.
4. **A CPU scope dominates exclusive frame time** (high, score 127.0)
   - Evidence: `Renderer.Record scene pass` accounts for 11.93 ms exclusive (45.0%) across 2 call(s).
   - Next experiment: Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.
5. **CPU frame time exceeds the target** (high, score 108.7)
   - Evidence: CPU p95/latest is 20.81 ms against a 16.67 ms budget; GPU is 8.16 ms
   - Next experiment: Start with the largest exclusive CPU hot path and the largest renderer CPU phase; capture a trace before changing code.
6. **Draw submission is unusually fine grained** (high, score 102.0)
   - Evidence: 4722 draws submit 179092 triangles (37.9 triangles/draw).
   - Next experiment: Prioritize instancing, compatible material sorting, and eliminating tiny or duplicate submissions; verify that draw count and command-recording time fall together.
