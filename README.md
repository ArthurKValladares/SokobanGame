# Sokoban 3D

A tiny C++20 engine seed for a future Sokoban-like 3D game. The first milestone is a Vulkan 1.4 and SDL3 Hello Triangle using dynamic rendering, synchronization2, extended dynamic state, and graphics pipeline libraries.

## Layout

- `src/engine/Application.*` owns the main loop.
- `src/engine/Window.*` keeps SDL3 platform setup isolated.
- `src/engine/render/VulkanRenderer.*` owns Vulkan instance, device, swapchain, and frame rendering.
- `shaders/` contains GLSL that CMake compiles to SPIR-V with `glslc`.

## Dependencies

- CMake 3.25+
- Vulkan SDK 1.4+
- A C++20 compiler

SDL 3.4.10 is vendored in `third_party/SDL` and is built statically by the root CMake project.

## Build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\sokoban.exe
```
