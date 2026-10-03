# Sokoban performance suite

- Mode: full
- Samples per case: 40
- Task workers: 4
- Hardware concurrency: 16
- CPU trace: `cpu-trace.json`
- Initial resident memory: 4.62 MiB
- Final resident memory: 6.11 MiB

Hardware timings are diagnostic baselines, not correctness thresholds. Compare runs on the same machine and power state.

## Results

| Group | Case | Median ms | P95 ms | P99 ms | ns/op |
|---|---|---:|---:|---:|---:|
| editor | editor-puzzle-overworld-identity | 0.446 | 0.568 | 0.602 | 3483.2 |
| editor | editor-level-browser-scan | 0.496 | 0.568 | 0.682 | 496050.0 |
| editor | editor-level-browser-snapshot | 0.002 | 0.002 | 0.002 | 17.6 |

## Ranked optimization candidates

No rule-based bottleneck was detected in this capture. Compare this baseline with a heavier representative scene before concluding that no optimization is needed.

## Method

Each case is warmed up, sampled into the engine's bounded `FrameTimeTelemetry`, and wrapped in a `CpuProfileScope`. The accompanying trace can be opened in Perfetto or Chrome tracing to inspect hot paths and worker overlap. Checksums keep benchmark work observable.
