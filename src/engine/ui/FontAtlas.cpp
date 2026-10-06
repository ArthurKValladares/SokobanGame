#include "engine/ui/FontAtlas.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include <hb.h>
#include <hb-ot.h>
#include <hb-gpu.h>
#include <SheenBidi/SheenBidi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace sokoban {
namespace {

template<class T, auto Destroy> using Owner = std::unique_ptr<T, decltype(Destroy)>;
using Buffer = Owner<hb_buffer_t, hb_buffer_destroy>;
using Blob = Owner<hb_blob_t, hb_blob_destroy>;
using Encoder = Owner<hb_gpu_draw_t, hb_gpu_draw_destroy>;

struct LayoutQuery { std::string_view text; float size; };
struct LayoutKey { std::string text; float size; };
struct LayoutHash {
    using is_transparent = void;
    std::size_t operator()(LayoutQuery key) const
    {
        return std::hash<std::string_view>()(key.text) ^ (std::hash<float>()(key.size) << 1);
    }
    std::size_t operator()(const LayoutKey& key) const { return (*this)(LayoutQuery { key.text, key.size }); }
};
struct LayoutEqual {
    using is_transparent = void;
    bool operator()(const LayoutKey& a, const LayoutKey& b) const { return a.size == b.size && a.text == b.text; }
    bool operator()(LayoutQuery a, const LayoutKey& b) const { return a.size == b.size && a.text == b.text; }
    bool operator()(const LayoutKey& a, LayoutQuery b) const { return (*this)(b, a); }
};

struct Face {
    std::vector<unsigned char> bytes;
    FT_Face ft = nullptr;
    hb_font_t* hb = nullptr;
    Face() = default;
    Face(const Face&) = delete;
    Face& operator=(const Face&) = delete;
    Face(Face&& other) noexcept
        : bytes(std::move(other.bytes)), ft(std::exchange(other.ft, nullptr)), hb(std::exchange(other.hb, nullptr)) {}
    ~Face() { if (hb) hb_font_destroy(hb); if (ft) FT_Done_Face(ft); }
};

struct Curve {
    hb_glyph_extents_t bounds {};
    uint32_t offset = 0;
    float unitsPerPixelHeight = 1000.0f;
    float advance = 0.0f;
};

FontGlyph curveGlyph(const Curve& curve, float size)
{
    if (curve.bounds.width == 0 || curve.bounds.height == 0) return {};
    const float scale = size / curve.unitsPerPixelHeight;
    // Expand both geometry and sampling bounds beyond the ink, allowing the
    // analytic filter to finish its edge instead of being cut by the quad.
    const float padding = 1.0f / scale;
    const float x = static_cast<float>(curve.bounds.x_bearing) - padding;
    const float y = static_cast<float>(curve.bounds.y_bearing) + padding;
    const float w = static_cast<float>(curve.bounds.width) + padding * 2.0f;
    const float h = -static_cast<float>(curve.bounds.height) + padding * 2.0f;
    return { .uv = { { x, y }, { w, -h } }, .offset = { x * scale, -y * scale },
        .size = { w * scale, h * scale }, .advance = curve.advance * scale,
        .curveOffset = curve.offset, .outline = true };
}

} // namespace

struct FontAtlas::Impl {
    FT_Library library = nullptr;
    std::vector<Face> faces;
    uint32_t side = 0;
    float baseSize = 0.0f, baseAscent = 0.0f, baseLineHeight = 0.0f;
    std::vector<std::byte> coverage;
    std::vector<std::byte> curves;
    std::array<FontGlyph, 95> ascii {};
    uint32_t shelfX = 0, shelfY = 0, shelfHeight = 0, permanentY = 0;
    uint32_t curveTexels = 0;
    uint64_t coverageRevision = 0, curveRevision = 0;
    std::vector<FontAtlasUpdate> coverageUpdates, curveUpdates;
    std::unordered_map<uint64_t, FontGlyph> rasterCache;
    std::unordered_map<uint64_t, Curve> curveCache;
    std::unordered_map<LayoutKey, TextLayout, LayoutHash, LayoutEqual> layouts;
    struct Icon { std::string name; Curve curve; };
    std::vector<Icon> icons;

