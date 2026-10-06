# Text dependencies

Builds consume local, SHA-256-verified source archives through `cmake/text`.
No network or installed font/text libraries are required. All libraries are
statically linked; upstream sources retain their licenses.

| Dependency | Upstream tag | Purpose |
| --- | --- | --- |
| [FreeType](https://github.com/freetype/freetype) | `VER-2-14-3` | Light-hinted grayscale glyph coverage at display size |
| [HarfBuzz](https://github.com/harfbuzz/harfbuzz) | `14.4.0` | OpenType shaping and analytic GPU outlines |
| [SheenBidi](https://github.com/Tehreer/SheenBidi) | `v3.0.0` | Unicode bidirectional ordering |
| [libunibreak](https://github.com/adah1972/libunibreak) | `libunibreak_8_0` | Unicode grapheme boundaries and line breaks |

HarfBuzz's GPU API is experimental. The pinned implementation is deliberately
isolated behind `FontAtlas`; its CPU encoder and GPU shader must be upgraded
together. `shaders/include/HbGpu.glsl` is the upstream
`src/hb-gpu-fragment.glsl`, adapted only to declare Vulkan binding 17 and a fixed
1024-texel atlas width. Its original license notice is retained.

The FreeType and libunibreak archives are unmodified GitHub tag archives.
HarfBuzz and SheenBidi retain all top-level files plus their source/header/build
directories; upstream tests, demos, documentation and test fonts are omitted to
keep this checkout small. No retained source files are modified. ZIP timestamps
are fixed to 2026-10-06. The original downloaded archive SHA-256 values are:

```
FreeType   3f624983444f9e9149707853a792dc5a897f3afc9f23a9b8669c03ac564779d1
HarfBuzz   ff71c2d5cd7bbeb511a39914a668717b5db5cc508237594f286830e33c9df23f
SheenBidi  4c3ebd5dcc3424a20a47e0337db65e19058ff2c7448e66b11fe082c18ee5462e
libunibreak a50cb9d2f557386470ee5cb92c75507afbb52778e237cfae129397e2d491e9f4
```

The hashes of the retained archives are enforced in CMake. License copies are
installed in the runtime package. This software uses the FreeType library;
FreeType copyright belongs to its respective authors under the FreeType License.

Fallback fonts in `assets/ui` were downloaded unmodified from the Google Fonts
repository on 2026-10-06, under the SIL Open Font License. Each font has its own
adjacent `-OFL.txt`. Sources are `ofl/notosans`, `ofl/notosansarabic`,
`ofl/notosanshebrew`, and `ofl/notosansdevanagari`; all use `[wdth,wght]` variable
fonts with their `wght` axis explicitly set to 400 in both FreeType and HarfBuzz.
Other axes retain their defaults. SHA-256 values:

```
NotoSans            bfb7bb691513f12e734dc346c03a03f784912432d7e3fa8e56efcf906fe86b3d
NotoSansArabic      63111b5b2e074dd48cc67692e0a2726d86ee94c1c37fe8598257b7b4e87e869e
NotoSansHebrew      7ef36a2c3593758cdb622e1bdef4f84523e92fbc3ccc667438dd80ff54c2de88
NotoSansDevanagari  14ec4af41f27482216d1c2229f417ff9b1425e1babb014e57d1d40d03229853e
```

These cover Latin extensions, Greek, Cyrillic, Arabic, Hebrew and Devanagari.
Additional writing systems require adding an appropriate licensed fallback to
`config::uiFallbackFontPaths`. Color emoji and bitmap-only fonts are outside this
outline/coverage implementation.
