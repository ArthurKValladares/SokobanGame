#pragma once

#include "engine/TileTypes.hpp"
#include "engine/ui/Ui.hpp"
#include "engine/ui/UiScroll.hpp"
#include "engine/ui/UiTextEdit.hpp"
#include "engine/ui/UiTextField.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <cstddef>
#include <cstdint>

namespace sokoban {

struct MakerLayout {
    UiRect toolbar;
    UiRect palette;
    UiRect board;
    UiRect footer;
    float scale = 1.0f;
};

struct MakerViewState {
    TileType brush = TileType::Ground;
    uint32_t quarterTurns = 0;
    uint32_t layer = 0;
    uint32_t layerCount = 1;
    bool erasing = false;
    bool canUndo = false;
    bool canRedo = false;
    std::optional<GridPosition3> hovered;
    std::string_view status;
    std::string_view draftName;
    bool unsavedChanges = false;
};

struct MakerViewInput {
    float wheelY = 0.0f;
    std::optional<Vec2> wheelPosition;
    UiNavigation navigation;
    bool extendTextSelection = false;
    bool interactive = true;
};

enum class MakerCommand : uint8_t {
    None,
    Back,
    Paint,
    Erase,
    Rotate,
    LayerDown,
    LayerUp,
    Undo,
    Redo,
    TestPlay,
    ReturnToMaker,
    Save,
    Load,
    NewDraft,
    ResizeBoard,
};

struct MakerViewAction {
    std::optional<TileType> tile;
    MakerCommand command = MakerCommand::None;
};

enum class MakerDraftDialogMode : uint8_t {
    Save, Load, Overwrite, Discard, NewDraft, DiscardNew, ResizeBoard, ConfirmCrop,
};

struct MakerDraftDialogState {
    MakerDraftDialogMode mode = MakerDraftDialogMode::Save;
    std::span<const std::string> names;
    std::string_view error;
    std::string_view pendingName;
    std::size_t removedTileCount = 0;
};

struct MakerDraftDialogAction {
    enum class Command : uint8_t { None, Cancel, Submit };
    Command command = Command::None;
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Draws the Maker controls and leaves the board viewport open for the scene.
// The same layout rectangle is shared by rendering and board hit testing.
class MakerView {
public:
    [[nodiscard]] static MakerLayout layout(Vec2 viewport, float scale = 1.0f);
    [[nodiscard]] bool focusTileSearch();
    // Returns true when the platform should refresh its text input target.
    [[nodiscard]] bool handleTileSearchPress(UiContext& ui, Vec2 position, bool extendSelection);
    void blurTileSearch();
    [[nodiscard]] bool tileSearchFocused() const { return searchFocused_; }
    [[nodiscard]] UiTextEdit* activeTextEdit() { return searchFocused_ ? &searchEdit_ : nullptr; }
    [[nodiscard]] UiRect textCaret() const { return textCaret_; }

    [[nodiscard]] MakerViewAction draw(UiContext& ui, const MakerLayout& layout,
        const MakerViewState& state, const MakerViewInput& input = {});
    [[nodiscard]] MakerCommand drawPlaytest(UiContext& ui, const MakerLayout& layout, bool solved);

    void beginDraftDialog(MakerDraftDialogMode mode, std::string_view initialName = {});
    void beginResizeBoardDialog(uint32_t width, uint32_t height);
    void resumeBoardSizeDialog();
    [[nodiscard]] MakerDraftDialogAction drawDraftDialog(UiContext& ui, Vec2 viewport, float scale,
        const MakerDraftDialogState& state, const MakerViewInput& input = {});
    [[nodiscard]] MakerDraftDialogAction submitDraftName() const;
    [[nodiscard]] bool handleDraftNamePress(UiContext& ui, Vec2 position, bool extendSelection);
    [[nodiscard]] bool draftNameFocused() const { return draftNameFocused_; }
    [[nodiscard]] UiTextEdit* activeDraftNameEdit() { return draftNameFocused_ ? &draftNameEdit_ : nullptr; }
    [[nodiscard]] UiRect draftNameCaret() const { return draftNameCaret_; }
    void blurDraftName();

private:
    void drawTileSearch(UiContext& ui, const MakerLayout& layout, const MakerViewInput& input);

    UiScroll paletteScroll_;
    UiControlId focus_ = 0;
    UiTextField searchField_;
    UiTextEdit searchEdit_ { 128 };
    std::string renderedQuery_;
    UiRect searchBounds_;
    UiRect searchHitBounds_;
    UiRect textCaret_;
    float searchTextSize_ = 0.0f;
    bool searchFocused_ = false;
    bool searchPressHandled_ = false;
    UiTextField draftNameField_;
    UiTextEdit draftNameEdit_ { 256 };
    UiRect draftNameBounds_;
    UiRect draftNameCaret_;
    float draftNameTextSize_ = 0.0f;
    bool draftNameFocused_ = false;
    bool draftNamePressHandled_ = false;
    UiControlId draftDialogFocus_ = 0;
    UiScroll draftListScroll_;
    std::size_t draftListSelection_ = 0;
    uint32_t draftWidth_ = 0;
    uint32_t draftHeight_ = 0;
    uint32_t resizeOriginalWidth_ = 0;
    uint32_t resizeOriginalHeight_ = 0;
};

} // namespace sokoban
