#include "engine/ui/TitleScreen.hpp"

#include "engine/ui/Ui.hpp"
#include "engine/ui/UiControls.hpp"
#include "engine/ui/UiLayout.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace sokoban {
namespace {

using uiControls::ButtonTone;

enum class MainRow {
    Continue,
    NewGame,
    LevelSelect,
    Options,
    Quit,
    Count,
};

enum class NewGameRow {
    Cancel,
    Confirm,
    Count,
};

template <typename Row>
constexpr int rowIndex(Row row)
{
    return static_cast<int>(row);
}

UiRect centeredPanel(Vec2 viewport, float desiredHeight)
{
    const float width = std::max(std::min(560.0f, viewport.x - 32.0f), 320.0f);
    const float height = std::max(std::min(desiredHeight, viewport.y - 32.0f), 400.0f);
    return {
        { (viewport.x - width) * 0.5f, (viewport.y - height) * 0.5f },
        { width, height },
    };
}

void drawTrailingText(
    UiContext& ui,
    UiRect row,
    std::string_view text,
    Vec4 color,
    float size,
    float rightPadding = 18.0f)
{
    const Vec2 measured = ui.measureText(text, size);
    ui.text({
        row.position.x + row.size.x - measured.x - rightPadding,
        row.position.y + (row.size.y - size) * 0.5f,
    }, text, color, size);
}

std::string formatTimeSeconds(double seconds)
{
    const int whole = std::max(0, static_cast<int>(seconds));
    const int minutes = whole / 60;
    const int remainder = whole % 60;
    std::string result = std::to_string(minutes) + ":";
    if (remainder < 10) {
        result += '0';
    }
    result += std::to_string(remainder);
    return result;
}

struct TitlePageLayout {
    TitlePageLayout()
        : tree(UiLayoutAxis::Vertical, { 42.0f, 34.0f, 42.0f, 40.0f })
    {
        title = tree.item(tree.root(), 58.0f);
        subtitle = tree.item(tree.root(), 24.0f);
        tree.spacer(tree.root(), 12.0f);
        divider = tree.item(tree.root(), 1.0f);
        tree.spacer(tree.root(), 26.0f);
    }

    UiLayoutTree tree;
    UiLayoutNode title {};
    UiLayoutNode subtitle {};
    UiLayoutNode divider {};
};

} // namespace

void TitleScreen::open(std::vector<TitleLevelInfo> levels)
{
    levels_ = std::move(levels);
    open_ = true;
    setPage(Page::Main);
}

void TitleScreen::close()
{
    open_ = false;
    setPage(Page::Main);
}

void TitleScreen::back()
{
    if (open_ && page_ != Page::Main) {
        setPage(Page::Main);
    }
}

void TitleScreen::setPage(Page page)
{
    page_ = page;
    selectedRow_ = 0;
    selectedScreen_ = 0;
}

void TitleScreen::navigateRows(const TitleScreenInput& input, int rowCount)
{
    if (rowCount <= 0) {
        return;
    }
    const int oldRow = selectedRow_;
    if (input.up) {
        selectedRow_ = (selectedRow_ + rowCount - 1) % rowCount;
    }
    if (input.down) {
        selectedRow_ = (selectedRow_ + 1) % rowCount;
    }
    if (selectedRow_ != oldRow) {
        selectedScreen_ = 0;
    }
}

int TitleScreen::selectableScreens(const TitleLevelInfo& level) const
{
    if (!level.unlocked || level.screenCount <= 0) {
        return 0;
    }
    if (level.completed) {
        return level.screenCount;
    }
    return std::clamp(level.reachedScreens, 1, level.screenCount);
}

TitleScreenResult TitleScreen::draw(
    UiContext& ui,
    Vec2 viewport,
    const TitleScreenInput& input)
{
    if (!open_) {
        return {};
    }

    ui.rect({ { 0.0f, 0.0f }, viewport }, { 0.015f, 0.020f, 0.021f, 0.82f });
    const float levelSelectHeight =
        320.0f + static_cast<float>(levels_.size()) * 64.0f;
    const float height = page_ == Page::LevelSelect
        ? std::min(levelSelectHeight, 760.0f)
        : 560.0f;
    const UiRect panel = centeredPanel(viewport, height);
    ui.panel(panel);

    switch (page_) {
    case Page::Main: return drawMain(ui, panel, input);
    case Page::NewGameConfirmation: return drawNewGameConfirmation(ui, panel, input);
    case Page::LevelSelect: return drawLevelSelect(ui, panel, input);
    }
    return {};
}

