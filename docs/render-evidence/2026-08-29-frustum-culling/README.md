# Main-scene frustum-culling evidence — 29 August 2026

This matched pair freezes the same 1280x720 scene for 360 frames with 4x MSAA
and SSAO. The only configuration difference is main-scene frustum culling,
controlled by `--evidence-disable-frustum-culling` for the baseline.

| Metric | Disabled | Enabled | Difference |
| --- | ---: | ---: | ---: |
| Retained renderables | 204 | 204 | unchanged |
| Visible / culled | 204 / 0 | 130 / 74 | 36.3% classified outside |
| Draw calls | 51 | 43 | -8 (-15.7%) |
| Triangles | 14,504 | 14,068 | -436 (-3.0%) |
| Scene preparation average | 1.743 ms | 1.768 ms | +0.025 ms |
| CPU draw-frame average | 5.504 ms | 5.489 ms | -0.015 ms |
| Whole-frame GPU average | 1.649 ms | 1.280 ms | -0.369 ms |

The composed scene images are byte-for-byte identical, as are the filtered-AO
images. The structural reductions are deterministic; the small CPU deltas and
the magnitude of the GPU delta are hardware/run specific and should not be
generalized beyond this capture.

The culling pass consumes retained world AABBs before Vulkan recording. Exact
tile, water and authored-face bounds may be rejected. Model-backed tiles remain
conservatively visible because their retained record does not yet contain the
loaded mesh's local AABB. Picking and both shadow caster lists intentionally
ignore the main-scene classification.

Both runs required Vulkan validation. The focused scene-preparation suite
passes 1,503 checks, including an on/off case that proves main draw lists change
while picking and shadow counts do not. The full Debug build and all 68 CTest
suites pass.

- `disabled/metrics-scale-100.md`: uncullled control telemetry.
- `enabled/metrics-scale-100.md`: default culling telemetry.
- Each directory also contains the matched scene and filtered-AO PNGs.
