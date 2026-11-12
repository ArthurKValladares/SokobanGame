#include "engine/Application.hpp"

#include "engine/Profiler.hpp"
#if SOKOBAN_ENABLE_DEBUG_UI
#include "engine/ApplicationTools.hpp"
#include "engine/AtomicFile.hpp"
#include "engine/ContentPipeline.hpp"
#include "engine/DebugUi.hpp"
#include "engine/DevSession.hpp"
#include "engine/LevelProjectStore.hpp"
#include "engine/SolutionStore.hpp"
#include <imgui.h>
#endif

#include "engine/ParticleConfig.hpp"
#include "engine/PerformanceFixtures.hpp"
#include "engine/GroundTileGeometry.hpp"
#include "engine/render/CameraConfig.hpp"
#include "engine/render/CompressedTextureArtifact.hpp"
#include "engine/render/ImageData.hpp"
#include "engine/render/FogOfWarConfig.hpp"
#include "engine/render/WaterGeometry.hpp"

#include "engine/Log.hpp"
#include "engine/PathText.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/Rules.hpp"
#include "engine/RuntimeContent.hpp"
#include "engine/UserSettingsConfig.hpp"
#include "engine/ui/SelectorPrompt.hpp"
#include "engine/ui/UiConfig.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <functional>
#include <limits>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sokoban {
namespace {

Vec2 scaleUiPosition(Vec2 position, Vec2 from, Vec2 to)
{
    return {
        from.x > 0.0f ? position.x * to.x / from.x : position.x,
        from.y > 0.0f ? position.y * to.y / from.y : position.y,
    };
}

UiRect fitSceneViewport(UiRect bounds, Vec2 source)
{
    const Vec2 start { std::ceil(bounds.position.x), std::ceil(bounds.position.y) };
    const Vec2 end {
        std::floor(bounds.position.x + bounds.size.x),
        std::floor(bounds.position.y + bounds.size.y),
    };
    const Vec2 available = end - start;
    if (source.x <= 0.0f || source.y <= 0.0f ||
        available.x <= 0.0f || available.y <= 0.0f) {
        return { start, {} };
    }
    const float scale = std::min(available.x / source.x, available.y / source.y);
    const Vec2 size { std::floor(source.x * scale), std::floor(source.y * scale) };
    const Vec2 spare = available - size;
    return {
        start + Vec2 { std::floor(spare.x * 0.5f), std::floor(spare.y * 0.5f) },
        size,
    };
}

bool showsMakerResizePreview(MakerDraftDialogMode mode)
{
    return mode == MakerDraftDialogMode::ResizeBoard || mode == MakerDraftDialogMode::ConfirmCrop;
}

template<class Load>
auto timedAssetStartup(std::string_view phase, Load&& load)
{
    const auto started = std::chrono::steady_clock::now();
    auto result = std::forward<Load>(load)();
    log::info(log::Category::Application)
        << "Asset startup phase (us): " << phase << '='
        << std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - started).count();
    return result;
}

AntiAliasingMode antiAliasingModeForSamples(int samples)
{
    switch (samples) {
    case 1: return AntiAliasingMode::None;
    case 2: return AntiAliasingMode::Msaa2x;
    case 4: return AntiAliasingMode::Msaa4x;
    default: return AntiAliasingMode::Msaa8x;
    }
}

AssetLoadingBudget assetLoadingBudgetFor(const ApplicationOptions& options)
{
    AssetLoadingBudget budget;
    if (options.textureResidencyBudgetKiB != 0) {
        if (options.textureResidencyBudgetKiB >
            std::numeric_limits<uint64_t>::max() / 1024ULL) {
            throw std::invalid_argument(
                "Texture residency budget is too large");
        }
        budget.textureResidencyBytes =
            options.textureResidencyBudgetKiB * 1024ULL;
    }
    return budget;
}

bool configureStartupWindow(
    Window& window, PlayerProfile& profile, bool smokeRun)
{
    // Apply the persisted mode before Vulkan creates its surface and
    // swapchain. Creating a default window first and changing it after the
    // renderer exists forces an otherwise redundant swapchain generation
    // before frame one.
    profile.normalize();
    // Automated renders need only the window's initial surface and extent.
    // Xvfb has no window manager to acknowledge fullscreen/maximize requests,
    // so keep the constructor's windowed mode for smoke and evidence runs.
    if (smokeRun) {
        return true;
    }
    const UserSettings::Video& video = profile.settings.video;
    if (video.fullscreen) {
        window.setFullscreen(true);
    } else if (video.windowMaximized) {
        window.setWindowedMaximized();
    } else {
        window.setWindowedSize(video.windowWidth, video.windowHeight);
    }
    return true;
}

void appendEvidenceWaterFixture(RenderFrameData& frame)
{
    if (frame.levelWidth == 0 || frame.levelHeight == 0) {
        return;
    }

    // Cross the board edge so one stable draw exercises water over both opaque
    // geometry and the clear background. Lift this evidence-only surface above
    // the ground; authored water sits below the surrounding lip and would be
    // depth-occluded if it were placed directly over an ordinary floor.
    const RenderFrameData::CameraExtent fixtureExtent =
        frame.cameraExtent.value_or(RenderFrameData::CameraExtent {
            .width = frame.levelWidth,
            .height = frame.levelHeight,
            .depth = frame.levelDepth,
        });
    constexpr float stripLeft = -1.0f;
    constexpr float stripWidth = 2.0f;
    const float stripTop = static_cast<float>(fixtureExtent.originY);
    const float stripHeight = std::max(
        static_cast<float>(fixtureExtent.height), 1.0f);
    constexpr uint32_t shorelineMask =
        waterShorelineBit(WaterShorelineEdge::PositiveX) |
        waterShorelineBit(WaterShorelineCorner::PositiveXNegativeY) |
        waterShorelineBit(WaterShorelineCorner::PositiveXPositiveY);
    frame.waterSurfaces.push_back({
        .cell = {
            static_cast<int>(stripLeft),
            fixtureExtent.originY,
            0,
        },
        .position = { stripLeft, stripTop },
        .size = { stripWidth, stripHeight },
        .color = frame.waterRendering.surfaceColor,
        .elevation = 1.0f + config::waterDepthBelowGround,
        .shorelineMask = shorelineMask,
        .pickable = false,
    });
}

bool SDLCALL simulationTimingEventWatch(void* userdata, SDL_Event* event)
{
    const auto& state = *static_cast<ApplicationTimingEventWatchState*>(
        userdata);
    auto& timing = *state.simulation;
    auto& framePacer = *state.framePacer;
    switch (event->type) {
    case SDL_EVENT_WINDOW_MINIMIZED:
        timing.setSuspended(SimulationSuspension::Minimized, true);
        framePacer.setSuspended(FramePacingSuspension::Minimized, true);
        break;
    case SDL_EVENT_WINDOW_RESTORED:
        timing.setSuspended(SimulationSuspension::Minimized, false);
        framePacer.setSuspended(FramePacingSuspension::Minimized, false);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        framePacer.setSuspended(FramePacingSuspension::Unfocused, true);
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        framePacer.setSuspended(FramePacingSuspension::Unfocused, false);
        break;
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        timing.setSuspended(SimulationSuspension::Backgrounded, true);
        framePacer.setSuspended(FramePacingSuspension::Backgrounded, true);
        break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        timing.setSuspended(SimulationSuspension::Backgrounded, false);
        framePacer.setSuspended(FramePacingSuspension::Backgrounded, false);
        break;
    default:
        break;
    }
    return true;
}

} // namespace

Application::Application(ApplicationOptions options)
    : window_(
          "Sokoban 3D",
          config::windowWidth,
          config::windowHeight)
    , saveSlots_(
          options.saveDirectoryOverride.empty()
              ? SaveStore::preferencePath("Sokoban3D", "Sokoban3D")
              : options.saveDirectoryOverride)
    , playerProfile_(saveSlots_.loadActiveProfile())
    , startupWindowConfigured_(
          configureStartupWindow(window_, playerProfile_, options.smokeFrames != 0))
    , assetRoot_(timedAssetStartup("content-validation",
#if SOKOBAN_ENABLE_DEBUG_UI
          [&options] {
              if (options.smokeFrames == 0 && options.evidenceOutputDirectory.empty()) {
                  return runtimeContentRoot(SOKOBAN_SOURCE_LEVEL_DIR);
              }
              return runtimeContentRoot();
          }))
#else
          [] { return runtimeContentRoot(); }))
#endif
    , assetManifest_(timedAssetStartup("manifest", [this] {
          return AssetManifest::loadFromFile(assetRoot_ / "manifest.json");
      }))
    , audioStartup_(std::async(std::launch::async, [this] {
          return std::make_unique<AudioSystem>(assetRoot_, assetManifest_);
      }))
    , animationCatalog_(timedAssetStartup("animation-catalog", [this] {
          return AnimationCatalog::loadFromFile(
              assetRoot_ / "animation_catalog.json", assetManifest_);
      }))
    , uiFont_(timedAssetStartup("font-atlas", [this] {
          return FontAtlas::loadDefault(assetRoot_);
      }))
    , inputPrompts_(timedAssetStartup("input-prompts", [this] {
          return InputPromptCatalog(assetRoot_, assetManifest_);
      }))
    , renderer_(
          window_.nativeHandle(),
          assetRoot_,
          saveSlots_.directory() / "vulkan_pipeline_cache.bin",
          assetManifest_,
          uiFont_,
          antiAliasingModeForSamples(
              options.evidenceOutputDirectory.empty()
                  ? playerProfile_.settings.video.antiAliasingSamples
                  : options.evidenceAntiAliasingSamples),
          options.evidenceOutputDirectory.empty()
              ? playerProfile_.settings.video.effectiveRenderScalePercent()
              : options.evidenceRenderScalePercent,
          {
              .vsync = options.evidenceOutputDirectory.empty()
                  ? playerProfile_.settings.video.vsync
                  : options.evidenceVsyncEnabled,
              .allowTearing = options.evidenceOutputDirectory.empty()
                  ? playerProfile_.settings.video.allowTearing
                  : !options.evidenceVsyncEnabled,
          },
          assetLoadingBudgetFor(options),
          options.parallelScenePreparationEnabled,
          options.pointShadowOptimizationsEnabled,
          options.recorderScratchReuseEnabled,
          options.showFailureDialogs,
          options.waterCellCacheEnabled)
    , ui_(uiFont_)
    , audioSystem_(audioStartup_.get())
    , mirrorSwapParticleEffect_(
          makeMirrorSwapParticleEffect(assetManifest_))
    , witchSwapParticleEffect_(
          makeWitchSwapParticleEffect(assetManifest_))
    , crystalChangeParticleEffect_(
          makeCrystalChangeParticleEffect(assetManifest_))
    , turretParticleEffects_(
          makeTurretParticleEffects(assetManifest_))
    , settingsCoordinator_(playerProfile_, presentationSettings_)
#if SOKOBAN_ENABLE_DEBUG_UI
    , tools_(std::make_unique<ApplicationTools>())
#endif
    , renderFrameArenas_ {
          FrameArena("render frame A", 2 * renderFrameArenaBytes()),
          FrameArena("render frame B", 2 * renderFrameArenaBytes()),
      }
    , smokeFrames_(options.smokeFrames)
    , launchLevel_(options.startLevel)
    , launchScreen_(options.startScreen)
    , launchEditDocument_(std::move(options.editDocument))
#if SOKOBAN_ENABLE_DEBUG_UI
    , devSessionPath_(saveSlots_.directory() / "dev-session.json")
#endif
    , evidenceOutputDirectory_(
          std::move(options.evidenceOutputDirectory))
    , evidenceLevel_(options.evidenceLevel)
    , evidenceScreen_(options.evidenceScreen)
    , evidenceEffects_(std::move(options.evidenceEffects))
    , launchContinue_(options.continueGame)
    , launchShowTitle_(options.showTitle)
    , evidenceAmbientOcclusionEnabled_(
          options.evidenceAmbientOcclusionEnabled)
    , evidenceWaterEnabled_(options.evidenceWaterEnabled)
    , evidencePointLightEnabled_(options.evidencePointLightEnabled)
    , evidencePointLightStressEnabled_(
          options.evidencePointLightStressEnabled)
    , evidenceDebugUi_(options.evidenceDebugUi)
    , evidenceAnimate_(options.evidenceAnimate)
    , evidenceWaterDisabled_(options.evidenceWaterDisabled)
    , evidenceWaterReflectionsDisabled_(options.evidenceWaterReflectionsDisabled)
{
    log::info(log::Category::Persistence)
        << saveSlots_.progressStatus();
    const auto levelCatalogStart = std::chrono::steady_clock::now();
    buildLevelCatalog();
    restoreProfileLocation();
    log::info(log::Category::Application)
        << "Asset startup phase (us): level-catalog="
        << std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - levelCatalogStart).count();
    SettingsEffects initialSettings = settingsCoordinator_.initialize();
    // The window was configured before renderer construction so Vulkan saw
    // the final startup extent. Reapplying it here would synchronize the OS
    // window and request a second swapchain for no visual change.
    initialSettings.window.reset();
    applySettingsEffects(initialSettings);
    if (!evidenceOutputDirectory_.empty()) {
        CpuProfiler::instance().setEnabled(options.evidenceProfilerEnabled);
#if !SOKOBAN_ENABLE_DEBUG_UI
        if (evidenceDebugUi_) {
            throw std::invalid_argument("--evidence-debug-ui requires developer tools");
        }
#endif
        // Evidence presentation policy is a process-local diagnostic override,
        // just like scale and MSAA above. Reapply it after user settings are
        // initialized so later swapchain recreation cannot silently return an
        // explicitly non-vsync capture to the persisted/default FIFO policy.
        renderer_.setPresentationPolicy({
            .vsync = options.evidenceVsyncEnabled,
            .allowTearing = !options.evidenceVsyncEnabled,
        });
        presentationSettings_.lighting.ambientOcclusionEnabled =
            evidenceAmbientOcclusionEnabled_;
        if (evidenceWaterEnabled_) {
            presentationSettings_.water =
                RenderFrameData::defaultWaterRendering();
        }
        renderer_.setFrustumCullingEnabled(
            options.evidenceFrustumCullingEnabled);
    }
    presentationSettings_.applyTileScales(assetManifest_);
    presentationSettings_.geometry.processGroundGeometry =
        options.groundGeometryProcessingEnabled;
    presentationSettings_.geometry.smoothGroundRim = options.groundRimEnabled;
    presentationSettings_.geometry.groundChunks = options.groundChunksEnabled;
    presentationSettings_.geometry.groundChunkMeshoptimizer = options.groundChunkMeshoptimizerEnabled;
    evidenceGroundRimFixture_ = options.evidenceGroundRimFixture;
    presentationSettings_.normalize();
    presentation_.setAnimationCatalog(&animationCatalog_);
    screenPreviewPresentation_.setAnimationCatalog(&animationCatalog_);
    // The title needs no world assets. Speculative work starts after the first
    // draw, using the saved screen or the nearby selectable puzzles only.
    openTitleScreen();

#if SOKOBAN_ENABLE_DEBUG_UI
    const auto toolsStartupStart = std::chrono::steady_clock::now();
    DebugUi::initialize();
    tools_->initialize(
        SOKOBAN_SOURCE_LEVEL_DIR,
        SOKOBAN_SOURCE_ASSET_DIR,
        assetRoot_,
        campaign_.currentLevel(),
        campaign_.currentScreen(),
        assetManifest_,
        animationCatalog_);
    log::info(log::Category::Application)
        << "Asset startup phase (us): developer-tools="
        << std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - toolsStartupStart).count();

    DebugUi::addTab("Engine", [this] {
        int completedTargets = 0;
        for (LevelLocation target : campaign_.overworldTargets()) {
            completedTargets += playerProfile_.screenCompleted(target) ? 1 : 0;
        }
        tools_->applicationDebugUi.draw({
            .currentLevel = campaign_.currentLevel(),
            .currentScreen = campaign_.currentScreen(),
            .inOverworld = campaign_.inOverworld(),
            .completedSelectorTargets = completedTargets,
            .selectorTargetCount = static_cast<int>(
                campaign_.overworldTargets().size()),
            .level = level_,
            .gameplaySession = gameplaySession_,
            .input = input_,
            .renderer = renderer_,
            .settings = presentationSettings_,
            .audio = *audioSystem_,
            .saveDiagnostics = saveSlots_.progressDiagnostics(),
            .audioSettings = playerProfile_.settings.audio,
            .updateAudioSettings = [this](
                PlayerProfile::AudioSettings settings,
                bool persist) {
                applySettingsEffects(
                    settingsCoordinator_.applyAudioSettings(settings, persist));
            },
        });
    });
    DebugUi::addTab("Profiler", [this] {
        tools_->profilerDebugUi.draw(renderer_);
    });
    if (const std::optional<DevSession> session =
            loadDevSession(devSessionPath_)) {
        tools_->resumeOnLaunch = session->resumeOnLaunch;
    }
    DebugUi::addMenu("Session", [this] {
        const LevelEditor& editor = tools_->levelEditor;
        if ((editor.editingDocument() || editor.playingDraft()) &&
            ImGui::MenuItem("Resume Campaign")) {
            resumeCampaign();
        }
        tools_->drawSessionMenu();
    });
    DebugUi::addMenu("Camera", [this] {
        tools_->drawDetachedCameraMenu(
            window_.nativeHandle(),
            preparedRenderFrame_ ? &*preparedRenderFrame_ : nullptr);
    });
    DebugUi::addTopBarControl([this] {
        float speed = settingsCoordinator_.userSettings()
                          .gameplay.simulationSpeed;
        ImGui::SetNextItemWidth(180.0f);
        const bool changed = ImGui::SliderFloat(
            "Simulation speed",
            &speed,
            config::simulationSpeedSliderMinimum,
            config::simulationSpeedSliderMaximum,
            "%.3fx");
        const bool committed = ImGui::IsItemDeactivatedAfterEdit();
        if (changed || committed) {
            applySettingsEffects(
                settingsCoordinator_.applySimulationSpeed(
                    speed, committed));
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Ctrl-click to type any non-negative value, including above "
                "10x. Rendering and editor input remain at normal speed.");
        }
    });
    tools_->enableShaderHotReload(assetRoot_);
    DebugUi::addTab("Tuning", [this] {
        tools_->tuningDebugUi.draw();
    });
    DebugUi::addTab("Shaders", [this] {
        tools_->drawShaderHotReloadPanel(renderer_);
    });
    DebugUi::addTab("Log", [this] {
        tools_->logDebugUi.draw();
    });
    DebugUi::addTab("Asset Manifest", [this] {
        tools_->assetManifestDebugUi.draw(
            tools_->assetManifestEditor, *audioSystem_, window_.nativeHandle());
        tools_->drawManifestReloadStatus();
    });
    DebugUi::addTab("Level Editor", [this] {
        tools_->levelEditorDebugUi.draw(
            tools_->levelEditor,
            tools_->overworldMapEditor,
            tools_->splatPainter,
            settingsCoordinator_.userSettings().input,
            {
            .playDraft = [this](Level level) {
                startEditorDraft(std::move(level), true);
            },
            .resumeCampaign = [this] { resumeCampaign(); },
            .openGroundPainting = [this] {
                return tools_->openGroundPainting(
                    SOKOBAN_SOURCE_ASSET_DIR,
                    assetRoot_,
                    assetManifest_,
                    renderer_);
            },
            .createGroundSplatMap = [this] {
                return tools_->createGroundSplatMap(
                    SOKOBAN_SOURCE_ASSET_DIR,
                    assetRoot_,
                    assetManifest_,
                    renderer_);
            },
            .createGroundBlendMask = [this] {
                return tools_->createGroundBlendMask(
                    SOKOBAN_SOURCE_ASSET_DIR,
                    assetRoot_,
                    assetManifest_,
                    renderer_);
            },
            .prepareGroundBlendMask = [this](std::string_view textureName) {
                return tools_->prepareGroundBlendMask(textureName,
                    SOKOBAN_SOURCE_ASSET_DIR, assetRoot_, assetManifest_, renderer_);
            },
            .assetManifest = [this]() -> const AssetManifest& { return assetManifest_; },
            .tileThumbnail = [this](TileType tile) {
                // VkDescriptorSet is what ImGui's Vulkan backend uses as a
                // texture id; null means "no thumbnail, draw the swatch".
                return reinterpret_cast<uint64_t>(
                    renderer_.tileThumbnail(tile));
            },
            .bakeTileThumbnails = [this] {
                // Deferred, not run here: this callback executes inside the
                // debug UI's ImGui frame, and the bake begins ImGui and UI
                // frames of its own. Nesting them would trip ImGui's
                // frame-scope assertion.
                tools_->bakeThumbnailsRequested = true;
                return true;
            },
            .decorationMeshes = [this]()
                -> const std::vector<DecorationMeshCatalog::Entry>& {
                return tools_->decorationMeshCatalog.entries();
            },
            .decorationMeshStatus = [this]() -> const std::string& {
                return tools_->decorationMeshCatalog.status();
            },
            .refreshDecorationMeshes = [this] {
                (void)tools_->decorationMeshCatalog.refresh(
                    SOKOBAN_SOURCE_ASSET_DIR, assetManifest_);
            },
            .registerDecorationMesh = [this](
                const std::filesystem::path& relativePath) {
                return tools_->registerDecorationMesh(
                    SOKOBAN_SOURCE_ASSET_DIR,
                    assetRoot_,
                    relativePath,
                    assetManifest_,
                    renderer_);
            },
            });
        tools_->drawSolutionPanel();
    });
    DebugUi::addTab("Animation", [this] {
        if (tools_->animationCatalogDebugUi.draw(
                tools_->animationCatalogEditor,
                assetManifest_,
                tools_->animationPreviewDebugUi,
                renderer_)) {
            animationCatalog_ = tools_->animationCatalogEditor.catalog();
        }
    });
    if (evidenceDebugUi_) {
        DebugUi::requestTabFocus("Level Editor");
    }
