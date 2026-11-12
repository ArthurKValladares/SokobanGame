#include "engine/ui/UiTextField.hpp"

#include "engine/ui/UiTextEdit.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace sokoban {
namespace {

constexpr float horizontalPadding = 9.0f;

struct CaretHit {
    std::size_t offset = 0;
    bool trailing = false;
};

CaretHit hitCaret(const TextLayout& layout, float x)
{
    CaretHit hit;
    float distance = std::numeric_limits<float>::infinity();
    const auto consider = [&](float edge, CaretHit candidate) {
        const float candidateDistance = std::abs(x - edge);
        if (candidateDistance < distance) {
            distance = candidateDistance;
            hit = candidate;
        }
    };
    for (const TextCaretSpan& span : layout.caretSpans) {
        const auto [left, right] = std::minmax(span.startX, span.endX);
        // Adjacent visual spans at a bidi boundary can share a coordinate
        // while mapping to different logical offsets. Use the clicked span.
        if (x >= left && x < right) {
            return std::abs(x - span.startX) <= std::abs(x - span.endX)
                ? CaretHit { span.start, false } : CaretHit { span.end, true };
        }
        consider(span.startX, { span.start, false });
        consider(span.endX, { span.end, true });
    }
    return hit;
}

float caretX(const TextLayout& layout, std::size_t offset, bool preferTrailing)
{
    std::optional<float> leading;
    std::optional<float> trailing;
    for (const TextCaretSpan& span : layout.caretSpans) {
        if (span.start == offset) leading = span.startX;
        // Composition may combine with adjacent committed text into a single
        // grapheme. Its internal selection shares the grapheme's trailing edge.
        if (span.start < offset && offset <= span.end) trailing = span.endX;
    }
    return preferTrailing ? trailing.value_or(leading.value_or(0.0f))
                          : leading.value_or(trailing.value_or(0.0f));
}

std::string_view displayText(const UiTextEdit& edit, bool preedit, std::string& scratch)
{
    if (!preedit) return edit.text();
    const UiTextRange replaced = edit.selection();
    const std::string_view committed = edit.text();
    scratch.assign(committed.substr(0, replaced.start));
    scratch.append(edit.composition().text);
    scratch.append(committed.substr(replaced.end));
    return scratch;
}

void drawRange(UiContext& ui, const TextLayout& layout, UiTextRange bytes,
    Vec2 origin, float height, Vec4 color)
{
    if (bytes.start == bytes.end) return;
    std::optional<float> segmentStart;
    float segmentEnd = 0.0f;
    const auto flush = [&] {
        if (!segmentStart) return;
        ui.rect({ { origin.x + *segmentStart, origin.y }, { segmentEnd - *segmentStart, height } }, color);
        segmentStart.reset();
    };
    for (const TextCaretSpan& span : layout.caretSpans) {
        if (span.start >= bytes.end || span.end <= bytes.start) {
            flush();
            continue;
        }
        const auto [left, right] = std::minmax(span.startX, span.endX);
        if (!segmentStart || left > segmentEnd + 0.01f) {
            flush();
            segmentStart = left;
            segmentEnd = right;
        } else {
            segmentEnd = std::max(segmentEnd, right);
        }
    }
    flush();
}

} // namespace

void UiTextField::placeCaret(UiContext& ui, UiRect bounds, UiTextEdit& edit, float textSize,
    Vec2 pointer, bool extend)
{
    if (textSize <= 0.0f) return;
    std::string scratch;
    const bool preedit = !edit.composition().text.empty();
    const auto& layout = ui.layoutText(displayText(edit, preedit, scratch), textSize,
        TextLayoutPurpose::Editing);
    auto hit = hitCaret(layout, pointer.x - bounds.position.x - horizontalPadding + scrollX_);
    if (preedit) {
        const UiTextRange replaced = edit.selection();
        const std::size_t preeditEnd = replaced.start + edit.composition().text.size();
        if (hit.offset >= replaced.start) {
            hit.offset = hit.offset >= preeditEnd ? replaced.end + hit.offset - preeditEnd : replaced.start;
        }
    }
    edit.setCaret(hit.offset, extend);
    trailingCaret_ = hit.trailing ? std::optional { edit.caret() } : std::nullopt;
}

