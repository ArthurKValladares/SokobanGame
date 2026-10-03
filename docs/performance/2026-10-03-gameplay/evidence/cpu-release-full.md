# Sokoban performance suite

- Mode: full
- Samples per case: 40
- Task workers: 4
- Hardware concurrency: 16
- CPU trace: `cpu-trace.json`
- Initial resident memory: 4.62 MiB
- Final resident memory: 7.77 MiB

Hardware timings are diagnostic baselines, not correctness thresholds. Compare runs on the same machine and power state.

## Results

| Group | Case | Median ms | P95 ms | P99 ms | ns/op |
|---|---|---:|---:|---:|---:|
| instrumentation | cpu-profiler-scope-capture | 0.293 | 0.324 | 0.441 | 71.5 |
| instrumentation | process-memory-sampling | 0.000 | 0.000 | 0.001 | 457.8 |
| telemetry | frame-telemetry-27-streams | 0.060 | 0.073 | 0.080 | 2232.9 |
| memory | frame-arena-allocate-reset | 0.012 | 0.019 | 0.021 | 3.0 |
| memory | heap-contiguous-frame-allocation | 0.005 | 0.006 | 0.025 | 1.2 |
| memory | heap-small-frame-allocations | 0.185 | 0.355 | 0.385 | 45.2 |
| draw-submission | opaque-sort-batch-512 | 0.028 | 0.050 | 0.161 | 53.9 |
| draw-submission | opaque-sort-batch-4096 | 0.540 | 0.639 | 0.684 | 131.7 |
| draw-submission | opaque-sort-batch-16384 | 2.738 | 3.715 | 3.808 | 167.1 |
| tasks | task-enqueue-roundtrip-256 | 0.394 | 0.625 | 0.689 | 1539.0 |
| tasks | task-future-sequential-256 | 4.684 | 4.924 | 5.025 | 18298.6 |
| tasks | task-scoped-sequential-256 | 4.676 | 4.981 | 5.128 | 18266.4 |
| tasks | compute-transform-serial | 3.442 | 4.778 | 5.271 | 13.1 |
| tasks | compute-transform-parallel | 1.190 | 1.379 | 1.436 | 4.5 |
| scene-preparation | scene-cold-serial-256 | 0.760 | 0.887 | 1.009 | 2968.9 |
| scene-preparation | scene-warm-serial-256 | 0.507 | 0.527 | 0.540 | 1981.6 |
| scene-preparation | scene-warm-parallel-256 | 0.295 | 0.353 | 0.394 | 1151.0 |
| scene-preparation | scene-cold-serial-1024 | 1.761 | 2.179 | 2.273 | 1720.0 |
| scene-preparation | scene-warm-serial-1024 | 1.415 | 1.853 | 1.901 | 1382.2 |
| scene-preparation | scene-warm-parallel-1024 | 0.962 | 1.304 | 1.419 | 939.9 |
| scene-preparation | scene-cold-serial-4096 | 8.140 | 9.271 | 9.845 | 1987.4 |
| scene-preparation | scene-warm-serial-4096 | 5.931 | 6.608 | 7.052 | 1448.0 |
| scene-preparation | scene-warm-parallel-4096 | 4.651 | 5.048 | 5.513 | 1135.6 |
| editor | editor-puzzle-overworld-identity | 0.849 | 1.321 | 1.338 | 6634.8 |
| editor | editor-level-browser-scan | 1.021 | 1.550 | 1.699 | 1021300.0 |
| effects | effects-emission-mirror-swap-1 | 0.001 | 0.001 | 0.001 | 550.0 |
| effects | effects-preparation-mirror-swap-1 | 0.256 | 0.326 | 0.411 | 973.1 |
| effects | effects-emission-mirror-swap-32 | 0.019 | 0.025 | 0.028 | 585.2 |
| effects | effects-preparation-mirror-swap-32 | 0.290 | 0.477 | 0.491 | 605.2 |
| effects | effects-emission-witch-swap-1 | 0.001 | 0.001 | 0.001 | 650.0 |
| effects | effects-preparation-witch-swap-1 | 0.279 | 0.386 | 0.507 | 1060.6 |
| effects | effects-emission-witch-swap-32 | 0.020 | 0.043 | 0.105 | 638.3 |
| effects | effects-preparation-witch-swap-32 | 0.288 | 0.382 | 0.452 | 599.5 |
| effects | effects-emission-turret-volley-1 | 0.001 | 0.001 | 0.001 | 750.0 |
| effects | effects-preparation-turret-volley-1 | 0.299 | 0.434 | 0.516 | 1127.3 |
| effects | effects-emission-turret-volley-32 | 0.044 | 0.051 | 0.127 | 1360.9 |
| effects | effects-preparation-turret-volley-32 | 0.354 | 0.457 | 0.556 | 650.5 |
| effects | effects-emission-portals-1 | 0.005 | 0.006 | 0.006 | 5500.0 |
| effects | effects-preparation-portals-1 | 0.349 | 0.508 | 0.591 | 935.5 |
| effects | effects-emission-portals-32 | 0.102 | 0.130 | 0.140 | 3191.4 |
| effects | effects-preparation-portals-32 | 0.856 | 1.022 | 1.062 | 213.9 |
| effects | effects-emission-special-blocks-1 | 0.001 | 0.001 | 0.001 | 850.0 |
| effects | effects-preparation-special-blocks-1 | 0.262 | 0.337 | 0.401 | 930.2 |
| effects | effects-emission-special-blocks-32 | 0.017 | 0.064 | 0.153 | 515.6 |
| effects | effects-preparation-special-blocks-32 | 0.679 | 0.821 | 0.877 | 624.1 |
| effects | effects-emission-mixed-stress-1 | 0.008 | 0.008 | 0.020 | 7525.0 |
| effects | effects-preparation-mixed-stress-1 | 0.293 | 0.370 | 0.410 | 694.4 |
| effects | effects-emission-mixed-stress-32 | 0.148 | 0.220 | 0.242 | 4618.8 |
| effects | effects-preparation-mixed-stress-32 | 1.145 | 1.294 | 1.422 | 205.6 |

