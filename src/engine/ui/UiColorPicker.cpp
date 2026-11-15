#include "engine/ui/UiColorPicker.hpp"

#include "engine/ui/UiColorValues.hpp"
#include "engine/ui/UiControls.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace sokoban {
namespace {

namespace colorValues = ui::color;
using colorValues::Format;

struct FieldInfo {
    Format format;
    std::string_view label;
    std::string_view id;
};

constexpr std::array fieldInfo {
    FieldInfo { Format::Hex, "HEX", "color.hex" },
    FieldInfo { Format::Rgb, "RGB", "color.rgb" },
    FieldInfo { Format::Cmyk, "CMYK", "color.cmyk" },
    FieldInfo { Format::Hsv, "HSV", "color.hsv" },
    FieldInfo { Format::Hsl, "HSL", "color.hsl" },
};
constexpr UiControlId svId = 1;
constexpr UiControlId hueId = 2;
constexpr UiControlId firstFieldId = 3;
constexpr UiControlId copyId = firstFieldId + fieldInfo.size();
constexpr UiControlId cancelId = copyId + 1;
constexpr UiControlId applyId = cancelId + 1;
constexpr UiControlId headerId = applyId + 1;

constexpr Vec4 ink { 0.94f, 0.95f, 0.90f, 1.0f };
constexpr Vec4 mutedInk { 0.68f, 0.75f, 0.72f, 1.0f };
constexpr Vec4 accentInk { 0.54f, 0.86f, 0.77f, 1.0f };
constexpr Vec4 errorInk { 1.0f, 0.66f, 0.57f, 1.0f };
constexpr Vec4 dark { 0.02f, 0.027f, 0.028f, 1.0f };
constexpr Vec4 titleBar { 0.10f, 0.14f, 0.14f, 1.0f };
constexpr Vec4 activeTitleBar { 0.15f, 0.21f, 0.20f, 1.0f };

Vec4 opaque(Vec3 color)
{
    const Vec3 linear = colorValues::displayLinear(color);
    return { linear.x, linear.y, linear.z, 1.0f };
}

float normalized(float coordinate, float start, float span)
{
    return std::clamp((coordinate - start) / span, 0.0f, 1.0f);
}

void drawFocus(UiContext& ui, UiRect bounds, float scale)
{
    const Vec2 inset { 2.0f * scale, 2.0f * scale };
    ui.rect({ bounds.position - inset, bounds.size + inset * 2.0f }, accentInk);
}

void drawColorField(UiContext& ui, UiRect bounds, Vec3 hueColor, Vec2 indicator, float scale)
{
    constexpr Vec3 white { 1.0f, 1.0f, 1.0f };
    ui.colorGradient(bounds, { white, hueColor, {}, {} });
    const Vec2 point = bounds.position + Vec2 { bounds.size.x * indicator.x, bounds.size.y * indicator.y };
    for (float radius : { 6.0f, 4.0f, 2.0f }) {
        const Vec2 inset = Vec2 { radius, radius } * scale;
        ui.rect({ point - inset, inset * 2.0f }, radius == 4.0f ? ink : dark);
    }
}

void drawHueBar(UiContext& ui, UiRect bounds, float hue, float scale)
{
    const float step = bounds.size.x / 6.0f;
    for (int segment = 0; segment < 6; ++segment) {
        const float start = static_cast<float>(segment);
        const Vec3 left = colorValues::fromHsv({ start * 60.0f, 1.0f, 1.0f });
        const Vec3 right = colorValues::fromHsv({ (start + 1.0f) * 60.0f, 1.0f, 1.0f });
        ui.colorGradient({ bounds.position + Vec2 { step * start, 0.0f }, { step, bounds.size.y } },
            { left, right, left, right });
    }
    const Vec2 center = bounds.position + Vec2 { bounds.size.x * hue / 360.0f, 0.0f };
    ui.rect({ center - Vec2 { 4.0f, 3.0f } * scale, { 8.0f * scale, bounds.size.y + 6.0f * scale } }, dark);
    ui.rect({ center - Vec2 { 2.0f, 1.0f } * scale, { 4.0f * scale, bounds.size.y + 2.0f * scale } }, ink);
}

bool isColorControl(UiControlId id)
{
    return id == svId || id == hueId;
}

} // namespace

void UiColorPicker::open(Vec3 initial, std::string_view title)
{
    title_ = title;
    const auto unit = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; };
    color_ = { unit(initial.x), unit(initial.y), unit(initial.z) };
    const auto hsv = colorValues::toHsv(color_);
    hue_ = hsv.hue;
    saturation_ = hsv.saturation;
    value_ = hsv.value;
    focus_ = svId;
    adjusting_ = false;
    pressHandled_ = false;
    panelPosition_.reset();
    headerGrab_.reset();
    fieldBounds_ = {};
    fields_ = {};
    error_.clear();
    resetFieldFocus();
    syncFields();
}

