# Packet 8 final validation

Validated September 15, 2026, at commit `d660728593f3a90125e61819a9c3b87b884cab05` on Windows x64 with Visual Studio 2022/MSVC 19.44 and Vulkan validation layer 1.4.309.0.

## Automated matrix

| Check | Result |
| --- | --- |
| Visual Studio Debug build with `SOKOBAN_WARNINGS_AS_ERRORS=ON` | Passed |
| Visual Studio Release build with `SOKOBAN_WARNINGS_AS_ERRORS=ON` | Passed |
| Debug CTest registry | 80/80 passed in 25.01 seconds |
| Release CTest registry | 80/80 passed in 6.69 seconds |
| Fresh Vulkan-disabled, headless-only warnings-as-errors build | Passed |
| Headless CTest registry | 72/72 passed in 32.87 seconds |
| Shipping preset build and ZIP packaging | Passed |
| Runtime ZIP validation from a fresh extraction | Passed: 252 indexed assets totaling 40,975,915 bytes; packaged executable completed 240 frames with an isolated profile |
| Debug application with required Vulkan validation | Passed: 240 frames, exit code 0, no Vulkan validation errors |

The configured `headless-tests` preset could not be invoked directly because Ninja was not on this shell's `PATH`. The equivalent fresh build used the Visual Studio generator with the preset's three validation gates unchanged: `CMAKE_DISABLE_FIND_PACKAGE_Vulkan=ON`, `SOKOBAN_HEADLESS_TESTS_ONLY=ON`, and `SOKOBAN_WARNINGS_AS_ERRORS=ON`.

## Device evidence

The direct validation run selected an NVIDIA GeForce RTX 4060 Laptop GPU, Vulkan API 1.4.341, NVIDIA driver 610.88, with a 1,024-entry texture descriptor capacity. `vulkaninfo` also reported an AMD Radeon 780M integrated GPU with driver 26.8.1; the application run did not select that adapter.

The Vulkan validation layer was required and active. The run emitted four `WARNING-Shader-OutputNotConsumed` messages for vertex location 10 and no validation errors. The system Vulkan loader and `vulkaninfo` also reported an unreadable Epic Online Services overlay manifest. That stale third-party implicit-layer registration did not prevent instance creation, rendering, or clean shutdown.

## Artifacts

| Artifact | SHA-256 |
| --- | --- |
| `Sokoban3D-0.1.0-Windows-x64-Runtime.zip` | `559FB61B47B012CF6D212ABBEECDCF6BD4E96D5712D4CA4504B7A2F197B7B8AA` |
| `Sokoban3D-0.1.0-Windows-x64-Symbols.zip` | `524A757476D59866D02F977715A268ABC4372A302527B10D12150EF72DB1315D` |

## Release-signoff limits

This packet completes the automated repository validation for the implemented review. It does not replace the manual release matrix in `packaging/ReleaseValidation.md`. The ten-minute controller/keyboard session, save-resume observation, swapchain/minimize/multi-monitor checks, clean-machine installer upgrade and uninstall lifecycle, signed-artifact verification, and separate AMD/Intel hardware rows were not executed in this automated pass.
