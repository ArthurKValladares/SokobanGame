# Render evidence — 100% scale, 4x MSAA

- Device: NVIDIA GeForce RTX 4060 Laptop GPU (discrete)
- Evidence location: level 5, screen 5
- Developer workspace: visible
- CPU profiler: enabled
- Effect fixture: none; 0 retained particles
- Authored water: disabled
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
- Draw calls: 57
- Triangles: 22222
- Prepared particles: 16
- Render passes: 11
- Point-shadow faces: 0 / 0 in range; 0 culled (0 face draws avoided)
- Point-shadow cube faces: 0 rendered, 0 reused
- Point-shadow quad submissions: 0 draws for 0 instances
- Point-shadow models: 0 / 0 in range; 0 culled
- Persistent renderables: 197 (visible 197, culled 0; bounds reused 197, rebuilt 0)
- Main-scene frustum culling: enabled
- Parallel scene preparation: enabled
- Point-shadow range/cache optimizations: enabled
- Recorder scratch reuse: enabled; 0 capacity growths this frame; 5120 bytes of model-recording capacity
- Asset scheduling: average 0.023 ms, p95 0.037 ms, maximum 0.057 ms (120 samples)
- Frame-fence wait: average 0.075 ms, p95 0.168 ms, maximum 0.392 ms (120 samples)
- Asset maintenance: average 0.384 ms, p95 0.616 ms, maximum 1.060 ms (120 samples)
- Image acquisition: average 0.018 ms, p95 0.033 ms, maximum 0.073 ms (120 samples)
- Command recording: average 1.644 ms, p95 2.646 ms, maximum 3.842 ms (120 samples)
- Submit/present: average 0.172 ms, p95 0.279 ms, maximum 0.781 ms (120 samples)
-   Recorder setup: average 0.067 ms, p95 0.133 ms, maximum 0.229 ms (120 samples)
-   Game command recording: average 1.130 ms, p95 1.815 ms, maximum 3.142 ms (120 samples)
-     Shadow command recording: average 0.436 ms, p95 0.631 ms, maximum 1.384 ms (120 samples)
-     Scene command recording: average 0.688 ms, p95 1.100 ms, maximum 1.787 ms (120 samples)
-   SSAO command recording: average 0.176 ms, p95 0.346 ms, maximum 0.631 ms (120 samples)
-   Atmosphere command recording: average 0.074 ms, p95 0.161 ms, maximum 0.333 ms (120 samples)
-   Preview command recording: unavailable
-   Output/UI command recording: average 0.223 ms, p95 0.344 ms, maximum 0.958 ms (120 samples)
- Asset publication events: average 0.670 ms, p95 2.151 ms, maximum 3.548 ms (54 samples)
- GPU shadows: average 0.051 ms, p95 0.060 ms, maximum 0.091 ms (120 samples)
- GPU scene color/depth: average 1.243 ms, p95 1.502 ms, maximum 2.088 ms (120 samples)
-   GPU scene raster/resolve: average 0.761 ms, p95 1.025 ms, maximum 1.607 ms (120 samples)
-     GPU scene surfaces: average 0.296 ms, p95 0.510 ms, maximum 1.010 ms (120 samples)
-     GPU scene models: average 0.076 ms, p95 0.103 ms, maximum 0.195 ms (120 samples)
-   GPU scene depth publish: average 0.001 ms, p95 0.001 ms, maximum 0.001 ms (120 samples)
-   GPU scene translucency: average 0.482 ms, p95 0.490 ms, maximum 0.593 ms (120 samples)
-   GPU mirror continuation: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU SSAO: average 0.720 ms, p95 0.731 ms, maximum 0.741 ms (120 samples)
-   GPU SSAO scene snapshot: average 0.170 ms, p95 0.174 ms, maximum 0.177 ms (120 samples)
-   GPU SSAO occlusion: average 0.231 ms, p95 0.239 ms, maximum 0.247 ms (120 samples)
-   GPU SSAO composite: average 0.319 ms, p95 0.322 ms, maximum 0.323 ms (120 samples)
- GPU volumetric atmosphere: average 0.394 ms, p95 0.408 ms, maximum 0.417 ms (120 samples)
-   GPU global atmosphere: average 0.394 ms, p95 0.408 ms, maximum 0.417 ms (120 samples)
-     GPU ray integration: average 0.098 ms, p95 0.102 ms, maximum 0.114 ms (120 samples)
-     GPU depth-aware composite: average 0.295 ms, p95 0.305 ms, maximum 0.307 ms (120 samples)
-   GPU bounded fog volumes: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU output/UI: average 0.791 ms, p95 0.818 ms, maximum 0.839 ms (120 samples)
- Asset publications: 54 across 54 frames
- Texture uploads: 25 submitted, 25 completed, 0 in flight
- Scene preparation: average 1.176 ms, p95 1.451 ms, maximum 2.217 ms (120 samples)
- Application frame (including profiler, excluding frame cap): average 4.982 ms, p95 6.483 ms, maximum 7.089 ms (120 samples)
- Frame interval (including pacing): average 4.995 ms, p95 6.485 ms, maximum 7.092 ms (120 samples)
- Frame pacing: average 0.001 ms, p95 0.002 ms, maximum 0.005 ms (120 samples)
- Application update: average 0.037 ms, p95 0.063 ms, maximum 0.216 ms (120 samples)
- Application UI: average 0.519 ms, p95 0.966 ms, maximum 1.537 ms (120 samples)
- Application frame build/prepare: average 1.779 ms, p95 2.147 ms, maximum 2.829 ms (120 samples)
- CPU frame: average 2.366 ms, p95 3.465 ms, maximum 4.629 ms (120 samples)
- GPU frame: average 3.203 ms, p95 3.480 ms, maximum 4.059 ms (120 samples)
- Process resident memory: 578.055 MiB (peak 578.055 MiB)
- GPU allocation memory: 785.643 MiB in 94 allocations; 876.438 MiB reserved in blocks
- Scene image: `scene-scale-100-msaa-4.png`
- Filtered SSAO image: `occlusion-scale-100-msaa-4.png`

