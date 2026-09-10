# Frame-time summary measurement

Measured 2026-09-09 on the Windows Release build produced by Visual Studio
2022/MSVC. The local processor reported `AMD64 Family 25 Model 117 Stepping 2`.

## Workload

The renderer owns 27 independent `FrameTimeTelemetry` streams:

- three summaries are requested every rendered frame;
- opening the Rendering Stats panel requests 24 additional summaries; and
- each stream retains at most 120 samples.

The reproducible benchmark mode in `sokoban_frame_time_telemetry_tests` fills
27 independent 120-sample histories, records one new value per active stream,
then measures either three or 27 summary calls per simulated frame. Each run
contains 20,000 frames. Run it with:

```powershell
.\out\visual-studio\Release\sokoban_frame_time_telemetry_tests.exe --benchmark
```

## Results

| Run | Stats hidden average | Stats hidden maximum | Stats visible average | Stats visible maximum |
| --- | ---: | ---: | ---: | ---: |
| 1 | 0.002181 ms/frame | 0.0530 ms | 0.021017 ms/frame | 0.1315 ms |
| 2 | 0.002170 ms/frame | 0.0543 ms | 0.021407 ms/frame | 0.1484 ms |
| 3 | 0.002186 ms/frame | 0.0507 ms | 0.021492 ms/frame | 0.1608 ms |

The visible workload averages about 0.0213 ms per frame, or 0.13% of a
16.67 ms frame budget and 0.26% of an 8.33 ms frame budget. Opening the panel
adds about 0.0191 ms per frame on this host.

## Decision

Keep `FrameTimeTelemetry::summary()` stateless. Normal rendering reads each
updated stream once, so a cache would not avoid the measured sort in the common
path. The measured cost is too small to justify cached summaries, dirty-state
invalidation in both `record()` and `reset()`, and roughly one additional
summary object per telemetry stream.

Revisit caching only if the sample capacity grows substantially, a consumer
begins reading the same stream repeatedly between records, or this benchmark
shows a material regression on a supported platform.