    ~Impl() { faces.clear(); if (library) FT_Done_FreeType(library); }
    float metricHeight(FT_Face face) const
    {
        const FT_Face primary = faces.front().ft;
        return float(face->units_per_EM) * float(primary->ascender - primary->descender) / float(primary->units_per_EM);
    }
    void addFace(const std::filesystem::path& path)
    {
        Face face;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() <= 0) throw std::runtime_error("Cannot read UI font: " + path.string());
        face.bytes.resize(static_cast<std::size_t>(file.tellg()));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(face.bytes.data()), static_cast<std::streamsize>(face.bytes.size()));
        if (!file || face.bytes.size() > static_cast<std::size_t>(std::numeric_limits<FT_Long>::max()) ||
            FT_New_Memory_Face(library, face.bytes.data(), static_cast<FT_Long>(face.bytes.size()), 0, &face.ft)) {
            throw std::runtime_error("Cannot parse UI font: " + path.string());
        }
        if (!FT_IS_SCALABLE(face.ft) || face.ft->ascender <= face.ft->descender) {
            throw std::runtime_error("UI font must contain scalable outlines: " + path.string());
        }
        if (FT_HAS_MULTIPLE_MASTERS(face.ft)) {
            FT_MM_Var* variation = nullptr;
            if (FT_Get_MM_Var(face.ft, &variation)) throw std::runtime_error("Cannot read UI font variations");
            std::vector<FT_Fixed> coordinates(variation->num_axis);
            for (FT_UInt axis = 0; axis < variation->num_axis; ++axis) {
                const auto info = variation->axis[axis];
                coordinates[axis] = info.tag == FT_MAKE_TAG('w', 'g', 'h', 't')
                    ? std::clamp(FT_Fixed { 400 * 65536 }, info.minimum, info.maximum) : info.def;
            }
            const FT_Error error = FT_Set_Var_Design_Coordinates(face.ft, variation->num_axis, coordinates.data());
            FT_Done_MM_Var(library, variation);
            if (error) throw std::runtime_error("Cannot select regular UI font weight");
        }
        Blob blob(hb_blob_create(reinterpret_cast<const char*>(face.bytes.data()),
            static_cast<unsigned>(face.bytes.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr), hb_blob_destroy);
        hb_face_t* hbFace = hb_face_create(blob.get(), 0);
        face.hb = hb_font_create(hbFace);
        hb_face_destroy(hbFace);
        hb_ot_font_set_funcs(face.hb);
        const hb_variation_t regular { HB_TAG('w', 'g', 'h', 't'), 400.0f };
        hb_font_set_variations(face.hb, &regular, 1);
        const int upem = face.ft->units_per_EM;
        hb_font_set_scale(face.hb, upem, upem);
        faces.push_back(std::move(face));
        layouts.clear();
    }

    void changed(uint32_t x, uint32_t y, uint32_t w, uint32_t h, bool outline = false)
    {
        auto& history = outline ? curveUpdates : coverageUpdates;
        auto& revision = outline ? curveRevision : coverageRevision;
        const uint32_t size = outline ? curveAtlasSize : side;
        if (history.size() >= 4096) {
            history.clear();
            history.push_back({ ++revision, 0, 0, size, size });
        } else {
            history.push_back({ ++revision, x, y, w, h });
        }
    }

    FontGlyph raster(uint16_t faceIndex, uint32_t id, float size, uint32_t phase = 0)
    {
        // 1/64 pixel size preserves fractional DPI scaling without an unbounded
        // continuous-size cache. Positions remain fractional and independent.
        const uint32_t size64 = static_cast<uint32_t>(std::lround(size * 64.0f));
        const uint64_t key = (uint64_t(faceIndex) << 56) | (uint64_t(size64) << 32) | (uint64_t(phase) << 30) | id;
        if (const auto it = rasterCache.find(key); it != rasterCache.end()) return it->second;
        FT_Face face = faces.at(faceIndex).ft;
        const float heightUnits = metricHeight(face);
        const auto em64 = static_cast<FT_F26Dot6>(std::lround(static_cast<float>(size64) *
            static_cast<float>(face->units_per_EM) / heightUnits));
        FT_Vector delta { static_cast<FT_Pos>(phase * 16), 0 };
        FT_Set_Transform(face, nullptr, &delta);
        if (FT_Set_Char_Size(face, 0, std::max(FT_F26Dot6 { 64 }, em64), 72, 72) ||
            FT_Load_Glyph(face, id, FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT) ||
            FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) {
            throw std::runtime_error("FreeType failed to rasterize a UI glyph");
        }
        const FT_GlyphSlot slot = face->glyph;
        FontGlyph glyph { .advance = static_cast<float>(slot->advance.x) / 64.0f };
        const FT_Bitmap& bitmap = slot->bitmap;
        if (bitmap.width && bitmap.rows) {
            constexpr uint32_t gutter = 2;
            const uint32_t w = bitmap.width + gutter * 2, h = bitmap.rows + gutter * 2;
            if (w > side || h > side) return {};
            if (shelfX + w > side) { shelfX = 0; shelfY += shelfHeight; shelfHeight = 0; }
            if (shelfY + h > side) return {}; // Caller uses the outline path when full.
            for (uint32_t row = 0; row < bitmap.rows; ++row) {
                const auto sourceRow = bitmap.pitch >= 0 ? row : bitmap.rows - 1 - row;
                std::memcpy(coverage.data() + (shelfY + gutter + row) * side + shelfX + gutter,
                    bitmap.buffer + sourceRow * static_cast<uint32_t>(std::abs(bitmap.pitch)), bitmap.width);
            }
            glyph.uv = { { float(shelfX) / float(side), float(shelfY) / float(side) },
                { float(w) / float(side), float(h) / float(side) } };
            glyph.offset = { float(slot->bitmap_left) - float(gutter) - float(phase) * 0.25f,
                -float(slot->bitmap_top) - float(gutter) };
            glyph.size = { float(w), float(h) };
            changed(shelfX, shelfY, w, h);
            shelfX += w;
            shelfHeight = std::max(shelfHeight, h);
        }
        rasterCache.emplace(key, glyph);
        return glyph;
    }

    Curve encode(hb_gpu_draw_t* encoder, float metricHeight, float advance)
    {
        Curve curve { .unitsPerPixelHeight = metricHeight, .advance = advance };
        Blob blob(hb_gpu_draw_encode(encoder, &curve.bounds), hb_blob_destroy);
        unsigned length = 0;
        const char* bytes = hb_blob_get_data(blob.get(), &length);
        if (!length) return curve;
        const uint32_t texels = (length + 7) / 8;
        if (texels > curveAtlasSize * curveAtlasSize - curveTexels) {
            throw std::runtime_error("UI curve atlas exhausted its 8 MiB budget");
        }
        if (curves.empty()) curves.resize(std::size_t(curveAtlasSize) * curveAtlasSize * 8);
        curve.offset = curveTexels;
        std::memcpy(curves.data() + std::size_t(curveTexels) * 8, bytes, length);
        const uint32_t firstRow = curveTexels / curveAtlasSize;
        curveTexels += texels;
        changed(0, firstRow, curveAtlasSize, (curveTexels + curveAtlasSize - 1) / curveAtlasSize - firstRow, true);
        return curve;
    }

    const Curve& outline(uint16_t faceIndex, uint32_t id)
    {
        const uint64_t key = (uint64_t(faceIndex) << 32) | id;
        if (const auto it = curveCache.find(key); it != curveCache.end()) return it->second;
        const Face& face = faces.at(faceIndex);
        Encoder encoder(hb_gpu_draw_create_or_fail(), hb_gpu_draw_destroy);
        if (!encoder || !hb_gpu_draw_glyph_or_fail(encoder.get(), face.hb, id)) {
            throw std::runtime_error("HarfBuzz failed to encode a UI glyph outline");
        }
        return curveCache.emplace(key, encode(encoder.get(), metricHeight(face.ft),
            float(hb_font_get_glyph_h_advance(face.hb, id)))).first->second;
    }

    TextLayout shape(std::string_view text, float size)
    {
        TextLayout result;
        result.ascent = baseAscent * size / baseSize;
        result.lineHeight = baseLineHeight * size / baseSize;
        float baseline = 0.0f;
        std::size_t start = 0;
        do {
            std::size_t newline = text.find_first_of("\n\r", start);
            std::size_t separator = 1;
            for (const std::string_view unicodeSeparator : { "\xe2\x80\xa8", "\xe2\x80\xa9" }) {
                const auto at = text.find(unicodeSeparator, start);
                if (at != std::string_view::npos && (newline == std::string_view::npos || at < newline)) {
                    newline = at;
                    separator = unicodeSeparator.size();
                }
            }
            if (newline != std::string_view::npos && text[newline] == '\r' && newline + 1 < text.size() && text[newline + 1] == '\n') separator = 2;
            const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
            const std::string_view part = text.substr(start, end - start);
            float pen = 0.0f;
            if (!part.empty()) shapeLine(result, part, size, start, baseline, pen);
            result.extent.x = std::max(result.extent.x, pen);
            result.extent.y = baseline + result.lineHeight;
            if (newline == std::string_view::npos) break;
            baseline += result.lineHeight;
            start = newline + separator;
        } while (start <= text.size());
        return result;
    }

    void shapeLine(TextLayout& result, std::string_view text, float size,
        std::size_t baseCluster, float baseline, float& pen)
    {
        const SBCodepointSequence sequence { SBStringEncodingUTF8, text.data(), text.size() };
        Owner<const _SBAlgorithm, SBAlgorithmRelease> algorithm(SBAlgorithmCreate(&sequence), SBAlgorithmRelease);
        Owner<const _SBParagraph, SBParagraphRelease> paragraph(
            algorithm ? SBAlgorithmCreateParagraph(algorithm.get(), 0, text.size(), SBLevelDefaultLTR) : nullptr,
            SBParagraphRelease);
        Owner<const _SBLine, SBLineRelease> line(
            paragraph ? SBParagraphCreateLine(paragraph.get(), 0, text.size()) : nullptr, SBLineRelease);
        if (!line) throw std::runtime_error("Failed to resolve UI text direction");
        const auto boundaries = textBoundaries(text);
        Buffer decoded(hb_buffer_create(), hb_buffer_destroy);
        hb_buffer_add_utf8(decoded.get(), text.data(), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));
        unsigned count = 0;
        const hb_glyph_info_t* codepoints = hb_buffer_get_glyph_infos(decoded.get(), &count);
        struct Item { std::size_t start, end; uint16_t face; hb_script_t script; };
        std::vector<Item> items;
        std::size_t clusterStart = 0;
        unsigned firstCodepoint = 0;
        hb_script_t previousScript = HB_SCRIPT_LATIN;
        for (const TextBoundary boundary : boundaries) {
            unsigned lastCodepoint = firstCodepoint;
            while (lastCodepoint < count && codepoints[lastCodepoint].cluster < boundary.end) ++lastCodepoint;
            uint16_t chosen = 0;
            hb_script_t script = HB_SCRIPT_COMMON;
            for (unsigned index = firstCodepoint; index < lastCodepoint; ++index) {
                const auto candidate = hb_unicode_script(hb_unicode_funcs_get_default(), codepoints[index].codepoint);
                if (candidate != HB_SCRIPT_COMMON && candidate != HB_SCRIPT_INHERITED && candidate != HB_SCRIPT_UNKNOWN) script = candidate;
            }
            if (script == HB_SCRIPT_COMMON) script = previousScript;
            previousScript = script;
            for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
                bool supported = true;
                for (unsigned index = firstCodepoint; index < lastCodepoint; ++index) {
                    const uint32_t cp = codepoints[index].codepoint;
                    if (hb_unicode_general_category(hb_unicode_funcs_get_default(), cp) == HB_UNICODE_GENERAL_CATEGORY_FORMAT ||
                        (cp >= 0xfe00 && cp <= 0xfe0f)) continue;
                    hb_codepoint_t glyphId = 0;
                    if (!hb_font_get_nominal_glyph(faces[faceIndex].hb, cp, &glyphId)) supported = false;
                }
                if (supported) { chosen = static_cast<uint16_t>(faceIndex); break; }
            }
            if (!items.empty() && items.back().face == chosen && items.back().script == script) items.back().end = boundary.end;
            else items.push_back({ clusterStart, boundary.end, chosen, script });
            clusterStart = boundary.end;
            firstCodepoint = lastCodepoint;
        }
        const SBRun* runs = SBLineGetRunsPtr(line.get());
        for (SBUInteger runIndex = 0; runIndex < SBLineGetRunCount(line.get()); ++runIndex) {
            const SBRun run = runs[runIndex];
            const bool rtl = (run.level & 1) != 0;
            const auto shapeItem = [&](const Item& item) {
                const std::size_t first = std::max(item.start, static_cast<std::size_t>(run.offset));
                const std::size_t last = std::min(item.end, static_cast<std::size_t>(run.offset + run.length));
                if (first >= last) return;
                Buffer buffer(hb_buffer_create(), hb_buffer_destroy);
                hb_buffer_set_direction(buffer.get(), rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
                hb_buffer_set_script(buffer.get(), item.script);
                hb_buffer_set_language(buffer.get(), hb_language_get_default());
                hb_buffer_set_cluster_level(buffer.get(), HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
                hb_buffer_add_utf8(buffer.get(), text.data(), static_cast<int>(text.size()),
                    static_cast<unsigned>(first), static_cast<int>(last - first));
                const Face& face = faces[item.face];
                hb_shape(face.hb, buffer.get(), nullptr, 0);
                const auto* info = hb_buffer_get_glyph_infos(buffer.get(), &count);
                const auto* positions = hb_buffer_get_glyph_positions(buffer.get(), nullptr);
                const float scale = size / metricHeight(face.ft);
                for (unsigned index = 0; index < count; ++index) {
                    const auto p = positions[index];
                    result.glyphs.push_back({ .id = info[index].codepoint,
                        .cluster = static_cast<uint32_t>(baseCluster + info[index].cluster), .face = item.face,
                        .position = { pen + float(p.x_offset) * scale, baseline - float(p.y_offset) * scale },
                        .advance = float(p.x_advance) * scale });
                    pen += float(p.x_advance) * scale;
                }
            };
            if (rtl) for (auto it = items.rbegin(); it != items.rend(); ++it) shapeItem(*it);
            else for (const Item& item : items) shapeItem(item);
        }
    }
};