#endif

    if (!SDL_AddEventWatch(
            simulationTimingEventWatch, &timingEventWatchState_)) {
        throw std::runtime_error(
            std::string("SDL_AddEventWatch failed: ") + SDL_GetError());
    }
}

Application::~Application()
{
    SDL_RemoveEventWatch(
        simulationTimingEventWatch, &timingEventWatchState_);

    // No longer waits for the world to go quiet.
    //
    // The gate was there because a snapshot taken mid-action could catch an
    // unreplayable half-transition. It does not: a snapshot normally holds the
    // committed state and its undo chain. If a later completed action already
    // used cells an older slide released, the snapshot first folds only those
    // completed slide legs into a copy of the chain. The remaining in-flight
    // action is lost on reload, but its momentum is still on disk, so ambient
    // motion simply plans the rest again.
    //
    // Under concurrency the world is rarely idle, so keeping the gate meant
    // saves quietly became rare exactly when there was most to lose. The one
    // Restored slide momentum recovers the completed action that caused it, so
    // the continuation still folds into the same undo entry.
    if (campaign_.gameLoaded()) {
        checkpointCurrentScreen(false);
    } else if (!playerProfile_.progressEmpty()) {
        persistProfile(true);
    }
    const SaveSlotManager::FlushResult persistence = saveSlots_.flush();
    if (persistence.progress.outcome ==
        AsyncSaveStore::PersistenceOutcome::RetryableFailure) {
        log::error(log::Category::Persistence)
            << persistence.progress.message;
    }
    if (persistence.settings.outcome ==
        AsyncSaveStore::PersistenceOutcome::RetryableFailure) {
        log::error(log::Category::Persistence)
            << persistence.settings.message;
    }
#if SOKOBAN_ENABLE_DEBUG_UI
    if (developerFilesWritable()) {
        tools_->flushSolutionStore();
        saveDevSession();
    }
    tools_->shutdownDetachedCamera(window_.nativeHandle());
    tools_->shutdownEditorCursors();
    DebugUi::clearTabs();
#endif
    renderer_.waitIdle();
}

#if SOKOBAN_ENABLE_DEBUG_UI
void Application::saveDevSession() const
{
    const LevelEditor& editor = tools_->levelEditor;
    DevSession session {
        .resumeOnLaunch = tools_->resumeOnLaunch,
        .editorDocument = editor.loadedDocumentPath(),
        .editingDocument = editor.editingDocument(),
        .activeLayer = static_cast<int>(editor.activeLayer()),
    };
    switch (editor.tool()) {
    case LevelEditor::Tool::Tiles:
        session.tool = "tiles";
        break;
    case LevelEditor::Tool::Decorations:
        session.tool = "decorations";
        break;
    case LevelEditor::Tool::TileDecorations:
        session.tool = "tile-decorations";
        break;
    case LevelEditor::Tool::Selectors:
        session.tool = "selectors";
        break;
    }
    try {
        sokoban::saveDevSession(devSessionPath_, session);
    } catch (const std::exception& error) {
        log::warning(log::Category::Editor)
            << "Could not save the developer session: " << error.what();
    }
}
#endif

void Application::applyLaunchRequest()
{
    bool continueGame = launchContinue_;
    std::filesystem::path editDocument = launchEditDocument_;
    std::optional<LevelLocation> startLocation;
    if (launchLevel_ >= 0) {
        startLocation = LevelLocation {
            .level = launchLevel_,
            .screen = launchScreen_,
        };
    }
#if SOKOBAN_ENABLE_DEBUG_UI
    std::optional<DevSession> session;
    const bool explicitRequest =
        launchShowTitle_ || continueGame || startLocation || !editDocument.empty();
    if (!explicitRequest) {
        session = loadDevSession(devSessionPath_);
        if (session && session->resumeOnLaunch) {
            continueGame = true;
            if (session->editingDocument) {
                editDocument = session->editorDocument;
            }
            log::info(log::Category::Application)
                << "Resuming the last developer session (Session menu, or "
                   "--title, to start at the title instead).";
        }
    }
#else
    if (startLocation || !editDocument.empty()) {
        log::warning(log::Category::Application)
            << "--level and --edit need a Debug build with developer tools; "
               "ignoring them.";
        startLocation.reset();
        editDocument.clear();
    }
#endif
    if (!continueGame && !startLocation && editDocument.empty()) {
        return;
    }

    // Continue exactly as the title's Continue does. A slot with no progress
    // has nothing to continue, so start it the way New Game would.
    if (playerProfile_.progressEmpty()) {
        startNewGame();
    } else {
        handleShellEvent(ShellTitleAction { title::Continue {} });
    }

#if SOKOBAN_ENABLE_DEBUG_UI
    if (startLocation) {
        if (!campaign_.screenExists(startLocation->level, startLocation->screen)) {
            log::warning(log::Category::Application)
                << "--level " << startLocation->level << " --screen "
                << startLocation->screen << " does not exist";
        } else {
            checkpointCurrentScreen(true);
            if (campaign_.startPuzzle(playerProfile_, *startLocation)) {
                loadCurrentScreen();
            }
        }
    }
    if (!editDocument.empty()) {
        LevelEditor& editor = tools_->levelEditor;
        std::filesystem::path path = editDocument;
        if (path.is_relative() && !std::filesystem::exists(path)) {
            // Accept paths relative to the repository as well as the
            // working directory: levels/level3/screen2.scr.
            path = std::filesystem::path(SOKOBAN_SOURCE_LEVEL_DIR).parent_path() /
                editDocument;
        }
        editor.selectDocument(path);
        if (editor.openDocument(path)) {
            if (session && session->editorDocument == editDocument) {
                if (session->tool == "decorations") {
                    editor.setTool(LevelEditor::Tool::Decorations);
                } else if (session->tool == "tile-decorations") {
                    editor.setTool(LevelEditor::Tool::TileDecorations);
                } else if (session->tool == "selectors") {
                    editor.setTool(LevelEditor::Tool::Selectors);
                } else {
                    editor.setTool(LevelEditor::Tool::Tiles);
                }
                editor.setActiveLayer(session->activeLayer);
            }
            editor.setEditingDocument(true);
            tools_->levelEditorDebugUi.initialize(editor);
            log::info(log::Category::Editor)
                << "Editing " << path.string() << " (layer "
                << editor.activeLayer() << ")";
        } else {
            log::warning(log::Category::Editor)
                << "Could not open " << path.string()
                << " for editing: " << editor.status();
        }
    }
#endif
}

#if SOKOBAN_ENABLE_DEBUG_UI
bool Application::bakeTileThumbnails()
{
    // The offline bake requests each icon's assets separately. Startup world
    // prefetches would also drain here and exhaust the shared upload ring.
    renderer_.cancelQueuedAssetPrefetches();
    return tools_->bakeTileThumbnails(
        renderer_,
        ui_,
        assetManifest_,
        presentationSettings_,
        animationCatalog_,
        SOKOBAN_SOURCE_ASSET_DIR,
        assetRoot_,
        window_.sizeInPixels());
}
#endif

bool Application::nativeTextFocused() const
{
    if (makerOpen_ && !makerPlaytest_ && !optionsMenu_.isOpen()) {
        return makerDraftDialog_ ? makerView_.draftNameFocused() : makerView_.tileSearchFocused();
    }
    return optionsMenuView_.controlsSearchFocused(optionsMenu_.state());
}

void Application::blurNativeTextInput()
{
    makerView_.blurTileSearch();
    makerView_.blurDraftName();
    optionsMenuView_.blurControlsSearch();
    nativeTextInput_.setTarget(nullptr, nullptr);
}

void Application::updateNativeTextInput()
{
    const bool makerInteractive = makerOpen_ && !makerPlaytest_ && !optionsMenu_.isOpen();
    const bool draftNameInteractive = makerInteractive && makerDraftDialog_.has_value();
    auto* target = makerInteractive
        ? (draftNameInteractive ? makerView_.activeDraftNameEdit() : makerView_.activeTextEdit())
        : optionsMenuView_.activeTextEdit(optionsMenu_.state());
    SDL_Rect area {};
    if (target) {
        const Vec2 size = window_.size();
        const Vec2 pixels = window_.sizeInPixels();
        if (pixels.x > 0.0f && pixels.y > 0.0f) {
            const UiRect caret = makerInteractive
                ? (draftNameInteractive ? makerView_.draftNameCaret() : makerView_.textCaret())
                : optionsMenuView_.textCaret();
            const Vec2 position = scaleUiPosition(caret.position, pixels, size);
            const Vec2 extent = scaleUiPosition(caret.size, pixels, size);
            area = {
                static_cast<int>(std::floor(position.x)),
                static_cast<int>(std::floor(position.y)),
                std::max(1, static_cast<int>(std::ceil(extent.x))),
                std::max(1, static_cast<int>(std::ceil(extent.y))),
            };
        }
    }
    nativeTextInput_.setTarget(window_.nativeHandle(), target, area);
}

bool Application::handleNativeTextInputEvent(const SDL_Event& event)
{
    const auto& options = optionsMenu_.state();
    const bool makerInteractive = makerOpen_ && !makerPlaytest_ && !options.open;
    const bool makerDialogInteractive = makerInteractive && makerDraftDialog_.has_value();
    if ((options.open || makerInteractive) && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event.button.button == SDL_BUTTON_LEFT &&
        event.button.windowID == SDL_GetWindowID(window_.nativeHandle())) {
        const Vec2 position = scaleUiPosition({ event.button.x, event.button.y },
            window_.size(), window_.sizeInPixels());
        const bool extend = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        const bool focusChanged = makerInteractive
            ? (makerDialogInteractive ? makerView_.handleDraftNamePress(ui_, position, extend)
                                      : makerView_.handleTileSearchPress(ui_, position, extend))
            : optionsMenuView_.handleControlsSearchPress(ui_, options, position, extend);
        if (focusChanged) {
            updateNativeTextInput();
        }
    }
    if (!makerDialogInteractive && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        event.key.scancode == SDL_SCANCODE_F &&
        (event.key.mod & SDL_KMOD_CTRL) != 0 &&
        (event.key.mod & (SDL_KMOD_ALT | SDL_KMOD_GUI)) == 0) {
        const auto* target = makerInteractive
            ? makerView_.activeTextEdit() : optionsMenuView_.activeTextEdit(options);
        if (!target || target->composition().text.empty()) {
            const bool focused = makerInteractive
                ? makerView_.focusTileSearch() : optionsMenuView_.focusControlsSearch(options);
            if (focused) {
                updateNativeTextInput();
                return true;
            }
        }
    }
    const auto result = nativeTextInput_.handleEvent(event);
    if (makerDialogInteractive && result == SdlTextInputResult::Submit) {
        makerDraftDialog_->inputAction = makerView_.submitDraftName();
    } else if (makerDialogInteractive && result == SdlTextInputResult::Blur &&
        event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        makerDraftDialog_->inputAction = { .command = MakerDraftDialogAction::Command::Cancel };
    } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
        result == SdlTextInputResult::Submit || result == SdlTextInputResult::Blur) {
        blurNativeTextInput();
    }
    return result != SdlTextInputResult::Ignored;
}

void Application::openMakerDraftDialog(MakerDraftDialogMode mode, std::string_view name)
{
    MakerDraftDialog dialog { .mode = mode, .pendingName = std::string(name) };
    if (mode == MakerDraftDialogMode::Load) {
        try {
            dialog.names = makerDraftStore_.list();
        } catch (const std::exception& error) {
            dialog.error = error.what();
        }
    }
    makerSession_->endStroke();
    blurNativeTextInput();
    if (mode == MakerDraftDialogMode::ResizeBoard) {
        const auto& editor = makerSession_->editor();
        makerView_.beginResizeBoardDialog(editor.documentWidth(), editor.documentHeight());
    } else {
        makerView_.beginDraftDialog(mode, dialog.pendingName);
    }
    ui_.cancelPointerInteraction();
    makerHover_.reset();
    makerDraftDialog_ = std::move(dialog);
}

void Application::closeMakerDraftDialog()
{
    if (showsMakerResizePreview(makerDraftDialog_->mode)) makerViewport_.reset();
    blurNativeTextInput();
    ui_.cancelPointerInteraction();
    makerDraftDialog_.reset();
}

void Application::handleMakerDraftDialogAction(const MakerDraftDialogAction& action)
{
    using Command = MakerDraftDialogAction::Command;
    if (action.command == Command::None) return;

    auto& dialog = *makerDraftDialog_;
    auto& maker = *makerSession_;
    if (action.command == Command::Cancel) {
        if (dialog.mode == MakerDraftDialogMode::Overwrite) {
            openMakerDraftDialog(MakerDraftDialogMode::Save, dialog.pendingName);
        } else if (dialog.mode == MakerDraftDialogMode::Discard) {
            openMakerDraftDialog(MakerDraftDialogMode::Load);
        } else if (dialog.mode == MakerDraftDialogMode::DiscardNew ||
                   dialog.mode == MakerDraftDialogMode::ConfirmCrop) {
            dialog.mode = dialog.mode == MakerDraftDialogMode::DiscardNew
                ? MakerDraftDialogMode::NewDraft : MakerDraftDialogMode::ResizeBoard;
            dialog.error.clear();
            makerView_.resumeBoardSizeDialog();
            ui_.cancelPointerInteraction();
        } else {
            closeMakerDraftDialog();
        }
        return;
    }

    if (dialog.mode == MakerDraftDialogMode::ResizeBoard ||
        dialog.mode == MakerDraftDialogMode::ConfirmCrop) {
        const bool cropConfirmed = dialog.mode == MakerDraftDialogMode::ConfirmCrop;
        const std::size_t removedTiles = cropConfirmed
            ? 0 : maker.resizeTileLossCount(action.width, action.height);
        if (removedTiles != 0) {
            openMakerDraftDialog(MakerDraftDialogMode::ConfirmCrop);
            makerDraftDialog_->removedTileCount = removedTiles;
        } else if (maker.resizeDraft(action.width, action.height, cropConfirmed)) {
            makerViewport_.reset();
            makerHover_.reset();
            closeMakerDraftDialog();
        } else {
            dialog.error = maker.status();
        }
        return;
    }

    if (dialog.mode == MakerDraftDialogMode::NewDraft ||
        dialog.mode == MakerDraftDialogMode::DiscardNew) {
        if (dialog.mode == MakerDraftDialogMode::NewDraft && maker.hasUnsavedChanges()) {
            openMakerDraftDialog(MakerDraftDialogMode::DiscardNew);
        } else if (maker.newDraft(action.width, action.height)) {
            makerViewport_.reset();
            makerHover_.reset();
            closeMakerDraftDialog();
        } else {
            dialog.error = maker.status();
        }
        return;
    }

    const std::string_view name = dialog.mode == MakerDraftDialogMode::Overwrite ||
        dialog.mode == MakerDraftDialogMode::Discard ? dialog.pendingName : action.name;
    if (dialog.mode == MakerDraftDialogMode::Save || dialog.mode == MakerDraftDialogMode::Overwrite) {
        const bool overwrite = dialog.mode == MakerDraftDialogMode::Overwrite ||
            name == maker.draftName();
        switch (maker.saveDraft(makerDraftStore_, name, overwrite)) {
        case MakerSession::SaveDraftResult::Saved:
            closeMakerDraftDialog();
            return;
        case MakerSession::SaveDraftResult::NameExists:
            openMakerDraftDialog(MakerDraftDialogMode::Overwrite, name);
            return;
        case MakerSession::SaveDraftResult::Failed:
            break;
        }
    } else if (dialog.mode == MakerDraftDialogMode::Load && maker.hasUnsavedChanges()) {
        openMakerDraftDialog(MakerDraftDialogMode::Discard, name);
        return;
    } else if (maker.loadDraft(makerDraftStore_, name)) {
        makerViewport_.reset();
        makerHover_.reset();
        closeMakerDraftDialog();
        return;
    }
    dialog.error = maker.status();
}

