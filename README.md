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
stb_image 2.30 and stb_truetype are vendored in `third_party/stb`; they provide
platform-independent texture decoding and real-font atlas generation. The
player-facing UI uses the staged Karla typeface under `assets/ui`.

## Layered Levels

Screens use sequential `@layer N` sections in the same `.scr` file. An optional
`@water N` directive makes every Air cell on that layer water and continues the
water beyond every side of the authored board. Ground normally occupies the
water layer, while walls, goals, pressure plates, the player, and movable blocks
occupy the layer above:

```text
@water 0

@layer 0
.....
.. ..
.....

@layer 1
#####
# C #
#####
```

`.` is a solid Ground block. A space is normally Air and produces no geometry;
on the configured water layer it resolves to Water instead. Entities move
through open cells supported by Ground, walls, or water directly beneath them;
unsupported air is not walkable. The legacy `W` tile is still accepted when
loading older screens, but new water layouts should use `@water N`. In the
editor, new documents start with a Ground layer and an Air/gameplay layer, and
selecting a layer shows that layer plus the layers beneath it.

## Build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\sokoban.exe
```