FontAtlas::FontAtlas() = default;
FontAtlas::~FontAtlas() = default;
FontAtlas::FontAtlas(FontAtlas&&) noexcept = default;
FontAtlas& FontAtlas::operator=(FontAtlas&&) noexcept = default;

FontAtlas FontAtlas::load(const std::filesystem::path& path, float pixelHeight, uint32_t atlasSize)
{
    if (!std::isfinite(pixelHeight) || pixelHeight <= 0.0f || pixelHeight > 256.0f || atlasSize < 128 || atlasSize > 4096) {
        throw std::invalid_argument("Invalid UI font atlas dimensions");
    }
    FontAtlas atlas;
    atlas.impl_ = std::make_unique<Impl>();
    Impl& data = *atlas.impl_;
    if (FT_Init_FreeType(&data.library)) throw std::runtime_error("Cannot initialize FreeType");
    data.addFace(path);
    data.side = atlasSize;
    data.baseSize = pixelHeight;
    const FT_Face face = data.faces.front().ft;
    const float scale = pixelHeight / float(face->ascender - face->descender);
    data.baseAscent = float(face->ascender) * scale;
    data.baseLineHeight = float(face->height) * scale;
    data.coverage.resize(std::size_t(atlasSize) * atlasSize);
    for (uint32_t index = 0; index < data.ascii.size(); ++index) {
        data.ascii[index] = data.raster(0, FT_Get_Char_Index(face, index + 32), pixelHeight);
        if (index != 0 && data.ascii[index].size.x == 0) throw std::runtime_error("UI font atlas is too small");
    }
    // Debug labels retain these ASCII UVs. Preserve their rows across eviction.
    data.permanentY = data.shelfY + data.shelfHeight;
    data.shelfX = 0; data.shelfY = data.permanentY; data.shelfHeight = 0;
    return atlas;
}

