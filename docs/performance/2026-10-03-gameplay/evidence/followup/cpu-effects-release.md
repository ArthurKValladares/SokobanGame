# Sokoban performance suite

- Mode: full
- Samples per case: 40
- Task workers: 4
- Hardware concurrency: 16
- CPU trace: `cpu-trace.json`
- Initial resident memory: 4.62 MiB
- Final resident memory: 6.09 MiB

Hardware timings are diagnostic baselines, not correctness thresholds. Compare runs on the same machine and power state.

## Results

| Group | Case | Median ms | P95 ms | P99 ms | ns/op |
|---|---|---:|---:|---:|---:|
| effects | effects-emission-mirror-swap-1 | 0.000 | 0.001 | 0.001 | 400.0 |
| effects | effects-preparation-mirror-swap-1 | 0.148 | 0.165 | 0.181 | 562.3 |
| effects | effects-emission-mirror-swap-32 | 0.012 | 0.012 | 0.012 | 383.6 |
| effects | effects-preparation-mirror-swap-32 | 0.156 | 0.183 | 0.218 | 325.9 |
| effects | effects-emission-witch-swap-1 | 0.000 | 0.001 | 0.001 | 400.0 |
| effects | effects-preparation-witch-swap-1 | 0.149 | 0.169 | 0.178 | 568.0 |
| effects | effects-emission-witch-swap-32 | 0.012 | 0.013 | 0.014 | 376.6 |
| effects | effects-preparation-witch-swap-32 | 0.156 | 0.185 | 0.207 | 325.9 |
| effects | effects-emission-turret-volley-1 | 0.001 | 0.001 | 0.001 | 500.0 |
| effects | effects-preparation-turret-volley-1 | 0.148 | 0.182 | 0.212 | 556.9 |
| effects | effects-emission-turret-volley-32 | 0.015 | 0.015 | 0.017 | 457.0 |
| effects | effects-preparation-turret-volley-32 | 0.163 | 0.211 | 0.233 | 300.4 |
| effects | effects-emission-portals-1 | 0.002 | 0.002 | 0.002 | 1650.0 |
| effects | effects-preparation-portals-1 | 0.156 | 0.205 | 0.227 | 418.9 |
| effects | effects-emission-portals-32 | 0.053 | 0.055 | 0.057 | 1652.3 |
| effects | effects-preparation-portals-32 | 0.428 | 0.545 | 0.564 | 107.0 |
| effects | effects-emission-special-blocks-1 | 0.000 | 0.001 | 0.001 | 450.0 |
| effects | effects-preparation-special-blocks-1 | 0.150 | 0.155 | 0.186 | 530.3 |
| effects | effects-emission-special-blocks-32 | 0.010 | 0.010 | 0.010 | 317.2 |
| effects | effects-preparation-special-blocks-32 | 0.341 | 0.347 | 0.378 | 313.8 |
| effects | effects-emission-mixed-stress-1 | 0.003 | 0.003 | 0.003 | 3275.0 |
| effects | effects-preparation-mixed-stress-1 | 0.155 | 0.188 | 0.213 | 367.5 |
| effects | effects-emission-mixed-stress-32 | 0.103 | 0.109 | 0.128 | 3225.8 |
| effects | effects-preparation-mixed-stress-32 | 0.570 | 0.664 | 0.682 | 102.4 |

## Ranked optimization candidates

No rule-based bottleneck was detected in this capture. Compare this baseline with a heavier representative scene before concluding that no optimization is needed.

## Method

Each case is warmed up, sampled into the engine's bounded `FrameTimeTelemetry`, and wrapped in a `CpuProfileScope`. The accompanying trace can be opened in Perfetto or Chrome tracing to inspect hot paths and worker overlap. Checksums keep benchmark work observable.
