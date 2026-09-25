# Content staging measurements

## Why it runs on every build

`CMakeLists.txt:695` declares `add_custom_target(sokoban_content ALL ...)`
with no `OUTPUT` or stamp file, so every build treats it as out of date.
`CMakeLists.txt:707` makes `sokoban` depend on it. Each run calls
`stageContent` (`src/engine/ContentPipeline.cpp:1286`), which:

1. deletes and recreates `assets.staging` (`:1299-1303`);
2. copies every inventory file (`:1306-1310`);
3. decodes and BC7-encodes every texture source to KTX2 (`:1317-1349`),
   one texture at a time on the calling thread;
4. writes and validates `content.index`, then swaps directories
   (`:1351-1373`).

The content tool is built in the active configuration
(`CMakeLists.txt:404`). In Debug, both `sokoban_core` and `sokoban_bc7enc16`
are unoptimized.

## Cloud reproduction (2-core Linux, clang 18)

The inputs were the 293 files that the manifest reaches, 59.1 MB after
staging, including 62 compiled textures.

| Content tool build | Wall time | Notes |
| --- | ---: | --- |
| Debug (current) | 24.8 s | `user` ≈ `real`: single-threaded |
| Debug with only `bc7enc16` at `-O2` | 11.2 s | One-line CMake change |
| Release | 6.9 s | |
| `--validate-only`, Debug | 0.11 s | Inventory and validation without copy/encode |
| `--validate-only`, Release | 0.06 s | |

A no-op `ninja` in the Debug build tree took 25.2 s. All of it was this step.

## Windows evidence from the local build tree

Timestamps and sizes were read from `build/` on the development machine.
Nothing was rebuilt.

- `build/tools/Debug/sokoban_content_tool.exe` was written at
  `16:37:09.0` UTC on September 24.
- The files in `build/Debug/assets/compiled-textures/` span `16:37:10.7`
  to `16:37:38.3`, which is **27.6 s of BC7 encoding**.
- `content.index` was written at `16:37:38.3`, and `sokoban.exe` was
  linked at `16:37:40.6`.
- The staged Release tree from September 10 shows a 6.0 s encoding
  window.

So every Debug build of `sokoban` in Visual Studio, including one where no
content changed, spends about 29 s in content staging before the game can
launch. The Release encoder window (6.0 s) matches the cloud Release
measurement (6.9 s).

## Test link output on Windows

`build/Debug` contains 79 test executables (358.7 MB) and their PDBs
(3,663.8 MB). `sokoban.pdb` is 344.2 MB. A change to any `sokoban_core`
source relinks all core-dependent tests when the default `ALL_BUILD` target
is built. That is the target the documented loop in `HANDOFF.md` uses.