void FontAtlas::addFallback(const std::filesystem::path& path)
{
    if (impl_->faces.size() >= 255) throw std::runtime_error("UI fallback font limit reached");
    impl_->addFace(path);
}

FontAtlas FontAtlas::loadDefault(const std::filesystem::path& assetRoot)
{
    FontAtlas font = load(assetRoot / config::uiFontPath, config::uiFontPixelHeight, config::uiFontAtlasSize);
    for (const std::string_view path : config::uiFallbackFontPaths) font.addFallback(assetRoot / path);
    return font;
}

void FontAtlas::beginFrame() const
{
    Impl& data = *impl_;
    if (data.layouts.size() > 2048) data.layouts.clear();
    if (data.shelfY + data.shelfHeight > data.side * 3 / 4) {
        std::fill(data.coverage.begin() + std::size_t(data.permanentY) * data.side, data.coverage.end(), std::byte {});
        data.rasterCache.clear();
        data.shelfX = 0; data.shelfY = data.permanentY; data.shelfHeight = 0;
        data.changed(0, data.permanentY, data.side, data.side - data.permanentY);
    }
}

const FontGlyph& FontAtlas::glyph(char character) const
{
    const auto code = static_cast<unsigned char>(character);
    return impl_->ascii[(code >= 32 && code <= 126 ? code : static_cast<unsigned char>('?')) - 32];
}

