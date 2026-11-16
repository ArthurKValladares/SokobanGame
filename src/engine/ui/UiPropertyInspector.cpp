#include "engine/ui/UiPropertyInspector.hpp"

#include "engine/ui/TextLayout.hpp"

#include <algorithm>
#include <utility>

namespace sokoban {
namespace {

constexpr float rowGap = 6.0f;
constexpr float actionHeight = 29.0f;
constexpr float minimumColumnWidth = 76.0f;
constexpr float noteSize = 14.0f;
constexpr float headingSize = 15.0f;
constexpr float lineHeight = 17.5f;
constexpr Vec4 noteInk { 0.68f, 0.75f, 0.72f, 1.0f };
constexpr Vec4 headingInk { 0.54f, 0.86f, 0.77f, 1.0f };
constexpr Vec4 actionInk { 0.92f, 0.94f, 0.92f, 1.0f };
constexpr Vec4 disabledInk { 0.50f, 0.52f, 0.51f, 0.72f };

} // namespace

UiPropertyInspector::UiPropertyInspector(UiContext& ui, UiRect viewport, float scale, UiScroll& scroll)
    : ui_(ui), viewport_(viewport), scale_(scale), scroll_(scroll)
{
}

void UiPropertyInspector::heading(std::string_view text)
{
    addText(text, true);
}

void UiPropertyInspector::note(std::string_view text)
{
    addText(text, false);
}

void UiPropertyInspector::addText(std::string_view text, bool heading)
{
    if (viewport_.size.x <= 0.0f || text.empty()) return;
    const float textSize = (heading ? headingSize : noteSize) * scale_;
    auto lines = wrapTextLines(text, viewport_.size.x,
        [&](std::string_view line) { return ui_.measureText(line, textSize).x; });
    for (auto& line : lines) {
        rows_.push_back({
            .rect = { { 0.0f, height_ }, { viewport_.size.x / scale_, lineHeight } },
            .label = std::move(line),
            .heading = heading,
        });
        height_ += lineHeight;
    }
    height_ += rowGap;
}

void UiPropertyInspector::action(Action action)
{
    actionRow(std::span { &action, 1 });
}

void UiPropertyInspector::actionRow(std::span<const Action> actions)
{
    if (viewport_.size.x <= 0.0f || actions.empty()) return;
    const float width = viewport_.size.x / scale_;
    const std::size_t columns = std::max<std::size_t>(1,
        static_cast<std::size_t>((width + rowGap) / (minimumColumnWidth + rowGap)));
    while (!actions.empty()) {
        const auto count = std::min(columns, actions.size());
        const float actionWidth = (width - static_cast<float>(count - 1) * rowGap) / static_cast<float>(count);
        for (std::size_t column = 0; column < count; ++column) {
            const auto& action = actions[column];
            const float left = static_cast<float>(column) * (actionWidth + rowGap);
            rows_.push_back({ { { left, height_ }, { actionWidth, actionHeight } },
                std::string(action.label), action.id, action.tone, action.enabled });
        }
        height_ += actionHeight + rowGap;
        actions = actions.subspan(count);
    }
}

UiInteraction UiPropertyInspector::resolve(std::string_view scope, std::string_view scrollbarId,
    std::span<const UiControlRegion> precedingControls, UiControlId& focus, Input input)
{
    scroll_.setExtents(viewport_.size.y / scale_, std::max(0.0f, height_ - rowGap));
    if (!input.interactive) return {};

    scroll_.scrollWheel(ui_, viewport_, input.wheelY, input.wheelPosition);
    auto navigation = input.navigation;
    if (navigation.previous != navigation.next && !ui_.mousePressPosition() && !ui_.mouseDown()) {
        std::vector<UiControlId> order;
        order.reserve(precedingControls.size() + rows_.size());
        for (const auto& control : precedingControls) {
            const auto visible = ui_.visibleRect(control.bounds);
            if (control.id && control.enabled && control.focusable && visible.size.x > 0.0f && visible.size.y > 0.0f) {
                order.push_back(control.id);
            }
        }
        for (const auto& row : rows_) if (row.id && row.enabled) order.push_back(row.id);

        if (!order.empty()) {
            const auto current = std::ranges::find(order, focus);
            const auto index = current == order.end() ? 0U : static_cast<std::size_t>(current - order.begin());
            focus = order[navigation.next ? (index + 1) % order.size() : (index + order.size() - 1) % order.size()];
            if (const auto target = std::ranges::find(rows_, focus, &Row::id); target != rows_.end()) {
                scroll_.reveal(target->rect.position.y, target->rect.position.y + target->rect.size.y);
            }
        }
        navigation.previous = navigation.next = false;
    }
    scroll_.drawScrollbar(ui_, scrollbarId, {
        { viewport_.position.x + viewport_.size.x + 5.0f * scale_, viewport_.position.y },
        { 6.0f * scale_, viewport_.size.y },
    });

    std::vector<UiControlRegion> regions(precedingControls.begin(), precedingControls.end());
    regions.reserve(precedingControls.size() + rows_.size());
    ui_.pushClip(viewport_);
    for (const auto& row : rows_) {
        if (row.id) regions.push_back({ row.id, ui_.visibleRect(displayRect(row.rect)), row.enabled });
    }
    ui_.popClip();
    const auto interaction = ui_.resolveControls(scope, regions, focus, navigation);
    focus = interaction.focused;
    return interaction;
}

UiControlId UiPropertyInspector::draw(const UiInteraction& interaction) const
{
    UiControlId activated = 0;
    ui_.pushClip(viewport_);
    for (const auto& row : rows_) {
        const auto bounds = displayRect(row.rect);
        if (ui_.visibleRect(bounds).size.y <= 0.0f) continue;
        if (!row.id) {
            ui_.text(bounds.position, row.label, row.heading ? headingInk : noteInk,
                (row.heading ? headingSize : noteSize) * scale_);
            continue;
        }
        (void)uiControls::button(ui_, bounds, "", {
            .tone = row.tone,
            .focused = interaction.focused == row.id,
            .enabled = row.enabled,
            .contentScale = 0.72f * scale_,
        });
        const float size = 15.0f * scale_;
        const float textWidth = ui_.measureText(row.label, size).x;
        const float available = std::max(0.0f, bounds.size.x - 8.0f * scale_);
        const float fitted = textWidth > available ? size * (available / textWidth) : size;
        if (fitted > 0.0f) {
            ui_.centeredText(bounds, row.label, row.enabled ? actionInk : disabledInk, fitted);
        }
        if (row.enabled && interaction.activated == row.id) activated = row.id;
    }
    ui_.popClip();
    return activated;
}

UiRect UiPropertyInspector::displayRect(UiRect logical) const
{
    return {
        viewport_.position + (logical.position - Vec2 { 0.0f, scroll_.offset() }) * scale_,
        logical.size * scale_,
    };
}

} // namespace sokoban
