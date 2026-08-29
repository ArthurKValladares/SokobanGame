# Render evidence — 100% scale

- Device: NVIDIA GeForce RTX 4060 Laptop GPU (discrete)
- Swapchain: 1280x720
- Scene target: 1280x720
- SSAO target: 640x360
- Ambient occlusion: enabled
- MSAA samples: 4
- Draw calls: 43
- Triangles: 14068
- Render passes: 6
- Persistent renderables: 204 (visible 130, culled 74; bounds reused 204, rebuilt 0)
- Main-scene frustum culling: enabled
- Scene preparation: average 1.768 ms, p95 2.161 ms, maximum 2.369 ms (120 samples)
- CPU frame: average 5.489 ms, p95 8.448 ms, maximum 10.089 ms (120 samples)
- GPU frame: average 1.280 ms, p95 1.497 ms, maximum 1.508 ms (120 samples)
- Scene image: `scene-scale-100.png`
- Filtered SSAO image: `occlusion-scale-100.png`

The simulation was frozen for the run. The two images differ only by the SSAO composite debug selector.
