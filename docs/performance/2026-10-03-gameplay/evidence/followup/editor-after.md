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
- Asset scheduling: average 0.025 ms, p95 0.031 ms, maximum 0.066 ms (120 samples)
- Frame-fence wait: average 0.075 ms, p95 0.120 ms, maximum 0.135 ms (120 samples)
- Asset maintenance: average 0.369 ms, p95 0.440 ms, maximum 0.495 ms (120 samples)
- Image acquisition: average 0.018 ms, p95 0.026 ms, maximum 0.032 ms (120 samples)
- Command recording: average 1.958 ms, p95 2.264 ms, maximum 2.966 ms (120 samples)
- Submit/present: average 0.192 ms, p95 0.247 ms, maximum 0.283 ms (120 samples)
-   Recorder setup: average 0.070 ms, p95 0.102 ms, maximum 0.152 ms (120 samples)
-   Game command recording: average 1.104 ms, p95 1.302 ms, maximum 1.621 ms (120 samples)
-     Shadow command recording: average 0.501 ms, p95 0.602 ms, maximum 0.680 ms (120 samples)
-     Scene command recording: average 0.597 ms, p95 0.743 ms, maximum 1.143 ms (120 samples)
-   SSAO command recording: average 0.166 ms, p95 0.209 ms, maximum 0.521 ms (120 samples)
-   Atmosphere command recording: average 0.066 ms, p95 0.082 ms, maximum 0.128 ms (120 samples)
-   Preview command recording: unavailable
-   Output/UI command recording: average 0.547 ms, p95 0.710 ms, maximum 1.495 ms (120 samples)
- Asset publication events: average 0.436 ms, p95 0.943 ms, maximum 1.820 ms (54 samples)
- GPU shadows: average 0.059 ms, p95 0.060 ms, maximum 0.087 ms (120 samples)
- GPU scene color/depth: average 1.410 ms, p95 1.516 ms, maximum 1.697 ms (120 samples)
-   GPU scene raster/resolve: average 0.909 ms, p95 1.013 ms, maximum 1.198 ms (120 samples)
-     GPU scene surfaces: average 0.309 ms, p95 0.377 ms, maximum 0.528 ms (120 samples)
-     GPU scene models: average 0.193 ms, p95 0.249 ms, maximum 0.298 ms (120 samples)
-   GPU scene depth publish: average 0.001 ms, p95 0.001 ms, maximum 0.001 ms (120 samples)
-   GPU scene translucency: average 0.501 ms, p95 0.513 ms, maximum 0.525 ms (120 samples)
-     GPU particles: average 0.001 ms, p95 0.002 ms, maximum 0.026 ms (120 samples)
-   GPU mirror continuation: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU SSAO: average 0.751 ms, p95 0.763 ms, maximum 0.767 ms (120 samples)
-   GPU SSAO scene snapshot: average 0.174 ms, p95 0.176 ms, maximum 0.176 ms (120 samples)
-   GPU SSAO occlusion: average 0.253 ms, p95 0.262 ms, maximum 0.265 ms (120 samples)
-   GPU SSAO composite: average 0.324 ms, p95 0.328 ms, maximum 0.328 ms (120 samples)
- GPU volumetric atmosphere: average 0.380 ms, p95 0.393 ms, maximum 0.415 ms (120 samples)
-   GPU global atmosphere: average 0.380 ms, p95 0.393 ms, maximum 0.415 ms (120 samples)
-     GPU ray integration: average 0.096 ms, p95 0.099 ms, maximum 0.125 ms (120 samples)
-     GPU depth-aware composite: average 0.283 ms, p95 0.291 ms, maximum 0.295 ms (120 samples)
-   GPU bounded fog volumes: average 0.000 ms, p95 0.000 ms, maximum 0.000 ms (120 samples)
- GPU output/UI: average 1.001 ms, p95 1.015 ms, maximum 1.047 ms (120 samples)
- Asset publications: 54 across 54 frames
- Texture uploads: 25 submitted, 25 completed, 0 in flight
- Scene preparation: average 1.179 ms, p95 1.324 ms, maximum 1.425 ms (120 samples)
- Application frame (including profiler, excluding frame cap): average 6.165 ms, p95 7.321 ms, maximum 8.821 ms (120 samples)
- Frame interval (including pacing): average 6.163 ms, p95 7.324 ms, maximum 8.824 ms (120 samples)
- Frame pacing: average 0.001 ms, p95 0.002 ms, maximum 0.008 ms (120 samples)
- Application update: average 0.041 ms, p95 0.071 ms, maximum 0.246 ms (120 samples)
- Application UI: average 1.388 ms, p95 2.085 ms, maximum 3.959 ms (120 samples)
- Application frame build/prepare: average 1.738 ms, p95 1.948 ms, maximum 2.789 ms (120 samples)
- CPU frame: average 2.686 ms, p95 3.075 ms, maximum 3.743 ms (120 samples)
- GPU frame: average 3.604 ms, p95 3.690 ms, maximum 3.889 ms (120 samples)
- Process resident memory: 577.355 MiB (peak 578.621 MiB)
- GPU allocation memory: 785.643 MiB in 94 allocations; 876.438 MiB reserved in blocks
- Scene image: `scene-scale-100-msaa-4.png`
- Filtered SSAO image: `occlusion-scale-100-msaa-4.png`

Simulation: fixed 1/60 s steps. The final two images share the same simulation state and differ by the SSAO composite debug selector.

## Ranked optimization candidates

1. **Command recording is a CPU hot phase** (high, score 177.6)
   - Evidence: Command recording is a CPU hot phase: 2.26 ms (73.7% of the renderer CPU frame).
   - Next experiment: Reduce state churn and command count, retain scratch capacity, and consider parallel recording only after measuring its scheduling cost.
2. **Scene preparation is a CPU hot phase** (high, score 122.5)
   - Evidence: Scene preparation is a CPU hot phase: 1.32 ms (43.1% of the renderer CPU frame).
   - Next experiment: Profile culling, face generation, depth sorting, bounds-cache misses, and parallel chunk balance.
3. **Main scene rendering is the dominant GPU pass** (high, score 113.7)
   - Evidence: Main scene rendering is the dominant GPU pass: 1.52 ms (41.1% of the GPU frame).
   - Next experiment: Inspect overdraw, material complexity, MSAA cost, visibility, and resolution scaling with a GPU capture.
4. **Output and UI are a dominant GPU pass** (high, score 92.0)
   - Evidence: Output and UI are a dominant GPU pass: 1.01 ms (27.5% of the GPU frame).
   - Next experiment: Inspect fullscreen bandwidth, tone mapping, UI overdraw, and final target transitions.
5. **A CPU scope dominates exclusive frame time** (medium, score 85.0)
   - Evidence: `Editor.Scan level directories` accounts for 1.70 ms exclusive (18.8%) across 1 call(s).
   - Next experiment: Drill into this scope with child instrumentation and optimize its exclusive work before its callers' inclusive totals.