void Application::startMakerPlaytest()
{
    auto level = makerSession_->buildPlaytestLevel();
    if (!level) return;

    auto& playtest = makerPlaytest_.emplace();
    playtest.level = std::move(*level);
    playtest.session.setStepDurationSeconds(gameplaySession_.stepDurationSeconds());
    playtest.session.setStepRates(gameplaySession_.stepRates());
    playtest.session.reset(playtest.level);
    playtest.presentation.setAnimationCatalog(&animationCatalog_);
    playtest.presentation.resetEntities(playtest.session.state());
    const CameraAngles angles = playtest.level.cameraAngles().value_or(CameraAngles {});
    playtest.presentation.updateCameraPitch(angles.pitchDegrees, 0.0f, 0.0f);
    playtest.presentation.updateCameraYaw(angles.yawDegrees, 0.0f, 0.0f);
    playtest.solved = rules::isAtUnlockedEnd(playtest.level, playtest.session.state());
    renderer_.ensureAssets(renderAssetRequirementsForLevel(
        playtest.level, assetManifest_, std::nullopt, &animationCatalog_));
    audioSystem_->setAtmosphericLevel(playtest.level);
    blurNativeTextInput();
    ui_.cancelPointerInteraction();
    makerViewport_.reset();
    makerHover_.reset();
}

void Application::returnToMaker()
{
    makerPlaytest_.reset();
    audioSystem_->setAtmosphericLevel(level_);
    ui_.cancelPointerInteraction();
    makerViewport_.reset();
    makerHover_.reset();
}

void Application::drawLecternDialog(
    LecternDialog& dialog, const Level& level, GridPosition3 cell, Vec2 viewport)
{
    if (const auto* lectern = level.lecternAt(cell)) {
        dialog.draw(ui_, viewport, lectern->text, {
            .bindings = &input_.bindings(),
            .prompts = &inputPrompts_,
            .deviceClass = input_.activeDevice() == ActiveInputDevice::Gamepad
                ? BindingDeviceClass::Gamepad : BindingDeviceClass::Keyboard,
            .gamepad = input_.activeGamepadPresentation(),
        });
    }
}

void Application::drawMakerWorkspace(
    Vec2 viewport, const InputRouter::Frame& input, std::optional<Vec2> wheelPosition)
{
    const float scale = std::clamp(
        std::max(SDL_GetWindowDisplayScale(window_.nativeHandle()), viewport.y / 1080.0f),
        1.0f, 3.0f);
    const MakerLayout layout = MakerView::layout(viewport, scale);
    const UiRect scene = fitSceneViewport(layout.board, viewport);
    ui_.setSceneViewport(scene);
    if (makerPlaytest_) {
        if (!optionsMenu_.isOpen()) {
            if (makerView_.drawPlaytest(ui_, layout, makerPlaytest_->solved) ==
                MakerCommand::ReturnToMaker) {
                returnToMaker();
            } else if (const auto cell = makerPlaytest_->session.readingLectern()) {
                drawLecternDialog(makerPlaytest_->lecternDialog, makerPlaytest_->level,
                    *cell, viewport);
            }
        }
        return;
    }
    // The previous prepared frame has the projection used by the scene the
    // pointer actually sees. After a resize, wait for the new layout to draw.
    const bool canPick = makerViewport_ == scene && preparedRenderFrame_.has_value();
    makerViewport_ = scene;

    MakerSession& maker = *makerSession_;
    if (optionsMenu_.isOpen()) {
        maker.endStroke();
        makerHover_.reset();
        return;
    }
    const auto pick = [&](Vec2 position) -> std::optional<GridPosition> {
        if (canPick) {
            if (const auto cell = renderer_.pickIsoGridCell(
                    *preparedRenderFrame_, position, scene)) {
                return GridPosition { cell->x, cell->y };
            }
        }
        return std::nullopt;
    };
    const auto hovered = makerDraftDialog_ ? std::nullopt : pick(ui_.mousePosition());
    const LevelEditor& editor = maker.editor();
    makerHover_ = hovered ? maker.previewCell(*hovered) : std::nullopt;
    const auto assets = renderer_.assetLoadingStats();
    const MakerViewInput viewInput {
        .wheelY = input.pointer.wheel.y,
        .wheelPosition = wheelPosition,
        .navigation = input.maker,
        .extendTextSelection = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0,
        .interactive = !makerDraftDialog_,
    };
    const MakerViewAction action = makerView_.draw(ui_, layout, {
        .brush = editor.selectedTile(),
        .quarterTurns = editor.selectedTileQuarterTurns(),
        .layer = editor.activeLayer(),
        .layerCount = editor.documentDepth(),
        .erasing = maker.tool() == MakerSession::Tool::Erase,
        .canUndo = editor.canUndo(),
        .canRedo = editor.canRedo(),
        .hovered = makerHover_,
        .status = maker.status().empty() && (assets.pendingModels || assets.pendingTextures)
            ? "Loading board assets" : maker.status(),
        .draftName = maker.draftName(),
        .unsavedChanges = maker.hasUnsavedChanges(),
    }, viewInput);
    if (auto& dialog = makerDraftDialog_) {
        MakerViewInput dialogInput = viewInput;
        const bool resizePreview = showsMakerResizePreview(dialog->mode);
        if (dialog->mode == MakerDraftDialogMode::NewDraft ||
            dialog->mode == MakerDraftDialogMode::DiscardNew || resizePreview) {
            dialogInput.navigation.activate |= input_.keyPressed(SDL_SCANCODE_RETURN) ||
                input_.keyPressed(SDL_SCANCODE_KP_ENTER);
        }
        const auto pointerAction = makerView_.drawDraftDialog(ui_, viewport, scale, {
            .mode = dialog->mode,
            .names = dialog->names,
            .error = dialog->error,
            .pendingName = dialog->pendingName,
            .removedTileCount = dialog->removedTileCount,
            .resizePreviewPanel = resizePreview ? std::optional { layout.palette } : std::nullopt,
        }, dialogInput);
        // Dismiss after drawing the modal so the frame's pointer edges never reach the board.
        const auto inputAction = std::exchange(dialog->inputAction, {});
        handleMakerDraftDialogAction(inputAction.command != MakerDraftDialogAction::Command::None
            ? inputAction : pointerAction);
        return;
    }

    switch (action.command) {
    case MakerCommand::None: break;
    case MakerCommand::Back:
        handleShellEvent(ShellBackPressed {});
        return;
    case MakerCommand::Paint: maker.setTool(MakerSession::Tool::Paint); break;
    case MakerCommand::Erase: maker.setTool(MakerSession::Tool::Erase); break;
    case MakerCommand::Rotate: maker.rotateTile(); break;
    case MakerCommand::LayerDown: maker.stepLayer(-1); break;
    case MakerCommand::LayerUp: maker.stepLayer(1); break;
    case MakerCommand::Undo:
    case MakerCommand::Redo: {
        const auto width = editor.documentWidth();
        const auto height = editor.documentHeight();
        if (action.command == MakerCommand::Undo) maker.undo();
        else maker.redo();
        if (editor.documentWidth() != width || editor.documentHeight() != height) {
            makerViewport_.reset();
            makerHover_.reset();
            return;
        }
        break;
    }
    case MakerCommand::NewDraft:
        openMakerDraftDialog(MakerDraftDialogMode::NewDraft);
        return;
    case MakerCommand::ResizeBoard:
        openMakerDraftDialog(MakerDraftDialogMode::ResizeBoard);
        return;
    case MakerCommand::Save:
        openMakerDraftDialog(MakerDraftDialogMode::Save, maker.draftName());
        return;
    case MakerCommand::Load:
        openMakerDraftDialog(MakerDraftDialogMode::Load);
        return;
    case MakerCommand::TestPlay:
        startMakerPlaytest();
        return;
    case MakerCommand::ReturnToMaker: break;
    }
    if (action.tile) {
        maker.selectTile(*action.tile);
    }

    const auto drag = makerView_.tileSearchFocused()
        ? std::nullopt : ui_.dragPosition("maker.board", scene);
    if (drag) {
        if (const auto press = ui_.mousePressPosition()) {
            maker.endStroke();
            if (const auto cell = pick(*press)) maker.beginStroke(*cell);
        }
        maker.continueStroke(pick(*drag));
    }
    if (maker.strokeActive() && (!drag || !ui_.mouseDown())) {
        maker.endStroke();
    }
    makerHover_ = hovered ? maker.previewCell(*hovered) : std::nullopt;
}

// One UI frame: sizes, the pointer mapping, every panel and overlay that can
// draw, and the ImGui/UI frame that brackets them.
//
// Returns whether the developer workspace is visible, because the renderer
// needs the same answer a few lines later - that one value is the only thing
// this phase leaves behind, which is what made it liftable out of a 281-line
// loop body.
bool Application::drawUiFrame(
    const InputRouter::Frame& routedInput, [[maybe_unused]] float dt)
{
    const auto uiStart = std::chrono::steady_clock::now();
    const Vec2 windowSize = window_.size();
    const Vec2 pixelSize = window_.sizeInPixels();
#if SOKOBAN_ENABLE_DEBUG_UI
    bool developerWorkspaceVisible =
        (evidenceOutputDirectory_.empty() || evidenceDebugUi_) &&
        !shellMenuOpen();
    if (!developerWorkspaceVisible) {
        tools_->releaseDetachedCameraMouse(window_.nativeHandle());
        renderer_.setGameViewportDisplay(std::nullopt);
    }
#else
    constexpr bool developerWorkspaceVisible = false;
#endif
    const auto mapPointer = [&](Vec2 position) {
        return renderer_.mapPointerToGameViewport(
            scaleUiPosition(position, windowSize, pixelSize), pixelSize);
    };
    const auto mapPointerEdge = [&](std::optional<Vec2> position) -> std::optional<Vec2> {
        return position ? std::optional { mapPointer(*position) } : std::nullopt;
    };

    ui_.beginFrame(pixelSize, {
        .position = mapPointer(routedInput.pointer.position),
        .down = routedInput.pointer.primaryDown,
        .pressedAt = mapPointerEdge(routedInput.pointer.primaryPressedAt),
        .releasedAt = mapPointerEdge(routedInput.pointer.primaryReleasedAt),
    });

    const auto debugFrameStart = std::chrono::steady_clock::now();
    renderer_.beginDebugUiFrame();
    const auto workspaceStart = std::chrono::steady_clock::now();
#if SOKOBAN_ENABLE_DEBUG_UI
    if (developerWorkspaceVisible) {
        const VkExtent2D gameExtent = renderer_.renderExtent();
        const DebugUi::DrawResult workspace = DebugUi::draw({
            .texture = renderer_.gameViewportTexture(),
            .width = gameExtent.width,
            .height = gameExtent.height,
        });
        if (workspace.gameplayFullWindow) {
            // Bypass the docked texture view so the renderer can present the
            // game directly across the full swapchain client area.
            developerWorkspaceVisible = false;
            renderer_.setGameViewportDisplay(std::nullopt);
        } else if (workspace.viewportWidth > 0.0f &&
            workspace.viewportHeight > 0.0f) {
            renderer_.setGameViewportDisplay(
                VulkanRenderer::GameViewportDisplay {
                    .position = {
                        workspace.viewportX,
                        workspace.viewportY,
                    },
                    .size = {
                        workspace.viewportWidth,
                        workspace.viewportHeight,
                    },
                    .hovered = workspace.viewportHovered,
                    .focused = workspace.viewportFocused,
                });
        } else {
            renderer_.setGameViewportDisplay(std::nullopt);
        }
    } else {
        renderer_.setGameViewportDisplay(std::nullopt);
    }
#endif
    const auto overlaysStart = std::chrono::steady_clock::now();
    if (!makerOpen_) {
        if (const auto cell = gameplaySession_.readingLectern(); cell && !shellMenuOpen()) {
            drawLecternDialog(lecternDialog_, level_, *cell, pixelSize);
        } else if (screenPreviewActive_) {
            drawScreenPreviewOverlay(pixelSize);
        } else {
            drawSelectorPrompt(
                preparedRenderFrame_ ? &*preparedRenderFrame_ : nullptr);
        }
#if SOKOBAN_ENABLE_DEBUG_UI
        tools_->drawShaderHotReloadOverlay(renderer_);
        tools_->drawDraftExitConfirmation();
        tools_->drawBrushPreview(
            renderer_,
            preparedRenderFrame_ ? &*preparedRenderFrame_ : nullptr);
        tools_->drawDecorationGizmo(
            renderer_,
            preparedRenderFrame_ ? &*preparedRenderFrame_ : nullptr,
            input_.mousePosition(),
            window_.size(),
            window_.sizeInPixels(),
            renderer_.wantsMouseCapture());
        tools_->drawSelectorLabels(
            renderer_,
            preparedRenderFrame_ ? &*preparedRenderFrame_ : nullptr);
#endif
    }

    if (makerOpen_) {
        drawMakerWorkspace(pixelSize, routedInput, mapPointerEdge(routedInput.pointer.wheelPosition));
    } else if (titleScreen_.isOpen() && optionsMenu_.isOpen()) {
        TitleScreen::drawBackground(ui_, pixelSize);
    } else if (const std::optional<TitleAction> titleAction = titleScreen_.draw(
                   ui_, pixelSize, routedInput.title)) {
        handleShellEvent(ShellTitleAction { *titleAction });
    }

    if (const auto query = optionsMenuView_.controlsSearchQuery(optionsMenu_.state());
        query && *query != optionsMenu_.state().controlsFilter) {
        (void)optionsMenu_.dispatch(settingsCoordinator_.userSettings(),
            options::intent::SetControlsFilter { std::string(*query) });
    }
    if (const std::optional<OptionsAction> optionsAction =
            optionsMenu_.handleInput(
                settingsCoordinator_.userSettings(),
                routedInput.options)) {
        handleShellEvent(ShellOptionsAction { *optionsAction });
    }
    const GamepadPresentation gamepadPresentation =
        input_.activeGamepadPresentation();
    if (const std::optional<OptionsMenuIntent> intent =
            optionsMenuView_.draw(
                ui_,
                pixelSize,
                optionsMenu_.state(),
                settingsCoordinator_.userSettings(),
                &inputPrompts_,
                &gamepadPresentation,
                {
                    .wheelY = routedInput.pointer.wheel.y,
                    .wheelPosition = mapPointerEdge(routedInput.pointer.wheelPosition),
                    .revealSelection = routedInput.options.up ||
                        routedInput.options.down || routedInput.options.left ||
                        routedInput.options.right || routedInput.options.confirm,
                    .extendTextSelection = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0,
                })) {
        if (const std::optional<OptionsAction> optionsAction =
                optionsMenu_.dispatch(
                    settingsCoordinator_.userSettings(),
                    *intent)) {
            handleShellEvent(ShellOptionsAction { *optionsAction });
        }
    }
    updateNativeTextInput();
    if (!ui_.drawData().sceneViewport) {
        drawAssetLoadingOverlay(pixelSize);
    }
#if SOKOBAN_ENABLE_DEBUG_UI
    tools_->animationPreviewDebugUi.update(dt, renderer_);
#endif
    ui_.endFrame();
    if (!startupReported_) {
        const auto elapsedUs = [](auto start, auto end) {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                end - start).count();
        };
        log::info(log::Category::Application)
            << "UI first-frame work (us): begin="
            << elapsedUs(uiStart, debugFrameStart)
            << " debug-frame=" << elapsedUs(debugFrameStart, workspaceStart)
            << " workspace=" << elapsedUs(workspaceStart, overlaysStart)
            << " overlays=" << elapsedUs(overlaysStart, std::chrono::steady_clock::now());
    }
    return developerWorkspaceVisible;
}

// The end of a smoke run: the evidence capture one frame early, the capture
// finish, the residency and phase-timing report, and the stop.
void Application::finishSmokeRunIfDue(std::uint64_t renderedFrames)
{
    if (!evidenceOutputDirectory_.empty() &&
        renderedFrames + 1 == smokeFrames_) {
        captureEvidenceScene();
    }
    if (smokeFrames_ > 0 && renderedFrames >= smokeFrames_) {
        if (!evidenceOutputDirectory_.empty()) {
            finishEvidenceCapture();
        }
        const VulkanModelResources::LoadingStats assetStats =
            renderer_.assetLoadingStats();
        log::info(log::Category::Rendering)
            << "Texture residency "
            << assetStats.textureResidencyBytes << " / "
            << assetStats.textureResidencyBudgetBytes
            << " bytes; mip levels "
            << assetStats.residentTextureMipLevels << " / "
            << assetStats.availableTextureMipLevels << "; "
            << assetStats.mipDegradedTextures
            << " reduced textures omitted "
            << assetStats.mipOmittedBytes << " bytes.";
        // Eviction counters were reachable only from the debug panel,
        // which a headless smoke run never draws - so the two numbers that
        // say whether residency was under pressure, and whether it coped,
        // were missing from exactly the captures taken to find out.
        log::info(log::Category::Rendering)
            << "Residency evictions " << assetStats.residencyEvictions
            << "; capacity blocks " << assetStats.residencyBudgetBlocks
            << " (" << assetStats.residencyOversizedBlocks
            << " larger than the budget, "
            << assetStats.residencyMipPlanBlocks
            << " no mip tail fits, "
            << assetStats.residencyNoVictimBlocks
            << " nothing evictable)"
            << "; dropped draws " << assetStats.droppedDrawInstances
            << ", dropped skinned poses "
            << assetStats.droppedSkinningInstances
            << "; model residency " << assetStats.modelResidencyBytes
            << " / " << assetStats.modelResidencyBudgetBytes
            << " bytes (peak " << assetStats.modelResidencyPeakBytes
            << ").";
        log::info(log::Category::Rendering)
            << "Asset transient memory " << assetStats.transientAssetBytes
            << " bytes (peak " << assetStats.transientAssetPeakBytes
            << "); prepared payload " << assetStats.preparedAssetBytes
            << " + " << assetStats.activeDecodeReservationBytes
            << " decoding bytes / "
            << assetStats.preparedAssetBudgetBytes
            << " budget; prepared-budget deferrals "
            << assetStats.preparedBudgetDeferrals
            << ", oversized starts " << assetStats.oversizedAssetStarts
            << "; skinned packing " << assetStats.skinnedPackingPasses
            << " passes, " << assetStats.skinnedPackingAllocations
            << " allocations, " << assetStats.skinnedPackingTemporaryBytes
            << " cumulative temporary bytes (peak "
            << assetStats.skinnedPackingPeakBytes << "), "
            << assetStats.skinnedUploadBytes << " upload bytes"
            << "; queued source bytes "
            << assetStats.modelStages.queuedSourceBytes << " models, "
            << assetStats.textureStages.queuedSourceBytes << " textures, "
            << assetStats.animationStages.queuedSourceBytes << " animations"
            << "; decoding source bytes "
            << assetStats.modelStages.decodingSourceBytes << " models, "
            << assetStats.textureStages.decodingSourceBytes << " textures, "
            << assetStats.animationStages.decodingSourceBytes << " animations"
            << "; residency deferrals "
            << assetStats.modelStages.residencyDeferrals << " models over "
            << assetStats.modelStages.residencyDeferredMicroseconds
            << " us, " << assetStats.textureStages.residencyDeferrals
            << " textures over "
            << assetStats.textureStages.residencyDeferredMicroseconds
            << " us.";
        log::info(log::Category::Application)
            << "Smoke run finished after " << renderedFrames
            << " frames.";
        running_ = false;
    }
}

