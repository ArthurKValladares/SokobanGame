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

// Main-menu rows in order; the Save Slots row is hidden while no save
// exists anywhere (the New Game flow picks the slot instead).
enum class MainRow {
    Primary, // Continue, or New Game when the active slot has no progress
    SaveSlots,
    Options,
    Quit,
    Count,
};

enum class DeleteRow {
    Cancel,
    Confirm,
    Count,
};

template <typename Row>
constexpr int rowIndex(Row row)
{
    return static_cast<int>(row);
}

constexpr float titleBackgroundAspect = 16.0f / 9.0f;

UiRect aspectFillUv(Vec2 viewport, float imageAspect)
{
    if (viewport.x <= 0.0f || viewport.y <= 0.0f || imageAspect <= 0.0f) {
        return { {}, { 1.0f, 1.0f } };
    }
    const float viewportAspect = viewport.x / viewport.y;
    if (viewportAspect > imageAspect) {
        const float height = imageAspect / viewportAspect;
        return { { 0.0f, (1.0f - height) * 0.5f }, { 1.0f, height } };
    }
    const float width = viewportAspect / imageAspect;
    return { { (1.0f - width) * 0.5f, 0.0f }, { width, 1.0f } };
}

UiRect centeredColumn(Vec2 viewport, float desiredWidth)
{
    const float width = std::min(desiredWidth, std::max(viewport.x - 32.0f, 0.0f));
    return {
        { (viewport.x - width) * 0.5f, 0.0f },
        { width, viewport.y },
    };
}

