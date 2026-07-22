#include "engine/ui/TitleScreen.hpp"

#include "engine/ui/MenuKit.hpp"
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

UiRect leftColumn(Vec2 viewport, float desiredWidth)
{
    const float width = std::min(desiredWidth, std::max(viewport.x - 32.0f, 0.0f));
    const float preferredMargin = std::clamp(viewport.x * 0.055f, 16.0f, 96.0f);
    return {
        { std::min(preferredMargin, std::max(viewport.x - width, 0.0f)), 0.0f },
        { width, viewport.y },
    };
}

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

void TitleScreen::navigate(const menuKit::RowList& rows, const TitleScreenInput& input)
{
    if (rows.navigate(selectedRow_, input.up, input.down)) {
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
        saveSlots_[static_cast<std::size_t>(activeSlot_)].state ==
            SaveSlotState::Ready;
}

bool TitleScreen::anySaveExists() const
{
    return std::any_of(saveSlots_.begin(), saveSlots_.end(),
        [](const SaveSlotInfo& slot) {
            return saveSlotHasStoredData(slot.state);
        });
}

std::optional<TitleAction> TitleScreen::draw(
    UiContext& ui,
    Vec2 viewport,
    const TitleScreenInput& input)
{
    if (!open_) {
        return std::nullopt;
    }

    const UiRect fullscreen { { 0.0f, 0.0f }, viewport };
    ui.image(fullscreen, aspectFillUv(viewport, titleBackgroundAspect));
    ui.rect(fullscreen, { 0.015f, 0.020f, 0.021f, 0.12f });
    const UiRect panel = page_ == Page::Main
        ? leftColumn(viewport, 520.0f)
        : menuKit::centeredColumn(
              viewport, page_ == Page::LevelSelect ? 640.0f : 520.0f);

    switch (page_) {
    case Page::Main: return drawMain(ui, panel, input);
    case Page::LevelSelect: return drawLevelSelect(ui, panel, input);
    case Page::SaveSlots: return drawSaveSlots(ui, panel, input);
    case Page::SlotDeleteConfirmation:
        return drawSlotDeleteConfirmation(ui, panel, input);
    }
    return std::nullopt;
}

std::optional<TitleAction> TitleScreen::drawMain(
    UiContext& ui,
    UiRect panel,
    const TitleScreenInput& input)
{
    const bool showSaveSlots = anySaveExists();
    menuKit::RowList rows;
    const int primaryRowIndex = rows.add();
    const int saveSlotsRowIndex = rows.addIf(showSaveSlots);
    const int optionsRowIndex = rows.add();
    const int quitRowIndex = rows.add();
    navigate(rows, input);

    menuKit::MenuPage page(26.0f, true);
    UiLayoutTree& tree = page.tree;
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

    // The brand block keeps its custom subtitle size.
    ui.centeredText(tree.rect(page.title), "SOKOBAN 3D",
        { 0.94f, 0.96f, 0.93f, 1.0f }, 44.0f);
    ui.centeredText(tree.rect(page.subtitle), "a tile-pushing puzzle",
        { 0.62f, 0.67f, 0.65f, 1.0f }, 18.0f);
    ui.divider(tree.rect(page.divider));

    std::optional<TitleAction> action;
    const bool hasSave = activeSlotHasSave();
    if (uiControls::button(
            ui, "title.primary", tree.rect(primaryRow),
            hasSave ? "Continue" : "New Game", {
            .tone = ButtonTone::Accent,
            .focused = selectedRow_ == primaryRowIndex,
            .activate = input.confirm && selectedRow_ == primaryRowIndex,
        })) {
        if (hasSave) {
            action = title::Continue {};
        } else if (anySaveExists()) {
            action = title::NewGame {};
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
        action = title::OpenOptions {};
    }
    ui.divider(tree.rect(quitDivider));
    if (uiControls::button(
            ui, "title.quit", tree.rect(quitRow), "Quit", {
            .tone = ButtonTone::Danger,
            .focused = selectedRow_ == quitRowIndex,
            .activate = input.confirm && selectedRow_ == quitRowIndex,
        })) {
        action = title::Quit {};
    }
    return action;
}

std::optional<TitleAction> TitleScreen::drawLevelSelect(
    UiContext& ui,
    UiRect panel,
    const TitleScreenInput& input)
{
    menuKit::RowList rows;
    for (std::size_t i = 0; i < levels_.size(); ++i) {
        (void)rows.add(); // level rows share their vector index
    }
    const int backRowIndex = rows.add();
    navigate(rows, input);

    menuKit::MenuPage page(26.0f, true);
    UiLayoutTree& tree = page.tree;
    std::vector<UiLayoutNode> levelRows;
    levelRows.reserve(levels_.size());
    for (std::size_t i = 0; i < levels_.size(); ++i) {
        levelRows.push_back(tree.item(tree.root(), 54.0f));
        tree.spacer(tree.root(), 10.0f);
    }
    tree.flexibleSpacer(tree.root());
    const UiLayoutNode backRow = tree.item(tree.root(), 52.0f);
    tree.arrange(panel);

    page.drawHeader(ui, "LEVEL SELECT", 40.0f,
        "Left/Right picks the starting screen");

    std::optional<TitleAction> action;
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
            menuKit::trailingText(ui, row, "LOCKED",
                { 0.45f, 0.48f, 0.47f, 0.7f }, 18.0f, 18.0f);
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
            action = title::StartLevel {
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
                status += " - " + menuKit::formatDuration(
                    *level.bestTimeSeconds,
                    menuKit::DurationStyle::MinutesSeconds);
            }
        } else if (!level.completed) {
            status = "In progress";
        }
        if (!status.empty()) {
            menuKit::trailingText(ui, row, status,
                focused
                    ? Vec4 { 0.68f, 0.88f, 0.82f, 1.0f }
                    : Vec4 { 0.62f, 0.67f, 0.65f, 1.0f },
                18.0f, 18.0f);
        }
    }

    const bool backFocused = selectedRow_ == backRowIndex;
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
    return action;
}