bool Application::run()
{
    startupConstructionMicroseconds_ =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - startupStarted_).count();
    const auto launchStart = std::chrono::steady_clock::now();
    if (smokeFrames_ > 0) {
        // Nobody is here to press New Game, and the title screen draws no
        // world behind it, so a smoke run that stayed on the title would
        // never record a tile, model, shadow or AO pass.
        log::info(log::Category::Application)
            << "Smoke run: starting a new game and rendering "
            << smokeFrames_ << " frames.";
        startNewGame();
        if (evidenceLevel_ >= 0) {
            const LevelLocation location { evidenceLevel_, evidenceScreen_ };
            if (!campaign_.screenExists(location.level, location.screen) ||
                !campaign_.startPuzzle(playerProfile_, location)) {
                throw std::invalid_argument("Evidence puzzle screen does not exist");
            }
            loadCurrentScreen();
        }
    } else if (evidenceOutputDirectory_.empty()) {
        applyLaunchRequest();
    }
    const auto launchFinished = std::chrono::steady_clock::now();
    std::uint64_t renderedFrames = 0;
    CpuProfiler::instance().setCurrentThreadName("Main");
    auto previousFrameStart = std::chrono::steady_clock::now();
    while (running_) {
        const auto applicationFrameStart = std::chrono::steady_clock::now();
        if (renderedFrames > 0 && !evidenceSceneCaptured_) {
            applicationIntervalTelemetry_.record(
                std::chrono::duration<double, std::milli>(
                    applicationFrameStart - previousFrameStart).count());
        }
        previousFrameStart = applicationFrameStart;
        CpuProfiler::instance().beginFrame(renderedFrames + 1);
        framePacer_.beginFrame();
#if SOKOBAN_ENABLE_DEBUG_UI
        // Serviced here, between frames, where no ImGui or UI frame is open.
        if (tools_->bakeThumbnailsRequested) {
            tools_->bakeThumbnailsRequested = false;
            (void)bakeTileThumbnails();
            // The bake drives its own frames. Do not use an older gameplay
            // frame for editor picking or gizmo projection afterward.
            preparedRenderFrame_.reset();
            // The files on disk changed, so drop what the palette already
            // loaded; it would otherwise keep showing the old pictures until
            // the next launch.
            renderer_.invalidateTileThumbnails();
        }
        tools_->serviceShaderHotReload(renderer_);
        observeEditorLevelPublications();
        if (renderedFrames > 0) {
            serviceSourceWatcher();
        }
        tools_->serviceSolutionStore();
#endif
        const auto eventsStart = std::chrono::steady_clock::now();
        input_.beginFrame();

        {
            SOKOBAN_PROFILE_SCOPE("Application.Events");
            SDL_Event event {};
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                    ui_.cancelPointerInteraction();
                    if (makerOpen_) makerSession_->endStroke();
                }
                if (handleNativeTextInputEvent(event)) {
                    continue;
                }
                renderer_.handleEvent(event);
#if SOKOBAN_ENABLE_DEBUG_UI
                const bool detachedCameraConsumed =
                    tools_->handleDetachedCameraEvent(
                        event,
                        window_.nativeHandle(),
                        !shellMenuOpen() && evidenceOutputDirectory_.empty(),
                        renderer_.wantsMouseCapture());
                if (detachedCameraConsumed) {
                    continue;
                }
#endif
                InputRouter::EventContext eventContext {
                    .bindingCapture = optionsMenu_.capturingBinding(),
                    .shellMenuOpen = shellMenuOpen(),
                    .keyboardCaptured = renderer_.wantsKeyboardCapture(),
                    .mouseCaptured = renderer_.wantsMouseCapture(),
                };
#if SOKOBAN_ENABLE_DEBUG_UI
                eventContext.editorEditing =
                    tools_->levelEditor.editingDocument();
                if (tools_->detachedCameraCapturingMouse()) {
                    eventContext.keyboardCaptured = false;
                    eventContext.mouseCaptured = false;
                }
#endif
                const InputRouter::EventResult routedEvent =
                    inputRouter_.routeEvent(event, input_, eventContext);
                if (routedEvent.bindingCandidate) {
                    if (const std::optional<OptionsAction> action =
                            optionsMenu_.provideBindingCandidate(
                                settingsCoordinator_.userSettings(),
                                *routedEvent.bindingCandidate)) {
                        handleShellEvent(ShellOptionsAction { *action });
                    }
                }
                if (routedEvent.closeRequested) {
                    handleShellEvent(ShellCloseRequested {});
                }
            }
        }

        const auto eventsFinished = std::chrono::steady_clock::now();
        const InputRouter::BackAction backAction = !makerOpen_ && levelTransition_.active()
            ? InputRouter::BackAction::None
            : inputRouter_.backAction(input_, inputRoutingContext());
        switch (backAction) {
        case InputRouter::BackAction::CloseDraftConfirmation:
#if SOKOBAN_ENABLE_DEBUG_UI
            tools_->draftExitConfirmationOpen = false;
#endif
            break;
        case InputRouter::BackAction::OpenDraftConfirmation:
#if SOKOBAN_ENABLE_DEBUG_UI
            tools_->draftExitConfirmationOpen = true;
#endif
            break;
        case InputRouter::BackAction::CancelDecorationPlacement:
#if SOKOBAN_ENABLE_DEBUG_UI
            tools_->levelEditor.cancelDecorationPlacement();
#endif
            break;
        case InputRouter::BackAction::ShellBack:
            if (makerDraftDialog_ && !optionsMenu_.isOpen()) {
                makerDraftDialog_->inputAction = { .command = MakerDraftDialogAction::Command::Cancel };
            } else if (nativeTextFocused()) {
                blurNativeTextInput();
            } else if (makerPlaytest_ && !optionsMenu_.isOpen()) {
                returnToMaker();
            } else {
                handleShellEvent(ShellBackPressed {});
            }
            break;
        case InputRouter::BackAction::None:
            break;
        }

        const InputRouter::Frame routedInput =
            inputRouter_.routeFrame(input_, inputRoutingContext());
#if SOKOBAN_ENABLE_DEBUG_UI
        handleDraftPlaybackShortcuts(routedInput);
#endif
        const float measuredDt = frameTimer_.tick(simulationTiming_);
        // Animated evidence advances by a fixed step, independent of GPU speed.
        // Otherwise freeze the scene for the existing image A/B workflow.
        const float wallClockDt = evidenceOutputDirectory_.empty()
            ? measuredDt
            : (evidenceAnimate_ && renderedFrames + 1 < smokeFrames_ ? 1.0f / 60.0f : 0.0f);
        const float dt = SimulationTiming::scaledDelta(
            wallClockDt,
            settingsCoordinator_.userSettings().gameplay.simulationSpeed);
        const auto updateStart = std::chrono::steady_clock::now();
        {
            SOKOBAN_PROFILE_SCOPE("Application.Update");
            update(
                dt,
                routedInput,
                preparedRenderFrame_ ? &*preparedRenderFrame_ : nullptr);
        }
        const auto uiStart = std::chrono::steady_clock::now();
        if (!evidenceSceneCaptured_) {
            applicationUpdateTelemetry_.record(
                std::chrono::duration<double, std::milli>(uiStart - updateStart).count());
        }
        bool developerWorkspaceVisible = false;
        {
            SOKOBAN_PROFILE_SCOPE("Application.UI");
            // Menus and developer tools remain responsive in wall-clock time,
            // independent of the selected gameplay simulation speed.
            developerWorkspaceVisible = drawUiFrame(routedInput, wallClockDt);
        }
        const auto buildStart = std::chrono::steady_clock::now();
        if (!evidenceSceneCaptured_) {
            applicationUiTelemetry_.record(
                std::chrono::duration<double, std::milli>(buildStart - uiStart).count());
        }
        {
            SOKOBAN_PROFILE_SCOPE("Application.Build render frame");
            RenderFrameData frame = buildRenderFrame(routedInput.editor);
            auto preview = buildScreenPreviewRenderFrame(
                renderFrameArenas_[renderFrameArenaIndex_]);
            preparedRenderFrame_ = renderer_.prepareFrame(
                std::move(frame), std::move(preview));
        }
        if (!evidenceSceneCaptured_) {
            applicationBuildTelemetry_.record(
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - buildStart).count());
        }
        const auto renderStart = std::chrono::steady_clock::now();
        {
            SOKOBAN_PROFILE_SCOPE("Application.Render");
            renderer_.drawFrame(
                *preparedRenderFrame_,
                ui_.drawData(),
                developerWorkspaceVisible);
        }
        // Publish CPU work before deliberate pacing and evidence readback.
        // The application telemetry also includes the profiler's processing.
        CpuProfiler::instance().endFrame();
        if (!evidenceSceneCaptured_) {
            applicationFrameTelemetry_.record(
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - applicationFrameStart).count());
        }
        if (!startupReported_) {
            startupReported_ = true;
            log::info(log::Category::Application)
                << "Application startup phases (us): construction="
                << startupConstructionMicroseconds_
                << " first-frame="
                << std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - startupStarted_)
                       .count();
            const auto elapsedUs = [](auto start, auto end) {
                return std::chrono::duration_cast<std::chrono::microseconds>(
                    end - start).count();
            };
            log::info(log::Category::Application)
                << "Application first-frame work (us): launch="
                << elapsedUs(launchStart, launchFinished)
                << " services=" << elapsedUs(applicationFrameStart, eventsStart)
                << " events=" << elapsedUs(eventsStart, eventsFinished)
                << " input=" << elapsedUs(eventsFinished, updateStart)
                << " update=" << elapsedUs(updateStart, uiStart)
                << " ui=" << elapsedUs(uiStart, buildStart)
                << " build=" << elapsedUs(buildStart, renderStart)
                << " render=" << elapsedUs(renderStart, std::chrono::steady_clock::now());
            const RenderStats stats = renderer_.renderStats();
            log::info(log::Category::Application)
                << "Renderer first-frame work (ms): scheduling="
                << stats.assetSchedulingTiming.latestMilliseconds
                << " fence=" << stats.frameFenceWaitTiming.latestMilliseconds
                << " maintenance=" << stats.assetMaintenanceTiming.latestMilliseconds
                << " acquire=" << stats.imageAcquisitionTiming.latestMilliseconds
                << " commands=" << stats.commandRecordingTiming.latestMilliseconds
                << " present=" << stats.submitPresentTiming.latestMilliseconds;
        }
        ++renderedFrames;
        if (renderer_.hasFatalFailure()) {
            log::error(log::Category::Application)
                << "Rendering stopped: " << renderer_.fatalFailureMessage();
            running_ = false;
        } else {
            finishSmokeRunIfDue(renderedFrames);
        }
        if (running_) {
            serviceUpcomingAssetPrefetch();
            const auto pacingStart = std::chrono::steady_clock::now();
            framePacer_.pace();
            applicationPacingTelemetry_.record(
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - pacingStart).count());
        }
    }
#if SOKOBAN_ENABLE_DEBUG_UI
    observeEditorLevelSaves();
#endif
    return !renderer_.hasFatalFailure();
}

void Application::updateMakerPlaytest(float dt, const InputRouter::Frame& input)
{
    if (optionsMenu_.isOpen()) {
        audioSystem_->update(dt, false, false);
        return;
    }

    auto& playtest = *makerPlaytest_;
    auto& session = playtest.session;
    auto& presentation = playtest.presentation;
    const CameraAngles angles = playtest.level.cameraAngles().value_or(CameraAngles {});
    presentation.advanceClocks(dt, session.moving() && session.activeAction().reversed);
    presentation.updateCameraPitch(input.showTopDownView ? 0.0f : angles.pitchDegrees,
        dt, config::cameraPitchTransitionSeconds);

    const auto reading = session.readingLectern();
    const auto result = GameplayLoop::update(
        playtest.level, session, presentation, input.gameplay, dt, true);
    if (reading && (input.gameplay.interactPressed || input.gameplay.dismissPressed)) {
        audioSystem_->update(dt, false, false);
        return;
    }
    if (session.readingLectern() && session.readingLectern() != reading) {
        playtest.lecternDialog.reset();
    }
    if (!reading || !session.readingLectern() || session.moving() || result.stateCommitted) {
        playtest.particles.update(dt);
    }
    if (input.gameplay.restartPressed && !reading) {
        playtest.solved = false;
    }
    playtest.solved |= result.draftSolved;
    presentGameplayEffects(playtest.level, session, presentation, playtest.particles,
        playtest.turretFlames, result, input.gameplay, dt);
    updateGameplayAudio(dt, session, presentation);
}

void Application::update(
    float dt,
    const InputRouter::Frame& input,
    const VulkanRenderer::PreparedFrame* previousRenderFrame)
{
    screenPreviewActive_ = false;
    if (makerPlaytest_) {
        updateMakerPlaytest(dt, input);
        return;
    }
#if !SOKOBAN_ENABLE_DEBUG_UI
    (void)previousRenderFrame;
#endif

    const bool reversed =
        gameplaySession_.moving() &&
        gameplaySession_.activeAction().reversed;
    presentation_.advanceClocks(dt, reversed);
#if SOKOBAN_ENABLE_DEBUG_UI
    const bool editorDraftPlaying = tools_->levelEditor.playingDraft();
    if (!editorDraftPlaying && solveCapture_ && solveCapture_->playingDraft) {
        solveCapture_.reset();
    }
#else
    constexpr bool editorDraftPlaying = false;
#endif
    const bool detachedCameraActive =
#if SOKOBAN_ENABLE_DEBUG_UI
        tools_->detachedCameraActive();
#else
        false;
#endif
    const bool showOverworldMap =
        !detachedCameraActive && input.showOverworldMap &&
        campaign_.inOverworld() &&
        !editorDraftPlaying;
    const CameraAngles cameraAngles = gameplayCameraAngles();
    presentation_.updateCameraPitch(
        (!detachedCameraActive &&
                (input.showTopDownView || showOverworldMap))
            ? 0.0f
            : cameraAngles.pitchDegrees,
        dt,
        config::cameraPitchTransitionSeconds);
    presentation_.updateCameraYaw(
        cameraAngles.yawDegrees, dt, config::cameraPitchTransitionSeconds);
    const float overviewStep = config::cameraPitchTransitionSeconds <= 0.0f
        ? 1.0f
        : std::max(dt, 0.0f) / config::cameraPitchTransitionSeconds;
    overworldOverviewProgress_ = showOverworldMap
        ? std::min(overworldOverviewProgress_ + overviewStep, 1.0f)
        : std::max(overworldOverviewProgress_ - overviewStep, 0.0f);

#if SOKOBAN_ENABLE_DEBUG_UI
    // Last frame's board interaction decides the editor's tool cursor; any
    // menu, modal or fly camera puts the ordinary cursor back.
    tools_->updateEditorCursor(
        !shellMenuOpen() && !tools_->draftExitConfirmationOpen &&
        !detachedCameraActive && tools_->levelEditor.editingDocument());
#endif
    if (shellMenuOpen()) {
        audioSystem_->update(dt, false, false);
        return;
    }

#if SOKOBAN_ENABLE_DEBUG_UI
    if (tools_->draftExitConfirmationOpen) {
        audioSystem_->update(dt, false, false);
        return;
    }
    if (detachedCameraActive) {
        tools_->updateDetachedCamera(dt, input_);
    }
    if (tools_->levelEditor.editingDocument()) {
        audioSystem_->update(dt, false, false);
        audioSystem_->playMusicForLevel(
            campaign_.inOverworld() ? 0 : campaign_.currentLevel());
        if (!detachedCameraActive) {
            tools_->updateEditorInteraction(
                input.editor,
                previousRenderFrame,
                renderer_,
                window_.size(),
                window_.sizeInPixels());
        }
        return;
    }
#endif

    if (overworldFogReveal_) {
        overworldFogReveal_->progress += std::max(dt, 0.0f) /
            config::fogOfWarRevealDurationSeconds;
        if (overworldFogReveal_->progress >= 1.0f) {
            overworldFogReveal_.reset();
        }
    }

    if (levelTransition_.active()) {
        updateLevelTransition(dt);
        audioSystem_->update(dt, false, false);
        return;
    }

    if (!gameplaySession_.readingLectern() && updateScreenPreview(
            !detachedCameraActive && input.previewScreen,
            dt)) {
        audioSystem_->update(dt, false, false);
        return;
    }

    const auto readingBeforeUpdate = gameplaySession_.readingLectern();
    if (!readingBeforeUpdate && input.gameplay.interactPressed &&
        tryEnterSelector()) {
        audioSystem_->update(dt, false, false);
        return;
    }
    const bool dismissingLectern = readingBeforeUpdate &&
        (input.gameplay.interactPressed || input.gameplay.dismissPressed);
    const GameplayLoop::InputFrame gameplayInput = detachedCameraActive && !dismissingLectern
        ? GameplayLoop::InputFrame {} : input.gameplay;
    if (!readingBeforeUpdate) {
        if (!editorDraftPlaying) {
            campaign_.addElapsedTime(dt);
        }
        particleSystem_.update(dt);
    }
#if SOKOBAN_ENABLE_DEBUG_UI
    if (solveCapture_) {
        solveCapture_->frames.push_back({ .input = gameplayInput, .elapsedSeconds = dt });
    }
#endif
    const GameplayLoop::UpdateResult gameplayResult = GameplayLoop::update(
        level_,
        gameplaySession_,
        presentation_,
        gameplayInput,
        dt,
        editorDraftPlaying);
    if (dismissingLectern) {
        audioSystem_->update(dt, false, false);
        return;
    }
    if (const auto reading = gameplaySession_.readingLectern(); reading && reading != readingBeforeUpdate) {
        lecternDialog_.reset();
    }
    if (readingBeforeUpdate && (!gameplaySession_.readingLectern() ||
        gameplaySession_.moving() || gameplayResult.stateCommitted)) {
        if (!editorDraftPlaying) {
            campaign_.addElapsedTime(dt);
        }
        particleSystem_.update(dt);
    }
    if (gameplayResult.stateCommitted && campaign_.inOverworld() &&
        overworldMap_ && !editorDraftPlaying) {
        const std::optional<OverworldScreenId> playerScreen =
            CampaignSession::sharedPlayerScreen(
                *overworldMap_, gameplaySession_.state());
        if (playerScreen &&
            *playerScreen != campaign_.activeOverworldScreen()) {
            const bool firstVisit = !campaign_.overworldScreenDiscovered(
                playerProfile_, *playerScreen);
            if (!campaign_.transitionOverworldScreen(
                    playerProfile_, *playerScreen)) {
                log::error(log::Category::Gameplay)
                    << "Could not commit overworld screen transition to "
                    << *playerScreen;
            } else {
                upcomingAssetPrefetchPending_ = true;
                if (firstVisit && !gameplaySession_.state().players.empty()) {
                    const GridPosition3 entry =
                        gameplaySession_.state().players.front().cell;
                    overworldFogReveal_ = OverworldFogReveal {
                        .screen = *playerScreen,
                        .origin = {
                            static_cast<float>(entry.x) + 0.5f,
                            static_cast<float>(entry.y) + 0.5f,
                        },
                    };
                }
            }
        }
    }
    presentGameplayEffects(level_, gameplaySession_, presentation_, particleSystem_,
        turretFlames_, gameplayResult, input.gameplay, dt);
#if SOKOBAN_ENABLE_DEBUG_UI
    if (gameplayResult.draftSolved) {
        tools_->levelEditor.markDraftSolved();
    }
    if (solveCapture_ && (solveCapture_->playingDraft ? gameplayResult.draftSolved
                                                    : gameplayResult.screenSolved)) {
        auto solve = std::move(solveCapture_);
        tools_->saveSolve(std::move(*solve));
    } else if (gameplayResult.screenSolved && !campaign_.inOverworld() && developerFilesWritable()) {
        log::info(log::Category::Editor)
            << "Campaign solve of level" << campaign_.currentLevel()
            << "/screen" << campaign_.currentScreen() << " resumed from a checkpoint. "
               "Play Draft starts the screen at its authored state and captures the full solve.";
    }
#endif
    if (gameplayResult.screenSolved) {
        advanceScreen();
    } else if (!editorDraftPlaying && gameplayResult.stateCommitted &&
        campaign_.deferCheckpoint()) {
        checkpointCurrentScreen(true);
    }
    if (!gameplaySession_.readingLectern() && campaign_.updateDeferredCheckpoint(
            dt,
            gameplaySession_.moving(),
            editorDraftPlaying)) {
        checkpointCurrentScreen(true);
    }

    updateGameplayAudio(dt, gameplaySession_, presentation_);
    audioSystem_->playMusicForLevel(
        campaign_.inOverworld() ? 0 : campaign_.currentLevel(),
        gameplaySession_.activeHeroCharacter());
}

