#pragma once

#include "engine/AnimationPreviewDebugUi.hpp"
#include "engine/ApplicationDebugUi.hpp"
#include "engine/SaveSlotManager.hpp"
#include "engine/ShellFlow.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/AssetManifestDebugUi.hpp"
#include "engine/AssetManifestEditor.hpp"
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
#include "engine/LevelEditor.hpp"
#include "engine/LevelEditorDebugUi.hpp"
#include "engine/Math.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/PlayerProfile.hpp"
#include "engine/SettingsCoordinator.hpp"
#include "engine/SplatPainter.hpp"
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
    void checkpointCurrentScreen(bool immediateSave);
    void applySettingsEffects(const SettingsEffects& effects);
    void persistProfile(bool immediate);
    void update(
        float dt,
        const InputRouter::Frame& input,
        const VulkanRenderer::PreparedFrame* previousRenderFrame);
    void drawDraftExitConfirmation();
    // Ring showing where and how large the ground brush will paint.
    void drawBrushPreview();
    void updateEditorPainting(
        const InputRouter::EditorInput& input,
        const VulkanRenderer::PreparedFrame* previousRenderFrame);
    // Ground-splat brush painting, which replaces tile painting while a paint
    // session is open. Returns true when it handled the pointer.
    bool updateGroundPainting(
        const InputRouter::EditorInput& input,
        const VulkanRenderer::PreparedFrame* previousRenderFrame,
        Vec2 pointerPixels);
    // Opens the splat map belonging to the document currently being edited.
    bool openGroundPainting();
    // Creates and registers a splat map for the edited screen, then opens it
    // for painting. Lets a screen added in the editor be painted without
    // re-running tools/make_ground_textures.py or restarting.
    bool createGroundSplatMap();
    // Appends a texture entry to the source manifest (through the manifest
    // editor, so its tab stays in sync) and to the staged copy the running
    // build loads.
    void persistManifestTexture(
        const std::string& name, const std::string& relativePath);
    void pushPaintedSplatMap();
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
        const InputRouter::EditorInput& editorInput) const;

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
    InputState input_;
    InputRouter inputRouter_;
    FrameTimer frameTimer_;
    PresentationSettings presentationSettings_;
    SettingsCoordinator settingsCoordinator_;
    GameplayPresentation presentation_;
    ApplicationDebugUi applicationDebugUi_;
    AssetManifestEditor assetManifestEditor_;
    AssetManifestDebugUi assetManifestDebugUi_;
    LevelEditor levelEditor_;
    LevelEditorDebugUi levelEditorDebugUi_;
    AnimationPreviewDebugUi animationPreviewDebugUi_;
    std::optional<VulkanRenderer::PreparedFrame> preparedRenderFrame_;
    std::optional<GridPosition3> editorHoverCell_;
    SplatPainter splatPainter_;
    // World position of the brush under the pointer, for the preview ring:
    // x/y are board tiles, z is the height of the surface it landed on.
    // Empty when the pointer is not over paintable ground.
    std::optional<Vec3> editorBrushPoint_;
    // Last painted state pushed to the GPU. Comparing against the painter's
    // revision keeps re-uploads to at most one per frame, and none at all
    // while the brush is not changing anything.
    uint64_t uploadedSplatRevision_ = 0;
    bool running_ = true;
    bool draftExitConfirmationOpen_ = false;
    // Set by the editor's re-bake button and serviced between frames, because
    // the bake drives frames of its own.
    bool bakeThumbnailsRequested_ = false;
};

} // namespace sokoban
