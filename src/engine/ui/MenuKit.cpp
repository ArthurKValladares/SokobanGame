#include "engine/ui/MenuKit.hpp"

#include "engine/ui/Ui.hpp"

#include <algorithm>

namespace sokoban::menuKit {

bool RowList::navigateCount(int& selectedRow, int rowCount, bool up, bool down)
{
    if (rowCount <= 0) {
        return false;
    }
    const int oldRow = selectedRow;
    if (up) {
        selectedRow = (selectedRow + rowCount - 1) % rowCount;
    }
    if (down) {
        selectedRow = (selectedRow + 1) % rowCount;
    }
    return selectedRow != oldRow;
}

MenuPage::MenuPage(float afterHeader, bool withSubtitle)
    : tree(UiLayoutAxis::Vertical, { 42.0f, 34.0f, 42.0f, 40.0f })
    , hasSubtitle(withSubtitle)
{
    title = tree.item(tree.root(), 58.0f);
    if (withSubtitle) {
        subtitle = tree.item(tree.root(), 24.0f);
    }
    tree.spacer(tree.root(), 12.0f);
    divider = tree.item(tree.root(), 1.0f);
    tree.spacer(tree.root(), afterHeader);
}

void MenuPage::drawHeader(
    UiContext& ui,
    std::string_view titleText,
    float titleSize,
    std::string_view subtitleText) const
{
    ui.centeredText(tree.rect(title), titleText,
        { 0.94f, 0.96f, 0.93f, 1.0f }, titleSize);
    if (hasSubtitle && !subtitleText.empty()) {
        ui.centeredText(tree.rect(subtitle), subtitleText,
            { 0.62f, 0.67f, 0.65f, 1.0f }, 16.0f);
    }
    ui.divider(tree.rect(divider));
}

UiRect centeredPanel(
    Vec2 viewport,
    float desiredWidth,
    float desiredHeight,
    float minimumHeight)
{
    const float width =
        std::max(std::min(desiredWidth, viewport.x - 32.0f), 320.0f);
    const float height =
        std::max(std::min(desiredHeight, viewport.y - 32.0f), minimumHeight);
    return {
        { (viewport.x - width) * 0.5f, (viewport.y - height) * 0.5f },
        { width, height },
    };
}

UiRect centeredColumn(Vec2 viewport, float desiredWidth)
{
    const float width =
        std::min(desiredWidth, std::max(viewport.x - 32.0f, 0.0f));
    return {
        { (viewport.x - width) * 0.5f, 0.0f },
        { width, viewport.y },
    };
}

void trailingText(
    UiContext& ui,
    UiRect row,
    std::string_view text,
    Vec4 color,
    float size,
    float rightPadding)
{
    const Vec2 measured = ui.measureText(text, size);
    ui.text({
        row.position.x + row.size.x - measured.x - rightPadding,
        row.position.y + (row.size.y - size) * 0.5f,
    }, text, color, size);
}

std::string formatDuration(double seconds, DurationStyle style)
{
    const int tenths = std::max(0, static_cast<int>(seconds * 10.0));
    const int minutes = tenths / 600;
    const int remainderTenths = tenths % 600;
    std::string result = std::to_string(minutes) + ":";
    if (remainderTenths < 100) {
        result += '0';
    }
    result += std::to_string(remainderTenths / 10);
    if (style == DurationStyle::MinutesSecondsTenths) {
        result += '.';
        result += std::to_string(remainderTenths % 10);
    }
    return result;
}

} // namespace sokoban::menuKit