FontGlyph FontAtlas::glyph(const PositionedGlyph& glyph, float size, GlyphRendering rendering, float subpixelX) const
{
    if (!std::isfinite(size) || size <= 0.0f) return {};
    if (size > 4096.0f) throw std::invalid_argument("UI glyph size exceeds 4096 pixels");
    if (rendering == GlyphRendering::Outline || (rendering == GlyphRendering::Automatic && size >= 48.0f)) {
        return curveGlyph(impl_->outline(glyph.face, glyph.id), size);
    }
    const float snapped = std::round(subpixelX * 4.0f) * 0.25f;
    const uint32_t phase = static_cast<uint32_t>(std::lround((snapped - std::floor(snapped)) * 4.0f));
    const FontGlyph bitmap = impl_->raster(glyph.face, glyph.id, size, phase);
    if (bitmap.size.x > 0) return bitmap;
    // Spaces have advances but empty extents; avoid encoding those on cache hits.
    hb_glyph_extents_t extents {};
    if (hb_font_get_glyph_extents(impl_->faces[glyph.face].hb, glyph.id, &extents) && extents.width != 0) {
        return curveGlyph(impl_->outline(glyph.face, glyph.id), size);
    }
    return bitmap;
}

const TextLayout& FontAtlas::layoutText(std::string_view text, float size) const
{
    if (!std::isfinite(size) || size <= 0.0f || text.size() > std::size_t(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("Invalid text layout size");
    }
    const LayoutQuery query { text, size };
    if (const auto it = impl_->layouts.find(query); it != impl_->layouts.end()) return it->second;
    return impl_->layouts.emplace(LayoutKey { std::string(text), size }, impl_->shape(text, size)).first->second;
}
Vec2 FontAtlas::measureText(std::string_view text, float size) const { return layoutText(text, size).extent; }

uint32_t FontAtlas::registerIcon(std::string_view name, const VectorIcon& icon)
{
    if (name.empty() || icon.path.empty() || !std::isfinite(icon.advance) || icon.advance <= 0.0f || findIcon(name)) {
        throw std::invalid_argument("Vector icons require a unique name, outline and positive advance");
    }
    Encoder encoder(hb_gpu_draw_create_or_fail(), hb_gpu_draw_destroy);
    if (!encoder) throw std::runtime_error("Cannot allocate UI icon encoder");
    hb_gpu_draw_set_scale(encoder.get(), 1000, 1000);
    hb_draw_funcs_t* funcs = hb_gpu_draw_get_funcs(encoder.get());
    hb_draw_state_t state = HB_DRAW_STATE_DEFAULT;
    for (const IconPathCommand& command : icon.path) {
        for (const Vec2 p : command.points) {
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 7.0f || std::abs(p.y) > 7.0f) {
                throw std::invalid_argument("Vector icon coordinate exceeds curve encoder range");
            }
        }
        const Vec2 a = command.points[0] * 1000.0f;
        const Vec2 b = command.points[1] * 1000.0f;
        const Vec2 c = command.points[2] * 1000.0f;
        switch (command.kind) {
        case IconPathCommand::Kind::Move: hb_draw_move_to(funcs, encoder.get(), &state, a.x, a.y); break;
        case IconPathCommand::Kind::Line: hb_draw_line_to(funcs, encoder.get(), &state, a.x, a.y); break;
        case IconPathCommand::Kind::Quadratic: hb_draw_quadratic_to(funcs, encoder.get(), &state, a.x, a.y, b.x, b.y); break;
        case IconPathCommand::Kind::Cubic: hb_draw_cubic_to(funcs, encoder.get(), &state, a.x, a.y, b.x, b.y, c.x, c.y); break;
        case IconPathCommand::Kind::Close: hb_draw_close_path(funcs, encoder.get(), &state); break;
        }
    }
    impl_->icons.push_back({ std::string(name), impl_->encode(encoder.get(), 1000.0f, icon.advance * 1000.0f) });
    return static_cast<uint32_t>(impl_->icons.size());
}