std::optional<TitleAction> TitleScreen::drawSaveSlots(
    UiContext& ui,
    UiRect panel,
    const TitleScreenInput& input)
{
    menuKit::RowList rows;
    for (std::size_t i = 0; i < saveSlots_.size(); ++i) {
        (void)rows.add(); // slot rows share their vector index
    }
    const int backRowIndex = rows.add();
    navigate(rows, input);

    menuKit::MenuPage page(26.0f, true);
    UiLayoutTree& tree = page.tree;
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

    page.drawHeader(ui,
        slotPickForNewGame_ ? "CHOOSE A SLOT" : "SAVE SLOTS",
        40.0f,
        slotPickForNewGame_
            ? "Where should the new game live?"
            : "Progress is per slot; settings are shared");

    std::optional<TitleAction> action;
    for (std::size_t i = 0; i < saveSlots_.size(); ++i) {
        const SaveSlotInfo& slot = saveSlots_[i];
        const bool active = static_cast<int>(i) == activeSlot_;
        const bool rowFocused = selectedRow_ == static_cast<int>(i);
        const UiRect row = tree.rect(slotRows[i]);
        const bool deletable = saveSlotHasStoredData(slot.state) &&
            slot.state != SaveSlotState::Unavailable &&
            !slotPickForNewGame_;
        const bool loadable = saveSlotCanLoad(slot.state);

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
                .enabled = loadable,
            })) {
            if (slotPickForNewGame_) {
                action = title::NewGameOnSlot { static_cast<int>(i) };
            } else if (active) {
                setPage(Page::Main);
            } else {
                action = title::SwitchSlot { static_cast<int>(i) };
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
        if (slot.state == SaveSlotState::Empty) {
            status = "Empty";
        } else if (slot.state == SaveSlotState::Recoverable) {
            status = "Backup available";
        } else if (slot.state == SaveSlotState::Corrupt) {
            status = "Corrupt";
        } else if (slot.state == SaveSlotState::Unavailable) {
            status = "Unavailable";
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
        const bool damaged = slot.state == SaveSlotState::Corrupt ||
            slot.state == SaveSlotState::Unavailable;
        menuKit::trailingText(ui, slotRect, status,
            damaged
                ? Vec4 { 0.95f, 0.48f, 0.40f, 1.0f }
                : (rowFocused
                        ? Vec4 { 0.68f, 0.88f, 0.82f, 1.0f }
                        : Vec4 { 0.62f, 0.67f, 0.65f, 1.0f }),
            18.0f, 18.0f);
    }

    ui.centeredText(tree.rect(hint),
        slotPickForNewGame_
            ? ""
            : "Right focuses a slot's Delete button",
        { 0.58f, 0.63f, 0.62f, 1.0f }, 15.0f);

    const bool backFocused = selectedRow_ == backRowIndex;
    if (uiControls::button(
            ui, "title.save-slots.back", tree.rect(backRow), "Back", {
            .focused = backFocused,
            .activate = input.confirm && backFocused,
        })) {
        slotPickForNewGame_ = false;
        setPage(Page::Main);
    }
    return action;
}

std::optional<TitleAction> TitleScreen::drawSlotDeleteConfirmation(
    UiContext& ui,
    UiRect panel,
    const TitleScreenInput& input)
{
    menuKit::RowList rows;
    const int cancelRowIndex = rows.add();
    const int confirmRowIndex = rows.add();
    navigate(rows, input);

    menuKit::MenuPage page(26.0f, true);
    UiLayoutTree& tree = page.tree;
    tree.spacer(tree.root(), 8.0f);
    const UiLayoutNode message = tree.item(tree.root(), 44.0f);
    const UiLayoutNode detail = tree.item(tree.root(), 30.0f);
    tree.spacer(tree.root(), 58.0f);
    const UiLayoutNode cancelRow = tree.item(tree.root(), 58.0f);
    tree.spacer(tree.root(), 18.0f);
    const UiLayoutNode confirmRow = tree.item(tree.root(), 58.0f);
    tree.flexibleSpacer(tree.root());
    tree.arrange(panel);

    page.drawHeader(ui,
        "DELETE SLOT " + std::to_string(pendingDeleteSlot_ + 1) + "?", 44.0f);
    ui.centeredText(tree.rect(message),
        "Erase this save slot?",
        { 0.83f, 0.86f, 0.83f, 1.0f }, 22.0f);
    ui.centeredText(tree.rect(detail),
        "Its progress and best records will be gone for good.",
        { 0.86f, 0.62f, 0.52f, 1.0f }, 18.0f);

    std::optional<TitleAction> action;
    const bool cancelFocused = selectedRow_ == cancelRowIndex;
    if (uiControls::button(
            ui, "title.slot-delete.cancel", tree.rect(cancelRow), "Cancel", {
            .focused = cancelFocused,
            .activate = input.confirm && cancelFocused,
        })) {
        setPage(Page::SaveSlots);
    }
    const bool confirmFocused = selectedRow_ == confirmRowIndex;
    if (uiControls::button(
            ui, "title.slot-delete.confirm", tree.rect(confirmRow),
            "Delete Save", {
            .tone = ButtonTone::Danger,
            .focused = confirmFocused,
            .activate = input.confirm && confirmFocused,
        })) {
        action = title::DeleteSlot { pendingDeleteSlot_ };
        setPage(Page::SaveSlots);
    }
    return action;
}

} // namespace sokoban
