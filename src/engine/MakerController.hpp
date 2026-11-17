#pragma once

#include "engine/AnimationCatalog.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/CameraAngles.hpp"
#include "engine/DetachedCamera.hpp"
#include "engine/FrameArena.hpp"
#include "engine/GameplayPresentation.hpp"
#include "engine/GameplaySession.hpp"
#include "engine/GroundGeometry.hpp"
#include "engine/Input.hpp"
#include "engine/InputRouter.hpp"
#include "engine/Level.hpp"
#include "engine/LevelLocation.hpp"
#include "engine/MakerAutosaveWriter.hpp"
#include "engine/MakerDraftStore.hpp"
#include "engine/MakerLevelProgress.hpp"
#include "engine/MakerRecoveryStore.hpp"
#include "engine/MakerSession.hpp"
#include "engine/MakerViewportCamera.hpp"
#include "engine/Math.hpp"
#include "engine/OverworldMap.hpp"
#include "engine/ParticleSystem.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/ScreenSelectorState.hpp"
#include "engine/TurretParticleEffect.hpp"
#include "engine/render/RenderTypes.hpp"
#include "engine/ui/LecternDialog.hpp"
#include "engine/ui/MakerView.hpp"
#include "engine/ui/SdlTextInput.hpp"
#include "engine/ui/Ui.hpp"
#include "engine/ui/UiColorPicker.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct SDL_Window;
union SDL_Event;

namespace sokoban {

// The camera of the scene the player currently sees.
struct MakerSceneCamera {
    Vec3 position {};
    Vec3 forward {};
    Vec3 right {};
    float verticalFovDegrees = 60.0f;
    bool valid = false;
};

// Everything the Maker needs from the application around it.
//
// The Maker edits a board that somebody else renders, in a window somebody
// else owns, beside a shell and a text-input service it shares with the
// options menu. This interface is that boundary. Application implements it
// with the renderer and the window; a test implements it with a few lines and
// can then drive whole Maker interactions without a window or a GPU.
class MakerHost {
public:
    // Scene queries, answered against the frame that is on screen. They
    // return nothing until a frame has been prepared.
    [[nodiscard]] virtual std::optional<MakerSceneCamera> sceneCamera() = 0;
    [[nodiscard]] virtual std::optional<GridPosition3> pickCell(
        Vec2 position,
        UiRect scene) = 0;
    // Only portal apertures, including those on visible neighboring screens.
    [[nodiscard]] virtual std::optional<GridPosition3> pickPortalCell(
        Vec2 position,
        UiRect scene) = 0;
    [[nodiscard]] virtual std::optional<GridPosition3> pickCellOnPlane(
        Vec2 position,
        UiRect scene,
        int layer) = 0;
    [[nodiscard]] virtual std::optional<Vec2> projectToScene(
        Vec3 worldPoint,
        UiRect scene) = 0;
    [[nodiscard]] virtual std::optional<UiRect> primaryPlayerBounds() = 0;
    [[nodiscard]] virtual bool boardAssetsLoading() = 0;
    [[nodiscard]] virtual RenderTexture tileThumbnail(TileType tile) = 0;

    // The window. A host without one returns null and zero sizes.
    [[nodiscard]] virtual SDL_Window* window() = 0;
    [[nodiscard]] virtual Vec2 windowSize() = 0;
    [[nodiscard]] virtual Vec2 windowPixelSize() = 0;
    [[nodiscard]] virtual float displayScale() = 0;

    // The shell and the text-input service shared with the options menu.
    [[nodiscard]] virtual bool optionsOpen() = 0;
    virtual void requestBack() = 0;
    [[nodiscard]] virtual bool textInputFocused() = 0;
    virtual void blurTextInput() = 0;
    // Re-reads which text field is focused after the Maker moved the focus.
    virtual void refreshTextInput() = 0;

