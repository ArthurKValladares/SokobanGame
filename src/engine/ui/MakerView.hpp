#pragma once

#include "engine/CameraAngles.hpp"
#include "engine/MakerSession.hpp"
#include "engine/OverworldMap.hpp"
#include "engine/TileTypes.hpp"
#include "engine/ui/Ui.hpp"
#include "engine/ui/UiScroll.hpp"
#include "engine/ui/UiSearchPicker.hpp"
#include "engine/ui/UiTextEdit.hpp"
#include "engine/ui/UiTextField.hpp"

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <cstddef>
#include <cstdint>

namespace sokoban {

struct InputBindings;

struct MakerLayout {
    UiRect toolbar;
    UiRect palette;
    UiRect board;
    UiRect footer;
    float scale = 1.0f;
};

struct MakerLinkInspector {
    enum class Kind : uint8_t { Device, Portal, Object, Selector };

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
    std::optional<Vec3> groupColor;
    std::span<const Vec3> groupColors;
};

struct MakerLinkLine {
    Vec2 from;
    Vec2 to;
    Vec4 color;
    std::string_view label;
    bool directional = false;
};

struct MakerLinkMarker {
    Vec2 position;
    Vec4 color;
    bool selected = false;
};

struct MakerAreaViewState {
    enum class Mode : uint8_t { Rectangle, FloodFill, Paste };

    struct Outline {
        std::array<Vec2, 4> corners;
        Vec4 color;
    };

    std::optional<Mode> mode;
    bool hasSelection = false;
    bool hasClipboard = false;
    std::optional<Outline> outline;
};

enum class MakerPanel : uint8_t { Palette, Layers, Properties, Problems };

struct MakerLayerPanel {
    std::optional<uint32_t> waterLayer;
    bool canAdd = false;
    bool canDelete = false;
    bool canSetWater = false;
};

struct MakerPropertyPanel {
    std::optional<GridPosition3> cell;
    std::optional<TileType> tile;
    std::optional<bool> gateStartOpen;
    std::optional<bool> lockPlateStartEnabled;
    std::optional<uint8_t> minecartDirection;
    std::span<const uint8_t> minecartDirections;
    std::string_view minecartRoute;
    std::optional<bool> elevatorStopOnLayer;
    std::string_view elevatorStops;
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
    bool layerLocked = false;
    bool overworld = false;
    bool showNeighbors = true;
    GridPosition overworldScreen {};
    bool placingSelector = false;
    bool hasMultipleOverworldScreens = false;
    MakerAreaViewState area;
    MakerPanel panel = MakerPanel::Palette;
    MakerLayerPanel layerPanel;
    MakerPropertyPanel propertyPanel;
    bool topDown = false;
    bool canFrameSelection = false;
    bool showViewportControls = true;
    std::span<const MakerSession::Problem> problems;
    std::optional<Vec2> problemMarker;
    bool boardCursorFocused = false;
    std::optional<MakerAreaViewState::Outline> boardCursorOutline;
    std::string_view boardCursorHint;
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
    EditLinkGroupColor,
    Help,
    Camera,
    ToggleOverworldMode,
    ToggleOverworldNeighbors,
    PreviousOverworldScreen,
    NextOverworldScreen,
    PaintSelector,
    ConnectSelectors,
    SelectArea,
    FloodFill,
    FillSelection,
    EraseSelection,
    CopySelection,
    CutSelection,
    PasteArea,
    ShowPalette,
    ShowLayers,
    ShowProperties,
    SelectLayer,
    AddLayerAbove,
    AddLayerBelow,
    DeleteLayer,
    ToggleLayerLock,
    ToggleWaterLayer,
    ToggleGateStartOpen,
    ToggleLockPlateStartEnabled,
    SetMinecartDirection,
    ToggleTopDown,
    FrameSelection,
    OverworldMap,
    ShowProblems,
    JumpToProblem,
    ToggleBoardCursor,
    ToggleToolbarOverflow,
};

struct MakerViewAction {
    std::optional<TileType> tile;
    MakerCommand command = MakerCommand::None;
    std::optional<std::size_t> linkGroup;
    std::optional<uint32_t> layer;
    std::optional<uint8_t> minecartDirection;
    std::optional<std::size_t> problem;
};

enum class MakerDraftDialogMode : uint8_t {
    Save, Load, Overwrite, Discard, NewDraft, DiscardNew, ResizeBoard, ConfirmCrop,
    NameLinkGroup, RenameLinkGroup, LecternText, SelectorTarget, ConfirmDeleteLayer,
    RenameDraft, RecoverAutosave, DiscardAutosave,
};

struct MakerDraftDialogState {
    MakerDraftDialogMode mode = MakerDraftDialogMode::Save;
    std::span<const std::string> names;
    std::string_view error;
    std::string_view pendingName;
    std::size_t removedTileCount = 0;
    std::optional<UiRect> resizePreviewPanel;
    bool legacySelectorTarget = false;
    uint32_t layer = 0;
    std::size_t removedSelectorCount = 0;
    std::size_t removedDecorationCount = 0;
    std::size_t removedElevatorStopCount = 0;
    bool removesWaterLayer = false;
    bool overworld = false;
    bool canRecoverAutosave = true;
    bool otherAutosaveAvailable = false;
};

struct MakerDraftDialogAction {
    enum class Command : uint8_t { None, Cancel, Submit, Rename, Discard };
    Command command = Command::None;
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct MakerCameraDialogState {
    std::optional<CameraAngles> angles;
    CameraAngles defaults;
    bool detached = false;
    bool canDetach = false;
    bool mouseCaptured = false;
};

struct MakerCameraDialogAction {
    enum class Command : uint8_t { None, Cancel, Apply, ToggleDetached, ResetPose };
    Command command = Command::None;
    // The pending angles include slider edits and an explicit reset to default.
    std::optional<CameraAngles> angles;
};

struct MakerOverworldMapState {
    std::span<const OverworldScreenSpec> screens;
    OverworldScreenId activeScreen = 0;
    bool canUndo = false;
    bool canRedo = false;
    std::string_view status;
};

struct MakerOverworldMapAction {
    enum class Command : uint8_t { None, Close, Jump, Move, Delete, Undo, Redo };
    Command command = Command::None;
    OverworldScreenId screen = 0;
    OverworldSlot slot;
};

// Draws the Maker controls and leaves the board viewport open for the scene.
// The same layout rectangle is shared by rendering and board hit testing.
class MakerView {
public:
    [[nodiscard]] static MakerLayout layout(Vec2 viewport, float scale = 1.0f);
    [[nodiscard]] static MakerLayout playtestLayout(Vec2 viewport, float scale = 1.0f);
    [[nodiscard]] bool focusTileSearch();
    // Returns true when the platform should refresh its text input target.
    [[nodiscard]] bool handleTileSearchPress(UiContext& ui, Vec2 position, bool extendSelection);
    void blurTileSearch();
    [[nodiscard]] bool tileSearchFocused() const { return searchFocused_; }
    [[nodiscard]] UiTextEdit* activeTextEdit() { return searchFocused_ ? &searchEdit_ : nullptr; }
    [[nodiscard]] UiRect textCaret() const { return textCaret_; }