TitleScreenResult TitleScreen::drawMain(
    UiContext& ui,
    UiRect panel,
    const TitleScreenInput& input)
{
    navigateRows(input, rowIndex(MainRow::Count));

    TitlePageLayout layout;
    UiLayoutTree& tree = layout.tree;
    const UiLayoutNode continueRow = tree.item(tree.root(), 58.0f);
    tree.spacer(tree.root(), 14.0f);
    const UiLayoutNode newGameRow = tree.item(tree.root(), 58.0f);
    tree.spacer(tree.root(), 14.0f);
    const UiLayoutNode levelSelectRow = tree.item(tree.root(), 58.0f);
    tree.spacer(tree.root(), 14.0f);
    const UiLayoutNode optionsRow = tree.item(tree.root(), 58.0f);
    tree.flexibleSpacer(tree.root());
    const UiLayoutNode quitDivider = tree.item(tree.root(), 1.0f);
    tree.spacer(tree.root(), 20.0f);
    const UiLayoutNode quitRow = tree.item(tree.root(), 58.0f);
    tree.arrange(panel);

    ui.centeredText(tree.rect(layout.title), "SOKOBAN 3D",
        { 0.94f, 0.96f, 0.93f, 1.0f }, 44.0f);
    ui.centeredText(tree.rect(layout.subtitle), "a tile-pushing puzzle",
        { 0.62f, 0.67f, 0.65f, 1.0f }, 18.0f);
    ui.divider(tree.rect(layout.divider));

    TitleScreenResult result;
    if (uiControls::button(
            ui, "title.continue", tree.rect(continueRow), "Continue", {
            .tone = ButtonTone::Accent,
            .focused = selectedRow_ == rowIndex(MainRow::Continue),
            .activate = input.confirm && selectedRow_ == rowIndex(MainRow::Continue),
        })) {
        result.continueRequested = true;
    }
    if (uiControls::button(
            ui, "title.new-game", tree.rect(newGameRow), "New Game", {
            .focused = selectedRow_ == rowIndex(MainRow::NewGame),
            .activate = input.confirm && selectedRow_ == rowIndex(MainRow::NewGame),
        })) {
        setPage(Page::NewGameConfirmation);
    }
    if (uiControls::button(
            ui, "title.level-select", tree.rect(levelSelectRow), "Level Select", {
            .focused = selectedRow_ == rowIndex(MainRow::LevelSelect),
            .activate = input.confirm && selectedRow_ == rowIndex(MainRow::LevelSelect),
        })) {
        setPage(Page::LevelSelect);
    }
    if (uiControls::button(
            ui, "title.options", tree.rect(optionsRow), "Options", {
            .focused = selectedRow_ == rowIndex(MainRow::Options),
            .activate = input.confirm && selectedRow_ == rowIndex(MainRow::Options),
        })) {
        result.optionsRequested = true;
    }
    ui.divider(tree.rect(quitDivider));
    if (uiControls::button(
            ui, "title.quit", tree.rect(quitRow), "Quit", {
            .tone = ButtonTone::Danger,
            .focused = selectedRow_ == rowIndex(MainRow::Quit),
            .activate = input.confirm && selectedRow_ == rowIndex(MainRow::Quit),
        })) {
        result.quitRequested = true;
    }
    return result;
}

TitleScreenResult TitleScreen::drawNewGameConfirmation(
    UiContext& ui,
    UiRect panel,
    const TitleScreenInput& input)
{
    navigateRows(input, rowIndex(NewGameRow::Count));

    TitlePageLayout layout;
    UiLayoutTree& tree = layout.tree;
    tree.spacer(tree.root(), 8.0f);
    const UiLayoutNode message = tree.item(tree.root(), 44.0f);
    const UiLayoutNode detail = tree.item(tree.root(), 30.0f);
    tree.spacer(tree.root(), 58.0f);
    const UiLayoutNode cancelRow = tree.item(tree.root(), 58.0f);
    tree.spacer(tree.root(), 18.0f);
    const UiLayoutNode confirmRow = tree.item(tree.root(), 58.0f);
    tree.flexibleSpacer(tree.root());
    tree.arrange(panel);

    ui.centeredText(tree.rect(layout.title), "NEW GAME?",
        { 0.94f, 0.96f, 0.93f, 1.0f }, 44.0f);
    ui.divider(tree.rect(layout.divider));
    ui.centeredText(tree.rect(message),
        "Start over from the first level?",
        { 0.83f, 0.86f, 0.83f, 1.0f }, 22.0f);
    ui.centeredText(tree.rect(detail),
        "All progress and best records will be erased.",
        { 0.86f, 0.62f, 0.52f, 1.0f }, 18.0f);

    TitleScreenResult result;
    const bool cancelFocused = selectedRow_ == rowIndex(NewGameRow::Cancel);
    if (uiControls::button(
            ui, "title.new-game.cancel", tree.rect(cancelRow), "Cancel", {
            .focused = cancelFocused,
            .activate = input.confirm && cancelFocused,
        })) {
        setPage(Page::Main);
    }
    const bool confirmFocused = selectedRow_ == rowIndex(NewGameRow::Confirm);
    if (uiControls::button(
            ui, "title.new-game.confirm", tree.rect(confirmRow), "Erase And Start", {
            .tone = ButtonTone::Danger,
            .focused = confirmFocused,
            .activate = input.confirm && confirmFocused,
        })) {
        result.newGameConfirmed = true;
    }
    return result;
}

