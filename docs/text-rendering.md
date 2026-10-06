# Engine text and inline icons

The player UI uses a hybrid renderer:

- HarfBuzz shapes UTF-8 into positioned glyphs, including kerning, ligatures,
  contextual forms and combining marks. SheenBidi orders bidirectional runs.
- FreeType produces light-hinted grayscale coverage below 48 display pixels.
  Glyphs are cached at 1/64-pixel size increments and four horizontal subpixel
  phases. Baselines align to the pixel grid. Two transparent texels surround
  every glyph, and the draw quad includes that margin.
- At 48 pixels and above, HarfBuzz GPU encodes the actual outlines and the Slug
  shader analytically computes coverage. There is no enlarged bitmap or
  distance-field reconstruction. Quads extend one display pixel beyond the
  ink, so antialiasing cannot be cut off at the glyph boundary.

Measurement and drawing share the same cached `TextLayout`. Positions and
advances use design metrics; hinting changes coverage rather than text width.
The nominal size retains the original engine convention of font ascent minus
descent, rather than changing existing UI sizes to em height. Fallback faces
use the primary face's em scale, share its baseline, and select regular weight
400 when a variable font provides a weight axis.

`FontAtlas::loadDefault(assetRoot)` loads Karla and the configured Noto
fallbacks. These cover Latin extensions, Greek, Cyrillic, Arabic, Hebrew and
Devanagari. Add licensed fonts to `config::uiFallbackFontPaths` for additional
scripts. Unsupported characters render the font's missing-glyph symbol.
Color emoji and bitmap-only fonts are not currently supported.

`textBoundaries()` exposes Unicode grapheme boundaries and line-break
opportunities. Lectern wrapping never splits a UTF-8 sequence, combining
sequence or emoji joiner sequence. A binding chord remains atomic. The
lectern's merged text-run width is remeasured after shaping.

## Custom icons

Icons do not need Unicode codepoints or an icon font. Register named filled
paths, consisting of moves, lines, quadratic curves, cubic curves and closed
contours. Coordinates are in em units, y points up, and baseline is zero.
Multiple contours can describe holes using opposite winding. The encoder
copies the result; the source path can be temporary.

```cpp
using C = sokoban::IconPathCommand;
const std::array path {
    C { C::Kind::Move, { { 0.1f, 0.1f } } },
    C { C::Kind::Line, { { 0.5f, 0.8f } } },
    C { C::Kind::Line, { { 0.9f, 0.1f } } },
    C { C::Kind::Close },
};
const auto confirm = font.registerIcon("controller.confirm", { path, 1.0f });
const std::array runs {
    sokoban::UiInlineRun { .text = "Press " },
    sokoban::UiInlineRun { .vectorIcon = confirm },
    sokoban::UiInlineRun { .text = " to continue" },
};
const auto extent = ui.measureInlineText(runs, 32.0f);
ui.inlineText(position, runs, color, 32.0f);
```

Vector icons use the same atlas, analytic shader, color, opacity, baseline,
advance and batching as outline text. `UiInlineRun` can also hold a texture
and UV rectangle, preserving existing Kenney keyboard/controller artwork.
Inline runs describe a single line in supplied order; shape a complete text
run together, and let a paragraph layout owner wrap runs before drawing.
The existing lectern action tags still resolve the current binding, device
and controller theme before producing the inline icon.

Generated vector paths can be registered at runtime on the UI thread. Names
and numeric icon IDs are independent of localized prose. A later SVG importer
or procedural keycap generator can feed this path API without renderer changes.

## Cache and GPU lifetime

The coverage atlas occupies 4 MiB (2048 square, R8). At a frame boundary its
dynamic rows are retired when more than three quarters of the atlas is used.
Base ASCII rows remain permanent for existing debug coordinate labels. A
glyph that cannot fit during a frame uses its outline instead. Cached layouts
contain glyph identities and positions, so atlas retirement does not stale them.
The paragraph cache is retired at frame boundaries after 2048 entries.

The curve atlas has an 8 MiB budget (1024 square, RGBA16I). Encoded outlines
are cached by face and glyph, independent of display size; icon addresses are
also stable. Exhaustion reports an explicit error instead of corrupting text.

GPU updates copy only the union of changed atlas rectangles. Each in-flight
frame has its own host-coherent upload buffer, reused after that frame's fence.
Graphics-queue barriers order previous sampling, atlas writes and new sampling.
Runtime insertions do not wait for the device or queue to become idle.
Adjacent UI commands are drawn in one instance batch, preserving painter order.

UI is composed after tonemapping into an sRGB display target, using linear
coverage alpha and normal alpha blending. The display image stays at least at
native window resolution even when the scene render scale is reduced, including
the docked Game Viewport. Captures retain their scene-coordinate dimensions.
Docked viewport resizing still samples that native display image through ImGui.

## Verification and maintenance

`ui` tests cover shaping, kerning, script fallback, contextual Arabic forms,
bidirectional ordering, grapheme wrapping, filtering gutters, cache updates
and mixed text/icons. `scene_preparation_allocations` checks that warmed menu
frames continue to allocate no memory.

With `SOKOBAN_BUILD_VULKAN_SMOKE_TESTS=ON`, `text_rendering` renders small and
large text, multiple scripts and a generated icon through the real Vulkan
renderer. It compares analytic glyphs with an independent FreeType raster
reference, inserts glyphs while two frames are in flight, and verifies capture
dimensions at 100% and 50% scene scale. Pass an output PNG path to
`sokoban_text_rendering_tests` to inspect its rendered chart.

Dependencies are pinned and built from local archives. Versions, provenance,
licenses and hashes are in `third_party/text/README.md`. HarfBuzz's GPU API is
experimental: upgrade the encoder and `HbGpu.glsl` together and repeat the GPU
comparison under Vulkan validation on supported GPUs. The current baseline
was verified on Windows with an NVIDIA RTX 4060 Laptop GPU.
