#include "engine/ui/LevelCompleteOverlay.hpp"

#include "engine/ui/MenuKit.hpp"
#include "engine/ui/Ui.hpp"
#include "engine/ui/UiControls.hpp"
#include "engine/ui/UiLayout.hpp"

#include <string>
#include <vector>

namespace sokoban {
namespace {

using uiControls::ButtonTone;
using menuKit::DurationStyle;

constexpr Vec4 labelColor { 0.83f, 0.86f, 0.83f, 1.0f };
constexpr Vec4 valueColor { 0.68f, 0.88f, 0.82f, 1.0f };
constexpr Vec4 recordColor { 0.98f, 0.84f, 0.42f, 1.0f };

void drawStatRow(
    UiContext& ui,
    UiRect row,
    std::string_view label,
    std::string_view value,
    Vec4 color,
    float size = 22.0f)
{
    ui.text(row.position, label, labelColor, size);
    const Vec2 measured = ui.measureText(value, size);
    ui.text({
        row.position.x + row.size.x - measured.x,
        row.position.y,
    }, value, color, size);
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

std::optional<OverlayAction> LevelCompleteOverlay::draw(
    UiContext& ui,
    Vec2 viewport,
    const LevelCompleteInput& input)
{
    if (!open_) {
        return std::nullopt;
    }
    if (mode_ == Mode::Game) {
        return drawGameComplete(ui, viewport, input);
    }
    return drawLevelComplete(ui, viewport, input);
}

std::optional<OverlayAction> LevelCompleteOverlay::drawLevelComplete(
    UiContext& ui,
    Vec2 viewport,
    const LevelCompleteInput& input)
{
    menuKit::RowList rows;
    const int continueRowIndex = rows.add();
    const int titleRowIndex = rows.add();
    (void)rows.navigate(selectedRow_, input.up, input.down);

    ui.rect({ { 0.0f, 0.0f }, viewport }, { 0.015f, 0.020f, 0.021f, 0.72f });
    const UiRect panel = menuKit::centeredPanel(viewport, 560.0f, 520.0f, 420.0f);
    ui.panel(panel);

    menuKit::MenuPage page(26.0f);
    UiLayoutTree& tree = page.tree;
    const UiLayoutNode movesRow = tree.item(tree.root(), 34.0f);
    tree.spacer(tree.root(), 12.0f);
    const UiLayoutNode timeRow = tree.item(tree.root(), 34.0f);
    tree.flexibleSpacer(tree.root());
    const UiLayoutNode continueRow = tree.item(tree.root(), 58.0f);
    tree.spacer(tree.root(), 16.0f);
    const UiLayoutNode titleRow = tree.item(tree.root(), 52.0f);
    tree.arrange(panel);

    page.drawHeader(
        ui, "LEVEL " + std::to_string(stats_.level + 1) + " COMPLETE", 38.0f);

    std::string movesText = std::to_string(stats_.moves);
    if (stats_.newBestMoves) {
        movesText += "  NEW BEST";
    } else if (stats_.previousBestMoves) {
        movesText += "  (best " + std::to_string(*stats_.previousBestMoves) + ")";
    }
    drawStatRow(ui, tree.rect(movesRow), "Moves", movesText,
        stats_.newBestMoves ? recordColor : valueColor);

    std::string timeText = menuKit::formatDuration(
        stats_.timeSeconds, DurationStyle::MinutesSecondsTenths);
    if (stats_.newBestTime) {
        timeText += "  NEW BEST";
    } else if (stats_.previousBestTimeSeconds) {
        timeText += "  (best " +
            menuKit::formatDuration(
                *stats_.previousBestTimeSeconds,
                DurationStyle::MinutesSecondsTenths) +
            ")";
    }
    drawStatRow(ui, tree.rect(timeRow), "Time", timeText,
        stats_.newBestTime ? recordColor : valueColor);

    std::optional<OverlayAction> action;
    if (uiControls::button(
            ui, "level-complete.continue", tree.rect(continueRow),
            stats_.hasNextLevel ? "Next Level" : "Back To Start", {
            .tone = ButtonTone::Accent,
            .focused = selectedRow_ == continueRowIndex,
            .activate = input.confirm && selectedRow_ == continueRowIndex,
        })) {
        action = overlay::Continue {};
    }
    if (uiControls::button(
            ui, "level-complete.title", tree.rect(titleRow), "Title Screen", {
            .focused = selectedRow_ == titleRowIndex,
            .activate = input.confirm && selectedRow_ == titleRowIndex,
        })) {
        action = overlay::ToTitle {};
    }
    return action;
}

std::optional<OverlayAction> LevelCompleteOverlay::drawGameComplete(
    UiContext& ui,
    Vec2 viewport,
    const LevelCompleteInput& input)
{
    menuKit::RowList rows;
    const int levelSelectRowIndex = rows.add();
    const int titleRowIndex = rows.add();
    (void)rows.navigate(selectedRow_, input.up, input.down);

    ui.rect({ { 0.0f, 0.0f }, viewport }, { 0.015f, 0.020f, 0.021f, 0.86f });
    const float statsHeight = static_cast<float>(gameLevels_.size()) * 30.0f;
    const UiRect panel = menuKit::centeredPanel(
        viewport, 600.0f, 430.0f + statsHeight, 420.0f);
    ui.panel(panel);

    menuKit::MenuPage page(16.0f, true);
    UiLayoutTree& tree = page.tree;
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

    // The celebration heading keeps its gold color instead of the standard
    // header treatment.
    ui.centeredText(tree.rect(page.title), "GAME COMPLETE!", recordColor, 40.0f);
    ui.centeredText(tree.rect(page.subtitle),
        "Every level solved - congratulations!", labelColor, 18.0f);
    ui.divider(tree.rect(page.divider));

    int totalMoves = 0;
    double totalSeconds = 0.0;
    bool totalsComplete = true;
    for (std::size_t i = 0; i < gameLevels_.size(); ++i) {
        const GameCompleteLevelStats& level = gameLevels_[i];
        std::string valueText;
        if (level.bestMoves) {
            valueText = std::to_string(*level.bestMoves) + " moves";
            totalMoves += *level.bestMoves;
        } else {
            totalsComplete = false;
        }
        if (level.bestTimeSeconds) {
            valueText += (valueText.empty() ? "" : "  ") +
                menuKit::formatDuration(
                    *level.bestTimeSeconds, DurationStyle::MinutesSeconds);
            totalSeconds += *level.bestTimeSeconds;
        } else {
            totalsComplete = false;
        }
        if (valueText.empty()) {
            valueText = "-";
        }
        drawStatRow(ui, tree.rect(levelRows[i]),
            "Level " + std::to_string(i + 1), valueText, valueColor, 20.0f);
    }

    ui.divider(tree.rect(totalDivider));
    const std::string totalText = totalsComplete
        ? std::to_string(totalMoves) + " moves  " +
            menuKit::formatDuration(totalSeconds, DurationStyle::MinutesSeconds)
        : "-";
    drawStatRow(ui, tree.rect(totalRow), "Whole game", totalText, recordColor);

    std::optional<OverlayAction> action;
    if (uiControls::button(
            ui, "game-complete.level-select", tree.rect(levelSelectRow),
            "Level Select", {
            .tone = ButtonTone::Accent,
            .focused = selectedRow_ == levelSelectRowIndex,
            .activate = input.confirm && selectedRow_ == levelSelectRowIndex,
        })) {
        action = overlay::ToLevelSelect {};
    }
    if (uiControls::button(
            ui, "game-complete.title", tree.rect(titleRow), "Title Screen", {
            .focused = selectedRow_ == titleRowIndex,
            .activate = input.confirm && selectedRow_ == titleRowIndex,
        })) {
        action = overlay::ToTitle {};
    }
    return action;
}

} // namespace sokoban