    // Test Play borrows the game's simulation settings, assets and sound.
    virtual void configurePlaytest(
        GameplaySession& session,
        GameplayPresentation& presentation) = 0;
    virtual void ensureLevelAssets(const Level& level) = 0;
    virtual void setAtmosphere(const Level& level) = 0;
    virtual void restoreAtmosphere() = 0;
    virtual void drawLectern(
        LecternDialog& dialog,
        const Level& level,
        GridPosition3 cell,
        Vec2 viewport) = 0;

protected:
    ~MakerHost() = default;
};

// What the Maker's editing frame needs from the game's own presentation.
struct MakerEditFrameEnvironment {
    const AnimationCatalog* animations = nullptr;
    float worldAnimationTimeSeconds = 0.0f;
    float animationTransitionTimeSeconds = 0.0f;
    std::function<ScreenSelectorViewState(LevelLocation)> selectorState;
    GroundGeometryCache* groundGeometryCache = nullptr;
};

// The screen whose camera a Test Play of an overworld follows.
[[nodiscard]] OverworldScreenId makerPlaytestScreen(
    const OverworldMap& map,
    const GameState& state);
[[nodiscard]] CameraAngles makerPlaytestAngles(
    const Level& level,
    const OverworldMap* world,
    const GameState& state);

// The Maker: its sessions, panels, dialogs, autosave, board cursor, viewport
// and Test Play state, and the frame that draws and drives them.
//
// Application owns one, forwards events and one draw call per frame, and
// answers MakerHost. The simulation step and render frame of a Test Play stay
// with Application, which owns the gameplay effects and audio they share with
// the campaign; they reach the boards through playtest().
class MakerController {
public:
    struct PlaytestBoard {
        Level level;
        GameplaySession session;
        GameplayPresentation presentation;
        GroundGeometryCache groundGeometry;
        ParticleSystem particles;
        TurretFlameSystem turretFlames;
        LecternDialog lecternDialog;
        bool solved = false;
    };
    struct Playtest {
        struct Puzzle {
            std::string id;
            PlaytestBoard board;
        };
        PlaytestBoard board;
        std::optional<OverworldMap> overworld;
        std::optional<Puzzle> puzzle;
        std::unordered_map<uint32_t, MakerDraftStore::SavedLevel>
            selectorTargets;
        std::unordered_map<std::string, std::string> invalidLevels;
        MakerLevelProgress progress;

        PlaytestBoard& activeBoard() { return puzzle ? puzzle->board : board; }
        const PlaytestBoard& activeBoard() const
        {
            return puzzle ? puzzle->board : board;
        }
    };

    MakerController(
        MakerHost& host,
        UiContext& ui,
        InputState& input,
        const AssetManifest& assetManifest,
        const PresentationSettings& presentationSettings,
        const std::filesystem::path& saveDirectory);

    MakerController(const MakerController&) = delete;
    MakerController& operator=(const MakerController&) = delete;

    // Shell commands.
    void open();
    void close();
    // Reattaches the camera and writes both sessions' autosaves. Call before
    // the window goes away.
    void shutdown();

    [[nodiscard]] bool isOpen() const { return open_; }
    [[nodiscard]] bool hasSession() const { return session_ != nullptr; }
    [[nodiscard]] bool playing() const { return playtest_.has_value(); }
    // Open and not in Test Play.
    [[nodiscard]] bool editing() const { return open_ && !playtest_; }
    // A dialog, the help, the map or the toolbar overflow owns the input.
    [[nodiscard]] bool dialogOpen() const;
    [[nodiscard]] bool boardCursorFocused() const
    {
        return boardCursor_.has_value();
    }
    // The session being edited. Valid once the Maker has been opened.
    [[nodiscard]] MakerSession& session() { return *session_; }
    [[nodiscard]] const MakerSession& session() const { return *session_; }