void Application::presentGameplayEffects(const Level& level, const GameplaySession& session,
    GameplayPresentation& presentation, ParticleSystem& particles,
    TurretFlameSystem& turretFlames, const GameplayLoop::UpdateResult& result,
    const GameplayLoop::InputFrame& input, float dt)
{
    if (result.mirrorActivated) {
        audioSystem_->playOneShot("mirror-swap");
        for (GridPosition3 destination :
             result.mirrorSwapDestinations) {
            particles.emit(
                {
                    static_cast<float>(destination.x) + 0.5f,
                    static_cast<float>(destination.y) + 0.5f,
                    static_cast<float>(destination.z) +
                        config::mirrorSwapSmokeElevation,
                },
                mirrorSwapParticleEffect_);
        }
    }
    if (result.witchSwapped) {
        for (GridPosition3 destination :
             result.witchSwapDestinations) {
            particles.emit(
                {
                    static_cast<float>(destination.x) + 0.5f,
                    static_cast<float>(destination.y) + 0.5f,
                    static_cast<float>(destination.z) +
                        config::mirrorSwapSmokeElevation,
                },
                witchSwapParticleEffect_);
        }
    }
    for (GridPosition3 cell : result.crystalPuffs) {
        particles.emit(
            { static_cast<float>(cell.x) + 0.5f,
              static_cast<float>(cell.y) + 0.5f,
              static_cast<float>(cell.z) + 0.4f },
            crystalChangeParticleEffect_);
    }
    if (input.undoPressed || input.undoDown || input.restartPressed) {
        turretFlames.reset();
    }
    turretFlames.update(level, session.state(), session.readingLectern() ? 0.0f : dt,
        &presentation);
    for (const GameplayLoop::UpdateResult::TurretShotPresentation& shot :
         result.turretShots) {
        turretFlames.triggerShot(shot.shot, shot.impactDelaySeconds);
        presentation.triggerTurretShot(
            shot.shot.turret.id,
            shot.shot.direction);
    }
    for (const auto sound : result.sounds) {
        audioSystem_->playOneShot(soundSetName(sound));
    }
}

void Application::updateGameplayAudio(float dt, const GameplaySession& session,
    const GameplayPresentation& presentation)
{
    bool playerMoving = false;
    bool pushing = false;
    for (const GameplayPresentation::PlayerVisual& player :
         presentation.players()) {
        playerMoving |= player.motion.moving;
        pushing |= player.motion.moving &&
            (player.animationUse == AnimationUse::PlayerPush ||
                player.animationUse == AnimationUse::PlayerPull);
    }
    const bool minecartMoving = std::ranges::any_of(
        presentation.minecarts(), [](const auto& cart) { return cart.moving; });
    const bool elevatorMoving = std::ranges::any_of(
        presentation.elevators(), [](const auto& elevator) { return elevator.moving; });
    audioSystem_->update(dt, playerMoving, pushing, minecartMoving, elevatorMoving,
        AtmosphericAudio::listenerPosition(session.state(), presentation,
            session.activeHeroController()));
}

void Application::loadCurrentScreen()
{
    overworldFogReveal_.reset();
    refreshLevelCatalogIfNeeded();
    if (!campaign_.inOverworld() && !campaign_.screenExists(
            campaign_.currentLevel(), campaign_.currentScreen())) {
        restoreProfileLocation();
    }
    const CampaignSession::WorldRestore restore =
        campaign_.prepareWorldLoad(playerProfile_);

    const std::optional<LevelLocation> location = campaign_.inOverworld()
        ? std::nullopt
        : std::optional<LevelLocation> { campaign_.location() };
    const std::filesystem::path path = campaign_.inOverworld()
        ? (overworldMap_ ? overworldRoot() : overworldPath())
        : screenPath(campaign_.currentLevel(), campaign_.currentScreen());

    auto puzzleDefinition = location
        ? std::optional { Level::loadDefinitionFromFile(path) } : std::nullopt;
    Level loadedLevel = puzzleDefinition
        ? Level::loadFromDefinition(*puzzleDefinition, pathText(path))
        : catalogOverworldLevel();

    const bool restored = applyLevel(
        std::move(loadedLevel),
        restore.snapshot ? &*restore.snapshot : nullptr,
        location,
        campaign_.inOverworld() && overworldMap_.has_value());
    gameplayGroundGeometrySource_ = (campaign_.inOverworld() && overworldMap_
        ? path / "layout.json" : path).lexically_relative(assetRoot_ / "levels");
    if (restore.checkpointMatched && !restored) {
        log::warning(log::Category::Persistence)
            << "Discarded invalid gameplay checkpoint for "
            << (campaign_.inOverworld() ? "the overworld" : "puzzle");
        if (campaign_.inOverworld()) {
            playerProfile_.overworldCheckpoint.reset();
        } else {
            playerProfile_.activeScreen.reset();
        }
    }
    campaign_.finishWorldLoad(playerProfile_);
#if SOKOBAN_ENABLE_DEBUG_UI
    draftWorldLoaded_ = false;
    if (location && developerFilesWritable()) {
        const auto& state = gameplaySession_.state();
        const EntityId firstController = state.players.empty()
            ? invalidEntityId : rules::playerControllerId(state, 0);
        // A validated empty history starts on the authored board. The
        // controller and ambient-motion flag are independent of that history.
        const bool atAuthoredStart = !restored ||
            (restore.snapshot->undoStack.empty() && !gameplaySession_.automaticMotionPaused() &&
                gameplaySession_.activeHeroController() == firstController);
        if (atAuthoredStart) {
            solveCapture_ = std::make_unique<solution::SolveToStore>(solution::SolveToStore {
                .name = "level" + std::to_string(location->level) +
                    "/screen" + std::to_string(location->screen),
                .definition = std::move(*puzzleDefinition),
                .playingDraft = false,
            });
        }
    }
    tools_->levelEditor.setPlayingDraft(false);
    tools_->levelEditor.setEditingDocument(false);
    tools_->hoverCell.reset();
    tools_->pickedCell.reset();
#endif
    checkpointCurrentScreen(true);
    upcomingAssetPrefetchPending_ = true;

    log::debug(log::Category::Gameplay)
        << "player entered "
        << (campaign_.inOverworld() ? "the overworld" : "puzzle");
}

bool Application::applyLevel(
    Level level,
    const GameplaySession::Snapshot* snapshot,
    std::optional<LevelLocation> location,
    bool composedOverworld)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    solveCapture_.reset();
#endif
    // A level change makes speculative work for the prior world irrelevant.
    // Running tasks may finish, but queued disk work is discarded before it
    // competes with the assets the player is about to see.
    renderer_.cancelQueuedAssetPrefetches();
    // A composed overworld includes distant screens and every terrain variant.
    // drawFrame requests its exact visible models, splats and dependencies.
    // Warm action clips and brief effects before first use in both worlds.
    if (composedOverworld) {
        renderer_.ensureAssets(renderActionAssetRequirementsForLevel(
            level, assetManifest_, &animationCatalog_));
    } else {
        renderer_.ensureAssets(renderAssetRequirementsForLevel(
            level, assetManifest_, location, &animationCatalog_));
    }
    gameplayGroundGeometryStore_.invalidate();
    gameplayGroundGeometrySource_.clear();
    level_ = std::move(level);
    bool restored = snapshot && gameplaySession_.restore(
        level_, *snapshot, campaign_.inOverworld());
    if (restored && composedOverworld && overworldMap_ &&
        !overworldActionStateAllowed(*overworldMap_, gameplaySession_.state())) {
        restored = false;
    }
    if (!restored) {
        gameplaySession_.reset(level_);
    }
    if (composedOverworld && overworldMap_) {
        gameplaySession_.setActionAdmissionPolicy([this](const GameState& state) {
            return overworldMap_ &&
                overworldActionStateAllowed(*overworldMap_, state);
        });
    } else {
        gameplaySession_.clearActionAdmissionPolicy();
    }
    presentation_.resetEntities(gameplaySession_.state());
    audioSystem_->setAtmosphericLevel(level_);
    audioSystem_->playMusicForLevel(
        campaign_.inOverworld() ? 0 : campaign_.currentLevel(),
        gameplaySession_.activeHeroCharacter());
    particleSystem_.reset();
    turretFlames_.reset();
    campaign_.markWorldLoaded();
    return restored;
}

void Application::advanceScreen()
{
    if (campaign_.inOverworld()) {
        log::warning(log::Category::Gameplay)
            << "Ignored an overworld completion event; overworlds may not "
               "contain End tiles";
        return;
    }
    const int moveCount = gameplaySession_.playerMoveCount();
    beginLevelTransition([this, moveCount] {
        refreshLevelCatalogIfNeeded();
        if (!campaign_.screenExists(campaign_.currentLevel(), campaign_.currentScreen())) {
            loadCurrentScreen();
            return;
        }
        handlePuzzleCompleted(campaign_.completePuzzle(
            playerProfile_, moveCount, &catalogOverworldLevel()));
    });
}

void Application::handlePuzzleCompleted(
    const CampaignSession::PuzzleCompleted& completed)
{
    auto& checkpoint = playerProfile_.overworldCheckpoint;
    if (!completed.nextPuzzle && overworldMap_ && checkpoint &&
        checkpoint->topologyFingerprint == overworldMap_->fingerprint()) {
        const auto screen = CampaignSession::sharedPlayerScreen(
            *overworldMap_, checkpoint->session.state);
        if (screen && *screen != checkpoint->activeScreen &&
            campaign_.transitionOverworldScreen(playerProfile_, *screen)) {
            checkpoint->activeScreen = *screen;
        }
    }
    persistProfile(true);
    if (completed.nextPuzzle) {
        (void)campaign_.startPuzzle(playerProfile_, *completed.nextPuzzle);
    }
    loadCurrentScreen();
}

void Application::beginLevelTransition(
    std::function<void()> midpointAction)
{
    if (!midpointAction || !levelTransition_.start()) {
        return;
    }
    levelTransitionMidpointAction_ = std::move(midpointAction);
    screenPreviewActive_ = false;
}

void Application::updateLevelTransition(float dt)
{
    const LevelTransition::UpdateResult result = levelTransition_.update(dt);
    if (!result.midpointReached || !levelTransitionMidpointAction_) {
        return;
    }

    std::function<void()> action =
        std::move(levelTransitionMidpointAction_);
    levelTransitionMidpointAction_ = {};
    action();
}

const Level::ScreenSelector* Application::currentOverworldSelector() const
{
    if (!campaign_.gameLoaded() || !campaign_.inOverworld() ||
        levelTransition_.active() || shellMenuOpen()
#if SOKOBAN_ENABLE_DEBUG_UI
        || tools_->levelEditor.editingDocument() ||
        tools_->levelEditor.playingDraft() || tools_->detachedCameraActive()
#endif
    ) {
        return nullptr;
    }
    return CampaignSession::selectorForInteraction(level_, gameplaySession_);
}

bool Application::tryEnterSelector()
{
    const Level::ScreenSelector* sharedSelector =
        currentOverworldSelector();
    if (!sharedSelector || !sharedSelector->target) {
        if (sharedSelector) {
            log::warning(log::Category::Gameplay)
                << "Selector " << sharedSelector->id
                << " has no assigned screen";
        }
        return false;
    }
    if (!campaign_.screenExists(
            sharedSelector->target->level,
            sharedSelector->target->screen)) {
        log::warning(log::Category::Gameplay)
            << "Selector " << sharedSelector->id
            << " targets missing screen " << sharedSelector->target->level
            << ':' << sharedSelector->target->screen;
        return false;
    }
    if (campaign_.selectorViewState(
            playerProfile_, *sharedSelector->target).status ==
        ScreenSelectorStatus::Unavailable) {
        log::debug(log::Category::Gameplay)
            << "Selector " << sharedSelector->id
            << " is not playable until the previous screen is solved";
        return false;
    }

    // Queue the current return point before changing the profile context.
    checkpointCurrentScreen(true);
    const LevelLocation target = *sharedSelector->target;
    beginLevelTransition([this, target] {
        refreshLevelCatalogIfNeeded();
        (void)campaign_.startPuzzle(playerProfile_, target);
        loadCurrentScreen();
    });
    return true;
}

bool Application::updateScreenPreview(bool requested, float dt)
{
    if (!requested) {
        return false;
    }

    const Level::ScreenSelector* selector =
        currentOverworldSelector();
    if (!selector || !selector->target ||
        !campaign_.screenExists(
            selector->target->level, selector->target->screen) ||
        campaign_.selectorViewState(
            playerProfile_, *selector->target).status ==
            ScreenSelectorStatus::Unavailable) {
        return false;
    }

    if (!screenPreviewLevel_ ||
        screenPreviewTarget_ != selector->target) {
        try {
            Level preview = Level::loadFromFile(screenPath(
                selector->target->level,
                selector->target->screen));
            screenPreviewSession_.reset(preview);
            screenPreviewPresentation_.resetEntities(
                screenPreviewSession_.state());
            screenPreviewLevel_ = std::move(preview);
            screenPreviewTarget_ = *selector->target;
            previewGroundGeometryStore_.invalidate();
            previewGroundGeometrySource_ = screenPath(
                selector->target->level, selector->target->screen)
                .lexically_relative(assetRoot_ / "levels");
        } catch (const std::exception& error) {
            log::error(log::Category::Gameplay)
                << "Could not preview screen "
                << selector->target->level << ':'
                << selector->target->screen << ": " << error.what();
            screenPreviewLevel_.reset();
            screenPreviewTarget_.reset();
            return false;
        }
    }

    screenPreviewPresentation_.advanceClocks(dt, false);
    screenPreviewActive_ = true;
    return true;
}

std::optional<RenderFrameData>
Application::buildScreenPreviewRenderFrame(FrameArena& arena) const
{
    if (!screenPreviewActive_ || !screenPreviewLevel_ ||
        !screenPreviewTarget_) {
        return std::nullopt;
    }

    const GameState& projectedState = screenPreviewSession_.projectedStateView();
    const auto& activationPreview = screenPreviewSession_.activationPreview(*screenPreviewLevel_);
    RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = assetManifest_,
        .level = *screenPreviewLevel_,
        .state = screenPreviewSession_.state(),
        .activeHeroController =
            screenPreviewSession_.activeHeroController(),
        .moving = false,
        .projectedState = projectedState,
        .presentation = screenPreviewPresentation_,
        .settings = presentationSettings_,
        .animations = &animationCatalog_,
        .conveyorBeltScrollOffset =
            screenPreviewPresentation_.conveyorBeltScrollOffset(
                screenPreviewSession_.stepDurationSeconds()),
        .levelLocation = screenPreviewTarget_,
        .selectorState = [this](LevelLocation target) {
            return campaign_.selectorViewState(playerProfile_, target);
        },
        .cachedActivationPreviews = true,
        .activationPreview = activationPreview ? &*activationPreview : nullptr,
        .invalidMirrorBeams = screenPreviewSession_.invalidMirrorBeams(*screenPreviewLevel_),
        .invalidMirrorEntities = screenPreviewSession_.invalidMirrorPreview(*screenPreviewLevel_).entities,
        .groundGeometryCache = &previewGroundGeometryCache_,
    }, arena);
    if (presentationSettings_.geometry.smoothGroundRim &&
        presentationSettings_.geometry.processGroundGeometry) {
        frame.processedGroundArtifact = previewGroundGeometryStore_.get(
            previewGroundGeometrySource_, *screenPreviewLevel_, assetManifest_,
            previewGroundGeometryCache_.rebuildCount(), assetRoot_);
    }
    // The preview and live world can contain the same stable actor IDs. Keep
    // their GPU animation instances independent so one idle pose cannot
    // overwrite the other's skinning buffers.
    constexpr uint64_t previewInstanceNamespace = uint64_t { 1 } << 63;
    for (RenderFrameData::Tile& tile : frame.tiles) {
        if (tile.animationInstanceId != 0) {
            tile.animationInstanceId |= previewInstanceNamespace;
        }
    }
    return frame;
}