TitleScreenResult TitleScreen::drawLevelSelect(
    UiContext& ui,
    UiRect panel,
    const TitleScreenInput& input)
{
    const int rowCount = static_cast<int>(levels_.size()) + 1; // + Back
    navigateRows(input, rowCount);

    TitlePageLayout layout;
    UiLayoutTree& tree = layout.tree;
    std::vector<UiLayoutNode> levelRows;
    levelRows.reserve(levels_.size());
    for (std::size_t i = 0; i < levels_.size(); ++i) {
        levelRows.push_back(tree.item(tree.root(), 54.0f));
        tree.spacer(tree.root(), 10.0f);
    }
    tree.flexibleSpacer(tree.root());
    const UiLayoutNode backRow = tree.item(tree.root(), 52.0f);
    tree.arrange(panel);

    ui.centeredText(tree.rect(layout.title), "LEVEL SELECT",
        { 0.94f, 0.96f, 0.93f, 1.0f }, 40.0f);
    ui.centeredText(tree.rect(layout.subtitle),
        "Left/Right picks the starting screen",
        { 0.62f, 0.67f, 0.65f, 1.0f }, 16.0f);
    ui.divider(tree.rect(layout.divider));

    TitleScreenResult result;
    for (std::size_t i = 0; i < levels_.size(); ++i) {
        const TitleLevelInfo& level = levels_[i];
        const UiRect row = tree.rect(levelRows[i]);
        const bool focused = selectedRow_ == static_cast<int>(i);
        const std::string label = "Level " + std::to_string(i + 1);

        if (!level.unlocked) {
            ui.panel(row);
            ui.text({
                row.position.x + 18.0f,
                row.position.y + (row.size.y - 22.0f) * 0.5f,
            }, label, { 0.45f, 0.48f, 0.47f, 0.7f }, 22.0f);
            drawTrailingText(ui, row, "LOCKED",
                { 0.45f, 0.48f, 0.47f, 0.7f }, 18.0f);
            continue;
        }

        const int screens = selectableScreens(level);
        if (focused) {
            const int oldScreen = selectedScreen_;
            if (input.left) {
                selectedScreen_ = std::max(selectedScreen_ - 1, 0);
            }
            if (input.right) {
                selectedScreen_ = std::min(
                    selectedScreen_ + 1, std::max(screens - 1, 0));
            }
            (void)oldScreen;
        }
        const int chosenScreen = focused ? selectedScreen_ : 0;

        if (uiControls::button(
                ui, "title.level-" + std::to_string(i), row, label, {
                .tone = level.completed ? ButtonTone::Normal : ButtonTone::Accent,
                .focused = focused,
                .activate = input.confirm && focused,
            })) {
            result.startRequested = TitleStartRequest {
                .level = static_cast<int>(i),
                .screen = chosenScreen,
            };
        }

        std::string status;
        if (focused && screens > 1) {
            status = "Screen " + std::to_string(chosenScreen + 1) +
                "/" + std::to_string(screens);
        } else if (level.completed && level.bestMoves) {
            status = "Best " + std::to_string(*level.bestMoves) + " moves";
            if (level.bestTimeSeconds) {
                status += " - " + formatTimeSeconds(*level.bestTimeSeconds);
            }
        } else if (!level.completed) {
            status = "In progress";
        }
        if (!status.empty()) {
            drawTrailingText(ui, row, status,
                focused
                    ? Vec4 { 0.68f, 0.88f, 0.82f, 1.0f }
                    : Vec4 { 0.62f, 0.67f, 0.65f, 1.0f },
                18.0f);
        }
    }

    const bool backFocused = selectedRow_ == rowCount - 1;
    if (uiControls::button(
            ui, "title.level-select.back", tree.rect(backRow), "Back", {
            .focused = backFocused,
            .activate = input.confirm && backFocused,
        })) {
        setPage(Page::Main);
    }
    return result;
}

} // namespace sokoban