    // One frame of the workspace: layout, picking, overlays, the view, the
    // command it returned and the pointer on the board.
    void drawWorkspace(
        Vec2 viewport,
        const InputRouter::Frame& input,
        std::optional<Vec2> wheelPosition);
    // Queues the autosave of the session being edited when it is due.
    void updateAutosave();
    void updateDetachedCamera(float dt);
    // The scene of the board being edited, with hover, previews and the
    // viewport camera applied.
    [[nodiscard]] RenderFrameData buildEditFrame(
        const InputRouter::Frame& input,
        FrameArena& arena,
        const MakerEditFrameEnvironment& environment);

    // Events.
    [[nodiscard]] bool handleCameraEvent(const SDL_Event& event);
    void windowFocusLost();
    // Unwinds one level of Maker state for a Back press: a dialog, a pan, a
    // focused field, Test Play, the board cursor, a panel, a selection. False
    // when nothing was left to unwind, and the shell should take the press.
    [[nodiscard]] bool handleBack();
    void releaseCameraMouse();
    // Drops pending dialog input before the quit confirmation takes over.
    void prepareForQuitConfirmation();

    // The text fields the application's native text input serves while
    // editing() and the options menu is closed.
    [[nodiscard]] bool textFieldsAvailable() const
    {
        return !helpOpen_ && !cameraDialog_;
    }
    [[nodiscard]] bool toolbarOverflowOpen() const
    {
        return view_.toolbarOverflowOpen();
    }
    [[nodiscard]] bool textFocused() const;
    void blurTextFields();
    [[nodiscard]] UiTextEdit* activeTextEdit();
    [[nodiscard]] UiRect textCaret() const;
    [[nodiscard]] bool handleTextPress(Vec2 position, bool extendSelection);
    // Ctrl+F: whether a search field can take focus now, the field it would
    // use, and the focus change itself.
    [[nodiscard]] bool searchShortcutAvailable() const;
    [[nodiscard]] UiTextEdit* searchTextEdit();
    [[nodiscard]] bool focusSearch();
    // Up and Down step a draft picker's rows while its search is empty.
    [[nodiscard]] bool handlePickerKey(const SDL_Event& event);
    // Applies the outcome of a native text event to a Maker dialog and
    // returns whether the event was consumed. Empty when no dialog claimed
    // it and the generic blur rules apply.
    [[nodiscard]] std::optional<bool> handleTextResult(
        const SDL_Event& event,
        SdlTextInputResult result);

    // Test Play.
    [[nodiscard]] Playtest* playtest()
    {
        return playtest_ ? &*playtest_ : nullptr;
    }
    [[nodiscard]] const Playtest* playtest() const
    {
        return playtest_ ? &*playtest_ : nullptr;
    }
    [[nodiscard]] bool tryEnterPlaytestSelector();
    [[nodiscard]] ScreenSelectorStatus playtestSelectorStatus(
        const OverworldSelectorRuntime& selector) const;

private:
    struct WorkspaceFrame;
    struct AutosaveSlot {
        std::optional<MakerRecoverySnapshot> pending;
        std::optional<uint64_t> writtenRevision;
        // Requests handed to the writer whose results have not arrived.
        std::optional<uint64_t> queuedRevision;
        bool discardQueued = false;
        std::chrono::steady_clock::time_point nextAttempt;
        std::string loadError;
        bool journalPresent = false;
        bool writeError = false;