UiTextFieldResult UiTextField::draw(UiContext& ui, std::string_view id, UiRect bounds,
    UiTextEdit& edit, UiTextFieldOptions options)
{
    if (bounds.size.x <= 0.0f || bounds.size.y <= 0.0f || options.textSize <= 0.0f) return {};

    constexpr float verticalPadding = 2.0f;
    const UiRect content {
        bounds.position + Vec2 { horizontalPadding, verticalPadding },
        { std::max(0.0f, bounds.size.x - horizontalPadding * 2.0f),
            std::max(0.0f, bounds.size.y - verticalPadding * 2.0f) },
    };
    UiTextFieldResult result {
        .pressed = options.enabled && (!options.pressAlreadyHandled || options.focused) && ui.clicked(bounds),
    };
    const bool focused = options.enabled && (options.focused || result.pressed);
    if (const auto drag = focused ? ui.dragPosition(id, bounds) : std::nullopt) {
        if (result.pressed) {
            const Vec2 press = *ui.mousePressPosition();
            lastDragX_ = press.x;
            if (!options.pressAlreadyHandled) {
                placeCaret(ui, bounds, edit, options.textSize, press, options.extendSelection);
            }
        }
        const bool beyondField = drag->x < bounds.position.x || drag->x >= bounds.position.x + bounds.size.x;
        if (lastDragX_ != drag->x || (beyondField && ui.mouseDown())) {
            placeCaret(ui, bounds, edit, options.textSize, *drag, true);
        }
        lastDragX_ = drag->x;
    } else {
        lastDragX_.reset();
    }

    const bool preedit = focused && !edit.composition().text.empty();
    std::string scratch;
    const std::string_view displayed = displayText(edit, preedit, scratch);
    const TextLayout& layout = ui.layoutText(displayed, options.textSize, TextLayoutPurpose::Editing);
    UiTextRange selected = edit.selection();
    UiTextRange composition;
    std::size_t caretOffset = edit.caret();
    if (preedit) {
        const std::size_t start = selected.start;
        const UiTextComposition& editing = edit.composition();
        composition = { start, start + editing.text.size() };
        selected = { start + editing.selection.start, start + editing.selection.end };
        caretOffset = selected.end;
    }
    const float cursorX = caretX(layout, caretOffset, !preedit && trailingCaret_ == caretOffset);
    constexpr float caretWidth = 1.5f;
    if (focused) {
        if (cursorX < scrollX_) scrollX_ = cursorX;
        else if (cursorX + caretWidth > scrollX_ + content.size.x) {
            scrollX_ = cursorX + caretWidth - content.size.x;
        }
    }
    const float maxScroll = std::max(0.0f, layout.extent.x + caretWidth - content.size.x);
    scrollX_ = std::clamp(scrollX_, 0.0f, maxScroll);

    const Vec4 border = focused ? Vec4 { 0.30f, 0.80f, 0.72f, 1.0f }
                                : Vec4 { 0.31f, 0.36f, 0.35f, 1.0f };
    ui.rect(bounds, border);
    ui.rect({ bounds.position + Vec2 { 1.0f, 1.0f }, bounds.size - Vec2 { 2.0f, 2.0f } },
        { 0.11f, 0.14f, 0.14f, 1.0f });
    ui.pushClip(content);
    const Vec2 origin { content.position.x - scrollX_,
        content.position.y + (content.size.y - layout.lineHeight) * 0.5f };
    const Vec4 ink = options.enabled ? Vec4 { 0.92f, 0.94f, 0.92f, 1.0f }
                                     : Vec4 { 0.58f, 0.62f, 0.60f, 1.0f };
    if (focused) drawRange(ui, layout, selected, origin, layout.lineHeight, { 0.18f, 0.42f, 0.39f, 1.0f });
    if (!displayed.empty()) {
        ui.drawTextLayout(origin, layout, ink, options.textSize);
    } else {
        ui.text({ content.position.x, origin.y }, options.placeholder,
            { 0.57f, 0.62f, 0.60f, 1.0f }, options.textSize);
    }
    if (preedit) {
        drawRange(ui, layout, composition, { origin.x, origin.y + layout.lineHeight - 2.0f },
            1.5f, { 0.56f, 0.86f, 0.80f, 1.0f });
    }
    if (focused) {
        result.caret = ui.visibleRect({ { origin.x + cursorX, origin.y + 1.0f },
            { caretWidth, std::max(1.0f, layout.lineHeight - 2.0f) } });
        ui.rect(result.caret, { 0.85f, 0.98f, 0.94f, 1.0f });
    }
    ui.popClip();
    return result;
}

} // namespace sokoban
