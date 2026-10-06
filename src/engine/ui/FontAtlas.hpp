#pragma once

#include "engine/ui/TextLayout.hpp"
#include "engine/ui/Ui.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace sokoban {

struct FontGlyph {
    UiRect uv {};
    Vec2 offset {};
    Vec2 size {};
    float advance = 0.0f;
    uint32_t curveOffset = 0;
    bool outline = false;
};

// Vector coordinates use em units, with y pointing up and the baseline at 0.
// Icons have stable named identities independent of Unicode or font files.
struct IconPathCommand {
    enum class Kind : uint8_t { Move, Line, Quadratic, Cubic, Close };
    Kind kind = Kind::Move;
    Vec2 points[3] {};
};
struct VectorIcon {
    std::span<const IconPathCommand> path;
    float advance = 1.0f;
};

struct FontAtlasUpdate {
    uint64_t revision = 0;
    uint32_t x = 0, y = 0, width = 0, height = 0;
};

// Owns font faces, shaped paragraph cache, hinted coverage cache and analytic
// curve atlas. Used on the UI/renderer thread. Layout never contains atlas UVs.
class FontAtlas {
public:
    FontAtlas();
    ~FontAtlas();
    FontAtlas(FontAtlas&&) noexcept;
    FontAtlas& operator=(FontAtlas&&) noexcept;
    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;

    [[nodiscard]] static FontAtlas load(const std::filesystem::path& path,
        float pixelHeight = 36.0f, uint32_t atlasSize = 2048);
    [[nodiscard]] static FontAtlas loadDefault(const std::filesystem::path& assetRoot);
    void addFallback(const std::filesystem::path& path);
    // Eviction happens only before commands are produced for the next frame.
    void beginFrame() const;
    [[nodiscard]] const FontGlyph& glyph(char character) const;
    [[nodiscard]] FontGlyph glyph(const PositionedGlyph& glyph, float size,
        GlyphRendering rendering = GlyphRendering::Automatic, float subpixelX = 0.0f) const;
    // References remain valid until beginFrame() or a fallback font change.
    [[nodiscard]] const TextLayout& layoutText(std::string_view text, float size) const;
    [[nodiscard]] Vec2 measureText(std::string_view text, float size) const;
    [[nodiscard]] uint32_t registerIcon(std::string_view name, const VectorIcon& icon);
    [[nodiscard]] uint32_t findIcon(std::string_view name) const;
    [[nodiscard]] FontGlyph iconGlyph(uint32_t icon, float size) const;

    [[nodiscard]] uint32_t width() const;
    [[nodiscard]] uint32_t height() const;
    [[nodiscard]] float pixelHeight() const;
    [[nodiscard]] float ascent() const;
    [[nodiscard]] float lineHeight() const;
    [[nodiscard]] const std::vector<std::byte>& pixels() const;
    static constexpr uint32_t curveAtlasSize = 1024;
    [[nodiscard]] const std::vector<std::byte>& curvePixels() const;
    [[nodiscard]] FontAtlasUpdate updatesSince(uint64_t revision, bool curves = false) const;
    [[nodiscard]] std::size_t cachedLayoutCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sokoban
