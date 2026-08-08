#pragma once

#include "engine/AnimationCatalog.hpp"
#include "engine/SaveSlotManager.hpp"
#include "engine/ShellFlow.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/AudioSystem.hpp"
#include "engine/MirrorParticleEffect.hpp"
#include "engine/ParticleSystem.hpp"
#include "engine/CampaignSession.hpp"
#include "engine/GameplayLoop.hpp"
#include "engine/GameplayPresentation.hpp"
#include "engine/InputRouter.hpp"
#include "engine/Input.hpp"
#include "engine/GameplaySession.hpp"
#include "engine/Level.hpp"
#include "engine/LevelCatalog.hpp"
#include "engine/Math.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/PlayerProfile.hpp"
#include "engine/SettingsCoordinator.hpp"
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

class ApplicationTools;

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();
    // Renders each tile type through the normal frame path and writes the
    // captured result to the source and staged asset trees. Returns false if
    // any tile failed. Blocking; the process is expected to exit afterwards.
    [[nodiscard]] bool bakeTileThumbnails();

private:
    void loadCurrentScreen();
    void openTitleScreen();
    [[nodiscard]] std::vector<SaveSlotInfo> saveSlotInfos() const;
    void switchSaveSlot(int slot);
    void deleteSaveSlot(int slot);
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
    void solveCurrentScreenForDebug();
    void handleCampaignAdvance(const CampaignSession::AdvanceResult& result);
    void checkpointCurrentScreen(bool immediateSave);
    void applySettingsEffects(const SettingsEffects& effects);
    void persistProfile(bool immediate);
    void update(
        float dt,
        const InputRouter::Frame& input,
        const VulkanRenderer::PreparedFrame* previousRenderFrame);
    [[nodiscard]] InputRouter::RoutingContext inputRoutingContext() const;
    [[nodiscard]] std::filesystem::path screenPath(int levelIndex, int screenIndex) const;
    // Scans levels/ once into CampaignSession; the level set is fixed
    // staged content, so title/progress queries read the cache instead of
    // hitting the filesystem per open. Rebuilt on screen loads so the debug
    // editor's mirrored changes are still reflected.
    void buildLevelCatalog();
    void restoreProfileLocation();
    [[nodiscard]] RenderAssetRequirements levelAssetRequirements(int levelIndex) const;
    void preloadUpcomingAssets();
    [[nodiscard]] RenderFrameData buildRenderFrame(
        const InputRouter::EditorInput& editorInput);

    Window window_;
    // Owns slot stores, the shared settings store, the marker, and every
    // other disk decision; Application owns the live profile and the
    // gameplay consequences.
    SaveSlotManager saveSlots_;
    PlayerProfile playerProfile_;
    std::filesystem::path assetRoot_;
    // Declared before the renderer/audio members that hold references to it.
    AssetManifest assetManifest_;
    AnimationCatalog animationCatalog_;
    FontAtlas uiFont_;
    VulkanRenderer renderer_;
    UiContext ui_;
    OptionsMenu optionsMenu_;
    OptionsMenuView optionsMenuView_;
    TitleScreen titleScreen_;
    LevelCompleteOverlay levelCompleteOverlay_;
    // Pure shell routing; Application executes the commands it emits.
    ShellFlow shellFlow_;
    AudioSystem audioSystem_;
    ParticleSystem particleSystem_;
    ParticleEffectDefinition mirrorSwapParticleEffect_;
    Level level_;
    GameplaySession gameplaySession_;
    CampaignSession campaign_;
    std::vector<LevelMetadata> levelMetadata_;
    InputState input_;
    InputRouter inputRouter_;
    FrameTimer frameTimer_;
    PresentationSettings presentationSettings_;
    SettingsCoordinator settingsCoordinator_;
    GameplayPresentation presentation_;
    std::unique_ptr<ApplicationTools> tools_;
    FrameArena renderFrameArena_;
    std::optional<VulkanRenderer::PreparedFrame> preparedRenderFrame_;
    bool running_ = true;
};

} // namespace sokoban
