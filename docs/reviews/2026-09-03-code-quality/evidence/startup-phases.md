# Startup phase timing

Captured 2026-09-10 for MQ-04.

## Workload and threshold

`sokoban_vulkan_smoke_tests --benchmark-startup` measures the staged Release
package beside the executable. It validates `content.index`, loads the asset
and animation manifests, discovers the initial render requirements, inspects
every manifest glTF/GLB document for runtime texture dependencies, creates the
Vulkan device and resource owners, and requests the same clean-profile preload
set as the application: the composed overworld plus every screen in level 0.
The measured set contains 11 models, 18 textures, and 5 animations.

The benchmark prevents model and texture residency while the ordinary worker
scheduler decodes the requested assets. This makes the CPU-ready boundary
observable without changing production loading behavior. It then removes the
hold and measures publication plus GPU fence completion from the already
prepared payloads. A 30-second deadline and the loader's failure counters make
either phase fail rather than report a partial result.

Persistent metadata caching was considered only if manifest plus glTF
inspection reached either 50 ms or 10 percent of the measured asset-readiness
path. The first-playable-frame comparison uses the interval from the session
start log entry through the completed frame in a one-frame application smoke
run. Each sample has a fresh save directory, so the interval includes
clean-start pipeline-cache behavior while excluding process teardown. All
measurements used the Release build on an NVIDIA GeForce RTX 4060 Laptop GPU.
One startup-benchmark warm-up was discarded before the five recorded runs.

## Measurements

All values are microseconds.

| Run | Package validation | Manifest inspection | Requirement discovery | glTF discovery | Device setup | Resource setup | Decode | Upload | Assets ready |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 21,575 | 1,360 | 659 | 17,433 | 253,882 | 36,623 | 38,388 | 3,042 | 372,981 |
| 2 | 21,309 | 960 | 769 | 16,923 | 254,794 | 39,709 | 35,917 | 6,977 | 377,376 |
| 3 | 26,918 | 670 | 700 | 16,537 | 237,984 | 37,463 | 37,127 | 3,967 | 361,385 |
| 4 | 23,009 | 725 | 623 | 17,190 | 254,205 | 32,941 | 35,446 | 4,804 | 368,961 |
| 5 | 21,781 | 1,265 | 748 | 16,790 | 252,523 | 35,671 | 36,519 | 3,170 | 368,486 |
| **Median** | **21,781** | **960** | **700** | **16,923** | **253,882** | **36,623** | **36,519** | **3,967** | **368,961** |

The manifest and glTF phases total 17,883 us at the median. That is 4.8
percent of the asset-readiness path. Including package validation raises the
read-only inspection work to 39,664 us, still below 50 ms.

Five clean-save application runs produced first playable frames in 3,341,000,
2,266,000, 2,252,000, 2,115,000, and 2,234,000 us. The 2,252,000 us median
makes manifest plus glTF inspection 0.8 percent of observed startup-to-frame
time, or 1.8 percent when package validation is included.

## Decision

No persistent metadata cache was introduced. Runtime dependency discovery
already parses each distinct glTF/GLB document once per catalog construction
and reuses that inspection for material textures and prepared-size estimates.
The remaining 17.9 ms median does not meet the implementation threshold.

The staged `content.index` records package version, relative path, and byte
size. Those fields cannot distinguish a same-size document replacement, while
file modification times are not stable source identities across staging and
installation. A cache keyed by the current index could therefore return stale
material or sizing metadata. Adding content digests or build-generated runtime
metadata would broaden the package contract for less than one percent of the
measured first-frame time.

The benchmark remains available for future content growth and prints every
phase as one machine-readable `startup_phases` line. The residency-denial test
hooks are compiled only with test hooks enabled and exercise the loader's real
CPU-ready retry boundary.

Verification:

- Release startup benchmark completed for one warm-up and five recorded runs.
- Five Release one-frame application smoke runs returned zero.
- Vulkan smoke test passed with its ordinary CTest invocation.
- Full Debug and Release warning-as-error builds completed.
- Debug CTest registry: 80 of 80 passed.
- Release CTest registry: 80 of 80 passed.

MQ-04 is complete. The forward-looking roadmap begins with MQ-05.
