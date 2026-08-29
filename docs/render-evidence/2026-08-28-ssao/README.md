# SSAO render evidence — 28–29 August 2026

This evidence set exercises the completed half-resolution SSAO estimator and
full-resolution bilateral composite on the same frozen overworld scene at two
render scales. Every run used a Debug Vulkan build, 4x MSAA, 180 rendered
frames, the last 120 timing samples, and `--require-validation`.

## Result

The filtered AO images retain the same large-scale contacts and object
silhouettes at 100% and 50% render scale. The scene captures show no broad
halos, cross-wall bleeding, direct-light dimming, or image corruption. Fine
edge detail is reduced at 50%, as expected from a 320x180 AO target, without
changing the physical reach of the effect.

| Render scale | Scene target | AO target | GPU frame, AO on | GPU frame, AO off | Estimated AO path |
| --- | ---: | ---: | ---: | ---: | ---: |
| 100% | 1280x720 | 640x360 | 0.753 ms | 0.355 ms | 0.398 ms |
| 50% | 640x360 | 320x180 | 0.409 ms | 0.238 ms | 0.171 ms |

The matched control suggests that the complete AO path costs about 0.398 ms at
100% and 0.171 ms at 50% on the tested NVIDIA GeForce RTX 4060 Laptop GPU. The
lower-resolution path reduces that estimate by about 57%. These are
whole-frame timestamp differences between separate frozen runs, so the estimate
includes both AO passes and their associated draw/transition overhead; it is
not a per-shader microbenchmark.

## Artifacts

- `scene-scale-100.png` and `scene-scale-50.png`: normal post-tonemap output
  with AO enabled.
- `occlusion-scale-100.png` and `occlusion-scale-50.png`: the filtered AO
  composite debug view.
- `scene-scale-100-ao-off.png` and `scene-scale-50-ao-off.png`: matched visual
  controls with AO disabled for the complete run.
- `metrics-scale-*.md`: device, extent, scene complexity, CPU timing, GPU
  timing, and capture metadata emitted by the executable.

## Reproduction

Run each scale with a separate save directory so profile state and the Vulkan
pipeline cache are scoped to that configuration. The evidence runner freezes
simulation time and hides the developer workspace so all four runs present the
same camera and scene.

```powershell
.\out\visual-studio\Debug\sokoban.exe --smoke-frames 180 --save-directory out\evidence-profile-100 --evidence-output docs\render-evidence\2026-08-28-ssao --evidence-render-scale 100 --require-validation
.\out\visual-studio\Debug\sokoban.exe --smoke-frames 180 --save-directory out\evidence-profile-100-off --evidence-output docs\render-evidence\2026-08-28-ssao --evidence-render-scale 100 --evidence-disable-ao --require-validation
.\out\visual-studio\Debug\sokoban.exe --smoke-frames 180 --save-directory out\evidence-profile-50 --evidence-output docs\render-evidence\2026-08-28-ssao --evidence-render-scale 50 --require-validation
.\out\visual-studio\Debug\sokoban.exe --smoke-frames 180 --save-directory out\evidence-profile-50-off --evidence-output docs\render-evidence\2026-08-28-ssao --evidence-render-scale 50 --evidence-disable-ao --require-validation
```

The host has a stale Epic Online Services implicit-layer manifest. Vulkan logs
that loader-discovery problem as a general error, but the application gate
correctly counts only messages classified as validation errors. No Vulkan VUID
or validation-type error occurred in any of the four runs.
