#pragma once

#include "engine/AnimationCatalog.hpp"
#include "engine/SaveSlotManager.hpp"
#include "engine/ShellFlow.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/AudioSystem.hpp"
#include "engine/MirrorParticleEffect.hpp"
#include "engine/ParticleSystem.hpp"
#include "engine/OverworldView.hpp"
#include "engine/CampaignSession.hpp"
#include "engine/GameplayLoop.hpp"
#include "engine/GameplayPresentation.hpp"
#include "engine/InputRouter.hpp"
#include "engine/Input.hpp"
#include "engine/GameplaySession.hpp"
#include "engine/Level.hpp"
#include "engine/LevelTransition.hpp"
#include "engine/LevelCatalog.hpp"
#include "engine/Math.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/PlayerProfile.hpp"
#include "engine/SettingsCoordinator.hpp"
#include "engine/Time.hpp"
#include "engine/UserSettingsConfig.hpp"
#include "engine/Window.hpp"
#include "engine/render/VulkanRenderer.hpp"
#include "engine/ui/FontAtlas.hpp"
#include "engine/ui/LevelCompleteOverlay.hpp"
#include "engine/ui/InputPrompts.hpp"
#include "engine/ui/OptionsMenu.hpp"
#include "engine/ui/TitleScreen.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#ifndef SOKOBAN_ENABLE_DEBUG_UI
#define SOKOBAN_ENABLE_DEBUG_UI 0
#endif

