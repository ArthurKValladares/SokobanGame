#include "engine/ui/Ui.hpp"

#include "engine/ui/FontAtlas.hpp"

#include <algorithm>

namespace sokoban {

UiContext::UiContext(const FontAtlas& font)
    : font_(&font)
{
}

void UiContext::beginFrame(
    Vec2 viewportSize,
    Vec2 mousePosition,
    bool mouseDown,
    bool mousePressed)
{
    drawData_.viewportSize = viewportSize;
    drawData_.commands.clear();
    mousePosition_ = mousePosition;
    mouseDown_ = mouseDown;
    mousePressed_ = mousePressed;
    if (!mouseDown_) {
        activeControl_.clear();
    }
}

void UiContext::endFrame()
{
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

void UiContext::text(Vec2 position, std::string_view value, Vec4 color, float size)
{
    const float scale = size / font_->pixelHeight();
    float cursorX = position.x;
    float baseline = position.y + font_->ascent() * scale;
    for (char character : value) {
        if (character == '\n') {
            cursorX = position.x;
            baseline += font_->lineHeight() * scale;
            continue;
        }
        const FontGlyph& glyph = font_->glyph(character);
        if (glyph.size.x > 0.0f && glyph.size.y > 0.0f) {
            drawData_.commands.push_back({
                .kind = UiDrawKind::FontGlyph,
                .rect = {
                    { cursorX + glyph.offset.x * scale, baseline + glyph.offset.y * scale },
                    { glyph.size.x * scale, glyph.size.y * scale },
                },
                .uvRect = glyph.uv,
                .color = color,
            });
        }
        cursorX += glyph.advance * scale;
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