void Application::drawSelectorPrompt(
    const VulkanRenderer::PreparedFrame* frame)
{
    if (!frame) {
        return;
    }

    const Level::ScreenSelector* selector =
        currentOverworldSelector();
    if (!selector || !selector->target ||
        !campaign_.screenExists(
            selector->target->level, selector->target->screen) ||
        campaign_.selectorViewState(
            playerProfile_, *selector->target).status ==
            ScreenSelectorStatus::Unavailable ||
        presentation_.players().empty()) {
        return;
    }

    const std::optional<UiRect> playerBounds =
        renderer_.primaryPlayerBoundsToPixels(*frame);
    std::optional<Vec2> arrowTip;
    if (playerBounds) {
        constexpr float gapAbovePlayer = 10.0f;
        constexpr float minimumArrowTipY = 56.0f;
        arrowTip = Vec2 {
            playerBounds->position.x + playerBounds->size.x * 0.5f,
            std::max(
                playerBounds->position.y - gapAbovePlayer,
                minimumArrowTipY),
        };
    }
    const BindingDeviceClass device =
        input_.activeDevice() == ActiveInputDevice::Gamepad
        ? BindingDeviceClass::Gamepad
        : BindingDeviceClass::Keyboard;
    const InputBinding* enterBinding =
        SelectorPrompt::bindingView(
            input_.bindings(), InputAction::MenuConfirm, device);
    const InputBinding* previewBinding =
        SelectorPrompt::bindingView(
            input_.bindings(), InputAction::PreviewScreen, device);
    if (arrowTip && enterBinding && previewBinding) {
        const GamepadPresentation gamepad = input_.activeGamepadPresentation();
        const std::optional<InputPromptGlyph> enterGlyph =
            inputPrompts_.glyphForBinding(*enterBinding, gamepad);
        const std::optional<InputPromptGlyph> previewGlyph =
            inputPrompts_.glyphForBinding(*previewBinding, gamepad);
        if (enterGlyph && previewGlyph) {
            SelectorPrompt::draw(ui_, *arrowTip, *enterGlyph, *previewGlyph);
        } else {
            const std::string_view enterLabel =
                SelectorPrompt::bindingLabel(
                    input_.bindings(), InputAction::MenuConfirm, device, ui_.frameArena());
            const std::string_view previewLabel =
                SelectorPrompt::bindingLabel(
                    input_.bindings(), InputAction::PreviewScreen, device, ui_.frameArena());
            if (!enterLabel.empty() && !previewLabel.empty()) {
                SelectorPrompt::draw(ui_, *arrowTip, enterLabel, previewLabel);
            }
        }
    }
}

void Application::drawAssetLoadingOverlay(Vec2 viewport)
{
    const VulkanModelResources::LoadingStats stats =
        renderer_.assetLoadingStats();
    const uint32_t pending = stats.pendingModels + stats.pendingTextures +
        stats.pendingAnimations;
    if (pending == 0 && stats.failedAssets == 0) {
        return;
    }

    const float width = std::min(460.0f, std::max(260.0f, viewport.x - 32.0f));
    const UiRect panel {
        .position = { (viewport.x - width) * 0.5f, 22.0f },
        .size = { width, stats.failedAssets > 0 ? 104.0f : 78.0f },
    };
    ui_.rect(panel, { 0.025f, 0.035f, 0.045f, 0.90f });
    ui_.rect({
        .position = panel.position,
        .size = { panel.size.x, 2.0f },
    }, { 0.24f, 0.70f, 0.95f, 0.95f });
    ui_.centeredText({
        .position = { panel.position.x, panel.position.y + 8.0f },
        .size = { panel.size.x, 24.0f },
    }, "Loading world assets", { 0.91f, 0.95f, 1.0f, 1.0f }, 19.0f);

    const float progress = stats.requestedAssets == 0
        ? 0.0f
        : std::clamp(
            static_cast<float>(stats.readyRequestedAssets) /
                static_cast<float>(stats.requestedAssets),
            0.0f,
            1.0f);
    const UiRect track {
        .position = { panel.position.x + 18.0f, panel.position.y + 39.0f },
        .size = { panel.size.x - 36.0f, 10.0f },
    };
    ui_.rect(track, { 0.12f, 0.16f, 0.20f, 1.0f });
    ui_.rect({
        .position = track.position,
        .size = { track.size.x * progress, track.size.y },
    }, { 0.24f, 0.70f, 0.95f, 1.0f });

    char detail[96] {};
    std::snprintf(detail, sizeof(detail), "%u / %u ready  -  %u pending",
        stats.readyRequestedAssets, stats.requestedAssets, pending);
    ui_.centeredText({
        .position = { panel.position.x, panel.position.y + 53.0f },
        .size = { panel.size.x, 20.0f },
    }, detail, { 0.76f, 0.82f, 0.88f, 1.0f }, 15.0f);
    if (stats.failedAssets > 0) {
        ui_.centeredText({
            .position = { panel.position.x, panel.position.y + 76.0f },
            .size = { panel.size.x, 20.0f },
        }, "Some art failed to load; fallback visuals are in use.",
            { 1.0f, 0.72f, 0.38f, 1.0f }, 14.0f);
    }
}

void Application::drawScreenPreviewOverlay(Vec2 viewport)
{
    ScreenPreviewOverlay::draw(ui_, viewport);
}

void Application::openTitleScreen()
{
    titleScreen_.setSaveSlots(saveSlotInfos(), saveSlots_.activeSlot());
    titleScreen_.open();
}

std::vector<SaveSlotInfo> Application::saveSlotInfos() const
{
    std::vector<SaveSlotInfo> slots;
    for (const SaveSlotManager::SlotSummary& summary :
        saveSlots_.slotSummaries(
            playerProfile_, campaign_.overworldTargets())) {
        slots.push_back({
            .state = summary.state,
            .completed = summary.completed,
            .currentLevel = summary.currentLevel,
            .completedLevels = summary.completedLevels,
        });
    }
    return slots;
}

bool Application::switchSaveSlot(int slot)
{
    if (slot < 0 || slot >= SaveSlotManager::slotCount) {
        return false;
    }
    if (slot == saveSlots_.activeSlot()) {
        return true;
    }

    // Settle the outgoing slot on disk first. Not gated on the world being
    // idle - see the destructor for why that gate was wrong under concurrency.
    if (campaign_.gameLoaded()) {
        checkpointCurrentScreen(true);
    } else {
        persistProfile(true);
    }

    std::optional<PlayerProfile> switched;
    try {
        switched = saveSlots_.switchTo(slot, playerProfile_);
    } catch (const std::exception& error) {
        const std::string message =
            "Could not switch to save slot " + std::to_string(slot + 1) +
            ": " + error.what();
        log::error(log::Category::Persistence) << message;
        titleScreen_.setSaveSlotError(message);
        return false;
    }
    if (!switched) {
        return false;
    }
    playerProfile_ = std::move(*switched);
    titleScreen_.setSaveSlotError({});
    log::info(log::Category::Persistence)
        << saveSlots_.progressStatus();

    // The new slot's world loads on Continue/New Game, like at boot.
    campaign_.resetForProfile(playerProfile_);
    renderer_.cancelQueuedAssetPrefetches();
    upcomingAssetPrefetchPending_ = true;
    openTitleScreen();
    return true;
}

void Application::deleteSaveSlot(int slot)
{
    if (slot < 0 || slot >= SaveSlotManager::slotCount) {
        return;
    }
    const SaveSlotManager::DeleteResult deletion = saveSlots_.deleteSlot(slot);
    if (!deletion.succeeded) {
        const std::string message =
            "Could not delete save slot " + std::to_string(slot + 1) +
            ": " + deletion.message;
        log::error(log::Category::Persistence) << message;
        // Keep the current profile and world exactly as they were. Refreshing
        // the summaries retains the saved slot instead of presenting a false
        // empty state.
        titleScreen_.setSaveSlots(saveSlotInfos(), saveSlots_.activeSlot());
        titleScreen_.setSaveSlotError(message);
        return;
    }

    if (slot == saveSlots_.activeSlot()) {
        // The file stays absent until the player starts playing again.
        playerProfile_.resetProgress();
        campaign_.resetForProfile(playerProfile_);
        renderer_.cancelQueuedAssetPrefetches();
        upcomingAssetPrefetchPending_ = true;
    }
    titleScreen_.setSaveSlots(saveSlotInfos(), saveSlots_.activeSlot());
    if (deletion.cleanupPending) {
        log::warning(log::Category::Persistence) << deletion.message;
        titleScreen_.setSaveSlotError(deletion.message);
    } else {
        titleScreen_.setSaveSlotError({});
    }
}

void Application::persistSettings(bool immediate)
{
    saveSlots_.saveSettings(playerProfile_, immediate);
}

ShellFacts Application::shellFacts() const
{
    return {
        .gameLoaded = campaign_.gameLoaded(),
        .optionsOpen = optionsMenu_.isOpen(),
        .titleOpen = titleScreen_.isOpen(),
        .titleAtMainPage = titleScreen_.page() == TitleScreen::Page::Main,
#if SOKOBAN_ENABLE_DEBUG_UI
        .editorActive = tools_->levelEditor.editingDocument() || draftWorldLoaded_,
#endif
        .makerOpen = makerOpen_,
    };
}

void Application::handleShellEvent(const ShellEvent& event)
{
    for (const ShellCommand& command : shellFlow_.handle(event, shellFacts())) {
        executeShellCommand(command);
    }
}

void Application::executeShellCommand(const ShellCommand& command)
{
    std::visit(flow::Overloaded {
        [&](const shell::LoadCurrentScreen&) {
            restoreProfileLocation();
            loadCurrentScreen();
        },
        [&](const shell::ResumeCampaign&) {
#if SOKOBAN_ENABLE_DEBUG_UI
            resumeCampaign();
#endif
        },
        [&](const shell::CloseTitle&) { titleScreen_.close(); },
        [&](const shell::OpenTitle&) { openTitleScreen(); },
        [&](const shell::TitleBack&) { titleScreen_.back(); },
        [&](const shell::OpenMaker&) {
            if (!makerSession_) makerSession_.emplace();
            makerOpen_ = true;
        },
        [&](const shell::CloseMaker&) {
            if (makerPlaytest_) returnToMaker();
            if (makerDraftDialog_) closeMakerDraftDialog();
            blurNativeTextInput();
            makerSession_->endStroke();
            makerOpen_ = false;
            makerViewport_.reset();
            makerHover_.reset();
        },
        [&](const shell::StartNewGame&) { startNewGame(); },
        [&](const shell::StartNewGameOnSlot& start) {
            (void)executeNewGameOnSlot(
                start,
                [&](int slot) { return switchSaveSlot(slot); },
                [&] { startNewGame(); });
        },
        [&](const shell::SwitchSlot& switchSlot) {
            (void)switchSaveSlot(switchSlot.slot);
        },
        [&](const shell::DeleteSlot& deleteSlot) {
            deleteSaveSlot(deleteSlot.slot);
        },
        [&](const shell::OpenOptions& open) {
            optionsMenu_.open(open.pauseContext);
        },
        [&](const shell::CloseOptions&) { optionsMenu_.close(); },
        [&](const shell::OptionsBack&) { optionsMenu_.back(); },
        [&](const shell::ApplySettings& apply) {
            applySettingsEffects(
                settingsCoordinator_.applyUserSettings(
                    apply.settings));
        },
        [&](const shell::RequestQuitConfirmation&) {
            if (makerDraftDialog_) makerDraftDialog_->inputAction = {};
            blurNativeTextInput();
            optionsMenu_.requestQuitConfirmation();
        },
        [&](const shell::Quit&) { running_ = false; },
    }, command);
}

bool Application::shellMenuOpen() const
{
    return optionsMenu_.isOpen() || titleScreen_.isOpen() || makerOpen_;
}

void Application::startNewGame()
{
    titleScreen_.close();
    refreshLevelCatalogIfNeeded();
    campaign_.startNewGame(playerProfile_);
    loadCurrentScreen();
    persistProfile(true);
}

void Application::checkpointCurrentScreen(bool immediateSave)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (draftWorldLoaded_) {
        persistProfile(immediateSave);
        return;
    }
#endif
    campaign_.writeCheckpoint(playerProfile_, gameplaySession_.snapshot());
    persistProfile(immediateSave);
}

void Application::applySettingsEffects(const SettingsEffects& effects)
{
    if (effects.window) {
        if (effects.window->fullscreen) {
            window_.setFullscreen(true);
        } else if (effects.window->maximized) {
            window_.setWindowedMaximized();
        } else {
            window_.setWindowedSize(
                effects.window->width, effects.window->height);
        }
    }
    if (effects.antiAliasingSamples) {
        renderer_.setAntiAliasingMode(
            antiAliasingModeForSamples(*effects.antiAliasingSamples));
    }
    if (effects.renderScalePercent) {
        renderer_.setRenderScalePercent(*effects.renderScalePercent);
    }
    if (effects.presentation) {
        renderer_.setPresentationPolicy(*effects.presentation);
    }
    if (effects.frameRateLimit) {
        framePacer_.setFrameRateLimit(*effects.frameRateLimit);
    }
    if (effects.audio) {
        audioSystem_->setMasterVolume(effects.audio->masterVolume);
        audioSystem_->setMusicVolume(effects.audio->musicVolume);
        audioSystem_->setSoundVolume(effects.audio->soundVolume);
    }
    if (effects.input) {
        input_.setBindings(*effects.input);
    }
    if (effects.saveProgress) {
        persistProfile(effects.immediatePersistence);
    }
    if (effects.saveSettings) {
        persistSettings(effects.immediatePersistence);
    }
}

void Application::persistProfile(bool immediate)
{
    // Slots with no progress stay off disk entirely (settings persist through
    // their own shared store), preserving the fresh-install empty slate.
    if (!campaign_.gameLoaded() && playerProfile_.progressEmpty()) {
        return;
    }
    saveSlots_.saveProgress(playerProfile_, immediate);
}

InputRouter::RoutingContext Application::inputRoutingContext() const
{
    InputRouter::RoutingContext context {
        .optionsOpen = optionsMenu_.isOpen(),
        .titleOpen = titleScreen_.isOpen(),
        .makerOpen = makerOpen_,
        .makerPlaying = makerPlaytest_.has_value(),
        .lecternOpen = makerPlaytest_
            ? makerPlaytest_->session.readingLectern().has_value()
            : gameplaySession_.readingLectern().has_value() && !shellMenuOpen(),
        .keyboardCaptured = renderer_.wantsKeyboardCapture(),
        .mouseCaptured = renderer_.wantsMouseCapture(),
        .nativeTextEditing = nativeTextFocused(),
        .makerDialogOpen = makerDraftDialog_.has_value(),
    };
#if SOKOBAN_ENABLE_DEBUG_UI
    context.editorEditing = tools_->levelEditor.editingDocument();
    context.tilePlacementReady = context.editorEditing &&
        tools_->levelEditor.tool() == LevelEditor::Tool::Tiles;
    context.decorationPlacementReady =
        context.editorEditing &&
        tools_->levelEditor.placingDecoration();
    context.tileDecorationPlacementReady = context.decorationPlacementReady &&
        tools_->levelEditor.tool() == LevelEditor::Tool::TileDecorations;
    context.editorViewportShortcutsAllowed =
        renderer_.allowsViewportShortcuts();
    context.draftPlaying = tools_->levelEditor.playingDraft();
    context.draftExitConfirmationOpen = tools_->draftExitConfirmationOpen;
#endif
    return context;
}

std::filesystem::path Application::screenPath(
    int levelIndex,
    int screenIndex) const
{
    if (const auto* path = puzzleCatalog_.findScreen({ levelIndex, screenIndex })) {
        return *path;
    }
    throw std::out_of_range("puzzle screen " + std::to_string(levelIndex) + ':' +
        std::to_string(screenIndex) + " is not in the staged catalog");
}

std::filesystem::path Application::overworldPath() const
{
    return assetRoot_ / "levels" / "overworld.scr";
}

std::filesystem::path Application::overworldRoot() const
{
    return assetRoot_ / "levels" / "overworld";
}

const Level& Application::catalogOverworldLevel() const
{
    return overworldMap_ ? overworldMap_->level() : *legacyOverworld_;
}

void Application::refreshLevelCatalogIfNeeded()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    observeEditorLevelPublications();
#endif
    if (levelCatalogStale_) {
        try {
            buildLevelCatalog();
        } catch (const std::exception& error) {
            levelCatalogStale_ = false;
            log::error(log::Category::Gameplay)
                << "Could not refresh staged campaign catalog: " << error.what();
        }
    }
}

void Application::buildLevelCatalog()
{
    PuzzleCatalog puzzleCatalog = loadPuzzleCatalog(assetRoot_ / "levels");
    std::vector<int> screenCounts = puzzleCatalog.screenCounts();
    std::optional<OverworldMap> overworld;
    std::optional<Level> legacyOverworld;
    std::vector<OverworldScreenId> screenIds;
    const std::filesystem::path layoutPath = overworldRoot() / "layout.json";
    if (std::filesystem::is_regular_file(layoutPath)) {
        overworld.emplace(OverworldMap::load(overworldRoot()));
        overworld->validatePuzzleSelectors(
            screenCounts, OverworldValidationMode::Structural);
        screenIds.reserve(overworld->screens().size());
        for (const OverworldScreenRuntime& screen : overworld->screens()) {
            screenIds.push_back(screen.id);
        }
    } else {
        legacyOverworld.emplace(Level::loadFromFile(overworldPath()));
        screenIds.push_back(1);
        for (const auto& selector : legacyOverworld->selectors()) {
            if (selector.target && !puzzleCatalog.findScreen(*selector.target)) {
                throw std::runtime_error("overworld selector " + std::to_string(selector.id) +
                    " targets a puzzle outside the staged catalog");
            }
        }
    }

    campaign_.setLevelScreenCounts(std::move(screenCounts));
    campaign_.setOverworldSelectors(
        (overworld ? overworld->level() : *legacyOverworld).selectors());
    campaign_.setOverworldTopology(overworld ? overworld->fingerprint() : 0,
        std::move(screenIds), overworld ? overworld->startScreen() : 1);
    puzzleCatalog_ = std::move(puzzleCatalog);
    overworldMap_ = std::move(overworld);
    legacyOverworld_ = std::move(legacyOverworld);
    levelCatalogStale_ = false;
}

void Application::restoreProfileLocation()
{
    refreshLevelCatalogIfNeeded();
    const bool restoringOverworld =
        playerProfile_.worldContext == PlayerProfile::WorldContext::Overworld;
    const LevelLocation saved {
        .level = playerProfile_.currentLevel,
        .screen = playerProfile_.currentScreen,
    };
    if (!campaign_.restoreProfileLocation(playerProfile_)) {
        if (restoringOverworld) {
            log::warning(log::Category::Persistence)
                << "Saved overworld checkpoint does not match the current "
                   "topology; returning to the authored start";
        } else {
            log::warning(log::Category::Persistence)
                << "Saved level location " << saved.level << ':' <<
                saved.screen << " does not exist; falling back to the overworld";
        }
    }
}

void Application::serviceUpcomingAssetPrefetch()
{
    if (!upcomingAssetPrefetchPending_ || levelCatalogStale_) {
        return;
    }
    upcomingAssetPrefetchPending_ = false;
    renderer_.cancelQueuedAssetPrefetches();
#if SOKOBAN_ENABLE_DEBUG_UI
    if (tools_->levelEditor.editingDocument() ||
        tools_->levelEditor.playingDraft()) {
        return;
    }
#endif
    std::vector<LevelLocation> targets;
    if (!campaign_.gameLoaded() && !campaign_.inOverworld()) {
        targets.push_back({ campaign_.currentLevel(), campaign_.currentScreen() });
    } else {
        targets = puzzleAssetPrefetchTargets(
            campaign_, playerProfile_, overworldMap_ ? &*overworldMap_ : nullptr);
    }
    RenderAssetRequirements requirements;
    for (LevelLocation target : targets) {
        try {
            requirements.merge(renderAssetRequirementsForLevel(
                Level::loadFromFile(screenPath(target.level, target.screen)),
                assetManifest_,
                target,
                &animationCatalog_));
        } catch (const std::exception& error) {
            log::warning(log::Category::Assets)
                << "asset preload skipped "
                << target.level << ':' << target.screen
                << ": " << error.what();
        }
    }
    renderer_.preloadAssets(requirements);
}

