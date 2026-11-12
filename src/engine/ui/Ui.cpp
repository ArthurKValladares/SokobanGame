#include "engine/ui/Ui.hpp"

#include "engine/Log.hpp"
#include "engine/ui/FontAtlas.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace sokoban {
namespace {

UiRect intersect(UiRect first, UiRect second)
{
    const Vec2 start {
        std::max(first.position.x, second.position.x),
        std::max(first.position.y, second.position.y),
    };
    const Vec2 end {
        std::min(first.position.x + first.size.x, second.position.x + second.size.x),
        std::min(first.position.y + first.size.y, second.position.y + second.size.y),
    };
    return { start, { std::max(0.0f, end.x - start.x), std::max(0.0f, end.y - start.y) } };
}

} // namespace

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
    const bool released = !mouseDown && (pointer_.down || mousePressed);
    beginFrame(viewportSize, {
        .position = mousePosition,
        .down = mouseDown,
        .pressedAt = mousePressed ? std::optional { mousePosition } : std::nullopt,
        .releasedAt = released ? std::optional { mousePosition } : std::nullopt,
    });
}

void UiContext::beginFrame(Vec2 viewportSize, UiPointerInput pointer)
{
    font_->beginFrame();
    // The head goes back to the start and last frame's commands cease to
    // exist. Nothing needs destroying first: the arena holds trivially
    // destructible data and the draw data owns none of it.
    frameArena_.reset();
    drawData_ = UiDrawData(frameArena_);
    drawData_.viewportSize = viewportSize;
    clips_.clear();
    pointer_ = pointer;
    resolvedControls_ = false;
    pointerPressOwnedByControls_ = false;
    if (pointer_.pressedAt || (!pointer_.down && !pointer_.releasedAt)) {
        activeControl_.clear();
    }
}

void UiContext::endFrame()
{
    if (!pointer_.down) {
        activeControl_.clear();
    }
    if (!resolvedControls_) {
        capturedControl_ = 0;
    }
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
    return contains(visibleRect(rectValue), pointer_.position);
}

bool UiContext::clicked(UiRect rectValue) const
{
    return !pointerPressOwnedByControls_ && activeControl_.empty() &&
        pointer_.pressedAt && contains(visibleRect(rectValue), *pointer_.pressedAt);
}

std::optional<Vec2> UiContext::dragPosition(std::string_view id, UiRect rectValue)
{
    if (id.empty()) {
        return std::nullopt;
    }
    if (clicked(rectValue)) {
        activeControl_ = id;
    }
    if (activeControl_ != id) {
        return std::nullopt;
    }
    return pointer_.down ? std::optional { pointer_.position } : pointer_.releasedAt;
}