uint32_t FontAtlas::findIcon(std::string_view name) const
{
    for (std::size_t index = 0; index < impl_->icons.size(); ++index) {
        if (impl_->icons[index].name == name) return static_cast<uint32_t>(index + 1);
    }
    return 0;
}
FontGlyph FontAtlas::iconGlyph(uint32_t icon, float size) const
{
    if (!icon || icon > impl_->icons.size() || !std::isfinite(size) || size <= 0.0f) return {};
    return curveGlyph(impl_->icons[icon - 1].curve, size);
}
uint32_t FontAtlas::width() const { return impl_->side; }
uint32_t FontAtlas::height() const { return impl_->side; }
float FontAtlas::pixelHeight() const { return impl_->baseSize; }
float FontAtlas::ascent() const { return impl_->baseAscent; }
float FontAtlas::lineHeight() const { return impl_->baseLineHeight; }
const std::vector<std::byte>& FontAtlas::pixels() const { return impl_->coverage; }
const std::vector<std::byte>& FontAtlas::curvePixels() const { return impl_->curves; }
std::size_t FontAtlas::cachedLayoutCount() const { return impl_->layouts.size(); }

FontAtlasUpdate FontAtlas::updatesSince(uint64_t revision, bool curves) const
{
    const uint64_t latest = curves ? impl_->curveRevision : impl_->coverageRevision;
    FontAtlasUpdate update { .revision = latest };
    const auto& history = curves ? impl_->curveUpdates : impl_->coverageUpdates;
    bool first = true;
    for (const FontAtlasUpdate& rect : history) {
        if (rect.revision <= revision) continue;
        if (first) { update = rect; first = false; }
        else {
            const uint32_t right = std::max(update.x + update.width, rect.x + rect.width);
            const uint32_t bottom = std::max(update.y + update.height, rect.y + rect.height);
            update.x = std::min(update.x, rect.x); update.y = std::min(update.y, rect.y);
            update.width = right - update.x; update.height = bottom - update.y;
        }
    }
    update.revision = latest;
    return update;
}

} // namespace sokoban