FrameArena& Application::beginRenderFrameArena()
{
    renderFrameArenaIndex_ =
        (renderFrameArenaIndex_ + 1) % renderFrameArenas_.size();
    FrameArena& arena = renderFrameArenas_[renderFrameArenaIndex_];
    arena.reset();
    return arena;
}

#if SOKOBAN_ENABLE_DEBUG_UI
void Application::resumeCampaign()
{
    tools_->splatPainter.close();
    tools_->draftExitConfirmationOpen = false;
    if (draftWorldLoaded_ || levelCatalogStale_) {
        if (!draftWorldLoaded_) {
            checkpointCurrentScreen(false);
        }
        loadCurrentScreen();
    } else {
        tools_->levelEditor.setEditingDocument(false);
        tools_->hoverCell.reset();
        tools_->pickedCell.reset();
        upcomingAssetPrefetchPending_ = true;
    }
    DebugUi::requestGameViewportFocus();
}

void Application::startEditorDraft(Level level, bool fromAuthoredStart)
{
    // F5 can return to the editor while the draft still owns the gameplay
    // session. Save the campaign before the first draft replaces that session.
    if (!draftWorldLoaded_) {
        checkpointCurrentScreen(true);
        draftWorldLoaded_ = true;
    }
    // A puzzle draft keeps the campaign wherever it currently is,
    // so derive its render assets from the edited document rather
    // than from the unrelated campaign location. Composed
    // overworld drafts select maps through their per-screen
    // regions instead.
    const std::optional<LevelLocation> draftLocation =
        tools_->levelEditor.draftOverworldMap()
        ? std::nullopt
        : levelLocationFromScreenPath(
              tools_->levelEditor.loadedDocumentPath());
    // Playing a draft leaves the document view; a half-finished
    // paint session would otherwise keep painting on the level
    // being played.
    tools_->splatPainter.close();
    (void)applyLevel(
        std::move(level), nullptr, draftLocation);
    if (tools_->levelEditor.draftOverworldMap()) {
        gameplaySession_.setActionAdmissionPolicy(
            [this](const GameState& state) {
                const OverworldMap* map =
                    tools_->levelEditor.draftOverworldMap();
                return map &&
                    overworldActionStateAllowed(*map, state);
            });
    } else if (fromAuthoredStart && !tools_->levelEditor.editingOverworld() &&
        developerFilesWritable()) {
        const LevelEditor& editor = tools_->levelEditor;
        const auto& path = editor.loadedDocumentPath();
        std::string name = "the editor draft";
        if (!path.empty()) {
            const auto absolute = std::filesystem::absolute(path).lexically_normal();
            const auto sourceRoot = std::filesystem::absolute(editor.sourceLevelRoot()).lexically_normal();
            const auto relative = absolute.lexically_relative(sourceRoot);
            const auto display = (!relative.empty() && *relative.begin() != ".."
                    ? relative : absolute).generic_u8string();
            name.assign(display.begin(), display.end());
            name += " (draft)";
        }
        solveCapture_ = std::make_unique<solution::SolveToStore>(solution::SolveToStore {
            .name = std::move(name),
            .definition = editor.documentDefinition(),
            .playingDraft = true,
        });
    }
}

void Application::playEditorDraft(std::optional<GridPosition3> heroStart)
{
    if (std::optional<Level> level =
            tools_->levelEditor.beginDraftPlayback(
                &tools_->overworldMapEditor, heroStart)) {
        startEditorDraft(std::move(*level), !heroStart);
    }
}

namespace {

// Copies an edited source file over its staged copy, so a later load (and
// the next launch) sees it without a content build.
void mirrorIntoRuntime(
    const std::filesystem::path& source,
    const std::filesystem::path& runtime)
{
    std::ifstream stream(source, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot read " + source.string());
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    std::filesystem::create_directories(runtime.parent_path());
    atomicFile::write(runtime, contents.str());
}

} // namespace

void Application::configureSourceWatcher()
{
    const std::filesystem::path sourceAssets = SOKOBAN_SOURCE_ASSET_DIR;
    SourceWatcher& watcher = tools_->sourceWatcher;
    watcher.watchTree(SOKOBAN_SOURCE_LEVEL_DIR, { ".scr", ".json" }, true);
    std::vector<std::filesystem::path> files {
        sourceAssets / "manifest.json",
        sourceAssets / "animation_catalog.json",
    };
    for (const AssetManifest::Texture& texture : assetManifest_.textures()) {
        files.push_back(sourceAssets / texture.path);
    }
    watcher.setWatchedFiles(files);
    (void)watcher.poll();
    log::info(log::Category::Assets)
        << "Watching " << watcher.watchedCount()
        << " source files for edits made outside the game.";
}

void Application::observeEditorLevelSaves()
{
    const auto revision = tools_->levelEditor.sourceLevelSaveRevision();
    if (developerFilesWritable() && revision != editorSourceLevelSaveRevision_) {
        editorSourceLevelSaveRevision_ = revision;
        tools_->requestSolutionReconcile();
    }
}

void Application::observeEditorLevelPublications()
{
    observeEditorLevelSaves();
    const auto levelRevision = tools_->levelEditor.runtimePublicationRevision();
    const auto overworldRevision = tools_->overworldMapEditor.runtimePublicationRevision();
    if (levelRevision == editorLevelPublicationRevision_ &&
        overworldRevision == editorOverworldPublicationRevision_) {
        return;
    }
    editorLevelPublicationRevision_ = levelRevision;
    editorOverworldPublicationRevision_ = overworldRevision;
    invalidateLevelCatalog();
}

void Application::invalidateLevelCatalog()
{
    // The active world still uses the old topology until a world load.
    levelCatalogStale_ = true;
    screenPreviewActive_ = false;
    screenPreviewLevel_.reset();
    screenPreviewTarget_.reset();
    gameplayGroundGeometryStore_.invalidate();
    previewGroundGeometryStore_.invalidate();
    renderer_.cancelQueuedAssetPrefetches();
}

void Application::serviceSourceWatcher()
{
    // Smoke and evidence runs must see exactly the staged content.
    if (!developerFilesWritable()) {
        return;
    }
    const std::uint64_t now = SDL_GetTicks();
    if (now - tools_->lastSourcePollTicks < 500) {
        return;
    }
    tools_->lastSourcePollTicks = now;
    if (!tools_->sourceWatcherConfigured) {
        tools_->sourceWatcherConfigured = true;
        configureSourceWatcher();
        // Levels may have changed while the game was closed.
        tools_->requestSolutionReconcile();
        return;
    }
    const std::filesystem::path sourceLevels = SOKOBAN_SOURCE_LEVEL_DIR;
    const std::filesystem::path sourceAssets = SOKOBAN_SOURCE_ASSET_DIR;
    std::vector<std::filesystem::path> changedLevels;
    for (const std::filesystem::path& changed : tools_->sourceWatcher.poll()) {
        try {
            const std::filesystem::path relativeToLevels =
                changed.lexically_relative(sourceLevels);
            if (!relativeToLevels.empty() &&
                *relativeToLevels.begin() != "..") {
                if (*relativeToLevels.begin() != "Deleted") {
                    changedLevels.push_back(changed);
                }
                continue;
            }
            if (changed == (sourceAssets / "manifest.json").lexically_normal()) {
                reloadSourceManifest(changed);
                continue;
            }
            if (changed ==
                (sourceAssets / "animation_catalog.json").lexically_normal()) {
                reloadSourceAnimationCatalog(changed);
                continue;
            }
            for (const AssetManifest::Texture& texture :
                 assetManifest_.textures()) {
                if ((sourceAssets / texture.path).lexically_normal() ==
                    changed) {
                    reloadSourceTexture(changed, texture.name, texture.path);
                    break;
                }
            }
        } catch (const std::exception& error) {
            log::error(log::Category::Assets)
                << "Could not reload " << changed.string() << ": "
                << error.what();
        }
    }
    if (!changedLevels.empty()) {
        try {
            reloadSourceLevels(changedLevels);
        } catch (const std::exception& error) {
            log::error(log::Category::Assets)
                << "Could not reload source levels: " << error.what();
        }
    }
}

void Application::reloadSourceLevels(
    const std::vector<std::filesystem::path>& sources)
{
    const std::filesystem::path sourceLevels = SOKOBAN_SOURCE_LEVEL_DIR;
    const std::filesystem::path runtimeLevels = assetRoot_ / "levels";
    const auto result = LevelProjectStore::publishRuntime(sourceLevels, runtimeLevels);
    if (!result.succeeded) {
        throw std::runtime_error(result.message);
    }
    invalidateLevelCatalog();
    // A saved draft may now match a recording waiting in solutions/drafts/.
    tools_->requestSolutionReconcile();

    LevelEditor& editor = tools_->levelEditor;
    const std::filesystem::path openDocument = editor.loadedDocumentPath().empty()
        ? std::filesystem::path {}
        : std::filesystem::absolute(editor.loadedDocumentPath()).lexically_normal();
    const auto changedDocument = std::ranges::find(sources, openDocument);
    if (changedDocument != sources.end()) {
        if (editor.dirty()) {
            log::warning(log::Category::Editor)
                << changedDocument->filename().string()
                << " changed on disk; the editor keeps your unsaved draft.";
        } else if (std::filesystem::is_regular_file(*changedDocument) &&
            editor.reloadFromDisk()) {
            log::info(log::Category::Editor)
                << "Reloaded " << changedDocument->filename().string()
                << " in the editor after it changed on disk.";
        }
    }

    if (!campaign_.gameLoaded() || campaign_.inOverworld() ||
        editor.editingDocument() || editor.playingDraft() || levelTransition_.active()) {
        return;
    }
    const auto currentScreen = screenPath(campaign_.currentLevel(), campaign_.currentScreen());
    const auto currentSource = sourceLevels / currentScreen.lexically_relative(runtimeLevels);
    if (std::ranges::find(sources, currentSource.lexically_normal()) == sources.end()) {
        return;
    }
    campaign_.writeCheckpoint(playerProfile_, gameplaySession_.snapshot());
    loadCurrentScreen();
    log::info(log::Category::Gameplay)
        << "Reloaded gameplay after its source levels changed.";
}

void Application::reloadSourceTexture(
    const std::filesystem::path& source,
    const std::string& textureName,
    const std::string& relativePath)
{
    const std::filesystem::path runtime = assetRoot_ / relativePath;
    mirrorIntoRuntime(source, runtime);
    // Prepared (BC7) copies of the old pixels would otherwise win on the
    // next load.
    invalidateCompressedTextureArtifactsForSource(assetRoot_, relativePath);
    (void)refreshContentPackageIndex(assetRoot_);
    const RenderTexture texture =
        assetManifest_.findTextureIdByName(textureName);
    // A texture that is not resident yet picks the file up when it loads.
    const bool updated = !texture.isNone() &&
        renderer_.updateTexture(texture, loadRgbaImage(runtime));
    log::info(log::Category::Assets)
        << "Reloaded texture " << textureName
        << (updated ? "" : " (loads on first use)");
}

void Application::reloadSourceManifest(const std::filesystem::path& source)
{
    AssetManifest updated = AssetManifest::loadFromFile(source);
    if (assetManifest_ == updated) {
        // An edit that needed a restart may have been reverted.
        if (!tools_->manifestReloadStatus.empty()) {
            tools_->manifestReloadStatus =
                "manifest.json matches the running game again.";
        }
        return;
    }
    if (!assetManifest_.adoptLiveFields(updated)) {
        tools_->manifestReloadStatus =
            "manifest.json changed in a way that needs a restart (a model, "
            "texture, animation, role or tile model). Tile scales and "
            "volumes apply live.";
        log::warning(log::Category::Assets) << tools_->manifestReloadStatus;
        return;
    }
    mirrorIntoRuntime(source, assetRoot_ / "manifest.json");
    (void)refreshContentPackageIndex(assetRoot_);
    presentationSettings_.applyTileScales(assetManifest_);
    gameplayGroundGeometryStore_.invalidate();
    previewGroundGeometryStore_.invalidate();
    audioSystem_->applyManifestVolumes();
    tools_->manifestReloadStatus =
        "Applied manifest.json live (tile scales and volumes).";
    log::info(log::Category::Assets) << tools_->manifestReloadStatus;
}

void Application::reloadSourceAnimationCatalog(
    const std::filesystem::path& source)
{
    AnimationCatalogEditor& editor = tools_->animationCatalogEditor;
    if (editor.dirty()) {
        log::warning(log::Category::Assets)
            << "animation_catalog.json changed on disk; the Animation tab "
               "keeps your unsaved edits.";
        return;
    }
    if (!editor.reload(assetManifest_)) {
        log::warning(log::Category::Assets) << editor.status();
        return;
    }
    mirrorIntoRuntime(source, assetRoot_ / "animation_catalog.json");
    (void)refreshContentPackageIndex(assetRoot_);
    animationCatalog_ = editor.catalog();
    log::info(log::Category::Assets)
        << "Reloaded animation_catalog.json after it changed on disk.";
}

void Application::handleDraftPlaybackShortcuts(const InputRouter::Frame& input)
{
    if (levelTransition_.active()) {
        return;
    }
    if (input.toggleDraftPlaybackPressed &&
        tools_->levelEditor.playingDraft()) {
        tools_->stopDraftPlayback();
        return;
    }
    if (!tools_->levelEditor.editingDocument()) {
        return;
    }
    if (input.toggleDraftPlaybackPressed) {
        playEditorDraft(std::nullopt);
    } else if (input.playDraftFromCursorPressed) {
        // Off the board, the editor reports that the cursor must be on it.
        playEditorDraft(
            tools_->pickedCell.value_or(GridPosition3 { -1, -1, 0 }));
    }
}
#endif

#if SOKOBAN_ENABLE_DEBUG_UI
// Builds the editor-only frame from the open document, pending move,
// neighbouring overworld screens, and palette selector state.
RenderFrameData Application::buildEditorRenderFrame(
    const InputRouter::EditorInput& editorInput, float beltScrollOffset,
    FrameArena& arena)
{
    const auto& editorLevels = tools_->levelEditor.levelBrowserSnapshot();
    const std::optional<LevelEditor::MoveObject>& pendingMove =
        tools_->levelEditor.pendingMove();
    const std::optional<TileType> movedTile =
        pendingMove &&
            pendingMove->kind == LevelEditor::MoveObject::Kind::Tile
        ? std::optional<TileType> { pendingMove->tile }
        : std::nullopt;
    ArenaArray<RenderFrameBuilder::EditorInput::OverworldNeighbor>
        overworldNeighbors(arena, 8);
    const std::optional<OverworldScreenId> editedOverworldScreen =
        tools_->levelEditor.overworldScreenId();
    const OverworldMapEditor& topology = tools_->overworldMapEditor;
    const bool matchingTopologyRoot =
        tools_->levelEditor.showOverworldNeighbors() &&
        editedOverworldScreen && topology.loaded() &&
        tools_->matchingOverworldEditorRoot();
    if (tools_->levelEditor.showOverworldNeighbors() &&
        editedOverworldScreen && topology.loaded() &&
        matchingTopologyRoot) {
        const OverworldScreenSpec* active =
            topology.screen(*editedOverworldScreen);
        if (active) {
            for (const OverworldScreenSpec& candidate :
                 topology.layout().screens) {
                const int deltaX = candidate.slot.x - active->slot.x;
                const int deltaY = candidate.slot.y - active->slot.y;
                if ((deltaX == 0 && deltaY == 0) ||
                    std::abs(deltaX) > 1 || std::abs(deltaY) > 1) {
                    continue;
                }
                if (const Level::Definition* definition =
                        tools_->groundPaintingNeighborDefinition(candidate.id)) {
                    overworldNeighbors.push_back({
                        .screen = candidate.id,
                        .origin = {
                            deltaX * static_cast<int>(
                                topology.layout().screenWidth),
                            deltaY * static_cast<int>(
                                topology.layout().screenHeight),
                        },
                        .width = topology.layout().screenWidth,
                        .height = topology.layout().screenHeight,
                        .definition = definition,
                    });
                }
            }
        }
    }
    return RenderFrameBuilder::buildEditor({
        .manifest = assetManifest_,
        .editor = tools_->levelEditor,
        .settings = presentationSettings_,
        .animations = &animationCatalog_,
        .showDebugView = tools_->levelEditorDebugUi.showDebugView(),
        .hoverCell = tools_->hoverCell,
        .hoverDecoration = tools_->hoverDecoration,
        .deleting =
            (editorInput.deleting &&
                tools_->levelEditor.tool() == LevelEditor::Tool::Tiles) ||
            (editorInput.moving && !pendingMove),
        .selectingMoveSource = editorInput.moving && !pendingMove,
        .editorPreviewTile = editorInput.moving
            ? movedTile
            : std::nullopt,
        .worldAnimationTimeSeconds =
            presentation_.worldAnimationTimeSeconds(),
        .animationTransitionTimeSeconds =
            presentation_.animationTransitionTimeSeconds(),
        .conveyorBeltScrollOffset = beltScrollOffset,
        .levelLocation =
            levelLocationFromScreenPath(tools_->levelEditor.loadedDocumentPath()),
        .overworldScreen = editedOverworldScreen,
        .overworldNeighbors = overworldNeighbors,
        .selectorState = [this, &editorLevels](LevelLocation target) {
            const auto level = std::ranges::find(
                editorLevels,
                target.level,
                &LevelEditor::LevelDirectory::index);
            const bool targetExists = level != editorLevels.end() &&
                std::ranges::find(
                    level->screens,
                    target.screen,
                    &LevelEditor::ScreenFile::index) != level->screens.end();
            return ScreenSelectorViewState {
                .status = targetExists
                    ? playerProfile_.selectorStatus(target)
                    : ScreenSelectorStatus::Unavailable,
                .lastScreenInLevel = targetExists &&
                    level->screens.back().index == target.screen,
            };
        },
        .groundGeometryCache = &editorGroundGeometryCache_,
    }, arena);
}
#endif

// The two evidence point-light fixtures: one animated caster, or the stress
// grid. Only a capture run turns either on, and neither has anything to say
// about an ordinary frame, which is why they are out of buildRenderFrame().
void Application::appendEvidencePointLights(RenderFrameData& frame) const
{
    if (evidencePointLightEnabled_) {
        std::optional<std::size_t> animatedCaster;
        for (std::size_t tileIndex = 0;
             tileIndex < frame.tiles.size();
             ++tileIndex) {
            if (frame.tiles[tileIndex].animationInstanceId != 0) {
                animatedCaster = tileIndex;
                break;
            }
        }
        frame.lighting.pointLightCount = 1;
        frame.lighting.pointLights[0] = {
            .position = {
                static_cast<float>(frame.levelWidth) * 0.42f,
                static_cast<float>(frame.levelHeight) * 0.45f,
                2.25f,
            },
            .color = { 1.0f, 0.72f, 0.48f },
            .intensity = 7.0f,
            .range = 3.5f,
            .castsShadows = true,
            .shadowBias = 0.012f,
            .shadowOpacity = 0.85f,
            .emitterTileIndex = animatedCaster,
        };
    } else if (evidencePointLightStressEnabled_) {
        constexpr std::array<Vec3, RenderFrameData::pointLightCapacity>
            stressColors {
                Vec3 { 1.0f, 0.42f, 0.30f },
                Vec3 { 0.35f, 0.62f, 1.0f },
                Vec3 { 0.45f, 1.0f, 0.52f },
                Vec3 { 1.0f, 0.82f, 0.34f },
                Vec3 { 0.90f, 0.38f, 1.0f },
                Vec3 { 0.30f, 1.0f, 0.92f },
                Vec3 { 1.0f, 0.56f, 0.74f },
                Vec3 { 0.72f, 0.76f, 1.0f },
            };
        constexpr std::array<Vec2, RenderFrameData::pointLightCapacity>
            stressPositions {
                Vec2 { 0.18f, 0.18f }, Vec2 { 0.50f, 0.15f },
                Vec2 { 0.82f, 0.18f }, Vec2 { 0.85f, 0.50f },
                Vec2 { 0.82f, 0.82f }, Vec2 { 0.50f, 0.85f },
                Vec2 { 0.18f, 0.82f }, Vec2 { 0.15f, 0.50f },
            };
        const float width = static_cast<float>(frame.levelWidth);
        const float height = static_cast<float>(frame.levelHeight);
        const float range = std::max(width, height) * 1.35f;
        frame.lighting.pointLightCount =
            RenderFrameData::pointLightCapacity;
        for (std::size_t lightIndex = 0;
             lightIndex < RenderFrameData::pointLightCapacity;
             ++lightIndex) {
            frame.lighting.pointLights[lightIndex] = {
                .position = {
                    width * stressPositions[lightIndex].x,
                    height * stressPositions[lightIndex].y,
                    2.1f + 0.15f * static_cast<float>(lightIndex % 3),
                },
                .color = stressColors[lightIndex],
                .intensity = 3.5f,
                .range = range,
                .castsShadows = true,
                .shadowBias = 0.012f,
                .shadowOpacity = 0.72f,
            };
        }
    }
}

CameraAngles Application::gameplayCameraAngles() const
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (tools_->levelEditor.editingDocument()) {
        return tools_->levelEditor.cameraAngles().value_or(CameraAngles {});
    }
    const OverworldMap* draftOverworld = tools_->levelEditor.draftOverworldMap();
    const bool editorDraftPlaying = tools_->levelEditor.playingDraft();
#else
    constexpr const OverworldMap* draftOverworld = nullptr;
    constexpr bool editorDraftPlaying = false;
#endif
    const OverworldMap* overworld = draftOverworld
        ? draftOverworld
        : (!editorDraftPlaying && campaign_.inOverworld() && overworldMap_
              ? &*overworldMap_
              : nullptr);
    if (overworld) {
        const std::optional<OverworldScreenId> activeScreen = draftOverworld
            ? CampaignSession::sharedPlayerScreen(*overworld, gameplaySession_.state())
            : std::optional<OverworldScreenId> { campaign_.activeOverworldScreen() };
        if (activeScreen) {
            if (const OverworldScreenRuntime* screen = overworld->screen(*activeScreen)) {
                return screen->definition.cameraAngles.value_or(CameraAngles {});
            }
        }
    }
    return level_.cameraAngles().value_or(CameraAngles {});
}