void UiColorPicker::setError(std::string_view error)
{
    error_ = error;
}

UiTextEdit* UiColorPicker::activeTextEdit()
{
    return activeField_ ? &edits_[*activeField_] : nullptr;
}

void UiColorPicker::focusField(std::size_t index)
{
    activeField_ = index;
    parsedText_ = edits_[index].text();
    focus_ = firstFieldId + index;
    adjusting_ = false;
}

bool UiColorPicker::handleTextPress(UiContext& ui, Vec2 position, bool extendSelection)
{
    pressHandled_ = true;
    const auto target = std::ranges::find_if(fieldBounds_, [&](UiRect bounds) { return ui.contains(bounds, position); });
    const bool hadFocus = textFocused();
    if (target == fieldBounds_.end()) {
        submitText();
        return hadFocus;
    }

    const auto index = static_cast<std::size_t>(target - fieldBounds_.begin());
    if (activeField_ != index) {
        submitText();
        if (textFocused()) return true;
        focusField(index);
    }
    fields_[index].placeCaret(ui, *target, edits_[index], textSize_, position, extendSelection);
    return true;
}

void UiColorPicker::submitText()
{
    if (activeField_ && updateField(true)) {
        resetFieldFocus();
        syncFields();
    }
}

void UiColorPicker::blurText()
{
    if (activeField_) {
        (void)updateField(false);
        resetFieldFocus();
        syncFields();
    }
}

void UiColorPicker::resetFieldFocus()
{
    if (auto* edit = activeTextEdit()) edit->clearComposition();
    activeField_.reset();
    caret_ = {};
    fieldError_ = false;
}

bool UiColorPicker::updateField(bool showError)
{
    auto* edit = activeTextEdit();
    if (!edit) return true;
    if (!edit->composition().text.empty()) return false;
    if (edit->text() == parsedText_) {
        fieldError_ = false;
        return true;
    }
    const auto parsed = colorValues::parse(edit->text(), fieldInfo[*activeField_].format);
    if (!parsed) {
        fieldError_ |= showError;
        return false;
    }
    parsedText_ = edit->text();
    fieldError_ = false;
    if (*parsed != color_) {
        color_ = *parsed;
        const auto hsv = colorValues::toHsv(color_);
        if (hsv.saturation > 0.0f) hue_ = hsv.hue;
        saturation_ = hsv.saturation;
        value_ = hsv.value;
        error_.clear();
    }
    syncFields();
    return true;
}

void UiColorPicker::syncFields()
{
    static_assert(fieldCount == fieldInfo.size());
    for (std::size_t i = 0; i < edits_.size(); ++i) {
        if (activeField_ == i) continue;
        const std::string text = colorValues::format(color_, fieldInfo[i].format);
        if (edits_[i].text() != text) edits_[i].setText(text);
    }
}

void UiColorPicker::setHsv(float hue, float saturation, float value)
{
    resetFieldFocus();
    hue_ = std::clamp(hue, 0.0f, 359.999f);
    saturation_ = std::clamp(saturation, 0.0f, 1.0f);
    value_ = std::clamp(value, 0.0f, 1.0f);
    const Vec3 next = colorValues::fromHsv({ hue_, saturation_, value_ });
    if (next != color_) error_.clear();
    color_ = next;
    syncFields();
}

