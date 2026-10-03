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
- Asset scheduling: average 0.049 ms, p95 0.088 ms, maximum 0.202 ms (120 samples)
- Frame-fence wait: average 0.156 ms, p95 0.288 ms, maximum 0.365 ms (120 samples)
- Asset maintenance: average 0.709 ms, p95 1.103 ms, maximum 2.208 ms (120 samples)
- Image acquisition: average 0.039 ms, p95 0.071 ms, maximum 0.131 ms (120 samples)
- Command recording: average 3.692 ms, p95 6.035 ms, maximum 8.500 ms (120 samples)
- Submit/present: average 0.486 ms, p95 0.847 ms, maximum 1.240 ms (120 samples)
-   Recorder setup: average 0.137 ms, p95 0.232 ms, maximum 0.404 ms (120 samples)
-   Game command recording: average 2.105 ms, p95 3.333 ms, maximum 4.806 ms (120 samples)
-     Shadow command recording: average 0.811 ms, p95 1.225 ms, maximum 1.497 ms (120 samples)
-     Scene command recording: average 1.280 ms, p95 2.354 ms, maximum 3.317 ms (120 samples)
-   SSAO command recording: average 0.316 ms, p95 0.607 ms, maximum 1.060 ms (120 samples)
-   Atmosphere command recording: average 0.129 ms, p95 0.244 ms, maximum 0.471 ms (120 samples)
-   Preview command recording: unavailable
-   Output/UI command recording: average 0.996 ms, p95 1.841 ms, maximum 2.587 ms (120 samples)
- Asset publication events: average 0.608 ms, p95 1.128 ms, maximum 1.394 ms (54 samples)
- GPU shadows: average 0.080 ms, p95 0.092 ms, maximum 0.104 ms (120 samples)
- GPU scene color/depth: average 31.025 ms, p95 81.229 ms, maximum 88.145 ms (120 samples)
-   GPU scene raster/resolve: average 3.908 ms, p95 14.061 ms, maximum 14.608 ms (120 samples)
-     GPU scene surfaces: average 0.364 ms, p95 0.421 ms, maximum 0.498 ms (120 samples)
-     GPU scene models: average 2.255 ms, p95 6.920 ms, maximum 7.138 ms (120 samples)
-   GPU scene depth publish: average 0.001 ms, p95 0.001 ms, maximum 0.002 ms (120 samples)
-   GPU scene translucency: average 27.115 ms, p95 73.557 ms, maximum 80.253 ms (120 samples)
-     GPU particles: average 0.467 ms, p95 0.889 ms, maximum 6.908 ms (120 samples)
-   GPU mirror continuation: average 0.000 ms, p95 0.000 ms, maximum 0.001 ms (120 samples)
- GPU SSAO: average 6.284 ms, p95 20.920 ms, maximum 21.298 ms (120 samples)
-   GPU SSAO scene snapshot: average 1.965 ms, p95 6.949 ms, maximum 7.079 ms (120 samples)
-   GPU SSAO occlusion: average 1.280 ms, p95 6.863 ms, maximum 7.230 ms (120 samples)
-   GPU SSAO composite: average 3.039 ms, p95 13.508 ms, maximum 13.908 ms (120 samples)
- GPU volumetric atmosphere: average 2.995 ms, p95 7.617 ms, maximum 14.251 ms (120 samples)
-   GPU global atmosphere: average 2.995 ms, p95 7.617 ms, maximum 14.251 ms (120 samples)
-     GPU ray integration: average 0.383 ms, p95 0.200 ms, maximum 7.160 ms (120 samples)
-     GPU depth-aware composite: average 2.263 ms, p95 7.221 ms, maximum 7.472 ms (120 samples)
-   GPU bounded fog volumes: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU output/UI: average 8.339 ms, p95 21.736 ms, maximum 28.447 ms (120 samples)
- Asset publications: 54 across 54 frames
- Texture uploads: 25 submitted, 25 completed, 0 in flight
- Scene preparation: average 2.385 ms, p95 3.660 ms, maximum 5.039 ms (120 samples)
- Application frame (including profiler, excluding frame cap): average 243.361 ms, p95 374.036 ms, maximum 514.985 ms (120 samples)
- Frame interval (including pacing): average 243.051 ms, p95 374.044 ms, maximum 515.000 ms (120 samples)
- Frame pacing: average 0.002 ms, p95 0.004 ms, maximum 0.005 ms (120 samples)
- Application update: average 0.093 ms, p95 0.166 ms, maximum 0.296 ms (120 samples)
- Application UI: average 232.392 ms, p95 355.378 ms, maximum 491.106 ms (120 samples)
- Application frame build/prepare: average 3.558 ms, p95 5.298 ms, maximum 7.960 ms (120 samples)
- CPU frame: average 5.243 ms, p95 8.499 ms, maximum 11.388 ms (120 samples)
- GPU frame: average 48.972 ms, p95 136.395 ms, maximum 145.085 ms (120 samples)
- Process resident memory: 579.891 MiB (peak 586.793 MiB)
- GPU allocation memory: 785.643 MiB in 94 allocations; 876.438 MiB reserved in blocks
- Scene image: `scene-scale-100-msaa-4.png`
- Filtered SSAO image: `occlusion-scale-100-msaa-4.png`

Simulation: fixed 1/60 s steps. The final two images share the same simulation state and differ by the SSAO composite debug selector.

## Ranked optimization candidates

1. **GPU frame time exceeds the target** (high, score 351.4)
   - Evidence: GPU p95/latest is 136.39 ms against a 16.67 ms budget; renderer CPU is 8.50 ms
   - Next experiment: Optimize the dominant GPU pass first, then verify at the same resolution, render scale, and MSAA setting.
2. **A CPU scope dominates exclusive frame time** (high, score 203.5)
   - Evidence: `Editor.Draw panel` accounts for 186.54 ms exclusive (92.8%) across 1 call(s).
   - Next experiment: Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.
3. **Command recording is a CPU hot phase** (high, score 172.8)
   - Evidence: Command recording is a CPU hot phase: 6.04 ms (71.0% of the renderer CPU frame).
   - Next experiment: Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost.
4. **Main scene rendering is the dominant GPU pass** (high, score 163.3)
   - Evidence: Main scene rendering is the dominant GPU pass: 81.23 ms (59.6% of the GPU frame).
   - Next experiment: Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture.
5. **Scene preparation is a CPU hot phase** (high, score 122.5)
   - Evidence: Scene preparation is a CPU hot phase: 3.66 ms (43.1% of the renderer CPU frame).
   - Next experiment: Profile culling, face generation, depth sorting, bounds-cache misses, and parallel chunk balance.
6. **CPU frame pacing has a long tail** (medium, score 58.0)
   - Evidence: CPU standard deviation is 1.54 ms, p95 is 8.50 ms, and p99 is 11.03 ms.
   - Next experiment: Inspect the slowest trace frames for waits, asset publication, allocator growth, and uneven task chunks instead of optimizing only the average.