UiRect leftColumn(Vec2 viewport, float desiredWidth)
{
    const float width = std::min(desiredWidth, std::max(viewport.x - 32.0f, 0.0f));
    const float preferredMargin = std::clamp(viewport.x * 0.055f, 16.0f, 96.0f);
    return {
        { std::min(preferredMargin, std::max(viewport.x - width, 0.0f)), 0.0f },
        { width, viewport.y },
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
    levelSelectOnly_ = false;
    setPage(Page::Main);
}

void TitleScreen::setSaveSlots(std::vector<SaveSlotInfo> slots, int activeSlot)
{
    saveSlots_ = std::move(slots);
    activeSlot_ = std::clamp(
        activeSlot, 0, std::max(static_cast<int>(saveSlots_.size()) - 1, 0));
}

void TitleScreen::openLevelSelect(std::vector<TitleLevelInfo> levels)
{
    levels_ = std::move(levels);
    open_ = true;
    levelSelectOnly_ = true;
    setPage(Page::LevelSelect);
}

void TitleScreen::close()
{
    open_ = false;
    levelSelectOnly_ = false;
    slotPickForNewGame_ = false;
    setPage(Page::Main);
}

void TitleScreen::back()
{
    if (!open_ || page_ == Page::Main) {
        return;
    }
    if (page_ == Page::SlotDeleteConfirmation) {
        setPage(Page::SaveSlots);
        return;
    }
    if (levelSelectOnly_ && page_ == Page::LevelSelect) {
        close();
        return;
    }
    slotPickForNewGame_ = false;
    setPage(Page::Main);
}

void TitleScreen::setPage(Page page)
{
    page_ = page;
    selectedRow_ = 0;
    selectedScreen_ = 0;
    deleteColumnFocused_ = false;
    if (page != Page::SlotDeleteConfirmation) {
        pendingDeleteSlot_ = -1;
    }
    if (page != Page::SaveSlots && page != Page::SlotDeleteConfirmation) {
        slotPickForNewGame_ = false;
    }
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
        deleteColumnFocused_ = false;
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

bool TitleScreen::activeSlotHasSave() const
{
    return activeSlot_ >= 0 &&
        activeSlot_ < static_cast<int>(saveSlots_.size()) &&
        !saveSlots_[static_cast<std::size_t>(activeSlot_)].empty;
}

bool TitleScreen::anySaveExists() const
{
    return std::any_of(saveSlots_.begin(), saveSlots_.end(),
        [](const SaveSlotInfo& slot) { return !slot.empty; });
}

TitleScreenResult TitleScreen::draw(
    UiContext& ui,
    Vec2 viewport,
    const TitleScreenInput& input)
{
    if (!open_) {
        return {};
    }

    const UiRect fullscreen { { 0.0f, 0.0f }, viewport };
    ui.image(fullscreen, aspectFillUv(viewport, titleBackgroundAspect));
    ui.rect(fullscreen, { 0.015f, 0.020f, 0.021f, 0.12f });
    const UiRect panel = page_ == Page::Main
        ? leftColumn(viewport, 520.0f)
        : centeredColumn(viewport, page_ == Page::LevelSelect ? 640.0f : 520.0f);

    switch (page_) {
    case Page::Main: return drawMain(ui, panel, input);
    case Page::LevelSelect: return drawLevelSelect(ui, panel, input);
    case Page::SaveSlots: return drawSaveSlots(ui, panel, input);
    case Page::SlotDeleteConfirmation:
        return drawSlotDeleteConfirmation(ui, panel, input);
    }
    return {};
}

TitleScreenResult TitleScreen::drawMain(
    UiContext& ui,
    UiRect panel,
    const TitleScreenInput& input)
{
    const bool showSaveSlots = anySaveExists();
    const int saveSlotsRowIndex = rowIndex(MainRow::SaveSlots);
    const int optionsRowIndex = saveSlotsRowIndex + (showSaveSlots ? 1 : 0);
    const int quitRowIndex = optionsRowIndex + 1;
    navigateRows(input, quitRowIndex + 1);

    TitlePageLayout layout;
    UiLayoutTree& tree = layout.tree;
    tree.spacer(tree.root(), 36.0f);
    const UiLayoutNode primaryRow = tree.item(tree.root(), 58.0f);
    tree.spacer(tree.root(), 14.0f);
    UiLayoutNode saveSlotsRow {};
    if (showSaveSlots) {
        saveSlotsRow = tree.item(tree.root(), 58.0f);
        tree.spacer(tree.root(), 14.0f);
    }
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
    const bool hasSave = activeSlotHasSave();
    if (uiControls::button(
            ui, "title.primary", tree.rect(primaryRow),
            hasSave ? "Continue" : "New Game", {
            .tone = ButtonTone::Accent,
            .focused = selectedRow_ == rowIndex(MainRow::Primary),
            .activate = input.confirm && selectedRow_ == rowIndex(MainRow::Primary),
        })) {
        if (hasSave) {
            result.continueRequested = true;
        } else if (anySaveExists()) {
            result.newGameRequested = true;
        } else {
            // First run (or every save deleted): pick the slot to begin on.
            slotPickForNewGame_ = true;
            setPage(Page::SaveSlots);
            slotPickForNewGame_ = true;
        }
    }
    if (showSaveSlots &&
        uiControls::button(
            ui, "title.save-slots", tree.rect(saveSlotsRow),
            "Save Slot " + std::to_string(activeSlot_ + 1), {
            .focused = selectedRow_ == saveSlotsRowIndex,
            .activate = input.confirm && selectedRow_ == saveSlotsRowIndex,
        })) {
        setPage(Page::SaveSlots);
    }
    if (uiControls::button(
            ui, "title.options", tree.rect(optionsRow), "Options", {
            .focused = selectedRow_ == optionsRowIndex,
            .activate = input.confirm && selectedRow_ == optionsRowIndex,
        })) {
        result.optionsRequested = true;
    }
    ui.divider(tree.rect(quitDivider));
    if (uiControls::button(
            ui, "title.quit", tree.rect(quitRow), "Quit", {
            .tone = ButtonTone::Danger,
            .focused = selectedRow_ == quitRowIndex,
            .activate = input.confirm && selectedRow_ == quitRowIndex,
        })) {
        result.quitRequested = true;
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
            if (input.left) {
                selectedScreen_ = std::max(selectedScreen_ - 1, 0);
            }
            if (input.right) {
                selectedScreen_ = std::min(
                    selectedScreen_ + 1, std::max(screens - 1, 0));
            }
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
        if (levelSelectOnly_) {
            close();
        } else {
            setPage(Page::Main);
        }
    }
    return result;
}

TitleScreenResult TitleScreen::drawSaveSlots(
    UiContext& ui,
    UiRect panel,
    const TitleScreenInput& input)
{
    const int rowCount = static_cast<int>(saveSlots_.size()) + 1; // + Back
    navigateRows(input, rowCount);

    TitlePageLayout layout;
    UiLayoutTree& tree = layout.tree;
    std::vector<UiLayoutNode> slotRows;
    slotRows.reserve(saveSlots_.size());
    for (std::size_t i = 0; i < saveSlots_.size(); ++i) {
        slotRows.push_back(tree.item(tree.root(), 58.0f));
        tree.spacer(tree.root(), 12.0f);
    }
    tree.spacer(tree.root(), 6.0f);
    const UiLayoutNode hint = tree.item(tree.root(), 24.0f);
    tree.flexibleSpacer(tree.root());
    const UiLayoutNode backRow = tree.item(tree.root(), 52.0f);
    tree.arrange(panel);

    ui.centeredText(tree.rect(layout.title),
        slotPickForNewGame_ ? "CHOOSE A SLOT" : "SAVE SLOTS",
        { 0.94f, 0.96f, 0.93f, 1.0f }, 40.0f);
    ui.centeredText(tree.rect(layout.subtitle),
        slotPickForNewGame_
            ? "Where should the new game live?"
            : "Progress is per slot; settings are shared",
        { 0.62f, 0.67f, 0.65f, 1.0f }, 16.0f);
    ui.divider(tree.rect(layout.divider));

    TitleScreenResult result;
    for (std::size_t i = 0; i < saveSlots_.size(); ++i) {
        const SaveSlotInfo& slot = saveSlots_[i];
        const bool active = static_cast<int>(i) == activeSlot_;
        const bool rowFocused = selectedRow_ == static_cast<int>(i);
        const UiRect row = tree.rect(slotRows[i]);
        const bool deletable = !slot.empty && !slotPickForNewGame_;

        if (rowFocused && deletable) {
            if (input.right) {
                deleteColumnFocused_ = true;
            }
            if (input.left) {
                deleteColumnFocused_ = false;
            }
        }
        const bool slotFocused = rowFocused && !(deletable && deleteColumnFocused_);
        const bool deleteFocused = rowFocused && deletable && deleteColumnFocused_;

        constexpr float deleteWidth = 104.0f;
        constexpr float deleteGap = 12.0f;
        UiRect slotRect = row;
        if (deletable) {
            slotRect.size.x -= deleteWidth + deleteGap;
        }

        if (uiControls::button(
                ui, "title.slot-" + std::to_string(i), slotRect,
                "Slot " + std::to_string(i + 1), {
                .tone = active ? ButtonTone::Accent : ButtonTone::Normal,
                .focused = slotFocused,
                .activate = input.confirm && slotFocused,
            })) {
            if (slotPickForNewGame_) {
                result.newGameSlotSelected = static_cast<int>(i);
            } else if (active) {
                setPage(Page::Main);
            } else {
                result.slotSelected = static_cast<int>(i);
            }
        }
        if (deletable &&
            uiControls::button(
                ui, "title.slot-delete-" + std::to_string(i),
                {
                    { row.position.x + row.size.x - deleteWidth, row.position.y },
                    { deleteWidth, row.size.y },
                },
                "Delete", {
                .tone = ButtonTone::Danger,
                .focused = deleteFocused,
                .activate = input.confirm && deleteFocused,
            })) {
            pendingDeleteSlot_ = static_cast<int>(i);
            setPage(Page::SlotDeleteConfirmation);
            pendingDeleteSlot_ = static_cast<int>(i);
        }

        std::string status;
        if (slot.empty) {
            status = "Empty";
        } else if (slot.completed) {
            status = "Completed!";
        } else {
            status = "Level " + std::to_string(slot.currentLevel + 1);
            if (slot.completedLevels > 0) {
                status += " - " + std::to_string(slot.completedLevels) + " done";
            }
        }
        if (active && !slotPickForNewGame_) {
            status += "  (active)";
        }
        drawTrailingText(ui, slotRect, status,
            rowFocused
                ? Vec4 { 0.68f, 0.88f, 0.82f, 1.0f }
                : Vec4 { 0.62f, 0.67f, 0.65f, 1.0f },
            18.0f);
    }

    ui.centeredText(tree.rect(hint),
        slotPickForNewGame_
            ? ""
            : "Right focuses a slot's Delete button",
        { 0.58f, 0.63f, 0.62f, 1.0f }, 15.0f);

    const bool backFocused = selectedRow_ == rowCount - 1;
    if (uiControls::button(
            ui, "title.save-slots.back", tree.rect(backRow), "Back", {
            .focused = backFocused,
            .activate = input.confirm && backFocused,
        })) {
        slotPickForNewGame_ = false;
        setPage(Page::Main);
    }
    return result;
}

TitleScreenResult TitleScreen::drawSlotDeleteConfirmation(
    UiContext& ui,
    UiRect panel,
    const TitleScreenInput& input)
{
    navigateRows(input, rowIndex(DeleteRow::Count));

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

    const std::string heading =
        "DELETE SLOT " + std::to_string(pendingDeleteSlot_ + 1) + "?";
    ui.centeredText(tree.rect(layout.title), heading,
        { 0.94f, 0.96f, 0.93f, 1.0f }, 44.0f);
    ui.divider(tree.rect(layout.divider));
    ui.centeredText(tree.rect(message),
        "Erase this save slot?",
        { 0.83f, 0.86f, 0.83f, 1.0f }, 22.0f);
    ui.centeredText(tree.rect(detail),
        "Its progress and best records will be gone for good.",
        { 0.86f, 0.62f, 0.52f, 1.0f }, 18.0f);

    TitleScreenResult result;
    const bool cancelFocused = selectedRow_ == rowIndex(DeleteRow::Cancel);
    if (uiControls::button(
            ui, "title.slot-delete.cancel", tree.rect(cancelRow), "Cancel", {
            .focused = cancelFocused,
            .activate = input.confirm && cancelFocused,
        })) {
        setPage(Page::SaveSlots);
    }
    const bool confirmFocused = selectedRow_ == rowIndex(DeleteRow::Confirm);
    if (uiControls::button(
            ui, "title.slot-delete.confirm", tree.rect(confirmRow),
            "Delete Save", {
            .tone = ButtonTone::Danger,
            .focused = confirmFocused,
            .activate = input.confirm && confirmFocused,
        })) {
        result.slotDeleteRequested = pendingDeleteSlot_;
        setPage(Page::SaveSlots);
    }
    return result;
}

} // namespace sokoban