UiColorPickerAction UiColorPicker::draw(UiContext& ui, Vec2 viewport, float scale,
    const UiColorPickerInput& input)
{
    using Command = UiColorPickerAction::Command;
    constexpr Vec2 preferred { 608.0f, 450.0f };
    const bool stacked = viewport.x < 540.0f * scale && viewport.y > viewport.x;
    const Vec2 logicalSize = stacked ? Vec2 { 372.0f, 652.0f } : preferred;
    scale = std::min({ scale, viewport.x / (logicalSize.x + 24.0f), viewport.y / (logicalSize.y + 24.0f) });
    if (!std::isfinite(scale) || scale <= 0.0f) return {};
    const Vec2 panelSize = logicalSize * scale;
    const float margin = 12.0f * scale;
    const float headerHeight = 49.0f * scale;
    const bool resized = viewport != previousViewport_ || panelSize != previousPanelSize_;
    previousViewport_ = viewport;
    previousPanelSize_ = panelSize;
    Vec2 position = panelPosition_.value_or((viewport - panelSize) * 0.5f);
    if (resized) {
        position.x = std::clamp(position.x, margin, std::max(margin, viewport.x - panelSize.x - margin));
        position.y = std::clamp(position.y, margin, std::max(margin, viewport.y - panelSize.y - margin));
    }

    const UiRect headerBeforeDrag { position, { panelSize.x, headerHeight } };
    const bool grabbedHeader = ui.clicked(headerBeforeDrag);
    if (grabbedHeader) headerGrab_ = *ui.mousePressPosition() - position;
    const auto pointer = ui.dragPosition("color.window-header", headerBeforeDrag);
    if (pointer && headerGrab_) {
        if (resized && !grabbedHeader) *headerGrab_ = *pointer - position;
        const Vec2 target = *pointer - *headerGrab_;
        // The panel can be moved aside even in a compact viewport. Keep the
        // header reachable, and fit the whole panel again when the viewport changes.
        const float gripWidth = 128.0f * scale;
        position.x = std::clamp(target.x, margin + gripWidth - panelSize.x, viewport.x - margin - gripWidth);
        position.y = std::clamp(target.y, margin, viewport.y - margin - headerHeight);
    }
    if (!pointer || !ui.mouseDown()) headerGrab_.reset();
    panelPosition_ = position;

    const UiRect panel { position, panelSize };
    const auto boundsAt = [&](float x, float y, float w, float h) -> UiRect {
        return { panel.position + Vec2 { x, y } * scale, Vec2 { w, h } * scale };
    };
    const float visualWidth = stacked ? 332.0f : 276.0f;
    const float visualHeight = stacked ? 172.0f : 222.0f;
    const UiRect sv = boundsAt(20.0f, 63.0f, visualWidth, visualHeight);
    const UiRect hue = boundsAt(20.0f, 78.0f + visualHeight, visualWidth, 18.0f);
    const UiRect swatch = boundsAt(20.0f, 110.0f + visualHeight, 56.0f, 28.0f);
    const float valuesX = stacked ? 20.0f : 318.0f;
    const float valuesY = stacked ? 326.0f : 63.0f;
    const float fieldsWidth = logicalSize.x - valuesX - 68.0f;
    const UiRect copy = boundsAt(logicalSize.x - 108.0f, valuesY, 88.0f, 34.0f);
    const UiRect cancel = boundsAt(logicalSize.x - 230.0f, logicalSize.y - 58.0f, 96.0f, 38.0f);
    const UiRect apply = boundsAt(logicalSize.x - 124.0f, logicalSize.y - 58.0f, 104.0f, 38.0f);
    for (std::size_t i = 0; i < fieldBounds_.size(); ++i) {
        const float width = i == 0 ? fieldsWidth - 96.0f : fieldsWidth;
        fieldBounds_[i] = boundsAt(valuesX + 48.0f, valuesY + 49.0f * static_cast<float>(i), width, 34.0f);
    }
    textSize_ = 17.0f * scale;
    bool handled = std::exchange(pressHandled_, false);
    if (!handled) {
        if (const auto press = ui.mousePressPosition()) {
            if (grabbedHeader) submitText();
            else (void)handleTextPress(ui, *press, input.extendTextSelection);
            pressHandled_ = false;
            handled = true;
        }
    }

    (void)updateField(false);
    if (pointer) adjusting_ = false;
    if (const auto point = ui.dragPosition("color.saturation-value", sv)) {
        adjusting_ = false;
        setHsv(hue_, normalized(point->x, sv.position.x, sv.size.x),
            1.0f - normalized(point->y, sv.position.y, sv.size.y));
    }
    if (const auto point = ui.dragPosition("color.hue", hue)) {
        adjusting_ = false;
        setHsv(360.0f * normalized(point->x, hue.position.x, hue.size.x), saturation_, value_);
    }

    const bool wasAdjusting = adjusting_;
    UiNavigation navigation = pointer ? UiNavigation {} : input.navigation;
    if (adjusting_) {
        navigation.previous = navigation.next = false;
        if (input.horizontalStep || input.verticalStep) {
            const float dx = static_cast<float>(input.horizontalStep);
            const float dy = static_cast<float>(input.verticalStep);
            if (focus_ == hueId) {
                setHsv(std::fmod(hue_ + (dx + dy) * 3.0f + 360.0f, 360.0f), saturation_, value_);
            } else {
                setHsv(hue_, saturation_ + dx * 0.02f, value_ + dy * 0.02f);
            }
        }
    } else if (activeField_ && (navigation.previous || navigation.next)) {
        submitText();
        if (activeField_) navigation.previous = navigation.next = false;
    }

    ui.panel(panel);
    const UiRect header { panel.position, { panelSize.x, headerHeight } };
    ui.rect(header, headerGrab_ || ui.hovered(header) ? activeTitleBar : titleBar);
    const UiRect title = boundsAt(20.0f, 12.0f, logicalSize.x - 69.0f, 32.0f);
    ui.pushClip(title);
    ui.text(title.position, title_, ink, 25.0f * scale);
    ui.popClip();
    for (float top : { 19.0f, 24.0f, 29.0f }) {
        ui.rect(boundsAt(logicalSize.x - 34.0f, top, 14.0f, 1.0f), mutedInk);
    }
    ui.divider(boundsAt(20.0f, 49.0f, logicalSize.x - 40.0f, 1.0f));
    if (focus_ == svId) drawFocus(ui, sv, scale);
    if (focus_ == hueId) drawFocus(ui, hue, scale);
    drawColorField(ui, sv, colorValues::fromHsv({ hue_, 1.0f, 1.0f }), { saturation_, 1.0f - value_ }, scale);
    drawHueBar(ui, hue, hue_, scale);
    ui.rect(swatch, mutedInk);
    const Vec2 swatchInset = Vec2 { 1.0f, 1.0f } * scale;
    ui.rect({ swatch.position + swatchInset, swatch.size - swatchInset * 2.0f }, opaque(color_));
    ui.text(swatch.position + Vec2 { 66.0f, 2.0f } * scale, "Preview", mutedInk, 15.0f * scale);
    if (adjusting_) {
        ui.text(swatch.position + Vec2 { 130.0f, 4.0f } * scale, "Arrow keys adjust color", mutedInk, 13.0f * scale);
    }

    std::array<UiControlRegion, 2 + fieldInfo.size() + 4> regions;
    regions[0] = { svId, sv };
    regions[1] = { hueId, hue };
    for (std::size_t i = 0; i < fieldInfo.size(); ++i) {
        const auto& field = fieldInfo[i];
        const auto fieldBounds = fieldBounds_[i];
        ui.text({ panel.position.x + valuesX * scale, fieldBounds.position.y + 7.0f * scale },
            field.label, mutedInk, 14.0f * scale);
        const auto result = fields_[i].draw(ui, field.id, fieldBounds, edits_[i], {
            .focused = activeField_ == i,
            .extendSelection = input.extendTextSelection,
            .pressAlreadyHandled = handled,
            .textSize = textSize_,
        });
        if (activeField_ == i) caret_ = result.caret;
        regions[i + 2] = { firstFieldId + i, fieldBounds };
    }
    regions[regions.size() - 4] = { copyId, copy };
    regions[regions.size() - 3] = { cancelId, cancel };
    regions[regions.size() - 2] = { applyId, apply };
    regions.back() = { headerId, grabbedHeader ? headerBeforeDrag : header, true, false };
    const auto interaction = ui.resolveControls("Color picker", regions, focus_, navigation);
    const UiControlId previousFocus = std::exchange(focus_, interaction.focused);
    if (!isColorControl(focus_)) adjusting_ = false;
    const bool keyboardActivate = navigation.activate && !ui.mousePressPosition() && !interaction.released;
    const auto editedField = activeField_;
    if (keyboardActivate) {
        if (isColorControl(interaction.activated)) adjusting_ = !wasAdjusting;
        else if (editedField && interaction.activated == firstFieldId + *editedField) submitText();
    }
    // Arrow/Tab navigation activates a field for typing. A mouse press goes
    // through its caret hit-test before controls resolve.
    const bool enteredField = !editedField && keyboardActivate && interaction.activated == focus_;
    const bool movedToField = focus_ != previousFocus && (navigation.previous || navigation.next);
    if (focus_ >= firstFieldId && focus_ < copyId && !activeField_ && (enteredField || movedToField)) {
        const auto index = static_cast<std::size_t>(focus_ - firstFieldId);
        focusField(index);
        edits_[index].selectAll();
    }

    const auto button = [&](UiControlId id, UiRect bounds, std::string_view label, uiControls::ButtonTone tone) {
        ui.pushClip(bounds);
        const bool pressed = uiControls::button(ui, bounds, label, {
            .tone = tone,
            .focused = focus_ == id,
            .activate = interaction.activated == id,
            .contentScale = 0.73f * scale,
        });
        ui.popClip();
        return pressed;
    };
    const bool copied = button(copyId, copy, "Copy HEX", uiControls::ButtonTone::Normal);
    const bool cancelled = button(cancelId, cancel, "Cancel", uiControls::ButtonTone::Normal);
    const bool submitted = button(applyId, apply, "Apply", uiControls::ButtonTone::Accent);
    if (submitted || copied) submitText();
    const UiRect errorBounds = boundsAt(20.0f, logicalSize.y - 86.0f, logicalSize.x - 40.0f, 23.0f);
    const std::string_view error = fieldError_
        ? std::string_view("Enter a valid value for the selected color format.") : std::string_view(error_);
    ui.pushClip(errorBounds);
    ui.text(errorBounds.position, error, errorInk, 15.0f * scale);
    ui.popClip();
    if (cancelled) return { .command = Command::Cancel };
    if (activeField_) return {};
    if (submitted) return { .command = Command::Apply, .color = color_ };
    if (copied) return { .command = Command::CopyHex, .hex = colorValues::format(color_, Format::Hex) };
    return {};
}

} // namespace sokoban
