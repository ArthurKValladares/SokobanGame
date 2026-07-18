#pragma once

#include "engine/AnimationPreviewDebugUi.hpp"
#include "engine/ApplicationDebugUi.hpp"
#include "engine/AsyncSaveStore.hpp"
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

#include <filesystem>
#include <optional>

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
    [[nodiscard]] bool applyLevel(
        Level level,
        const GameplaySession::Snapshot* snapshot = nullptr);
    void advanceScreen();
    void checkpointCurrentScreen(bool immediateSave);
    void deferCurrentScreenCheckpoint();
    void updateDeferredCheckpoint(float dt);
    void applyAudioSettings(const PlayerProfile::AudioSettings& settings, bool persist);
    void persistProfile(bool immediate);
    void update(float dt);
    void drawEditorModeIndicator();
    void drawQuitConfirmation();
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
    [[nodiscard]] bool screenExists(int levelIndex, int screenIndex) const;
    [[nodiscard]] RenderAssetRequirements levelAssetRequirements(int levelIndex) const;
    void preloadUpcomingAssets();
    [[nodiscard]] RenderFrameData buildRenderFrame() const;

    Window window_;
    AsyncSaveStore saveStore_;
    PlayerProfile playerProfile_;
    std::filesystem::path assetRoot_;
    // Declared before the renderer/audio members that hold references to it.
    AssetManifest assetManifest_;
    VulkanRenderer renderer_;
    UiContext ui_;
    AudioSystem audioSystem_;
    Level level_;
    GameplaySession gameplaySession_;
    int currentLevel_ = 0;
    int currentScreen_ = 0;
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
    bool quitConfirmationOpen_ = false;
    bool draftExitConfirmationOpen_ = false;
};

} // namespace sokoban
