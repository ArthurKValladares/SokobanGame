# SSAO banding regression — 29 August 2026

This deterministic 1280x720 capture verifies the estimator change made after
horizontal bands were reported in the filtered-AO debug view. The old
interleaved-gradient rotation pattern had weak variation between adjacent rows;
half-resolution estimation and upsampling exposed that correlated error on the
large ground plane.

`ssao.frag.glsl` now derives kernel rotation from a full two-axis integer hash.
The captured filtered-AO frame has irregular, spatially distributed sampling
noise instead of long periodic horizontal stripes while retaining contacts and
silhouette rejection. The normal scene frame confirms that ambient-only
composition remains intact.

- `occlusion-scale-100.png`: filtered-AO debug output after the fix.
- `scene-scale-100.png`: the same frozen frame with normal composition.
- `metrics-scale-100.md`: render configuration, telemetry and image metadata.

The run used `--smoke-frames 180`, `--evidence-render-scale 100` and
`--require-validation`. The full Debug build and all 68 registered CTest suites
passed afterward; the focused SSAO reference reports 2,110 checks.
