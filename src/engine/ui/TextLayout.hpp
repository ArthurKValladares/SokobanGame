#pragma once

#include "engine/Math.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace sokoban {

enum class GlyphRendering : uint8_t { Automatic, Coverage, Outline };

// Layout contains glyph identities and positions, never atlas addresses. A
// raster cache can be retired without invalidating a cached paragraph.
struct PositionedGlyph {
    uint32_t id = 0;
    uint32_t cluster = 0; // Byte offset in the original UTF-8 string.
    uint16_t face = 0;
    Vec2 position {}; // Relative to the first baseline, in display pixels.
    float advance = 0.0f;
};

struct TextLayout {
    std::vector<PositionedGlyph> glyphs;
    Vec2 extent {};
    float ascent = 0.0f;
    float lineHeight = 0.0f;
};

struct TextBoundary {
    std::size_t end = 0; // Exclusive UTF-8 byte offset after a whole grapheme.
    bool lineBreak = false;
};

// UAX #29 graphemes and UAX #14 break opportunities. Used by every wrapping
// caller, including the lectern's text interspersed with atomic input prompts.
[[nodiscard]] std::vector<TextBoundary> textBoundaries(std::string_view text);

} // namespace sokoban
