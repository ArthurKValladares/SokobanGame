#include "engine/ui/LevelCompleteOverlay.hpp"

#include "engine/ui/Ui.hpp"
#include "engine/ui/UiControls.hpp"
#include "engine/ui/UiLayout.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace sokoban {
namespace {

using uiControls::ButtonTone;

enum class Row {
    Continue,
    Title,
    Count,
};

constexpr int rowIndex(Row row)
{
    return static_cast<int>(row);
}

std::string formatTimeSeconds(double seconds)
{
    const int tenths = std::max(0, static_cast<int>(seconds * 10.0));
    const int minutes = tenths / 600;
    const int remainderTenths = tenths % 600;
    std::string result = std::to_string(minutes) + ":";
    if (remainderTenths < 100) {
        result += '0';
    }
    result += std::to_string(remainderTenths / 10);
    result += '.';
    result += std::to_string(remainderTenths % 10);
    return result;
}

} // namespace

void LevelCompleteOverlay::open(const LevelCompleteStats& stats)
{
    stats_ = stats;
    open_ = true;
    selectedRow_ = 0;
}

void LevelCompleteOverlay::close()
{
    open_ = false;
    selectedRow_ = 0;
}

LevelCompleteResult LevelCompleteOverlay::draw(
    UiContext& ui,
    Vec2 viewport,
    const LevelCompleteInput& input)
{
    if (!open_) {
        return {};
    }

    if (input.up || input.down) {
        selectedRow_ =
            (selectedRow_ + rowIndex(Row::Count) + (input.up ? -1 : 1)) %
            rowIndex(Row::Count);
    }

    ui.rect({ { 0.0f, 0.0f }, viewport }, { 0.015f, 0.020f, 0.021f, 0.72f });
    const float width = std::max(std::min(560.0f, viewport.x - 32.0f), 320.0f);
    const float height = std::max(std::min(520.0f, viewport.y - 32.0f), 420.0f);
    const UiRect panel {
        { (viewport.x - width) * 0.5f, (viewport.y - height) * 0.5f },
        { width, height },
    };
    ui.panel(panel);

    UiLayoutTree tree(UiLayoutAxis::Vertical, { 42.0f, 34.0f, 42.0f, 40.0f });
    const UiLayoutNode title = tree.item(tree.root(), 52.0f);
    tree.spacer(tree.root(), 12.0f);
    const UiLayoutNode divider = tree.item(tree.root(), 1.0f);
    tree.spacer(tree.root(), 26.0f);
    const UiLayoutNode movesRow = tree.item(tree.root(), 34.0f);
    tree.spacer(tree.root(), 12.0f);
    const UiLayoutNode timeRow = tree.item(tree.root(), 34.0f);
    tree.flexibleSpacer(tree.root());
    const UiLayoutNode continueRow = tree.item(tree.root(), 58.0f);
    tree.spacer(tree.root(), 16.0f);
    const UiLayoutNode titleRow = tree.item(tree.root(), 52.0f);
    tree.arrange(panel);

    const std::string heading =
        "LEVEL " + std::to_string(stats_.level + 1) + " COMPLETE";
    ui.centeredText(tree.rect(title), heading,
        { 0.94f, 0.96f, 0.93f, 1.0f }, 38.0f);
    ui.divider(tree.rect(divider));

    const Vec4 labelColor { 0.83f, 0.86f, 0.83f, 1.0f };
    const Vec4 valueColor { 0.68f, 0.88f, 0.82f, 1.0f };
    const Vec4 recordColor { 0.98f, 0.84f, 0.42f, 1.0f };

    const UiRect moves = tree.rect(movesRow);
    ui.text(moves.position, "Moves", labelColor, 22.0f);
    std::string movesText = std::to_string(stats_.moves);
    if (stats_.newBestMoves) {
        movesText += "  NEW BEST";
    } else if (stats_.previousBestMoves) {
        movesText += "  (best " + std::to_string(*stats_.previousBestMoves) + ")";
    }
    const Vec2 movesMeasured = ui.measureText(movesText, 22.0f);
    ui.text({
        moves.position.x + moves.size.x - movesMeasured.x,
        moves.position.y,
    }, movesText, stats_.newBestMoves ? recordColor : valueColor, 22.0f);

    const UiRect time = tree.rect(timeRow);
    ui.text(time.position, "Time", labelColor, 22.0f);
    std::string timeText = formatTimeSeconds(stats_.timeSeconds);
    if (stats_.newBestTime) {
        timeText += "  NEW BEST";
    } else if (stats_.previousBestTimeSeconds) {
        timeText += "  (best " +
            formatTimeSeconds(*stats_.previousBestTimeSeconds) + ")";
    }
    const Vec2 timeMeasured = ui.measureText(timeText, 22.0f);
    ui.text({
        time.position.x + time.size.x - timeMeasured.x,
        time.position.y,
    }, timeText, stats_.newBestTime ? recordColor : valueColor, 22.0f);

    LevelCompleteResult result;
    const bool continueFocused = selectedRow_ == rowIndex(Row::Continue);
    if (uiControls::button(
            ui, "level-complete.continue", tree.rect(continueRow),
            stats_.hasNextLevel ? "Next Level" : "Back To Start", {
            .tone = ButtonTone::Accent,
            .focused = continueFocused,
            .activate = input.confirm && continueFocused,
        })) {
        result.continueRequested = true;
    }
    const bool titleFocused = selectedRow_ == rowIndex(Row::Title);
    if (uiControls::button(
            ui, "level-complete.title", tree.rect(titleRow), "Title Screen", {
            .focused = titleFocused,
            .activate = input.confirm && titleFocused,
        })) {
        result.titleRequested = true;
    }
    return result;
}

} // namespace sokoban