RenderFrameData Application::buildGroundRimEvidenceFrame(FrameArena& arena) const
{
    RenderFrameData frame(arena);
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = 10;
    frame.levelHeight = 7;
    frame.cameraExtent = RenderFrameData::CameraExtent { 0, 0, 0, 10, 7, 1 };
    frame.cameraPitchDegrees = 40.0f;
    frame.cameraYawDegrees = -35.0f;
    frame.lighting = presentationSettings_.renderLighting();
    frame.gridOverlay = presentationSettings_.renderGridOverlay();
    frame.outputTransform = presentationSettings_.renderOutputTransform();
    frame.waterRendering = presentationSettings_.water;
    frame.waterGridBounds = { 0, 0, 10, 7 };
    frame.groundSplat = groundSplatTexturesForScreen(
        [this](std::string_view name) { return assetManifest_.findTextureIdByName(name); },
        std::nullopt);
    const auto append = [&](int x, int y) {
        auto tile = tileVisual(TileType::Ground, { x, y, 0 }, assetManifest_, presentationSettings_);
        constexpr std::array<std::string_view, 10> names {
            "GroundRock01", "GroundRock02", "GroundRock03", "GroundRock04", "GroundRock05",
            "GroundRock06", "GroundRock07", "GroundRock08", "GroundRock09", "GroundRock10",
        };
        if (auto model = assetManifest_.findModelIdByName(names[(x + y * 3) % 10])) {
            tile.model = *model;
            tile.groundRockVariant = static_cast<uint32_t>((x + y * 3) % 10);
        }
        frame.tiles.push_back(tile);
    };
    for (int y = 1; y <= 3; ++y) for (int x = 1; x <= 3; ++x) {
        if (x != 2 || y != 2) append(x, y);
    }
    for (int y = 1; y <= 2; ++y) for (int x = 6; x <= 7; ++x) append(x, y);
    for (int x = 5; x <= 8; ++x) append(x, 5);
    append(1, 5);
    frame.waterSurfaces.push_back({ .cell = { 0, 1, 0 }, .position = { 0, 1 },
        .size = { 1, 1 }, .color = { 0.2f, 0.4f, 0.7f, 1.0f }, .elevation = 0.85f });
    frame.lighting.pointLights[0] = { .position = { 4, 3, 4 },
        .color = { 1.0f, 0.82f, 0.65f }, .intensity = 5.0f, .range = 10.0f };
    frame.lighting.pointLightCount = 1;
    processGroundTileGeometry({ frame.tiles.data(), frame.tiles.size() }, assetManifest_, &arena);
    if (presentationSettings_.geometry.processGroundGeometry) {
        processGroundGeometry({ frame.tiles.data(), frame.tiles.size() }, assetManifest_,
            &arena, &gameplayGroundGeometryCache_);
        if (presentationSettings_.geometry.smoothGroundRim) {
            frame.requestedGroundRimWidth = presentationSettings_.geometry.groundRimWidth;
            frame.requestedGroundRimDepth = presentationSettings_.geometry.groundRimDepth;
            for (auto& tile : frame.tiles) if (tile.groundGeometryEligible) {
                tile.groundRimWidth = frame.requestedGroundRimWidth;
                tile.groundRimDepth = frame.requestedGroundRimDepth;
            }
        }
    }
    return frame;
}

RenderFrameData Application::buildMakerPlaytestFrame(FrameArena& arena)
{
    auto& playtest = *makerPlaytest_;
    const auto& level = playtest.level;
    const auto& session = playtest.session;
    const auto& presentation = playtest.presentation;
    const GameState projectedState = session.projectedState();
    const auto& activation = session.activationPreview(level);
    const auto& projectedActivation = session.activationPreview(level, true);
    RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = assetManifest_,
        .level = level,
        .state = session.state(),
        .activeHeroController = session.activeHeroController(),
        .moving = session.moving(),
        .projectedState = projectedState,
        .presentation = presentation,
        .settings = presentationSettings_,
        .animations = &animationCatalog_,
        .conveyorBeltScrollOffset = presentation.conveyorBeltScrollOffset(
            session.stepDurationSeconds()),
        .cameraPitchDegrees = presentation.cameraPitchDegrees(),
        .cameraYawDegrees = presentation.cameraYawDegrees(),
        .cachedActivationPreviews = true,
        .activationPreview = activation ? &*activation : nullptr,
        .projectedActivationPreview = session.moving() && projectedActivation
            ? &*projectedActivation : nullptr,
        .invalidMirrorBeams = session.invalidMirrorBeams(level),
        .projectedInvalidMirrorBeams = session.invalidMirrorBeams(level, true),
        .invalidMirrorEntities = session.invalidMirrorPreview(level).entities,
        .projectedInvalidMirrorEntities = session.invalidMirrorPreview(level, true).entities,
        .groundGeometryCache = &playtest.groundGeometry,
    }, arena);
    playtest.particles.appendRenderData(frame);
    playtest.turretFlames.appendRenderData(frame, turretParticleEffects_);
    return frame;
}

RenderFrameData Application::buildRenderFrame(
    const InputRouter::EditorInput& editorInput)
{
    (void)editorInput;
    FrameArena& arena = beginRenderFrameArena();
    if (makerPlaytest_) {
        return buildMakerPlaytestFrame(arena);
    }
    if (ui_.drawData().sceneViewport && makerSession_) {
        const auto resizePreviewSize = makerDraftDialog_ && showsMakerResizePreview(makerDraftDialog_->mode)
            ? std::optional { makerView_.resizePreviewSize() } : std::nullopt;
        RenderFrameData frame = RenderFrameBuilder::buildEditor({
            .manifest = assetManifest_,
            .editor = makerSession_->editor(),
            .settings = presentationSettings_,
            .animations = &animationCatalog_,
            .hoverCell = makerHover_,
            .deleting = makerSession_->tool() == MakerSession::Tool::Erase,
            .allowBoardExpansion = false,
            .worldAnimationTimeSeconds = presentation_.worldAnimationTimeSeconds(),
            .animationTransitionTimeSeconds = presentation_.animationTransitionTimeSeconds(),
            .groundGeometryCache = &editorGroundGeometryCache_,
            .resizePreviewSize = resizePreviewSize,
        }, arena);
        frame.cameraPitchDegrees = 55.0f;
        frame.cameraYawDegrees = 24.0f;
        frame.gridOverlay = { { 0.10f, 0.12f, 0.14f, 0.78f }, 2.0f };
        if (!resizePreviewSize) {
            frame.cameraExtent = RenderFrameData::CameraExtent {
                .originX = -1,
                .originY = -1,
                .originZ = -1,
                .width = frame.levelWidth + 2,
                .height = frame.levelHeight + 2,
                .depth = frame.levelDepth + 1,
            };
        }
        return frame;
    }
    if (evidenceEffects_ == "rock-scratches") {
        if (!evidenceRockScratchFixture_) {
            evidenceRockScratchFixture_ =
                std::make_unique<RockScratchEvidenceFixture>(&animationCatalog_);
        }
        return evidenceRockScratchFixture_->build(assetManifest_, presentationSettings_,
            gameplayGroundGeometryCache_, arena);
    }
    if (evidenceGroundRimFixture_) {
        return buildGroundRimEvidenceFrame(arena);
    }
    const float beltScrollOffset =
        presentation_.conveyorBeltScrollOffset(
            gameplaySession_.stepDurationSeconds());
#if SOKOBAN_ENABLE_DEBUG_UI
    if (std::optional<RenderFrameData> preview =
            tools_->animationPreviewDebugUi.previewFrame(
                assetManifest_, presentationSettings_, &arena)) {
        RenderFrameData frame = std::move(*preview);
        tools_->applyDetachedCamera(frame);
        return frame;
    }
    if (tools_->levelEditor.editingDocument()) {
        RenderFrameData frame =
            buildEditorRenderFrame(editorInput, beltScrollOffset, arena);
        tools_->applyDetachedCamera(frame);
        return frame;
    }
#endif

    if (!campaign_.gameLoaded()) {
        // Title-only: nothing to draw behind the fullscreen menu.
        return RenderFrameData {};
    }

    // Held by reference for the duration of the call, so it has to outlive it.
    const GameState& projectedState = gameplaySession_.projectedStateView();
#if SOKOBAN_ENABLE_DEBUG_UI
    const OverworldMap* draftOverworld =
        tools_->levelEditor.draftOverworldMap();
    const bool editorDraftPlaying = tools_->levelEditor.playingDraft();
#else
    constexpr const OverworldMap* draftOverworld = nullptr;
    constexpr bool editorDraftPlaying = false;
#endif
    // A regular editor draft does not change the campaign location. Do not
    // let an overworld underneath that draft contribute its camera,
    // visibility mask, or splat regions to the draft frame.
    const bool renderCampaignOverworld =
        !editorDraftPlaying &&
        campaign_.inOverworld() && overworldMap_;
    const OverworldMap* renderedOverworld = draftOverworld
        ? draftOverworld
        : (renderCampaignOverworld
              ? &*overworldMap_
              : nullptr);
    std::optional<OverworldView> overworldView;
    std::optional<OverworldScreenId> renderedActiveScreen;
    if (renderedOverworld && !presentation_.players().empty()) {
        renderedActiveScreen = draftOverworld
            ? CampaignSession::sharedPlayerScreen(
                  *renderedOverworld, gameplaySession_.state())
            : std::optional<OverworldScreenId> {
                  campaign_.activeOverworldScreen() };
    }
    if (renderedOverworld && renderedActiveScreen &&
        !presentation_.players().empty()) {
        overworldView = calculateOverworldView(
            *renderedOverworld,
            *renderedActiveScreen,
            gameplaySession_.state(),
            projectedState,
            presentation_.players().front().motion.renderPosition,
            overworldOverviewProgress_, &arena);
    }
    std::function<bool(GridPosition3)> visibleOverworldCell;
    ArenaArray<RenderFrameBuilder::GameplayInput::GroundSplatRegion>
        groundSplatRegions(arena, overworldView ? overworldView->visibleScreens.size() : 0);
    if (overworldView && renderedOverworld) {
        visibleOverworldCell = [
            renderedOverworld,
            &overworldView
        ](GridPosition3 cell) {
            const std::optional<OverworldScreenId> owner =
                renderedOverworld->screenAt(cell);
            const auto& visibleScreens = overworldView->visibleScreens;
            return owner && std::ranges::find(visibleScreens, *owner) !=
                visibleScreens.end();
        };
        for (OverworldScreenId screenId : overworldView->visibleScreens) {
            const OverworldScreenRuntime* screen =
                renderedOverworld->screen(screenId);
            if (screen == nullptr) {
                continue;
            }
            groundSplatRegions.push_back({
                .screenId = screenId,
                .origin = screen->origin,
                .width = renderedOverworld->layout().screenWidth,
                .height = renderedOverworld->layout().screenHeight,
                .definition = &screen->definition,
            });
        }
    }
    const auto& activationPreview = gameplaySession_.activationPreview(level_);
    const auto& projectedActivationPreview = gameplaySession_.activationPreview(level_, true);
    RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = assetManifest_,
        .level = level_,
        .state = gameplaySession_.state(),
        .activeHeroController = gameplaySession_.activeHeroController(),
        .moving = gameplaySession_.moving(),
        .projectedState = projectedState,
        .presentation = presentation_,
        .settings = presentationSettings_,
        .animations = &animationCatalog_,
        .conveyorBeltScrollOffset = beltScrollOffset,
        .cameraPitchDegrees = presentation_.cameraPitchDegrees(),
        .cameraYawDegrees = presentation_.cameraYawDegrees(),
        .cameraExtent = overworldView
            ? std::optional<RenderFrameData::CameraExtent> {
                  overworldView->cameraExtent }
            : std::nullopt,
        .cameraExtentTransitionTarget = overworldView
            ? std::optional<RenderFrameData::CameraExtent> {
                  overworldView->overviewCameraExtent }
            : std::nullopt,
        .cameraExtentTransitionProgress = overworldView
            ? overworldView->overviewProgress
            : 0.0f,
        .cameraOffset = overworldView
            ? overworldView->cameraOffset
            : Vec2 {},
        .visibleCell = std::move(visibleOverworldCell),
        .groundSplatRegions = groundSplatRegions,
        // Draft play does not move the campaign. Select the map belonging to
        // the edited puzzle even when the campaign is in the overworld or on
        // another screen, matching the editor preview above.
#if SOKOBAN_ENABLE_DEBUG_UI
        .levelLocation = editorDraftPlaying
            ? levelLocationFromScreenPath(
                  tools_->levelEditor.loadedDocumentPath())
            : campaign_.inOverworld()
                ? std::nullopt
                : std::optional<LevelLocation> { campaign_.location() },
#else
        .levelLocation = campaign_.inOverworld()
            ? std::nullopt
            : std::optional<LevelLocation> { campaign_.location() },
#endif
        .selectorState = [this](LevelLocation target) {
            return campaign_.selectorViewState(playerProfile_, target);
        },
        .cachedActivationPreviews = true,
        .activationPreview = activationPreview ? &*activationPreview : nullptr,
        .projectedActivationPreview = gameplaySession_.moving() && projectedActivationPreview
            ? &*projectedActivationPreview : nullptr,
        .invalidMirrorBeams = gameplaySession_.invalidMirrorBeams(level_),
        .projectedInvalidMirrorBeams = gameplaySession_.invalidMirrorBeams(level_, true),
        .invalidMirrorEntities = gameplaySession_.invalidMirrorPreview(level_).entities,
        .projectedInvalidMirrorEntities = gameplaySession_.invalidMirrorPreview(level_, true).entities,
        .groundGeometryCache = &gameplayGroundGeometryCache_,
    }, arena);
    frame.groundChunksRequested = !editorDraftPlaying && presentationSettings_.geometry.groundChunks &&
        presentationSettings_.geometry.processGroundGeometry && presentationSettings_.geometry.smoothGroundRim;
    frame.groundChunkMeshoptimizer = presentationSettings_.geometry.groundChunkMeshoptimizer;
    if (!editorDraftPlaying && !gameplayGroundGeometrySource_.empty() &&
        presentationSettings_.geometry.smoothGroundRim &&
        presentationSettings_.geometry.processGroundGeometry) {
        frame.processedGroundArtifact = gameplayGroundGeometryStore_.get(
            gameplayGroundGeometrySource_, level_, assetManifest_,
            gameplayGroundGeometryCache_.rebuildCount(), assetRoot_);
    }
    if (renderCampaignOverworld && overworldView && renderedOverworld) {
        appendOverworldFogVolumes(
            frame.overworldFogVolumes,
            *renderedOverworld,
            overworldView->visibleScreens,
            playerProfile_.overworldDiscovery.screens,
            overworldFogReveal_);
    }
    particleSystem_.appendRenderData(frame);
    turretFlames_.appendRenderData(frame, turretParticleEffects_);
    frame.levelTransitionAmount = levelTransition_.amount();
    if (evidenceWaterEnabled_) {
        appendEvidenceWaterFixture(frame);
    }
    appendEvidencePointLights(frame);
    appendEvidenceEffects(frame);
    if (evidenceWaterDisabled_) {
        frame.waterSurfaces.clear();
    }
    if (evidenceWaterReflectionsDisabled_) {
        frame.waterRendering.reflectionStrength = 0.0f;
    }
#if SOKOBAN_ENABLE_DEBUG_UI
    tools_->applyDetachedCamera(frame);
#endif
    return frame;
}

} // namespace sokoban