namespace sokoban {

// Non-default ways to start the process. Both fields exist for the headless
// smoke run: CI needs the real frame loop to execute and then stop, and it
// must not write into a real player profile to do it.
struct ApplicationOptions {
    // Render this many frames through the ordinary loop, start a game so the
    // scene pass is actually exercised, and exit. Zero runs until quit.
    //
    // A title-screen-only run would be close to worthless as a smoke test:
    // buildRenderFrame returns an empty RenderFrameData while no game is
    // loaded, so no tiles, models, shadows or SSAO are recorded at all.
    std::uint64_t smokeFrames = 0;
    // Roots saves, settings and the pipeline cache here instead of the user's
    // preference path. Empty keeps the normal location.
    std::filesystem::path saveDirectoryOverride;
    // Optional smoke-run artifact destination. See CommandLineOptions: this
    // is deliberately explicit so ordinary play never performs readback.
    std::filesystem::path evidenceOutputDirectory;
    int evidenceRenderScalePercent = 100;
    int evidenceAntiAliasingSamples = config::antiAliasingSamples;
    bool evidenceAmbientOcclusionEnabled = true;
    bool evidenceFrustumCullingEnabled = true;
    bool evidencePointLightEnabled = false;
    bool evidencePointLightStressEnabled = false;
    bool parallelScenePreparationEnabled = true;
    bool pointShadowOptimizationsEnabled = true;
    bool recorderScratchReuseEnabled = true;
    // Zero keeps the production default. A non-zero override exists for
    // deterministic residency stress/validation runs.
    std::uint64_t textureResidencyBudgetKiB = 0;
};

struct ApplicationTimingEventWatchState {
    SimulationTiming* simulation = nullptr;
    FramePacer* framePacer = nullptr;
};

#if SOKOBAN_ENABLE_DEBUG_UI
class ApplicationTools;
#endif

class Application {
public:
    explicit Application(ApplicationOptions options = {});
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();
#if SOKOBAN_ENABLE_DEBUG_UI
    // Renders each tile type through the normal frame path and writes the
    // captured result to the source and staged asset trees. Returns false if
    // any tile failed. Blocking; the process is expected to exit afterwards.
    [[nodiscard]] bool bakeTileThumbnails();
#endif

private:
    void captureEvidenceScene();
    void finishEvidenceCapture();
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
        const GameplaySession::Snapshot* snapshot = nullptr,
        std::optional<LevelLocation> location = std::nullopt,
        bool composedOverworld = false);
    void advanceScreen();
#if SOKOBAN_ENABLE_DEBUG_UI
    void solveCurrentScreenForDebug();
#endif
    void handlePuzzleCompleted(const CampaignSession::PuzzleCompleted& completed);
    void beginLevelTransition(std::function<void()> midpointAction);
    void updateLevelTransition(float dt);
    void tryEnterSelector();
    [[nodiscard]] bool updateScreenPreview(bool requested, float dt);
    [[nodiscard]] std::optional<RenderFrameData>
        buildScreenPreviewRenderFrame() const;
    void drawSelectorPrompt(const VulkanRenderer::PreparedFrame* frame);
    void drawScreenPreviewOverlay(Vec2 viewport);
    void drawAssetLoadingOverlay(Vec2 viewport);
    void checkpointCurrentScreen(bool immediateSave);
    void applySettingsEffects(const SettingsEffects& effects);
    void persistProfile(bool immediate);
    void update(
        float dt,
        const InputRouter::Frame& input,
        const VulkanRenderer::PreparedFrame* previousRenderFrame);
    [[nodiscard]] InputRouter::RoutingContext inputRoutingContext() const;
    [[nodiscard]] std::filesystem::path screenPath(int levelIndex, int screenIndex) const;
    [[nodiscard]] std::filesystem::path overworldPath() const;
    [[nodiscard]] std::filesystem::path overworldRoot() const;
    // Scans levels/ once into CampaignSession; the level set is fixed
    // staged content, so title/progress queries read the cache instead of
    // hitting the filesystem per open. Rebuilt on screen loads so the debug
    // editor's mirrored changes are still reflected.
    void buildLevelCatalog();
    void restoreProfileLocation();
    [[nodiscard]] RenderAssetRequirements levelAssetRequirements(int levelIndex) const;
    void preloadUpcomingAssets();
    // Advances to the arena the previous prepared frame does not reference,
    // clears it, and returns it. The only place a render-frame arena is reset.
    [[nodiscard]] FrameArena& beginRenderFrameArena();
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
    InputPromptCatalog inputPrompts_;
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
    std::optional<OverworldMap> overworldMap_;
    GameplaySession gameplaySession_;
    std::optional<LevelLocation> screenPreviewTarget_;
    std::optional<Level> screenPreviewLevel_;
    GameplaySession screenPreviewSession_;
    GameplayPresentation screenPreviewPresentation_;
    CampaignSession campaign_;
    std::vector<LevelMetadata> levelMetadata_;
    InputState input_;
    InputRouter inputRouter_;
    SimulationTiming simulationTiming_;
    FrameTimer frameTimer_;
    FramePacer framePacer_;
    ApplicationTimingEventWatchState timingEventWatchState_ {
        &simulationTiming_, &framePacer_ };
    PresentationSettings presentationSettings_;
    SettingsCoordinator settingsCoordinator_;
    GameplayPresentation presentation_;
    LevelTransition levelTransition_;
    std::function<void()> levelTransitionMidpointAction_;
#if SOKOBAN_ENABLE_DEBUG_UI
    std::unique_ptr<ApplicationTools> tools_;
#endif
    // Two arenas, alternating. The previous frame's PreparedFrame still holds
    // a RenderFrameData whose arrays point into the arena it was built from,
    // and the update step reads those tiles - decoration picking and player
    // bounds - before the next frame is built.
    //
    // One arena made that safe only by accident of ordering: every such read
    // happened earlier in the loop than the reset buried inside
    // buildRenderFrame. Moving that reset to the top of the loop, which is the
    // natural place for it, would have turned it into a use-after-free that
    // Linux CI cannot catch, because the render path never runs there.
    // Alternating means the arena being reset is never the one the previous
    // prepared frame points into, and the invariant stops being invisible.
    //
    // The cost is a second arena's worth of memory. Only one frame is ever
    // read back, so two is enough.
    std::array<FrameArena, 2> renderFrameArenas_;
    std::size_t renderFrameArenaIndex_ = 0;
    std::uint64_t smokeFrames_ = 0;
    std::filesystem::path evidenceOutputDirectory_;
    RenderStats evidenceStats_ {};
    bool evidenceAmbientOcclusionEnabled_ = true;
    bool evidencePointLightEnabled_ = false;
    bool evidencePointLightStressEnabled_ = false;
    bool evidenceSceneCaptured_ = false;
    std::optional<VulkanRenderer::PreparedFrame> preparedRenderFrame_;
    float overworldOverviewProgress_ = 0.0f;
    bool screenPreviewActive_ = false;
    bool running_ = true;
};

} // namespace sokoban
