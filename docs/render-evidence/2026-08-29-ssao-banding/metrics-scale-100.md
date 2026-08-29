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
- Persistent renderables: 204 (bounds reused 204, rebuilt 0)
- CPU frame: average 5.490 ms, p95 8.903 ms, maximum 15.045 ms (120 samples)
- GPU frame: average 0.952 ms, p95 2.497 ms, maximum 2.534 ms (120 samples)
- Scene image: `scene-scale-100.png`
- Filtered SSAO image: `occlusion-scale-100.png`

The simulation was frozen for the run. The two images differ only by the SSAO composite debug selector.
