#include "engine/ui/LevelCompleteOverlay.hpp"

#include "engine/ui/Ui.hpp"
#include "engine/ui/UiControls.hpp"
#include "engine/ui/UiLayout.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace sokoban {
namespace {

using uiControls::ButtonTone;

enum class Row {
    Continue,
    Title,
    Count,
};

// Game-complete mode rows: Level Select replaces Continue.
enum class GameRow {
    LevelSelect,
    Title,
    Count,
};

template <typename RowEnum>
constexpr int rowIndex(RowEnum row)
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
    mode_ = Mode::Level;
    open_ = true;
    selectedRow_ = 0;
}

void LevelCompleteOverlay::openGameComplete(std::vector<GameCompleteLevelStats> levels)
{
    gameLevels_ = std::move(levels);
    mode_ = Mode::Game;
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
    if (mode_ == Mode::Game) {
        return drawGameComplete(ui, viewport, input);
    }
    return drawLevelComplete(ui, viewport, input);
}

LevelCompleteResult LevelCompleteOverlay::drawLevelComplete(
    UiContext& ui,
    Vec2 viewport,
    const LevelCompleteInput& input)
{
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

LevelCompleteResult LevelCompleteOverlay::drawGameComplete(
    UiContext& ui,
    Vec2 viewport,
    const LevelCompleteInput& input)
{
    if (input.up || input.down) {
        selectedRow_ =
            (selectedRow_ + rowIndex(GameRow::Count) + (input.up ? -1 : 1)) %
            rowIndex(GameRow::Count);
    }

    ui.rect({ { 0.0f, 0.0f }, viewport }, { 0.015f, 0.020f, 0.021f, 0.86f });
    const float statsHeight = static_cast<float>(gameLevels_.size()) * 30.0f;
    const float width = std::max(std::min(600.0f, viewport.x - 32.0f), 320.0f);
    const float height = std::max(
        std::min(430.0f + statsHeight, viewport.y - 32.0f), 420.0f);
    const UiRect panel {
        { (viewport.x - width) * 0.5f, (viewport.y - height) * 0.5f },
        { width, height },
    };
    ui.panel(panel);

    UiLayoutTree tree(UiLayoutAxis::Vertical, { 42.0f, 34.0f, 42.0f, 40.0f });
    const UiLayoutNode title = tree.item(tree.root(), 52.0f);
    const UiLayoutNode subtitle = tree.item(tree.root(), 26.0f);
    tree.spacer(tree.root(), 10.0f);
    const UiLayoutNode divider = tree.item(tree.root(), 1.0f);
    tree.spacer(tree.root(), 16.0f);
    std::vector<UiLayoutNode> levelRows;
    levelRows.reserve(gameLevels_.size());
    for (std::size_t i = 0; i < gameLevels_.size(); ++i) {
        levelRows.push_back(tree.item(tree.root(), 26.0f));
        tree.spacer(tree.root(), 4.0f);
    }
    tree.spacer(tree.root(), 8.0f);
    const UiLayoutNode totalDivider = tree.item(tree.root(), 1.0f);
    tree.spacer(tree.root(), 10.0f);
    const UiLayoutNode totalRow = tree.item(tree.root(), 28.0f);
    tree.flexibleSpacer(tree.root());
    const UiLayoutNode levelSelectRow = tree.item(tree.root(), 58.0f);
    tree.spacer(tree.root(), 16.0f);
    const UiLayoutNode titleRow = tree.item(tree.root(), 52.0f);
    tree.arrange(panel);

    ui.centeredText(tree.rect(title), "GAME COMPLETE!",
        { 0.98f, 0.84f, 0.42f, 1.0f }, 40.0f);
    ui.centeredText(tree.rect(subtitle),
        "Every level solved - congratulations!",
        { 0.83f, 0.86f, 0.83f, 1.0f }, 18.0f);
    ui.divider(tree.rect(divider));

    const Vec4 labelColor { 0.83f, 0.86f, 0.83f, 1.0f };
    const Vec4 valueColor { 0.68f, 0.88f, 0.82f, 1.0f };

    int totalMoves = 0;
    double totalSeconds = 0.0;
    bool totalsComplete = true;
    for (std::size_t i = 0; i < gameLevels_.size(); ++i) {
        const GameCompleteLevelStats& level = gameLevels_[i];
        const UiRect row = tree.rect(levelRows[i]);
        ui.text(row.position, "Level " + std::to_string(i + 1), labelColor, 20.0f);

        std::string valueText;
        if (level.bestMoves) {
            valueText = std::to_string(*level.bestMoves) + " moves";
            totalMoves += *level.bestMoves;
        } else {
            totalsComplete = false;
        }
        if (level.bestTimeSeconds) {
            valueText += (valueText.empty() ? "" : "  ") +
                formatTimeSeconds(*level.bestTimeSeconds);
            totalSeconds += *level.bestTimeSeconds;
        } else {
            totalsComplete = false;
        }
        if (valueText.empty()) {
            valueText = "-";
        }
        const Vec2 measured = ui.measureText(valueText, 20.0f);
        ui.text({
            row.position.x + row.size.x - measured.x,
            row.position.y,
        }, valueText, valueColor, 20.0f);
    }

    ui.divider(tree.rect(totalDivider));
    const UiRect total = tree.rect(totalRow);
    ui.text(total.position, "Whole game", labelColor, 22.0f);
    const std::string totalText = totalsComplete
        ? std::to_string(totalMoves) + " moves  " + formatTimeSeconds(totalSeconds)
        : "-";
    const Vec2 totalMeasured = ui.measureText(totalText, 22.0f);
    ui.text({
        total.position.x + total.size.x - totalMeasured.x,
        total.position.y,
    }, totalText, { 0.98f, 0.84f, 0.42f, 1.0f }, 22.0f);

    LevelCompleteResult result;
    const bool levelSelectFocused = selectedRow_ == rowIndex(GameRow::LevelSelect);
    if (uiControls::button(
            ui, "game-complete.level-select", tree.rect(levelSelectRow),
            "Level Select", {
            .tone = ButtonTone::Accent,
            .focused = levelSelectFocused,
            .activate = input.confirm && levelSelectFocused,
        })) {
        result.levelSelectRequested = true;
    }
    const bool titleFocused = selectedRow_ == rowIndex(GameRow::Title);
    if (uiControls::button(
            ui, "game-complete.title", tree.rect(titleRow), "Title Screen", {
            .focused = titleFocused,
            .activate = input.confirm && titleFocused,
        })) {
        result.titleRequested = true;
    }
    return result;
}

} // namespace sokoban
