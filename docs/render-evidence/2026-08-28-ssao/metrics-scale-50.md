# Render evidence — 50% scale

- Device: NVIDIA GeForce RTX 4060 Laptop GPU (discrete)
- Swapchain: 1280x720
- Scene target: 640x360
- SSAO target: 320x180
- Ambient occlusion: enabled
- MSAA samples: 4
- Draw calls: 51
- Triangles: 14504
- Render passes: 6
- CPU frame: average 5.257 ms, p95 8.713 ms, maximum 13.997 ms (120 samples)
- GPU frame: average 0.409 ms, p95 0.602 ms, maximum 0.758 ms (120 samples)
- Scene image: `scene-scale-50.png`
- Filtered SSAO image: `occlusion-scale-50.png`

The simulation was frozen for the run. The two images differ only by the SSAO composite debug selector.
