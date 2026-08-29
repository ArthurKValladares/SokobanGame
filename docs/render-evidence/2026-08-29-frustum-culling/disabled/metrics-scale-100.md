# Render evidence — 100% scale

- Device: NVIDIA GeForce RTX 4060 Laptop GPU (discrete)
- Swapchain: 1280x720
- Scene target: 1280x720
- SSAO target: 640x360
- Ambient occlusion: enabled
- MSAA samples: 4
- Draw calls: 51
- Triangles: 14504
- Render passes: 6
- Persistent renderables: 204 (visible 204, culled 0; bounds reused 204, rebuilt 0)
- Main-scene frustum culling: disabled
- Scene preparation: average 1.743 ms, p95 2.166 ms, maximum 2.324 ms (120 samples)
- CPU frame: average 5.504 ms, p95 7.135 ms, maximum 7.637 ms (120 samples)
- GPU frame: average 1.649 ms, p95 1.714 ms, maximum 1.720 ms (120 samples)
- Scene image: `scene-scale-100.png`
- Filtered SSAO image: `occlusion-scale-100.png`

The simulation was frozen for the run. The two images differ only by the SSAO composite debug selector.
