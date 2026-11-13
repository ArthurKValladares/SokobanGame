#pragma once

#include "engine/TileTypes.hpp"
#include "engine/ui/Ui.hpp"
#include "engine/ui/UiScroll.hpp"
#include "engine/ui/UiTextEdit.hpp"
#include "engine/ui/UiTextField.hpp"

#include <functional>
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

struct MakerLinkInspector {
    enum class Kind : uint8_t { Device, Portal, Object };

    std::optional<Kind> kind;
    std::string_view title;
    std::string_view detail;
    std::span<const std::string> members;
    std::span<const std::string> groups;
    std::optional<std::size_t> activeGroup;
    bool canDisconnect = false;
    bool canClear = false;
    bool canRename = false;
    bool canCreate = false;
    bool canSelectOther = false;
    std::optional<bool> elevatorStopOnLayer;
};

struct MakerLinkLine {
    Vec2 from;
    Vec2 to;
    Vec4 color;
    std::string_view label;
};

struct MakerLinkMarker {
    Vec2 position;
    Vec4 color;
    bool selected = false;
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
    std::string_view placementHint;
    bool placementBlocked = false;
    std::optional<TileType> boardTile;
    std::optional<MakerLinkInspector> linkInspector;
    std::span<const MakerLinkLine> linkLines;
    std::span<const MakerLinkMarker> linkMarkers;
    std::optional<UiRect> linkViewport;
    bool linkLayerIsolated = false;
};

struct MakerViewInput {
    float wheelY = 0.0f;
    std::optional<Vec2> wheelPosition;
    UiNavigation navigation;
    bool extendTextSelection = false;
    bool interactive = true;
    std::function<RenderTexture(TileType)> tileThumbnail;
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
    Connect,
    Disconnect,
    ClearLinkSelection,
    NewLinkGroup,
    RenameLinkGroup,
    AssignLinkGroup,
    SelectOtherLinkEndpoint,
    ToggleLinkLayer,
    ToggleElevatorStop,
};

struct MakerViewAction {
    std::optional<TileType> tile;
    MakerCommand command = MakerCommand::None;
    std::optional<std::size_t> linkGroup;
};

enum class MakerDraftDialogMode : uint8_t {
    Save, Load, Overwrite, Discard, NewDraft, DiscardNew, ResizeBoard, ConfirmCrop,
    NameLinkGroup, RenameLinkGroup,
};

struct MakerDraftDialogState {
    MakerDraftDialogMode mode = MakerDraftDialogMode::Save;
    std::span<const std::string> names;
    std::string_view error;
    std::string_view pendingName;
    std::size_t removedTileCount = 0;
    std::optional<UiRect> resizePreviewPanel;
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
    [[nodiscard]] GridPosition resizePreviewSize() const
    {
        return { static_cast<int>(draftWidth_), static_cast<int>(draftHeight_) };
    }
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
    [[nodiscard]] MakerViewAction drawEditorControls(UiContext& ui, const MakerLayout& layout,
        const MakerViewState& state, const MakerViewInput& input);

    UiScroll paletteScroll_;
    UiScroll linkScroll_;
    UiControlId focus_ = 0;
    Vec2 palettePointer_;
    bool paletteKeyboardFocus_ = false;
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