Simulation: fixed 1/60 s steps. The final two images share the same simulation state and differ by the SSAO composite debug selector.

## Ranked optimization candidates

1. **Command recording is a CPU hot phase** (high, score 182.5)
   - Evidence: Command recording is a CPU hot phase: 2.65 ms (76.4% of the renderer CPU frame).
   - Next experiment: Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost.
2. **Scene preparation is a CPU hot phase** (high, score 120.4)
   - Evidence: Scene preparation is a CPU hot phase: 1.45 ms (41.9% of the renderer CPU frame).
   - Next experiment: Profile culling, face generation, depth sorting, bounds-cache misses, and parallel chunk balance.
3. **Main scene rendering is the dominant GPU pass** (high, score 117.1)
   - Evidence: Main scene rendering is the dominant GPU pass: 1.50 ms (43.2% of the GPU frame).
   - Next experiment: Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture.
4. **Output and UI are a dominant GPU pass** (medium, score 85.6)
   - Evidence: Output and UI are a dominant GPU pass: 0.82 ms (23.5% of the GPU frame).
   - Next experiment: Inspect fullscreen bandwidth, tone mapping, UI overdraw, and final target transitions.
5. **A CPU scope dominates exclusive frame time** (medium, score 83.9)
   - Evidence: `Application.UI` accounts for 1.21 ms exclusive (18.1%) across 1 call(s).
   - Next experiment: Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.
6. **Asset maintenance is a CPU hot phase** (medium, score 77.0)
   - Evidence: Asset maintenance is a CPU hot phase: 0.62 ms (17.8% of the renderer CPU frame).
   - Next experiment: Batch publication and reclamation work and move non-critical maintenance away from latency-sensitive frames.
7. **CPU frame pacing has a long tail** (medium, score 58.0)
   - Evidence: CPU standard deviation is 0.58 ms, p95 is 3.46 ms, and p99 is 4.18 ms.
   - Next experiment: Inspect the slowest trace frames for waits, asset publication, allocator growth, and uneven task chunks instead of optimizing only the average.