        [[nodiscard]] bool unresolved() const
        {
            return pending.has_value() || !loadError.empty();
        }
    };
    struct ProblemTarget {
        // Empty when the saved level was missing at the last check.
        std::optional<MakerDraftStore::DraftVersion> version;
        bool playable = false;
        std::chrono::steady_clock::time_point checkedAt;
    };
    struct DraftDialog {
        MakerDraftDialogMode mode;
        std::vector<std::string> names;
        std::vector<MakerDraftStore::SavedLevel> levelRows;
        std::string pendingName;
        std::string error;
        MakerDraftDialogAction inputAction;
        std::size_t removedTileCount = 0;
        uint32_t layer = 0;
        MakerSession::LayerLoss layerLoss;
        std::optional<GridPosition3> overworldItem;
        bool legacySelectorTarget = false;
        bool missingSelectorTarget = false;
        std::optional<std::string> savedLevelId;
        std::optional<MakerRecoveryKind> recoveryKind;
        UiNavigation pickerNavigation;
    };
    struct ColorDialog {
        LevelEditor::LinkEndpoint endpoint;
        Vec3 initial;
        UiNavigation tabNavigation;
        bool cancelRequested = false;
    };
    struct CameraDialog {
        std::optional<CameraAngles> angles;
    };

    // The workspace frame, in the order drawWorkspace runs it.
    void drawPlaytestWorkspace(
        Vec2 viewport,
        float scale,
        const InputRouter::Frame& input);
    // False when the options menu owns the frame.
    [[nodiscard]] bool beginWorkspaceFrame(WorkspaceFrame& frame);
    void resolveBoardCells(WorkspaceFrame& frame);
    void resolveAreaState(WorkspaceFrame& frame);
    void resolveHoverState(WorkspaceFrame& frame);
    void buildLinkOverlay(WorkspaceFrame& frame);
    void buildSelectorOverlay(WorkspaceFrame& frame);
    void buildInspectionMarkers(WorkspaceFrame& frame);
    void resolvePlacementHint(WorkspaceFrame& frame);
    void buildPropertyPanel(WorkspaceFrame& frame);
    void resolveFrameRegion(WorkspaceFrame& frame);
    void drawWorkspaceView(WorkspaceFrame& frame);
    // True when a modal drew and took the rest of the frame.
    [[nodiscard]] bool drawModal(WorkspaceFrame& frame);
    // True when the command ended the frame.
    [[nodiscard]] bool applyCommand(WorkspaceFrame& frame);
    void handleBoardPointer(WorkspaceFrame& frame);

    [[nodiscard]] std::optional<GridPosition3> pickBoardCell(
        const WorkspaceFrame& frame,
        Vec2 position);
    [[nodiscard]] std::optional<GridPosition3> pickAreaCell(
        const WorkspaceFrame& frame,
        Vec2 position);
    [[nodiscard]] std::optional<MakerAreaViewState::Outline> projectOutline(
        const WorkspaceFrame& frame,
        MakerSession::Region region,
        Vec4 color,
        float lift);
    [[nodiscard]] std::optional<GridPosition3> resolveHover(
        const WorkspaceFrame& frame);
    [[nodiscard]] std::string_view placementHint(
        const WorkspaceFrame& frame) const;
    void refreshLayerView();