## Ranked optimization candidates

1. **The task system accelerates sufficiently large independent CPU work** (high, score 120.0)
   - Evidence: compute-transform-serial is 2.89x the median time of compute-transform-parallel.
   - Next experiment: Apply parallelFor only to similarly sized, independent workloads and preserve a serial path below the measured crossover.
2. **Auxiliary scene preparation benefits from parallel execution** (medium, score 82.5)
   - Evidence: scene-warm-serial-256 is 1.72x the median time of scene-warm-parallel-256.
   - Next experiment: Keep this workload on the task system and profile whether another independent scene-preparation phase can overlap at this scene size.
3. **Retained scene caches materially reduce preparation cost** (medium, score 72.4)
   - Evidence: scene-cold-serial-256 is 1.50x the median time of scene-warm-serial-256.
   - Next experiment: Protect stable renderable IDs, bounds revisions, and vector capacity so production frames stay on the warm path.
4. **Auxiliary scene preparation benefits from parallel execution** (medium, score 71.2)
   - Evidence: scene-warm-serial-1024 is 1.47x the median time of scene-warm-parallel-1024.
   - Next experiment: Keep this workload on the task system and profile whether another independent scene-preparation phase can overlap at this scene size.
5. **Retained scene caches materially reduce preparation cost** (medium, score 66.8)
   - Evidence: scene-cold-serial-4096 is 1.37x the median time of scene-warm-serial-4096.
   - Next experiment: Protect stable renderable IDs, bounds revisions, and vector capacity so production frames stay on the warm path.
6. **Auxiliary scene preparation benefits from parallel execution** (medium, score 62.4)
   - Evidence: scene-warm-serial-4096 is 1.28x the median time of scene-warm-parallel-4096.
   - Next experiment: Keep this workload on the task system and profile whether another independent scene-preparation phase can overlap at this scene size.
7. **Retained scene caches materially reduce preparation cost** (medium, score 61.0)
   - Evidence: scene-cold-serial-1024 is 1.24x the median time of scene-warm-serial-1024.
   - Next experiment: Protect stable renderable IDs, bounds revisions, and vector capacity so production frames stay on the warm path.

## Method

Each case is warmed up, sampled into the engine's bounded `FrameTimeTelemetry`, and wrapped in a `CpuProfileScope`. The accompanying trace can be opened in Perfetto or Chrome tracing to inspect hot paths and worker overlap. Checksums keep benchmark work observable.
