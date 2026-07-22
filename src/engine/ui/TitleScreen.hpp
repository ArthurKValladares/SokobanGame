#pragma once

#include "engine/Math.hpp"
#include "engine/ui/MenuKit.hpp"

#include <optional>
#include <variant>
#include <vector>

namespace sokoban {

class UiContext;
struct UiRect;

struct TitleLevelInfo {
    int screenCount = 0;
    bool unlocked = false;
    bool completed = false;
    // Screens the player has entered (max reached index + 1). Completed
    // levels expose every screen regardless of this value.
    int reachedScreens = 0;
    std::optional<int> bestMoves;
    std::optional<double> bestTimeSeconds;
};

struct SaveSlotInfo {
    // "Empty" means no progress (fresh or deleted); the file may still exist.
    bool empty = true;
    bool completed = false; // every level has a completion record
    int currentLevel = 0; // 0-based
    int completedLevels = 0;
};

struct TitleScreenInput {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool confirm = false;
};

// A frame of title-screen interaction produces at most one action; the
// variant makes combinations unrepresentable. Slots are 0-based.
namespace title {

struct Continue {};
// Start a fresh game on the already-active slot.
struct NewGame {};
// Start a fresh game on this slot (no-saves first-run flow).
struct NewGameOnSlot {
    int slot = 0;
};
// Switch to this existing slot.
struct SwitchSlot {
    int slot = 0;
};
// Delete this slot's save (already player-confirmed).
struct DeleteSlot {
    int slot = 0;
};
struct StartLevel {
    int level = 0;
    int screen = 0;
};
struct OpenOptions {};
struct Quit {};

} // namespace title

using TitleAction = std::variant<
    title::Continue,
    title::NewGame,
    title::NewGameOnSlot,
    title::SwitchSlot,
    title::DeleteSlot,
    title::StartLevel,
    title::OpenOptions,
    title::Quit>;

// Headless fullscreen title-screen state. The world is not loaded while the
// main menu is up; only Continue/New Game results make the caller load it.
// The first main-menu row is Continue when the active slot has progress and
// New Game otherwise; with no saves anywhere, New Game first asks which slot
// to begin on. The Save Slots page switches slots and deletes saves (with a
// confirmation page). Level select is reachable only through
// openLevelSelect (in-game entry points).
class TitleScreen {
public:
    enum class Page {
        Main,
        LevelSelect,
        SaveSlots,
        SlotDeleteConfirmation,
    };

    void open(std::vector<TitleLevelInfo> levels);
    // Slot summaries shown on the Save Slots page; activeSlot is 0-based.
    void setSaveSlots(std::vector<SaveSlotInfo> slots, int activeSlot);
    // Opens straight onto the level-select page with fresh level data; in
    // this standalone mode Back/back() close the screen instead of falling
    // through to the title's main menu.
    void openLevelSelect(std::vector<TitleLevelInfo> levels);
    void close();
    // Steps sub-pages back toward Main (or closes a standalone level
    // select); no-op on Main.
    void back();
    [[nodiscard]] bool isOpen() const { return open_; }
    [[nodiscard]] Page page() const { return page_; }
    [[nodiscard]] int selectedRow() const { return selectedRow_; }
    [[nodiscard]] int selectedScreen() const { return selectedScreen_; }

    [[nodiscard]] std::optional<TitleAction> draw(
        UiContext& ui,
        Vec2 viewport,
        const TitleScreenInput& input);

private:
    void setPage(Page page);
    // Wrap navigation that also resets row-local state on selection change.
    void navigate(const menuKit::RowList& rows, const TitleScreenInput& input);
    [[nodiscard]] int selectableScreens(const TitleLevelInfo& level) const;
    [[nodiscard]] bool activeSlotHasSave() const;
    [[nodiscard]] bool anySaveExists() const;
    [[nodiscard]] std::optional<TitleAction> drawMain(
        UiContext& ui,
        UiRect panel,
        const TitleScreenInput& input);
    [[nodiscard]] std::optional<TitleAction> drawLevelSelect(
        UiContext& ui,
        UiRect panel,
        const TitleScreenInput& input);
    [[nodiscard]] std::optional<TitleAction> drawSaveSlots(
        UiContext& ui,
        UiRect panel,
        const TitleScreenInput& input);
    [[nodiscard]] std::optional<TitleAction> drawSlotDeleteConfirmation(
        UiContext& ui,
        UiRect panel,
        const TitleScreenInput& input);

    bool open_ = false;
    bool levelSelectOnly_ = false;
    // The Save Slots page doubles as the "choose a slot to begin" prompt
    // when New Game is confirmed with no saves anywhere.
    bool slotPickForNewGame_ = false;
    // Focus sits on the row's inline Delete button instead of the slot.
    bool deleteColumnFocused_ = false;
    int pendingDeleteSlot_ = -1;
    Page page_ = Page::Main;
    int selectedRow_ = 0;
    int selectedScreen_ = 0;
    std::vector<TitleLevelInfo> levels_;
    std::vector<SaveSlotInfo> saveSlots_;
    int activeSlot_ = 0;
};

} // namespace sokoban
