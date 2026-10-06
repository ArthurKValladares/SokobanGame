#include "engine/ui/Ui.hpp"

#include "engine/Log.hpp"
#include "engine/ui/FontAtlas.hpp"

#include <algorithm>
#include <cmath>

namespace sokoban {

UiContext::UiContext(const FontAtlas& font)
    : font_(&font)
    , frameArena_(
          "UI",
          arenaBytesFor<UiDrawCommand>(config::uiFrameCommandBudget) +
              128 * 1024) // Layout nodes, row geometry and binding labels.
    , drawData_(frameArena_)
{
}

void UiContext::beginFrame(
    Vec2 viewportSize,
    Vec2 mousePosition,
    bool mouseDown,
    bool mousePressed)
{
    font_->beginFrame();
    // The head goes back to the start and last frame's commands cease to
    // exist. Nothing needs destroying first: the arena holds trivially
    // destructible data and the draw data owns none of it.
    frameArena_.reset();
    drawData_ = UiDrawData(frameArena_);
    drawData_.viewportSize = viewportSize;
    mousePosition_ = mousePosition;
    mouseDown_ = mouseDown;
    mousePressed_ = mousePressed;
    if (!mouseDown_) {
        activeControl_.clear();
    }
}

void UiContext::endFrame()
{
    // Reported here rather than at the drop, so one warning covers the frame
    // instead of one per command past the budget.
    if (drawData_.commands.droppedCount() > 0 && !reportedDroppedCommands_) {
        reportedDroppedCommands_ = true;
        log::warning(log::Category::Rendering)
            << "UI frame exceeded its budget of "
            << config::uiFrameCommandBudget << " draw commands; "
            << drawData_.commands.droppedCount()
            << " were dropped. Raise config::uiFrameCommandBudget.";
    }
}

bool UiContext::contains(UiRect rectValue, Vec2 point) const
{
    return point.x >= rectValue.position.x &&
        point.y >= rectValue.position.y &&
        point.x < rectValue.position.x + rectValue.size.x &&
        point.y < rectValue.position.y + rectValue.size.y;
}

bool UiContext::hovered(UiRect rectValue) const
{
    return contains(rectValue, mousePosition_);
}

bool UiContext::clicked(UiRect rectValue) const
{
    return hovered(rectValue) && mousePressed_;
}

bool UiContext::drag(std::string_view id, UiRect rectValue)
{
    if (clicked(rectValue)) {
        activeControl_ = id;
    }
    return mouseDown_ && activeControl_ == id;
}

void UiContext::rect(UiRect rectValue, Vec4 color)
{
    if (rectValue.size.x <= 0.0f || rectValue.size.y <= 0.0f || color.w <= 0.0f) {
        return;
    }
    drawData_.commands.push_back({
        .kind = UiDrawKind::Solid,
        .rect = rectValue,
        .color = color,
    });
}

void UiContext::image(UiRect rectValue, UiRect uvRectValue, Vec4 color)
{
    if (rectValue.size.x <= 0.0f || rectValue.size.y <= 0.0f || color.w <= 0.0f) {
        return;
    }
    drawData_.commands.push_back({
        .kind = UiDrawKind::Image,
        .rect = rectValue,
        .uvRect = uvRectValue,
        .color = color,
    });
}

void UiContext::textureImage(
    UiRect rectValue,
    RenderTexture texture,
    UiRect uvRectValue,
    Vec4 color,
    NineSlice nineSlice)
{
    if (texture.isNone() || rectValue.size.x <= 0.0f ||
        rectValue.size.y <= 0.0f || color.w <= 0.0f) {
        return;
    }
    drawData_.commands.push_back({
        .kind = UiDrawKind::TextureImage,
        .rect = rectValue,
        .uvRect = uvRectValue,
        .color = color,
        .nineSlice = sanitizedNineSlice(nineSlice),
        .texture = texture,
    });
}

void UiContext::sceneImage(
    UiRect rectValue,
    UiRect uvRectValue,
    Vec4 color,
    Vec4 effectOptions)
{
    if (rectValue.size.x <= 0.0f || rectValue.size.y <= 0.0f ||
        color.w <= 0.0f) {
        return;
    }
    drawData_.commands.push_back({
        .kind = UiDrawKind::SceneImage,
        .rect = rectValue,
        .uvRect = uvRectValue,
        .color = color,
        .effectOptions = effectOptions,
    });
}

void UiContext::panel(UiRect rectValue)
{
    rect(rectValue, { 0.055f, 0.065f, 0.070f, 0.97f });
    rect({
        { rectValue.position.x + 1.0f, rectValue.position.y + 1.0f },
        { rectValue.size.x - 2.0f, rectValue.size.y - 2.0f },
    }, { 0.105f, 0.120f, 0.125f, 0.98f });
}

void UiContext::divider(UiRect rectValue)
{
    rect(rectValue, { 0.30f, 0.33f, 0.33f, 0.72f });
}

void UiContext::text(Vec2 position, std::string_view value, Vec4 color, float size, GlyphRendering rendering)
{
    if (value.empty() || size <= 0.0f || color.w <= 0.0f) return;
    const TextLayout& layout = font_->layoutText(value, size);
    for (const PositionedGlyph& placed : layout.glyphs) {
        Vec2 baseline { position.x + placed.position.x, position.y + layout.ascent + placed.position.y };
        const FontGlyph glyph = font_->glyph(placed, size, rendering, baseline.x);
        if (!glyph.outline) {
            baseline.x = std::round(baseline.x * 4.0f) * 0.25f;
            baseline.y = std::round(baseline.y);
        }
        drawGlyph(baseline, glyph, color, size);
    }
}

void UiContext::drawGlyph(Vec2 baseline, const FontGlyph& glyph, Vec4 color, float size)
{
    if (glyph.size.x <= 0.0f || glyph.size.y <= 0.0f) return;
    drawData_.commands.push_back({ .kind = UiDrawKind::FontGlyph,
        .rect = { baseline + glyph.offset, glyph.size }, .uvRect = glyph.uv, .color = color,
        .glyphCurveOffset = glyph.curveOffset, .outlineGlyph = glyph.outline, .fontSize = size });
}

Vec2 UiContext::measureInlineText(std::span<const UiInlineRun> runs, float size) const
{
    Vec2 extent { 0.0f, font_->lineHeight() * size / font_->pixelHeight() };
    for (const UiInlineRun& run : runs) {
        if (run.vectorIcon) extent.x += font_->iconGlyph(run.vectorIcon, size).advance;
        else if (!run.texture.isNone()) extent.x += size * run.iconAspectRatio;
        else extent.x += measureText(run.text, size).x;
    }
    return extent;
}

void UiContext::inlineText(Vec2 position, std::span<const UiInlineRun> runs, Vec4 color, float size)
{
    if (size <= 0.0f || color.w <= 0.0f) return;
    const float baseline = position.y + font_->ascent() * size / font_->pixelHeight();
    for (const UiInlineRun& run : runs) {
        if (run.vectorIcon) {
            const FontGlyph glyph = font_->iconGlyph(run.vectorIcon, size);
            drawGlyph({ position.x, baseline }, glyph, color, size);
            position.x += glyph.advance;
        } else if (!run.texture.isNone()) {
            const float width = size * run.iconAspectRatio;
            textureImage({ { position.x, baseline - size * 0.85f }, { width, size } }, run.texture, run.uvRect, color);
            position.x += width;
        } else {
            text(position, run.text, color, size);
            position.x += measureText(run.text, size).x;
        }
    }
}

Vec2 UiContext::measureText(std::string_view value, float size) const
{
    return font_->measureText(value, size);
}

void UiContext::centeredText(
    UiRect rectValue,
    std::string_view value,
    Vec4 color,
    float size)
{
    const Vec2 measured = measureText(value, size);
    text({
        rectValue.position.x + std::max((rectValue.size.x - measured.x) * 0.5f, 0.0f),
        rectValue.position.y + std::max((rectValue.size.y - measured.y) * 0.5f, 0.0f),
    }, value, color, size);
}

} // namespace sokoban
