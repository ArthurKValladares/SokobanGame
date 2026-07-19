#include "engine/ui/FontAtlas.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sokoban {
namespace {

std::vector<unsigned char> readFontFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("Failed to open UI font: " + path.string());
    }
    const std::streamoff size = stream.tellg();
    if (size <= 0) {
        throw std::runtime_error("UI font file is empty: " + path.string());
    }
    std::vector<unsigned char> data(static_cast<size_t>(size));
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!stream) {
        throw std::runtime_error("Failed to read UI font: " + path.string());
    }
    return data;
}

} // namespace

FontAtlas FontAtlas::load(
    const std::filesystem::path& path,
    float pixelHeight,
    uint32_t atlasSize)
{
    if (pixelHeight <= 0.0f || atlasSize < 128) {
        throw std::invalid_argument("Invalid UI font atlas dimensions");
    }

    const std::vector<unsigned char> fontData = readFontFile(path);
    const int fontOffset = stbtt_GetFontOffsetForIndex(fontData.data(), 0);
    stbtt_fontinfo info {};
    if (fontOffset < 0 || !stbtt_InitFont(&info, fontData.data(), fontOffset)) {
        throw std::runtime_error("Failed to parse UI font: " + path.string());
    }

    FontAtlas atlas;
    atlas.width_ = atlasSize;
    atlas.height_ = atlasSize;
    atlas.pixelHeight_ = pixelHeight;
    atlas.pixels_.resize(static_cast<size_t>(atlasSize) * atlasSize);

    std::array<stbtt_bakedchar, characterCount_> baked {};
    const int bakeResult = stbtt_BakeFontBitmap(
        fontData.data(),
        fontOffset,
        pixelHeight,
        reinterpret_cast<unsigned char*>(atlas.pixels_.data()),
        static_cast<int>(atlasSize),
        static_cast<int>(atlasSize),
        firstCharacter_,
        characterCount_,
        baked.data());
    if (bakeResult <= 0) {
        throw std::runtime_error("UI font atlas is too small for: " + path.string());
    }

    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    const float fontScale = stbtt_ScaleForPixelHeight(&info, pixelHeight);
    atlas.ascent_ = static_cast<float>(ascent) * fontScale;
    atlas.lineHeight_ = static_cast<float>(ascent - descent + lineGap) * fontScale;

    for (size_t index = 0; index < atlas.glyphs_.size(); ++index) {
        const stbtt_bakedchar& source = baked[index];
        atlas.glyphs_[index] = {
            .uv = {
                {
                    static_cast<float>(source.x0) / static_cast<float>(atlasSize),
                    static_cast<float>(source.y0) / static_cast<float>(atlasSize),
                },
                {
                    static_cast<float>(source.x1 - source.x0) / static_cast<float>(atlasSize),
                    static_cast<float>(source.y1 - source.y0) / static_cast<float>(atlasSize),
                },
            },
            .offset = { source.xoff, source.yoff },
            .size = {
                static_cast<float>(source.x1 - source.x0),
                static_cast<float>(source.y1 - source.y0),
            },
            .advance = source.xadvance,
        };
    }
    return atlas;
}

const FontGlyph& FontAtlas::glyph(char character) const
{
    const int code = static_cast<unsigned char>(character);
    const int safeCode = code >= firstCharacter_ && code < firstCharacter_ + characterCount_
        ? code
        : static_cast<int>('?');
    return glyphs_[static_cast<size_t>(safeCode - firstCharacter_)];
}

Vec2 FontAtlas::measureText(std::string_view text, float size) const
{
    const float scale = size / pixelHeight_;
    float width = 0.0f;
    float widest = 0.0f;
    float height = lineHeight_ * scale;
    for (char character : text) {
        if (character == '\n') {
            widest = std::max(widest, width);
            width = 0.0f;
            height += lineHeight_ * scale;
        } else {
            width += glyph(character).advance * scale;
        }
    }
    return { std::max(widest, width), height };
}

} // namespace sokoban
