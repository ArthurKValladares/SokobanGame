#pragma once

#include "engine/Math.hpp"
#include "engine/ui/Ui.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace sokoban {

struct FontGlyph {
    UiRect uv {};
    Vec2 offset {};
    Vec2 size {};
    float advance = 0.0f;
};

class FontAtlas {
public:
    [[nodiscard]] static FontAtlas load(
        const std::filesystem::path& path,
        float pixelHeight = 36.0f,
        uint32_t atlasSize = 512);

    [[nodiscard]] const FontGlyph& glyph(char character) const;
    [[nodiscard]] Vec2 measureText(std::string_view text, float size) const;
    [[nodiscard]] uint32_t width() const { return width_; }
    [[nodiscard]] uint32_t height() const { return height_; }
    [[nodiscard]] float pixelHeight() const { return pixelHeight_; }
    [[nodiscard]] float ascent() const { return ascent_; }
    [[nodiscard]] float lineHeight() const { return lineHeight_; }
    [[nodiscard]] const std::vector<std::byte>& pixels() const { return pixels_; }

private:
    static constexpr int firstCharacter_ = 32;
    static constexpr int characterCount_ = 95;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    float pixelHeight_ = 0.0f;
    float ascent_ = 0.0f;
    float lineHeight_ = 0.0f;
    std::array<FontGlyph, characterCount_> glyphs_ {};
    std::vector<std::byte> pixels_;
};

} // namespace sokoban