    void toggleOverworld();
    void resetViewportFraming();
    [[nodiscard]] bool updateViewportNavigation(
        const InputRouter::PointerInput& pointer,
        UiRect scene,
        std::optional<Vec2> wheelPosition,
        bool enabled);
    void toggleBoardCursor(std::optional<GridPosition3> hovered);
    void clearBoardCursor();
    std::optional<GridPosition3> updateBoardCursor(
        GridPosition direction,
        UiRect scene,
        bool projectionReady);
    void activateBoardCursor(
        GridPosition3 cell,
        const InputRouter::Frame& input);
    void drawOverworldMap(
        Vec2 viewport,
        float scale,
        const MakerViewInput& input);
    void closeOverworldMap();
    void setPanel(MakerPanel panel);
    void refreshProblems(bool force = false);
    [[nodiscard]] bool problemTargetPlayable(std::string_view name);
    void jumpToProblem(const MakerSession::Problem& problem);
    [[nodiscard]] bool openOverworldItem(
        GridPosition3 cell,
        bool selectorsOnly = false);
    void openDraftDialog(MakerDraftDialogMode mode, std::string_view name = {});
    void closeDraftDialog();
    void handleDraftDialogAction(const MakerDraftDialogAction& action);
    void refreshSavedLevels();
    [[nodiscard]] std::string_view selectorLabel(
        const Level::ScreenSelector* selector) const;
    void checkRecovery();
    void openRecovery(MakerRecoveryKind kind, bool confirmDiscard = false);
    void updateAutosave(MakerSession& maker, bool force = false);
    void applyAutosaveResults();
    [[nodiscard]] bool recoveryMatchesSaved(
        const MakerRecoverySnapshot& snapshot) const;
    void openColorDialog(
        LevelEditor::LinkEndpoint endpoint,
        Vec3 color,
        std::string_view groupName);
    void closeColorDialog();
    void drawColorDialog(
        Vec2 viewport,
        float scale,
        const MakerViewInput& input);
    void drawCameraDialog(
        const MakerLayout& layout,
        MakerViewInput input,
        bool canDetach);
    void closeCameraDialog();
    void initializePlaytestBoard(
        PlaytestBoard& board,
        Level level,
        const OverworldMap* world);
    void startPlaytest();
    void endPlaytest();
    void exitPlaytestBoard();
    [[nodiscard]] const OverworldSelectorRuntime* currentPlaytestSelector()
        const;
    [[nodiscard]] const MakerDraftStore::SavedLevel* playtestTarget(
        const OverworldSelectorRuntime& selector) const;
    [[nodiscard]] std::optional<uint32_t> playtestPrerequisite(
        const OverworldSelectorRuntime& selector) const;
    [[nodiscard]] std::string playtestSelectorHint(
        const OverworldSelectorRuntime& selector) const;
    void drawPlaytestSelectorPrompt(UiRect scene, Vec2 viewport);

    MakerHost& host_;
    UiContext& ui_;
    InputState& input_;
    const AssetManifest& assetManifest_;
    const PresentationSettings& presentationSettings_;

    MakerView view_;
    MakerPanel panel_ = MakerPanel::Palette;
    MakerDraftStore draftStore_;
    MakerRecoveryStore recoveryStore_;
    // Journal writes run on this writer's thread. Flush it before using
    // recoveryStore_ directly.
    MakerAutosaveWriter autosaveWriter_ { recoveryStore_ };
    std::vector<MakerDraftStore::SavedLevel> savedLevels_;
    std::vector<std::string> retiredLevelNames_;
    std::array<AutosaveSlot, 2> autosaves_;
    bool recoveryChecked_ = false;
    std::optional<MakerSession> puzzleSession_;
    std::optional<MakerSession> overworldSession_;
    MakerSession* session_ = nullptr;
    std::vector<MakerSession::Problem> problems_;
    std::optional<MakerSession::Problem> problemFocus_;
    std::chrono::steady_clock::time_point problemsCheckedAt_;
    std::unordered_map<std::string, ProblemTarget> problemTargets_;
    std::optional<DraftDialog> draftDialog_;
    UiColorPicker colorPicker_;
    std::optional<ColorDialog> colorDialog_;
    std::optional<CameraDialog> cameraDialog_;
    DetachedCamera detachedCamera_;
    std::optional<Playtest> playtest_;
    std::optional<UiRect> viewport_;
    MakerViewportCamera viewportCamera_;
    std::optional<Vec2> panPosition_;
    std::optional<GridPosition3> hover_;
    std::optional<GridPosition3> areaDragAnchor_;
    std::optional<GridPosition> boardCursor_;
    std::optional<GridPosition3> boardAreaAnchor_;
    GridPosition boardDirection_;
    std::chrono::steady_clock::time_point boardNextStep_;
    bool boardReveal_ = false;
    std::optional<RenderFrameData::CameraExtent> overworldCameraExtent_;
    // The fixed arrow and Enter keys of the frame being drawn.
    InputRouter::NavigationKeys navigationKeys_;
    bool open_ = false;
    bool helpOpen_ = false;
    bool overworldMapOpen_ = false;
};

} // namespace sokoban
