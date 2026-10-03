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
- Asset scheduling: average 0.050 ms, p95 0.081 ms, maximum 0.112 ms (120 samples)
- Frame-fence wait: average 0.192 ms, p95 0.298 ms, maximum 2.451 ms (120 samples)
- Asset maintenance: average 0.668 ms, p95 0.972 ms, maximum 1.555 ms (120 samples)
- Image acquisition: average 0.038 ms, p95 0.066 ms, maximum 0.093 ms (120 samples)
- Command recording: average 3.299 ms, p95 4.266 ms, maximum 5.649 ms (120 samples)
- Submit/present: average 0.381 ms, p95 0.610 ms, maximum 1.141 ms (120 samples)
-   Recorder setup: average 0.136 ms, p95 0.219 ms, maximum 0.422 ms (120 samples)
-   Game command recording: average 1.864 ms, p95 2.472 ms, maximum 3.716 ms (120 samples)
-     Shadow command recording: average 0.860 ms, p95 1.162 ms, maximum 2.128 ms (120 samples)
-     Scene command recording: average 0.992 ms, p95 1.382 ms, maximum 1.734 ms (120 samples)
-   SSAO command recording: average 0.283 ms, p95 0.488 ms, maximum 0.696 ms (120 samples)
-   Atmosphere command recording: average 0.111 ms, p95 0.180 ms, maximum 0.260 ms (120 samples)
-   Preview command recording: unavailable
-   Output/UI command recording: average 0.874 ms, p95 1.268 ms, maximum 1.784 ms (120 samples)
- Asset publication events: average 0.664 ms, p95 1.237 ms, maximum 2.461 ms (54 samples)
- GPU shadows: average 0.129 ms, p95 0.334 ms, maximum 1.078 ms (120 samples)
- GPU scene color/depth: average 4.330 ms, p95 5.134 ms, maximum 5.753 ms (120 samples)
-   GPU scene raster/resolve: average 2.891 ms, p95 3.493 ms, maximum 3.627 ms (120 samples)
-     GPU scene surfaces: average 0.464 ms, p95 0.570 ms, maximum 0.697 ms (120 samples)
-     GPU scene models: average 1.136 ms, p95 1.534 ms, maximum 1.641 ms (120 samples)
-   GPU scene depth publish: average 0.001 ms, p95 0.001 ms, maximum 0.001 ms (120 samples)
-   GPU scene translucency: average 1.438 ms, p95 2.227 ms, maximum 2.495 ms (120 samples)
-     GPU particles: average 0.019 ms, p95 0.003 ms, maximum 1.109 ms (120 samples)
-   GPU mirror continuation: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU SSAO: average 2.661 ms, p95 3.670 ms, maximum 3.825 ms (120 samples)
-   GPU SSAO scene snapshot: average 0.785 ms, p95 1.198 ms, maximum 1.232 ms (120 samples)
-   GPU SSAO occlusion: average 0.763 ms, p95 1.370 ms, maximum 1.401 ms (120 samples)
-   GPU SSAO composite: average 1.113 ms, p95 1.430 ms, maximum 1.824 ms (120 samples)
- GPU volumetric atmosphere: average 1.442 ms, p95 2.402 ms, maximum 2.409 ms (120 samples)
-   GPU global atmosphere: average 1.442 ms, p95 2.402 ms, maximum 2.409 ms (120 samples)
-     GPU ray integration: average 0.365 ms, p95 1.120 ms, maximum 1.124 ms (120 samples)
-     GPU depth-aware composite: average 1.004 ms, p95 1.404 ms, maximum 1.433 ms (120 samples)
-   GPU bounded fog volumes: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU output/UI: average 3.474 ms, p95 4.696 ms, maximum 4.784 ms (120 samples)
- Asset publications: 54 across 54 frames
- Texture uploads: 25 submitted, 25 completed, 0 in flight
- Scene preparation: average 1.953 ms, p95 2.441 ms, maximum 3.142 ms (120 samples)
- Application frame (including profiler, excluding frame cap): average 14.244 ms, p95 18.275 ms, maximum 20.964 ms (120 samples)
- Frame interval (including pacing): average 14.273 ms, p95 18.281 ms, maximum 20.972 ms (120 samples)
- Frame pacing: average 0.002 ms, p95 0.004 ms, maximum 0.008 ms (120 samples)
- Application update: average 0.083 ms, p95 0.124 ms, maximum 0.217 ms (120 samples)
- Application UI: average 5.728 ms, p95 7.568 ms, maximum 9.315 ms (120 samples)
- Application frame build/prepare: average 2.930 ms, p95 3.724 ms, maximum 4.318 ms (120 samples)
- CPU frame: average 4.730 ms, p95 6.097 ms, maximum 7.912 ms (120 samples)
- GPU frame: average 12.154 ms, p95 14.589 ms, maximum 14.976 ms (120 samples)
- Process resident memory: 583.676 MiB (peak 583.676 MiB)
- GPU allocation memory: 785.643 MiB in 94 allocations; 876.438 MiB reserved in blocks
- Scene image: `scene-scale-100-msaa-4.png`
- Filtered SSAO image: `occlusion-scale-100-msaa-4.png`

Simulation: fixed 1/60 s steps. The final two images share the same simulation state and differ by the SSAO composite debug selector.

## Ranked optimization candidates

1. **Command recording is a CPU hot phase** (high, score 170.9)
   - Evidence: Command recording is a CPU hot phase: 4.27 ms (70.0% of the renderer CPU frame).
   - Next experiment: Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost.
2. **Scene preparation is a CPU hot phase** (high, score 117.1)
   - Evidence: Scene preparation is a CPU hot phase: 2.44 ms (40.0% of the renderer CPU frame).
   - Next experiment: Profile culling, face generation, depth sorting, bounds-cache misses, and parallel chunk balance.
3. **Main scene rendering is the dominant GPU pass** (high, score 104.3)
   - Evidence: Main scene rendering is the dominant GPU pass: 5.13 ms (35.2% of the GPU frame).
   - Next experiment: Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture.
4. **Output and UI are a dominant GPU pass** (high, score 99.5)
   - Evidence: Output and UI are a dominant GPU pass: 4.70 ms (32.2% of the GPU frame).
   - Next experiment: Inspect fullscreen bandwidth, tone mapping, UI overdraw, and final target transitions.
5. **SSAO is a dominant GPU pass** (medium, score 88.3)
   - Evidence: SSAO is a dominant GPU pass: 3.67 ms (25.2% of the GPU frame).
   - Next experiment: Measure occlusion resolution, sample count, snapshot bandwidth, and composite cost separately.
6. **A CPU scope dominates exclusive frame time** (medium, score 85.1)
   - Evidence: `Editor.Scan level directories` accounts for 3.57 ms exclusive (18.8%) across 1 call(s).
   - Next experiment: Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.
7. **Asset maintenance is a CPU hot phase** (medium, score 73.7)
   - Evidence: Asset maintenance is a CPU hot phase: 0.97 ms (15.9% of the renderer CPU frame).
   - Next experiment: Batch publication and reclamation work and move non-critical maintenance away from latency-sensitive frames.
