#pragma once

#include "engine/AnimationPreviewDebugUi.hpp"
#include "engine/ApplicationDebugUi.hpp"
#include "engine/SaveSlotManager.hpp"
#include "engine/ShellFlow.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/AssetManifestDebugUi.hpp"
#include "engine/AssetManifestEditor.hpp"
#include "engine/AudioSystem.hpp"
#include "engine/GameplayPresentation.hpp"
#include "engine/Input.hpp"
#include "engine/GameplaySession.hpp"
#include "engine/Level.hpp"
#include "engine/LevelEditor.hpp"
#include "engine/LevelEditorDebugUi.hpp"
#include "engine/Math.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/PlayerProfile.hpp"
#include "engine/Time.hpp"
#include "engine/Window.hpp"
#include "engine/render/VulkanRenderer.hpp"
#include "engine/ui/FontAtlas.hpp"
#include "engine/ui/LevelCompleteOverlay.hpp"
#include "engine/ui/OptionsMenu.hpp"
#include "engine/ui/TitleScreen.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace sokoban {

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();

private:
    void loadCurrentScreen();
    void openTitleScreen();
    [[nodiscard]] std::vector<SaveSlotInfo> saveSlotInfos() const;
    void switchSaveSlot(int slot);
    void deleteSaveSlot(int slot);
    void applyLoadedProfileSettings();
    void persistSettings(bool immediate);
    [[nodiscard]] std::vector<TitleLevelInfo> titleLevelInfos() const;
    void startNewGame();
    void startLevel(int level, int screen);
    void resolveLevelComplete(bool toTitle);
    void openStandaloneLevelSelect();
    [[nodiscard]] ShellFacts shellFacts() const;
    void handleShellEvent(const ShellEvent& event);
    void executeShellCommand(const ShellCommand& command);
    [[nodiscard]] bool allLevelsCompleted() const;
    [[nodiscard]] bool shellMenuOpen() const;
    [[nodiscard]] bool applyLevel(
        Level level,
        const GameplaySession::Snapshot* snapshot = nullptr);
    void advanceScreen();
    void checkpointCurrentScreen(bool immediateSave);
    void deferCurrentScreenCheckpoint();
    void updateDeferredCheckpoint(float dt);
    void applyAudioSettings(const PlayerProfile::AudioSettings& settings, bool persist);
    [[nodiscard]] OptionsMenuSettings optionsMenuSettings() const;
    void applyOptionsMenuSettings(const OptionsMenuSettings& settings);
    void persistProfile(bool immediate);
    void update(float dt);
    void drawDraftExitConfirmation();
    void updateEditorPainting();
    void queuePressedCommands();
    void advancePlayerMovement(float dt);
    [[nodiscard]] bool completeActiveAction();
    [[nodiscard]] bool tryStartNextMove();
    [[nodiscard]] std::optional<MoveDirection> pressedVerticalDirection() const;
    [[nodiscard]] std::optional<MoveDirection> pressedHorizontalDirection() const;
    [[nodiscard]] std::optional<MoveDirection> heldVerticalDirection() const;
    [[nodiscard]] std::optional<MoveDirection> heldHorizontalDirection() const;
    [[nodiscard]] std::filesystem::path screenPath(int levelIndex, int screenIndex) const;
    // Scans levels/ once into levelScreenCounts_; the level set is fixed
    // staged content, so title/progress queries read the cache instead of
    // hitting the filesystem per open. Rebuilt on screen loads so the debug
    // editor's mirrored changes are still reflected.
    void buildLevelCatalog();
    void restoreProfileLocation();
    [[nodiscard]] int levelCount() const;
    [[nodiscard]] bool screenExists(int levelIndex, int screenIndex) const;
    [[nodiscard]] RenderAssetRequirements levelAssetRequirements(int levelIndex) const;
    void preloadUpcomingAssets();
    [[nodiscard]] RenderFrameData buildRenderFrame() const;

    Window window_;
    // Owns slot stores, the shared settings store, the marker, and every
    // other disk decision; Application owns the live profile and the
    // gameplay consequences.
    SaveSlotManager saveSlots_;
    PlayerProfile playerProfile_;
    std::filesystem::path assetRoot_;
    // Declared before the renderer/audio members that hold references to it.
    AssetManifest assetManifest_;
    FontAtlas uiFont_;
    VulkanRenderer renderer_;
    UiContext ui_;
    OptionsMenu optionsMenu_;
    TitleScreen titleScreen_;
    LevelCompleteOverlay levelCompleteOverlay_;
    // Pure shell routing; Application executes the commands it emits.
    ShellFlow shellFlow_;
    AudioSystem audioSystem_;
    Level level_;
    GameplaySession gameplaySession_;
    // Screens per level, indexed by level (see buildLevelCatalog).
    std::vector<int> levelScreenCounts_;
    int currentLevel_ = 0;
    int currentScreen_ = 0;
    // Next level to load once the level-complete overlay is resolved.
    std::optional<int> pendingNextLevel_;
    // False when the current run began at a later screen via level select;
    // completing such a run does not record best moves/time.
    bool levelRunFromStart_ = true;
    // False until Continue/New Game (or a debug draft) loads the world; the
    // title screen is up and no level, session, or checkpoint exists yet.
    bool gameLoaded_ = false;
    int completedLevelMoveCount_ = 0;
    double currentLevelElapsedSeconds_ = 0.0;
    double deferredCheckpointAgeSeconds_ = 0.0;
    bool deferredCheckpointPending_ = false;
    InputState input_;
    FrameTimer frameTimer_;
    PresentationSettings presentationSettings_;
    GameplayPresentation presentation_;
    ApplicationDebugUi applicationDebugUi_;
    AssetManifestEditor assetManifestEditor_;
    AssetManifestDebugUi assetManifestDebugUi_;
    LevelEditor levelEditor_;
    LevelEditorDebugUi levelEditorDebugUi_;
    AnimationPreviewDebugUi animationPreviewDebugUi_;
    std::optional<GridPosition3> editorHoverCell_;
    bool running_ = true;
    bool draftExitConfirmationOpen_ = false;
};

} // namespace sokoban
