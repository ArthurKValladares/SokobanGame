# Build impact measurements

Measured September 24, 2026, from a snapshot of the working tree at
`889ff0aed817aa9ff52019e50e09e1b114085855`.

## Environment

These numbers come from a 2-core Linux container: Ubuntu 24.04, clang 18.1.3,
Ninja, CMake 3.28, and a Debug configuration with `-ftime-trace`. Absolute
times will differ from the Windows/MSVC/Visual Studio build. The counts
(which files recompile and which executables relink) come from the dependency
graph, so they are the same on every generator.

## Rebuild fan-out per edited file

Method: [`measure-header-impact.sh`](measure-header-impact.sh). It touches
one file, runs a Ninja dry run against a copy of `build.ninja` with the
regeneration edge removed, counts compile and link steps, and restores the
file's mtime. The copy is needed because `CONFIGURE_DEPENDS` globbing makes
every dry run report "Re-running CMake" instead of the real plan.

| Edited file | TUs recompiled | Executables relinked | Static libs re-archived |
| --- | ---: | ---: | ---: |
| `render/FogOfWarConfig.hpp` | 4 | 78 | 2 |
| `render/WaterConfig.hpp` | 112 | 78 | 3 |
| `render/LightingConfig.hpp` | 9 | 78 | 2 |
| `ParticleConfig.hpp` | 6 | 78 | 1 |
| `Math.hpp` | 147 | 80 | 3 |
| `render/RenderTypes.hpp` | 112 | 78 | 3 |
| `Level.hpp` | 63 | 78 | 2 |
| `TileTypes.hpp` | 94 | 78 | 3 |
| `LevelEditor.hpp` | 12 | 78 | 1 |
| `AssetManifest.hpp` | 49 | 78 | 3 |
| `render/ShaderCatalog.hpp` | 3 | 78 | 2 |
| `Rules.cpp` | 1 | 78 | 1 |
| `render/VulkanSceneRecorder.cpp` | 1 | 6 | 1 |
| `render/VulkanRenderer.hpp` | 8 | 6 | 1 |
| `Application.hpp` | 3 | 1 | 0 |
| `Application.cpp` | 1 | 1 | 0 |
| `LevelEditorDebugUi.cpp` | 1 | 1 | 0 |

The 78 executables relinked for any `sokoban_core` change are `sokoban`,
`sokoban_content_tool`, and 76 test programs. Every one of these plans also
runs `sokoban_content` (see [content-staging.md](content-staging.md)).

`WaterConfig.hpp` holds tuning constants, but it fans out to 112 TUs because
`render/RenderTypes.hpp:7` includes it to default-initialize
`RenderFrameData::WaterRendering` (`RenderTypes.hpp:454-463`).

## Shader edits

Touching `shaders/atmosphere.frag.glsl` plans one `glslc` step and then
`sokoban_content`. Touching `shaders/include/PointShadow.glsl` recompiles
every shader, because each module depends on every include. A single
`glslc` invocation takes about 0.1 s. Compiling all 17 modules sequentially
takes 1.6 s.

## Wall-clock scenarios (2 cores)

| Scenario | Wall time | Content staging share |
| --- | ---: | ---: |
| No-op `ninja` after a complete build | 25.2 s | ~25 s (100%) |
| Touch `FogOfWarConfig.hpp`, build `sokoban` | 34.9 s | ~25 s |
| Then build the remaining default targets (76 test relinks plus content again) | 34.6 s | ~25 s |
| Touch `Rules.cpp`, build `sokoban` | 29.3 s | ~25 s |
| Then build the remaining default targets | 32.3 s | ~25 s |

Test executables relinked after the `Rules.cpp` touch took 15.7 s of summed
link time (79 test executables, maximum 0.69 s each).

## Compile-time profile (clean first-party Debug build)

Summed per-object compile durations, clang `-ftime-trace`:

| Group | TUs | CPU seconds |
| --- | ---: | ---: |
| `sokoban_core` | 90 | 135.7 |
| `sokoban_render_vulkan` (includes ImGui) | 30 | 42.6 |
| `sokoban` application | 11 | 28.5 |
| `sokoban_ui` | 11 | 14.7 |
| Tests | 79 | 117.9 |
| Vendored SDL | 307 | 16.4 |

Across 221 first-party TUs, the frontend (parsing and template instantiation)
accounts for 294 s of 334 s, or 88%. Backend code generation accounts for
37.8 s. The heaviest inclusive header parse totals are `RenderTypes.hpp`
(49 s over 112 TUs), `<filesystem>` (45 s over 169 TUs), `LevelCatalog.hpp`
(32 s over 122 TUs, which pulls `<filesystem>` into `RenderTypes.hpp`),
`<cmath>`, `AnimationCatalog.hpp`, `<chrono>`, and `Math.hpp`. Nested
inclusive totals overlap, so read these as a ranking, not an additive
breakdown. The largest template-instantiation families are
`nlohmann::basic_json` (26.8 s), `std::vector` (26.1 s), and `std::format`
machinery (~19 s).

The slowest single TUs are `PlayerProfileCodec.cpp` (6.7 s),
`PlayerProfileMigrations.cpp` (6.7 s), `tests/PlayerProfileTests.cpp`
(6.4 s), and `Application.cpp` (5.9 s). The first three include
`nlohmann/json.hpp`. `PlayerProfileMigrations.hpp` is the only first-party
header that includes it.

## Precompiled-header and unity experiments

Clean `sokoban_core` plus `sokoban_ui` build, Debug, 2 cores, after SDL was
already built:

| Variant | Wall time | Result |
| --- | ---: | --- |
| Current | 66.2 s | Builds |
| `target_precompile_headers` with 20 standard headers plus `engine/Math.hpp` on core and ui (MiniaudioImpl and bc7enc16 excluded) | 40.7 s (−39%) | Builds with no source changes |
| `CMAKE_UNITY_BUILD=ON`, batch size 8 | 30.3 s (−54%) | Two batches fail: duplicate `lowercase`, duplicate `alignUp`, and conflicting `json` aliases (`ordered_json` vs `json`) in anonymous namespaces |

The experiment patch was applied only to the measurement copy and is not in
the repository. Unity builds also make incremental edits more expensive
(one edit recompiles its whole batch), so the review recommends PCH for
development and treats unity builds as an optional CI accelerator.