UiInteraction UiContext::resolveControls(
    std::string_view scope,
    std::span<const UiControlRegion> regions,
    UiControlId focus,
    UiNavigation navigation)
{
    assert(!resolvedControls_);
    resolvedControls_ = true;
    const bool previouslyCaptured = capturedControl_ != 0;
    if (controlScope_ != scope) {
        controlScope_ = scope;
        capturedControl_ = 0;
    }

    const auto hit = [&](Vec2 position) -> const UiControlRegion* {
        for (auto region = regions.rbegin(); region != regions.rend(); ++region) {
            if (region->id != 0 && contains(visibleRect(region->bounds), position)) {
                return &*region;
            }
        }
        return nullptr;
    };
    const auto eligibleForFocus = [&](const UiControlRegion& region) {
        const UiRect visible = visibleRect(region.bounds);
        return region.id != 0 && region.enabled && region.focusable &&
            visible.size.x > 0.0f && visible.size.y > 0.0f;
    };

    UiInteraction result;
    const UiControlRegion* underPointer = hit(pointer_.position);
    if (underPointer && underPointer->enabled) {
        result.hovered = underPointer->id;
    }

    if (capturedControl_ != 0 &&
        std::ranges::none_of(regions, [&](const UiControlRegion& region) {
            return region.id == capturedControl_ && region.enabled;
        })) {
        capturedControl_ = 0;
    }
    const UiControlRegion* press = nullptr;
    if (pointer_.pressedAt) {
        press = hit(*pointer_.pressedAt);
        capturedControl_ = press && press->enabled ? press->id : 0;
        result.pressed = capturedControl_;
        if (press && eligibleForFocus(*press)) {
            focus = press->id;
        }
    }

    if (!pointer_.down && capturedControl_ != 0) {
        if (pointer_.releasedAt) {
            result.released = capturedControl_;
            const UiControlRegion* release = hit(*pointer_.releasedAt);
            if (release && release->id == capturedControl_) {
                result.activated = capturedControl_;
            }
        }
        capturedControl_ = 0;
    }
    result.held = capturedControl_;
    result.ownsPointer = underPointer || press || previouslyCaptured || result.held != 0;
    pointerPressOwnedByControls_ = press != nullptr;

    auto current = std::ranges::find_if(regions, [&](const UiControlRegion& region) {
        return region.id == focus && eligibleForFocus(region);
    });
    if (current == regions.end()) {
        current = std::ranges::find_if(regions, eligibleForFocus);
    }
    const bool keyboardNavigation = !pointer_.pressedAt && !previouslyCaptured;
    if (current != regions.end()) {
        result.focused = current->id;
        if (keyboardNavigation && navigation.previous != navigation.next) {
            const auto count = static_cast<std::ptrdiff_t>(regions.size());
            auto index = current - regions.begin();
            const int direction = navigation.next ? 1 : -1;
            do {
                index = (index + count + direction) % count;
            } while (!eligibleForFocus(regions[static_cast<std::size_t>(index)]));
            result.focused = regions[static_cast<std::size_t>(index)].id;
        }
    }
    if (keyboardNavigation && navigation.activate) {
        result.activated = result.focused;
    }
    return result;
}

void UiContext::cancelPointerInteraction()
{
    capturedControl_ = 0;
    activeControl_.clear();
}

void UiContext::pushClip(UiRect rectValue)
{
    clips_.push_back(visibleRect(rectValue));
}

void UiContext::popClip()
{
    assert(!clips_.empty());
    if (!clips_.empty()) {
        clips_.pop_back();
    }
}

UiRect UiContext::currentClip() const
{
    return clips_.empty() ? UiRect { {}, drawData_.viewportSize } : clips_.back();
}

UiRect UiContext::visibleRect(UiRect rectValue) const
{
    return intersect(currentClip(), rectValue);
}

void UiContext::pushCommand(UiDrawCommand command)
{
    if (!clips_.empty()) {
        const Vec2 visible = visibleRect(command.rect).size;
        if (visible.x <= 0.0f || visible.y <= 0.0f) {
            return;
        }
        // Preserve the quad and its UVs for partial intersections. The
        // scissor clips the result, including text and nine-slice borders.
        command.clip = currentClip();
    }
    drawData_.commands.push_back(command);
}

void UiContext::rect(UiRect rectValue, Vec4 color)
{
    if (rectValue.size.x <= 0.0f || rectValue.size.y <= 0.0f || color.w <= 0.0f) {
        return;
    }
    pushCommand({
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
    pushCommand({
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
    pushCommand({
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
    pushCommand({
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
    drawTextLayout(position, layoutText(value, size), color, size, rendering);
}

const TextLayout& UiContext::layoutText(std::string_view value, float size, TextLayoutPurpose purpose) const
{
    return font_->layoutText(value, size, purpose);
}

void UiContext::drawTextLayout(Vec2 position, const TextLayout& layout, Vec4 color, float size, GlyphRendering rendering)
{
    if (size <= 0.0f || color.w <= 0.0f) return;
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
    pushCommand({ .kind = UiDrawKind::FontGlyph,
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