    [[nodiscard]] MakerViewAction draw(UiContext& ui, const MakerLayout& layout,
        const MakerViewState& state, const MakerViewInput& input = {});
    [[nodiscard]] bool toolbarOverflowOpen() const { return toolbarOverflowOpen_; }
    bool closeToolbarOverflow();
    [[nodiscard]] MakerCommand drawPlaytest(UiContext& ui, const MakerLayout& layout, bool solved,
        std::string_view returnLabel = "Return to Maker", std::string_view hint = {});
    void beginHelp();
    [[nodiscard]] bool drawHelp(UiContext& ui, Vec2 viewport, float scale,
        const InputBindings& bindings, const MakerViewInput& input = {});
    void beginCameraDialog();
    [[nodiscard]] MakerCameraDialogAction drawCameraDialog(UiContext& ui, const MakerLayout& layout,
        const MakerCameraDialogState& state, const MakerViewInput& input = {});
    void beginOverworldMap();
    // Back cancels a pending move or deletion before closing the overview.
    [[nodiscard]] bool cancelOverworldMapAction();
    [[nodiscard]] MakerOverworldMapAction drawOverworldMap(UiContext& ui, Vec2 viewport, float scale,
        const MakerOverworldMapState& state, const MakerViewInput& input = {});

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
    [[nodiscard]] bool focusDraftSearch();
    [[nodiscard]] bool handleDraftNamePress(UiContext& ui, Vec2 position, bool extendSelection);
    [[nodiscard]] bool draftNameFocused() const
    {
        return draftUsesPicker_ ? draftPicker_.searchFocused() : draftNameFocused_;
    }
    [[nodiscard]] UiTextEdit* activeDraftNameEdit()
    {
        return draftUsesPicker_ ? draftPicker_.activeTextEdit() : draftNameFocused_ ? &draftNameEdit_ : nullptr;
    }
    [[nodiscard]] UiRect draftNameCaret() const { return draftUsesPicker_ ? draftPicker_.caret() : draftNameCaret_; }
    void blurDraftName();

private:
    struct MapMove {
        OverworldScreenId screen;
        OverworldSlot origin;
    };

    void drawTileSearch(UiContext& ui, const MakerLayout& layout, const MakerViewInput& input);
    [[nodiscard]] MakerViewAction drawEditorControls(UiContext& ui, const MakerLayout& layout,
        const MakerViewState& state, const MakerViewInput& input);
    [[nodiscard]] MakerViewAction drawToolbarOverflow(UiContext& ui, const MakerLayout& layout,
        const MakerViewState& state, const MakerViewInput& input);

    UiScroll paletteScroll_;
    using PaletteSections = std::array<bool, 7>;
    PaletteSections collapsedPaletteSections_ {};
    PaletteSections collapsedSearchSections_ {};
    UiScroll linkScroll_;
    UiScroll helpScroll_;
    UiScroll layerScroll_;
    UiScroll propertyScroll_;
    UiScroll problemsScroll_;
    UiScroll toolbarOverflowScroll_;
    UiControlId toolbarOverflowFocus_ = 0;
    bool toolbarOverflowOpen_ = false;
    UiControlId focus_ = 0;
    UiControlId cameraDialogFocus_ = 0;
    UiControlId overworldMapFocus_ = 0;
    std::optional<MapMove> overworldMapMove_;
    std::optional<OverworldScreenId> overworldMapDelete_;
    Vec2 palettePointer_;
    bool paletteKeyboardFocus_ = false;
    UiTextField searchField_;
    UiTextEdit searchEdit_ { 128 };
    std::string renderedQuery_;
    bool renderedOverworld_ = false;
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
    bool draftNameAllowsEmpty_ = false;
    bool draftUsesPicker_ = false;
    UiControlId draftDialogFocus_ = 0;
    UiSearchPicker draftPicker_;
    uint32_t draftWidth_ = 0;
    uint32_t draftHeight_ = 0;
    uint32_t resizeOriginalWidth_ = 0;
    uint32_t resizeOriginalHeight_ = 0;
};

} // namespace sokoban
