# Packet B implementation evidence

Implemented September 25, 2026, on top of packet A. The measurements below
were taken in the 2-core Linux container (clang 18 and GCC 13, Debug, with
lavapipe under Xvfb) that packets A and the review used. No Windows or GPU
run was possible from there.

## What changed

- **DI-07, shader hot reload.**
  - `ShaderHotReload` (in `sokoban_core`, Vulkan-free) watches the
    catalogued shader sources and every file in `shaders/include/`.
  - It compiles changed modules on a background thread. The compiler is
    the build's `glslc` with the same `SOKOBAN_GLSLC_FLAGS`, run through
    SDL's process API.
  - A pass publishes only if every module in it compiled. Publishing
    writes the new SPIR-V atomically into the staged tree and refreshes
    `content.index`.
  - `VulkanRenderer::requestShaderReload` bumps a `shaderRevision` in the
    reconfiguration queue. That drives the existing pipeline-only swap,
    which retires the old pipelines after their frames finish. If pipeline
    creation fails during a shader-only reload, the old pipelines stay
    active and the error is reported instead of thrown.
  - UI: a Shaders tab (status, compiler output, Recompile All, auto-watch
    toggle), the F6 shortcut, and an overlay that stays up while a compile
    or rebuild has failed.
  - New workspace tabs dock beside the existing ones instead of floating
    over the game.
- **DI-08, live tuning.**
  - `engine/Tuning.hpp` adds `SOKOBAN_TUNABLE_FLOAT`, `_UINT` and `_COLOR3`
    declarations. In builds without developer tools they are
    `inline constexpr`; with the tools they are registered variables.
  - The Tuning tab edits them live. Save rewrites only the literal value
    arguments in the header, keeping the rest of the file byte for byte and
    ignoring declarations quoted in comments.
  - `FogOfWarConfig.hpp` (17 values) is the first header migrated. Its call
    sites are unchanged.
- **DI-09, start where you left off.**
  - New launch options `--continue`, `--title`, `--level/--screen` and
    `--edit`, with parsing tests.
  - Debug developer builds write `dev-session.json` to the save directory
    on exit: resume flag, editor document, editing view, layer and tool.
  - The next plain launch continues the active slot and reopens the
    document.
  - The **Session** menu turns resuming off. Smoke and evidence runs
    ignore the file.

## Measurements

| Scenario | Before | After |
| --- | ---: | ---: |
| Edit one shader, see it in the game | ~29 s content stage plus restart plus navigation (Windows Debug, review measurement) | 4.5 s on lavapipe; see note |
| Edit a shared shader include | Same as above | 7.0 s on lavapipe for all 17 modules |
| Change a fog value | 4 TU recompile, relink, restart | Immediate (Tuning tab); Save plus next build compiles it in |
| Launch to the last edited document | Title, Continue, then find the document in the file browser | One launch; log shows `Editing …/screen1.scr (layer 1)` |

The shader timings were taken while lavapipe renders the scene on the CPU.
Reload is serviced once per frame, and this software renderer takes about a
second per frame on 2 cores, so those numbers are dominated by frame time.
Compiling one module takes 0.1 s, and all 17 take 1.6 s serially on the same
machine. On a GPU, the expected latency is roughly the compile time plus up
to 250 ms of polling; that has not been measured.

## Validation

| Check | Result |
| --- | --- |
| clang 18 Debug, full Vulkan | Built with no warnings; 83/83 CTest |
| GCC 13 Debug, `-DSOKOBAN_WARNINGS_AS_ERRORS=ON` | Built; 83/83 CTest |
| GCC 13 Release | 83/83 CTest; `nm` shows no tuning registrations in the Release game (34 in Debug) |
| `headless-tests` preset (tools off, so tunables compile as constants) | 76/76 CTest |
| New suites | `shader_hot_reload`: detection, include fan-out, failure publishes nothing, edits during a compile, real `glslc` success and error output. `tuning`: literal formatting, edit/revert, rewrite, save, and a round-trip check against every registered header. `dev_session`: round trip and damaged files. `command_line`: launch options. `renderer_reconfiguration`: shader-only rebuilds |
| Live game run (lavapipe, validation on) | A valid edit recompiled and rebuilt pipelines. An invalid edit logged `atmosphere.frag.glsl:320: error: 'this' : Reserved word.` and kept running. Restoring the file reloaded it. No validation errors |
| Launch paths | Resume from `dev-session.json` reopened the document at its layer. `--title` suppressed the resume. `--level 1 --screen 2` entered that puzzle |
| clang-tidy 18 on new and changed files | No findings in new code. The remaining findings in `Application.cpp` and shared headers are pre-existing |

## Not verified here

- Windows/MSVC builds of these changes. In particular, the MSVC
  compile of the tunable macros and SDL process creation with a
  `glslc.exe` path that contains spaces.
- A GPU run, and the reload latency on real hardware.
- Saving `dev-session.json` on a normal quit. The quit confirmation could
  not be driven from the headless test harness; the save runs in
  `Application`'s destructor alongside the existing profile flush.
