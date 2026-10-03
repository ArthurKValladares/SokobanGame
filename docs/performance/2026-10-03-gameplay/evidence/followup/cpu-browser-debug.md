# Sokoban performance suite

- Mode: full
- Samples per case: 40
- Task workers: 4
- Hardware concurrency: 16
- CPU trace: `cpu-trace.json`
- Initial resident memory: 5.29 MiB
- Final resident memory: 8.91 MiB

Hardware timings are diagnostic baselines, not correctness thresholds. Compare runs on the same machine and power state.

## Results

| Group | Case | Median ms | P95 ms | P99 ms | ns/op |
|---|---|---:|---:|---:|---:|
| editor | editor-level-browser-scan | 1.348 | 1.441 | 1.494 | 1348350.0 |
| editor | editor-level-browser-snapshot | 0.006 | 0.006 | 0.006 | 43.8 |

## Ranked optimization candidates

No rule-based bottleneck was detected in this capture. Compare this baseline with a heavier representative scene before concluding that no optimization is needed.

## Method

Each case is warmed up, sampled into the engine's bounded `FrameTimeTelemetry`, and wrapped in a `CpuProfileScope`. The accompanying trace can be opened in Perfetto or Chrome tracing to inspect hot paths and worker overlap. Checksums keep benchmark work observable.
