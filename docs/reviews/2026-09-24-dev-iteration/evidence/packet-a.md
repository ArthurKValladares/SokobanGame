# Packet A implementation evidence

Implemented September 24, 2026, on top of
`889ff0aed817aa9ff52019e50e09e1b114085855` and its uncommitted working tree.
The measurements come from the same 2-core Linux/clang 18/Ninja Debug
environment as [build-impact.md](build-impact.md), so the before and after
numbers are comparable with each other. They are not Windows timings.

## What changed

- **DI-01**
  - `sokoban_bc7enc16` moved to `cmake/bc7enc16/` and is built with Release
    flags in every configuration. This also avoids MSVC's `/RTC1` + `/O2`
    conflict.
  - `stageContent` gained a `ContentStageOptions` overload:
    - a per-configuration compressed-texture cache keyed by the source-file
      bytes, the interpretation, and `compressedTextureEncoderRevision`;
    - parallel encoding of cache misses through `TaskSystem`;
    - a stage record that lets an unchanged build skip staging.
  - The content tool exposes these as `--texture-cache` and `--incremental`.
    CMake passes both unless `SOKOBAN_INCREMENTAL_CONTENT=OFF`, which the
    shipping preset forces.
- **DI-02**
  - New `dev` configure/build/test presets and a `dev-all` build preset:
    Ninja, Debug, MSVC, Windows only.
  - `/MP` is added for Visual Studio generators.
- **DI-03**
  - Test suites link into `sokoban_tests` (73 suites) and
    `sokoban_vulkan_tests` (5 suites). `player_profile` is `STANDALONE`
    because it replaces global `operator new`.
  - Each suite's `main` is renamed per source file, and
    `tests/TestRunner.cpp` dispatches by CTest name. The 80 CTest names are
    unchanged, and each runs in its own process.
  - Sanitizer builds default to `SOKOBAN_TEST_RUNNERS=OFF`, which keeps one
    executable per suite.

## Measurements

| Scenario | Before | After |
| --- | ---: | ---: |
| No-op build of all default targets | 25.2 s | 0.1 s |
| Touch `Rules.cpp`, build `sokoban` | 29.3 s | 5.0 s |
| Then build the remaining targets | 32.3 s | 3.5 s |
| Touch `FogOfWarConfig.hpp`, build `sokoban` | 34.9 s | 9.7 s |
| Then build the remaining targets | 34.6 s | 5.4 s |
| Touch a `.scr`, build `sokoban` | ~25 s (full stage) | 1.5 s (restage, 62/62 textures from cache) |
| Touch `atmosphere.frag.glsl`, build `sokoban` | ~25 s | 1.7 s |
| Executables relinked by a `sokoban_core` change | 78 | 5 |

Content tool, Debug build, same inputs:

| Run | Time |
| --- | ---: |
| Clean stage, no cache (optimized encoder, 2 threads) | 5.9 s (was 24.8 s) |
| Cold cache | 6.0 s |
| Nothing changed | 0.09 s |
| One level changed | 1.5 s |

Changes to `sokoban_core` still restage from the cache, about 1.5 s. The
content tool links core, so its identity changes and the stage record no
longer matches. The restage is correct, and it is cheap with the cache.

## Package equivalence

- An incremental cold-cache stage and a plain `stageContent` stage of the
  same inputs produce identical file sets and bytes. The new
  `content_pipeline` checks assert this after cold, cached, partially changed,
  and damaged-cache stages.
- Compared with the old Debug tool, 7 of 62 compressed textures differ at the
  byte level, because the encoder is now optimized. The old Debug and old
  Release tools already disagreed on the same 7 files. The new Debug tool and
  the old Release tool differ on 3; the remaining difference is the
  unoptimized mip filter in Debug `sokoban_core`. Floating-point code
  generation varies with optimization level, so the texture cache is
  per-configuration. The shipping preset stages from scratch and does not use
  the cache.

## Validation

| Check | Result |
| --- | --- |
| clang 18 Debug, full Vulkan build | Built; 80/80 CTest (lavapipe under Xvfb) |
| GCC 13 Debug, full Vulkan build, `-DSOKOBAN_WARNINGS_AS_ERRORS=ON` | Built with no warnings; 80/80 CTest |
| `headless-tests` preset (warnings as errors) | Built; 73/73 CTest |
| clang 18 ASan+UBSan Debug, `SOKOBAN_TEST_RUNNERS` defaulted OFF | Built; 78/80. `vulkan_smoke` and `application_validation_teardown` fail on an unsymbolized lavapipe leak in this container. The unmodified baseline fails `vulkan_smoke` identically |
| clang-tidy 18 on `ContentPipeline.cpp`, `ContentTool.cpp`, `TestRunner.cpp`, `ContentPipelineTests.cpp` | No findings in changed code. clang-tidy 18 also reports pre-existing `performance-enum-size` findings in unchanged headers |
| `tools/check_core_is_vulkan_free.sh` | 89 sources, 0 reach Vulkan |
| Runner CLI | `--list`, unknown-suite and no-argument usage (exit 2), argument forwarding (`frame_time_telemetry --benchmark`), failure propagation |

GNU ld memory for the test link, measured with `wait4`:

| Link | Peak |
| --- | ---: |
| `sokoban_tests`, clang Debug | 961 MB |
| `sokoban_tests`, GCC Debug | 1,275 MB |
| `sokoban` game, GCC Debug | 576 MB |
| `sokoban_tests`, ASan+UBSan | Killed at the 6 GB container limit |
| Each third of `sokoban_tests`, ASan+UBSan | 2.7–3.7 GB |
| Full ASan+UBSan runner with lld | ~4 GB |
| Full ASan+UBSan runner with gold | 1.3 GB |

The ASan+UBSan result is why sanitizer builds keep per-suite executables.

## Not verified here

- MSVC and the Visual Studio generator: `/MP`, the bc7enc16 flag override
  under `/MTd`, and the Ninja+MSVC `dev` preset.
- Before and after timings on the development machine.

These are the remaining DI-01, DI-02, and DI-03 acceptance items. Suggested
commands:

```powershell
# Visual Studio generator, existing tree
cmake --build build --config Debug --target sokoban     # twice; the second should report "Content up to date"
# Ninja dev preset
cmake --preset dev
cmake --build --preset dev
cmake --build --preset dev-all
ctest --preset dev
```
