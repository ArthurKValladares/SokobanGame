#include "engine/Application.hpp"
#if SOKOBAN_ENABLE_DEBUG_UI
#include "engine/ApplicationTools.hpp"
#include "engine/DebugUi.hpp"
#endif

#include "engine/ParticleConfig.hpp"
#include "engine/render/CameraConfig.hpp"

#include "engine/Log.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/Rules.hpp"
#include "engine/RuntimeContent.hpp"
#include "engine/UserSettingsConfig.hpp"
#include "engine/ui/SelectorPrompt.hpp"
#include "engine/ui/UiConfig.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <exception>
#include <functional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sokoban {
namespace {

AntiAliasingMode antiAliasingModeForSamples(int samples)
{
    switch (samples) {
    case 1: return AntiAliasingMode::None;
    case 2: return AntiAliasingMode::Msaa2x;
    case 4: return AntiAliasingMode::Msaa4x;
    default: return AntiAliasingMode::Msaa8x;
    }
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
    , assetRoot_(runtimeContentRoot())
    , assetManifest_(AssetManifest::loadFromFile(assetRoot_ / "manifest.json"))
    , animationCatalog_(AnimationCatalog::loadFromFile(
          assetRoot_ / "animation_catalog.json", assetManifest_))
    , uiFont_(FontAtlas::load(
          assetRoot_ / config::uiFontPath,
          config::uiFontPixelHeight,
          config::uiFontAtlasSize))
    , inputPrompts_(assetRoot_, assetManifest_)
    , renderer_(
          window_.nativeHandle(),
          assetRoot_,
          saveSlots_.directory() / "vulkan_pipeline_cache.bin",
          assetManifest_,
          uiFont_,
          antiAliasingModeForSamples(
              playerProfile_.settings.video.antiAliasingSamples),
          options.evidenceOutputDirectory.empty()
              ? playerProfile_.settings.video.effectiveRenderScalePercent()
              : options.evidenceRenderScalePercent,
          {
              .vsync = playerProfile_.settings.video.vsync,
              .allowTearing = playerProfile_.settings.video.allowTearing,
          })
    , ui_(uiFont_)
    , audioSystem_(assetRoot_, assetManifest_)
    , mirrorSwapParticleEffect_(
          makeMirrorSwapParticleEffect(assetManifest_))
    , settingsCoordinator_(playerProfile_, presentationSettings_)
#if SOKOBAN_ENABLE_DEBUG_UI
    , tools_(std::make_unique<ApplicationTools>())
#endif
    , renderFrameArenas_ {
          FrameArena("render frame A", renderFrameArenaBytes()),
          FrameArena("render frame B", renderFrameArenaBytes()),
      }
    , smokeFrames_(options.smokeFrames)
    , evidenceOutputDirectory_(
          std::move(options.evidenceOutputDirectory))
    , evidenceAmbientOcclusionEnabled_(
          options.evidenceAmbientOcclusionEnabled)
{
    log::info(log::Category::Persistence)
        << saveSlots_.progressStatus();
    buildLevelCatalog();
    restoreProfileLocation();
    applySettingsEffects(settingsCoordinator_.initialize());
    if (!evidenceOutputDirectory_.empty()) {
        presentationSettings_.lighting.ambientOcclusionEnabled =
            evidenceAmbientOcclusionEnabled_;
    }
    presentationSettings_.applyTileScales(assetManifest_);
    presentationSettings_.normalize();
    presentation_.setAnimationCatalog(&animationCatalog_);
    screenPreviewPresentation_.setAnimationCatalog(&animationCatalog_);
    // The world stays unloaded until the title's Continue/New Game, but its
    // assets warm up in the background so that first load doesn't block.
    openTitleScreen();
    RenderAssetRequirements initialRequirements =
        renderAssetRequirementsForLevel(
            overworldMap_ ? overworldMap_->level()
                          : Level::loadFromFile(overworldPath()),
            assetManifest_);
    initialRequirements.merge(levelAssetRequirements(campaign_.currentLevel()));
    renderer_.preloadAssets(initialRequirements);

#if SOKOBAN_ENABLE_DEBUG_UI
    DebugUi::initialize();
    tools_->initialize(
        SOKOBAN_SOURCE_LEVEL_DIR,
        SOKOBAN_SOURCE_ASSET_DIR,
        assetRoot_,
        campaign_.currentLevel(),
        campaign_.currentScreen(),
        assetManifest_,
        animationCatalog_);

    DebugUi::addTab("Engine", [this] {
        int completedTargets = 0;
        for (LevelLocation target : campaign_.overworldTargets()) {
            completedTargets += playerProfile_.screenCompleted(target) ? 1 : 0;
        }
        const ApplicationDebugUi::Result result = tools_->applicationDebugUi.draw({
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
            .audio = audioSystem_,
            .saveDiagnostics = saveSlots_.progressDiagnostics(),
            .audioSettings = playerProfile_.settings.audio,
            .updateAudioSettings = [this](
                PlayerProfile::AudioSettings settings,
                bool persist) {
                applySettingsEffects(
                    settingsCoordinator_.applyAudioSettings(settings, persist));
            },
        });
        if (result.solveCurrentScreen) {
            solveCurrentScreenForDebug();
        }
    });
    DebugUi::addTab("Asset Manifest", [this] {
        tools_->assetManifestDebugUi.draw(tools_->assetManifestEditor);
    });
    DebugUi::addTab("Level Editor", [this] {
        tools_->levelEditorDebugUi.draw(
            tools_->levelEditor,
            tools_->overworldMapEditor,
            tools_->splatPainter,
            settingsCoordinator_.userSettings().input,
            {
            .playDraft = [this](Level level) {
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
                }
            },
            .returnToCurrentScreen = [this] {
                tools_->splatPainter.close();
                loadCurrentScreen();
            },
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
    // The gate was there because a snapshot taken mid-action would have caught
    // the world half-way through a transition. It does not: a snapshot holds
    // the *committed* state and the undo stack chained to it, and an action in
    // flight has contributed nothing to either. What is lost on reload is the
    // action itself, and the state it was planned from is still on disk with
    // whatever momentum it carried, so ambient motion simply plans it again.
    //
    // Under concurrency the world is rarely idle, so keeping the gate meant
    // saves quietly became rare exactly when there was most to lose. The one
    // cost is undo granularity: a slide whose push had already committed
    // reappears as its own undo entry rather than folded into that push.
    const bool editorDraftPlaying =
#if SOKOBAN_ENABLE_DEBUG_UI
        tools_->levelEditor.playingDraft();
#else
        false;
#endif
    if (campaign_.gameLoaded() && !editorDraftPlaying) {
        checkpointCurrentScreen(false);
    } else if (!playerProfile_.progressEmpty()) {
        persistProfile(true);
    }
    saveSlots_.flush();
    if (!saveSlots_.progressDiagnostics().lastWriteSucceeded) {
        log::error(log::Category::Persistence)
            << saveSlots_.progressStatus();
    }
    if (!saveSlots_.settingsDiagnostics().lastWriteSucceeded) {
        log::error(log::Category::Persistence)
            << saveSlots_.settingsStatus();
    }
#if SOKOBAN_ENABLE_DEBUG_UI
    DebugUi::clearTabs();
#endif
    renderer_.waitIdle();
}

#if SOKOBAN_ENABLE_DEBUG_UI
bool Application::bakeTileThumbnails()
{
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

void Application::run()
{
    if (smokeFrames_ > 0) {
        // Nobody is here to press New Game, and the title screen draws no
        // world behind it, so a smoke run that stayed on the title would
        // never record a tile, model, shadow or AO pass.
        log::info(log::Category::Application)
            << "Smoke run: starting a new game and rendering "
            << smokeFrames_ << " frames.";
        startNewGame();
    }
    std::uint64_t renderedFrames = 0;
    while (running_) {
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
#endif
        input_.beginFrame();

        SDL_Event event {};
        while (SDL_PollEvent(&event)) {
            renderer_.handleEvent(event);
            InputRouter::EventContext eventContext {
                .bindingCapture = optionsMenu_.capturingBinding(),
                .shellMenuOpen = shellMenuOpen(),
                .keyboardCaptured = renderer_.wantsKeyboardCapture(),
                .mouseCaptured = renderer_.wantsMouseCapture(),
            };
#if SOKOBAN_ENABLE_DEBUG_UI
            eventContext.editorEditing = tools_->levelEditor.editingDocument();
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

        const InputRouter::BackAction backAction = levelTransition_.active()
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
            handleShellEvent(ShellBackPressed {});
            break;
        case InputRouter::BackAction::None:
            break;
        }

        const InputRouter::Frame routedInput =
            inputRouter_.routeFrame(input_, inputRoutingContext());
        const float measuredDt = frameTimer_.tick(simulationTiming_);
        // Evidence runs compare separate renderer configurations. Freezing
        // simulation makes their scene/camera/animation inputs identical.
        const float dt = evidenceOutputDirectory_.empty()
            ? measuredDt
            : 0.0f;
        update(
            dt,
            routedInput,
            preparedRenderFrame_ ? &*preparedRenderFrame_ : nullptr);
        const Vec2 windowSize = window_.size();
        const Vec2 pixelSize = window_.sizeInPixels();
#if SOKOBAN_ENABLE_DEBUG_UI
        const bool developerWorkspaceVisible =
            evidenceOutputDirectory_.empty() &&
            !optionsMenu_.isOpen() && !titleScreen_.isOpen();
        if (!developerWorkspaceVisible) {
            renderer_.setGameViewportDisplay(std::nullopt);
        }
#else
        constexpr bool developerWorkspaceVisible = false;
#endif
        const Vec2 mouse = routedInput.pointer.position;
        const Vec2 mousePixels {
            windowSize.x > 0.0f
                ? mouse.x * pixelSize.x / windowSize.x
                : mouse.x,
            windowSize.y > 0.0f
                ? mouse.y * pixelSize.y / windowSize.y
                : mouse.y,
        };
        const Vec2 gameMousePixels = renderer_.mapPointerToGameViewport(
            mousePixels, pixelSize);

        ui_.beginFrame(
            pixelSize,
            gameMousePixels,
            routedInput.pointer.primaryDown,
            routedInput.pointer.primaryPressed);

        renderer_.beginDebugUiFrame();
#if SOKOBAN_ENABLE_DEBUG_UI
        if (developerWorkspaceVisible) {
            const VkExtent2D gameExtent = renderer_.renderExtent();
            const DebugUi::DrawResult workspace = DebugUi::draw({
                .texture = renderer_.gameViewportTexture(),
                .width = gameExtent.width,
                .height = gameExtent.height,
            });
            if (workspace.viewportWidth > 0.0f &&
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
        if (screenPreviewActive_) {
            drawScreenPreviewOverlay(pixelSize);
        } else {
            drawSelectorPrompt(
                preparedRenderFrame_ ? &*preparedRenderFrame_ : nullptr);
        }
#if SOKOBAN_ENABLE_DEBUG_UI
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

        if (const std::optional<TitleAction> titleAction = titleScreen_.draw(
                ui_, pixelSize, routedInput.title)) {
            handleShellEvent(ShellTitleAction { *titleAction });
        }

        if (const std::optional<OverlayAction> overlayAction =
                levelCompleteOverlay_.draw(
                    ui_, pixelSize, routedInput.overlay)) {
            handleShellEvent(ShellOverlayAction { *overlayAction });
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
                    &gamepadPresentation)) {
            if (const std::optional<OptionsAction> optionsAction =
                    optionsMenu_.dispatch(
                        settingsCoordinator_.userSettings(),
                        *intent)) {
                handleShellEvent(ShellOptionsAction { *optionsAction });
            }
        }
        drawAssetLoadingOverlay(pixelSize);
#if SOKOBAN_ENABLE_DEBUG_UI
        tools_->animationPreviewDebugUi.update(dt, renderer_);
#endif
        ui_.endFrame();
        preparedRenderFrame_ = renderer_.prepareFrame(
            buildRenderFrame(routedInput.editor),
            buildScreenPreviewRenderFrame());
        renderer_.drawFrame(
            *preparedRenderFrame_,
            ui_.drawData(),
            developerWorkspaceVisible);
        if (renderer_.hasFatalFailure()) {
            log::error(log::Category::Application)
                << "Rendering stopped: " << renderer_.fatalFailureMessage();
            running_ = false;
        }
        ++renderedFrames;
        if (!evidenceOutputDirectory_.empty() &&
            renderedFrames + 1 == smokeFrames_) {
            captureEvidenceScene();
        }
        if (smokeFrames_ > 0 && renderedFrames >= smokeFrames_) {
            if (!evidenceOutputDirectory_.empty()) {
                finishEvidenceCapture();
            }
            log::info(log::Category::Application)
                << "Smoke run finished after " << renderedFrames
                << " frames.";
            running_ = false;
        }
        if (running_) {
            framePacer_.pace();
        }
    }
}

void Application::update(
    float dt,
    const InputRouter::Frame& input,
    const VulkanRenderer::PreparedFrame* previousRenderFrame)
{
    screenPreviewActive_ = false;
#if !SOKOBAN_ENABLE_DEBUG_UI
    (void)previousRenderFrame;
#endif

    const bool reversed =
        gameplaySession_.moving() &&
        gameplaySession_.activeAction().reversed;
    presentation_.advanceClocks(dt, reversed);
    const bool editorDraftPlaying =
#if SOKOBAN_ENABLE_DEBUG_UI
        tools_->levelEditor.playingDraft();
#else
        false;
#endif
    const bool showOverworldMap =
        input.showOverworldMap && campaign_.inOverworld() &&
        !editorDraftPlaying;
    presentation_.updateCameraPitch(
        (input.showTopDownView || showOverworldMap)
            ? 0.0f
            : config::cameraPitchDegrees,
        dt,
        config::cameraPitchTransitionSeconds);
    const float overviewStep = config::cameraPitchTransitionSeconds <= 0.0f
        ? 1.0f
        : std::max(dt, 0.0f) / config::cameraPitchTransitionSeconds;
    overworldOverviewProgress_ = showOverworldMap
        ? std::min(overworldOverviewProgress_ + overviewStep, 1.0f)
        : std::max(overworldOverviewProgress_ - overviewStep, 0.0f);

    if (shellMenuOpen()) {
        audioSystem_.update(dt, false, false);
        return;
    }

#if SOKOBAN_ENABLE_DEBUG_UI
    if (tools_->draftExitConfirmationOpen) {
        audioSystem_.update(dt, false, false);
        return;
    }
    if (tools_->levelEditor.editingDocument()) {
        audioSystem_.update(dt, false, false);
        tools_->updateEditorInteraction(
            input.editor,
            previousRenderFrame,
            renderer_,
            window_.size(),
            window_.sizeInPixels());
        return;
    }
#endif

    if (levelTransition_.active()) {
        updateLevelTransition(dt);
        audioSystem_.update(dt, false, false);
        return;
    }

    if (updateScreenPreview(input.previewScreen, dt)) {
        audioSystem_.update(dt, false, false);
        return;
    }

    campaign_.addElapsedTime(dt);
    particleSystem_.update(dt);
    const GameplayLoop::UpdateResult gameplayResult = GameplayLoop::update(
        level_,
        gameplaySession_,
        presentation_,
        input.gameplay,
        dt,
        editorDraftPlaying);
    if (gameplayResult.stateCommitted && campaign_.inOverworld() &&
        overworldMap_ && !editorDraftPlaying) {
        const std::optional<OverworldScreenId> playerScreen =
            CampaignSession::sharedPlayerScreen(
                *overworldMap_, gameplaySession_.state());
        if (playerScreen &&
            *playerScreen != campaign_.activeOverworldScreen() &&
            !campaign_.transitionOverworldScreen(*playerScreen)) {
            log::error(log::Category::Gameplay)
                << "Could not commit overworld screen transition to "
                << *playerScreen;
        }
    }
    if (gameplayResult.mirrorActivated) {
        audioSystem_.playOneShot("mirror-swap");
        for (GridPosition3 destination :
             gameplayResult.mirrorSwapDestinations) {
            particleSystem_.emit(
                {
                    static_cast<float>(destination.x) + 0.5f,
                    static_cast<float>(destination.y) + 0.5f,
                    static_cast<float>(destination.z) +
                        config::mirrorSwapSmokeElevation,
                },
                mirrorSwapParticleEffect_);
        }
    }
    if (gameplayResult.draftSolved) {
#if SOKOBAN_ENABLE_DEBUG_UI
        tools_->levelEditor.markDraftSolved();
#endif
    }
    if (gameplayResult.screenSolved) {
        advanceScreen();
    } else if (input.gameplay.interactPressed &&
        campaign_.inOverworld() && !gameplaySession_.moving() &&
        !rules::hasPendingMotion(level_, gameplaySession_.state())) {
        tryEnterSelector();
    } else if (gameplayResult.stateCommitted &&
        campaign_.deferCheckpoint()) {
        checkpointCurrentScreen(true);
    }
    if (campaign_.updateDeferredCheckpoint(
            dt,
            gameplaySession_.moving(),
            editorDraftPlaying)) {
        checkpointCurrentScreen(true);
    }

    bool playerMoving = false;
    bool pushing = false;
    for (const GameplayPresentation::PlayerVisual& player :
         presentation_.players()) {
        playerMoving |= player.motion.moving;
        pushing |= player.motion.moving &&
            player.animationUse == AnimationUse::PlayerPush;
    }
    audioSystem_.update(dt, playerMoving, pushing);
}

void Application::loadCurrentScreen()
{
    // Editor draft play or a New Game may have changed the level set.
    buildLevelCatalog();
    const CampaignSession::WorldRestore restore =
        campaign_.prepareWorldLoad(playerProfile_);

    const std::optional<LevelLocation> location = campaign_.inOverworld()
        ? std::nullopt
        : std::optional<LevelLocation> { campaign_.location() };
    const std::filesystem::path path = campaign_.inOverworld()
        ? (overworldMap_ ? overworldRoot() : overworldPath())
        : screenPath(campaign_.currentLevel(), campaign_.currentScreen());

    Level loadedLevel = campaign_.inOverworld() && overworldMap_
        ? overworldMap_->level()
        : Level::loadFromFile(path);

    const bool restored = applyLevel(
        std::move(loadedLevel),
        restore.snapshot ? &*restore.snapshot : nullptr,
        location,
        campaign_.inOverworld() && overworldMap_.has_value());
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
    checkpointCurrentScreen(true);
    audioSystem_.playMusicForLevel(
        campaign_.inOverworld() ? 0 : campaign_.currentLevel());
    preloadUpcomingAssets();
#if SOKOBAN_ENABLE_DEBUG_UI
    tools_->levelEditor.setPlayingDraft(false);
    tools_->levelEditor.setEditingDocument(false);
    tools_->hoverCell.reset();
#endif

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
    // A level change makes speculative work for the prior world irrelevant.
    // Running tasks may finish, but queued disk work is discarded before it
    // competes with the assets the player is about to see.
    renderer_.cancelQueuedAssetPrefetches();
    // Same location the render frame will use, so the splat map this screen
    // draws with is the one guaranteed resident here.
    RenderAssetRequirements requirements = renderAssetRequirementsForLevel(
        level,
        assetManifest_,
        location,
        &animationCatalog_);
    if (composedOverworld && overworldMap_) {
        for (OverworldScreenId screenId : overworldMap_->visibleNeighborhood(
                 campaign_.activeOverworldScreen())) {
            const GroundSplatTextures textures =
                groundSplatTexturesForOverworldScreen(
                    [this](std::string_view name) {
                        return assetManifest_.findTextureIdByName(name);
                    },
                    screenId);
            requirements.requireTexture(textures.base);
            requirements.requireTexture(textures.detail);
            requirements.requireTexture(textures.splatMap);
        }
    }
    renderer_.ensureAssets(requirements);
    level_ = std::move(level);
    bool restored = snapshot && gameplaySession_.restore(level_, *snapshot);
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
    particleSystem_.reset();
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
        handlePuzzleCompleted(campaign_.completePuzzle(
            playerProfile_, moveCount));
    });
}

#if SOKOBAN_ENABLE_DEBUG_UI
void Application::solveCurrentScreenForDebug()
{
    if (!campaign_.gameLoaded() || tools_->levelEditor.playingDraft() ||
        levelCompleteOverlay_.isOpen()) {
        return;
    }
    if (campaign_.inOverworld()) {
        return;
    }
    const int moveCount = gameplaySession_.playerMoveCount();
    beginLevelTransition([this, moveCount] {
        handlePuzzleCompleted(campaign_.completePuzzle(
            playerProfile_, moveCount, false));
    });
}
#endif

void Application::handlePuzzleCompleted(
    const CampaignSession::PuzzleCompleted&)
{
    persistProfile(true);
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

void Application::tryEnterSelector()
{
    const GameState& state = gameplaySession_.state();
    const Level::ScreenSelector* sharedSelector =
        CampaignSession::selectorForInteraction(level_, state);
    if (!sharedSelector || !sharedSelector->target) {
        if (sharedSelector) {
            log::warning(log::Category::Gameplay)
                << "Selector " << sharedSelector->id
                << " has no assigned screen";
        }
        return;
    }
    if (!campaign_.screenExists(
            sharedSelector->target->level,
            sharedSelector->target->screen)) {
        log::warning(log::Category::Gameplay)
            << "Selector " << sharedSelector->id
            << " targets missing screen " << sharedSelector->target->level
            << ':' << sharedSelector->target->screen;
        return;
    }
    if (campaign_.selectorViewState(
            playerProfile_, *sharedSelector->target).status ==
        ScreenSelectorStatus::Unavailable) {
        log::debug(log::Category::Gameplay)
            << "Selector " << sharedSelector->id
            << " is not playable until the previous screen is solved";
        return;
    }

    // Make the return point durable before changing the profile context.
    checkpointCurrentScreen(true);
    const LevelLocation target = *sharedSelector->target;
    beginLevelTransition([this, target] {
        if (campaign_.startPuzzle(playerProfile_, target)) {
            loadCurrentScreen();
        }
    });
}

bool Application::updateScreenPreview(bool requested, float dt)
{
    if (!requested || !campaign_.gameLoaded() || !campaign_.inOverworld() ||
        gameplaySession_.moving() ||
        rules::hasPendingMotion(level_, gameplaySession_.state())) {
        return false;
    }

    const Level::ScreenSelector* selector =
        CampaignSession::selectorForInteraction(
            level_, gameplaySession_.state());
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
Application::buildScreenPreviewRenderFrame() const
{
    if (!screenPreviewActive_ || !screenPreviewLevel_ ||
        !screenPreviewTarget_) {
        return std::nullopt;
    }

    const GameState projectedState =
        screenPreviewSession_.projectedState();
    RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = assetManifest_,
        .level = *screenPreviewLevel_,
        .state = screenPreviewSession_.state(),
        .moving = false,
        .projectedState = projectedState,
        .presentation = screenPreviewPresentation_,
        .settings = presentationSettings_,
        .animations = &animationCatalog_,
        .conveyorBeltScrollOffset =
            screenPreviewPresentation_.conveyorBeltScrollOffset(
                screenPreviewSession_.stepDurationSeconds()),
        .cameraPitchDegrees = screenPreviewPresentation_.cameraPitchDegrees(),
        .levelLocation = *screenPreviewTarget_,
        .selectorState = [this](LevelLocation target) {
            return campaign_.selectorViewState(playerProfile_, target);
        },
    });
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
    if (!frame || levelTransition_.active() ||
        !campaign_.gameLoaded() || !campaign_.inOverworld() ||
        shellMenuOpen()
#if SOKOBAN_ENABLE_DEBUG_UI
        || tools_->levelEditor.editingDocument()
#endif
        ||
        gameplaySession_.moving() ||
        rules::hasPendingMotion(level_, gameplaySession_.state())) {
        return;
    }

    const GameState& state = gameplaySession_.state();
    const Level::ScreenSelector* selector =
        CampaignSession::selectorForInteraction(level_, state);
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
    const std::optional<InputBinding> enterBinding =
        SelectorPrompt::binding(
            input_.bindings(), InputAction::MenuConfirm, device);
    const std::optional<InputBinding> previewBinding =
        SelectorPrompt::binding(
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
            const std::optional<std::string> enterLabel =
                SelectorPrompt::bindingLabel(
                    input_.bindings(), InputAction::MenuConfirm, device);
            const std::optional<std::string> previewLabel =
                SelectorPrompt::bindingLabel(
                    input_.bindings(), InputAction::PreviewScreen, device);
            if (enterLabel && previewLabel) {
                SelectorPrompt::draw(ui_, *arrowTip, *enterLabel, *previewLabel);
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

    const std::string detail = std::to_string(stats.readyRequestedAssets) +
        " / " + std::to_string(stats.requestedAssets) +
        " ready  -  " + std::to_string(pending) + " pending";
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

void Application::resolveLevelComplete(bool toTitle)
{
    levelCompleteOverlay_.close();
    loadCurrentScreen();
    if (toTitle) {
        openTitleScreen();
    }
}

void Application::openTitleScreen()
{
    titleScreen_.setSaveSlots(saveSlotInfos(), saveSlots_.activeSlot());
    titleScreen_.open(titleLevelInfos());
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

void Application::switchSaveSlot(int slot)
{
    if (slot < 0 || slot >= SaveSlotManager::slotCount ||
        slot == saveSlots_.activeSlot()) {
        return;
    }

    // Settle the outgoing slot on disk first. Not gated on the world being
    // idle - see the destructor for why that gate was wrong under concurrency.
    const bool editorDraftPlaying =
#if SOKOBAN_ENABLE_DEBUG_UI
        tools_->levelEditor.playingDraft();
#else
        false;
#endif
    if (campaign_.gameLoaded() && !editorDraftPlaying) {
        checkpointCurrentScreen(true);
    } else {
        persistProfile(true);
    }

    std::optional<PlayerProfile> switched;
    try {
        switched = saveSlots_.switchTo(slot, playerProfile_);
    } catch (const std::exception& error) {
        log::error(log::Category::Persistence)
            << "Could not switch to save slot " << (slot + 1) <<
            ": " << error.what();
        return;
    }
    if (!switched) {
        return;
    }
    playerProfile_ = std::move(*switched);
    log::info(log::Category::Persistence)
        << saveSlots_.progressStatus();

    // The new slot's world loads on Continue/New Game, like at boot.
    levelCompleteOverlay_.close();
    campaign_.resetForProfile(playerProfile_);
    renderer_.preloadAssets(
        levelAssetRequirements(campaign_.currentLevel()));
    openTitleScreen();
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
        levelCompleteOverlay_.close();
        campaign_.resetForProfile(playerProfile_);
    }
    titleScreen_.setSaveSlots(saveSlotInfos(), saveSlots_.activeSlot());
    titleScreen_.setSaveSlotError({});
}

void Application::persistSettings(bool immediate)
{
    saveSlots_.saveSettings(playerProfile_, immediate);
}

void Application::openStandaloneLevelSelect()
{
    titleScreen_.openLevelSelect(titleLevelInfos());
}

ShellFacts Application::shellFacts() const
{
    return {
        .gameLoaded = campaign_.gameLoaded(),
        .optionsOpen = optionsMenu_.isOpen(),
        .overlayOpen = levelCompleteOverlay_.isOpen(),
        .titleOpen = titleScreen_.isOpen(),
        .titleAtMainPage = titleScreen_.page() == TitleScreen::Page::Main,
        .allLevelsCompleted = allLevelsCompleted(),
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
        [&](const shell::CloseTitle&) { titleScreen_.close(); },
        [&](const shell::OpenTitle&) { openTitleScreen(); },
        [&](const shell::TitleBack&) { titleScreen_.back(); },
        [&](const shell::StartNewGame&) { startNewGame(); },
        [&](const shell::SwitchSlot& switchSlot) {
            switchSaveSlot(switchSlot.slot);
        },
        [&](const shell::DeleteSlot& deleteSlot) {
            deleteSaveSlot(deleteSlot.slot);
        },
        [&](const shell::StartLevel& start) {
            startLevel(start.level, start.screen);
        },
        [&](const shell::OpenOptions& open) {
            optionsMenu_.open(
                open.pauseContext,
                open.allowLevelSelect);
        },
        [&](const shell::CloseOptions&) { optionsMenu_.close(); },
        [&](const shell::OptionsBack&) { optionsMenu_.back(); },
        [&](const shell::ApplySettings& apply) {
            applySettingsEffects(
                settingsCoordinator_.applyUserSettings(
                    apply.settings));
        },
        [&](const shell::RequestQuitConfirmation&) {
            optionsMenu_.requestQuitConfirmation();
        },
        [&](const shell::Quit&) { running_ = false; },
        [&](const shell::ResolveLevelComplete& resolve) {
            resolveLevelComplete(resolve.toTitle);
        },
        [&](const shell::OpenStandaloneLevelSelect&) {
            openStandaloneLevelSelect();
        },
    }, command);
}

bool Application::allLevelsCompleted() const
{
    return campaign_.allTargetsCompleted(playerProfile_);
}

bool Application::shellMenuOpen() const
{
    return optionsMenu_.isOpen() ||
        titleScreen_.isOpen() ||
        levelCompleteOverlay_.isOpen();
}

std::vector<TitleLevelInfo> Application::titleLevelInfos() const
{
    std::vector<TitleLevelInfo> result;
    for (int level = 0; level < campaign_.levelCount(); ++level) {
        const int screens = campaign_.screenCount(level);
        const PlayerProfile::LevelProgress* progress =
            playerProfile_.progressForLevel(level);
        int reached = progress ? progress->reachedScreens : 0;
        if (level == campaign_.currentLevel()) {
            reached = std::max(reached, campaign_.currentScreen() + 1);
        }
        result.push_back({
            .screenCount = screens,
            .name = levelMetadata_[static_cast<std::size_t>(level)].name,
            .screenNames =
                levelMetadata_[static_cast<std::size_t>(level)].screenNames,
            .unlocked = level <= playerProfile_.unlockedLevel,
            .completed = progress && progress->completed,
            .reachedScreens = reached,
            .bestMoves = progress ? progress->bestMoves : std::nullopt,
            .bestTimeSeconds = progress ? progress->bestTimeSeconds : std::nullopt,
        });
    }
    return result;
}

void Application::startNewGame()
{
    titleScreen_.close();
    levelCompleteOverlay_.close();
    campaign_.startNewGame(playerProfile_);
    loadCurrentScreen();
    persistProfile(true);
}

void Application::startLevel(int level, int screen)
{
    if (campaign_.startPuzzle(
            playerProfile_, LevelLocation { .level = level, .screen = screen })) {
        loadCurrentScreen();
    }
}

void Application::checkpointCurrentScreen(bool immediateSave)
{
    campaign_.writeCheckpoint(playerProfile_, gameplaySession_.snapshot());
    persistProfile(immediateSave);
}

void Application::applySettingsEffects(const SettingsEffects& effects)
{
    if (effects.window) {
        if (effects.window->fullscreen) {
            window_.setFullscreen(true);
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
        audioSystem_.setMasterVolume(effects.audio->masterVolume);
        audioSystem_.setMusicVolume(effects.audio->musicVolume);
        audioSystem_.setSoundVolume(effects.audio->soundVolume);
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
        .overlayOpen = levelCompleteOverlay_.isOpen(),
        .keyboardCaptured = renderer_.wantsKeyboardCapture(),
        .mouseCaptured = renderer_.wantsMouseCapture(),
    };
#if SOKOBAN_ENABLE_DEBUG_UI
    context.editorEditing = tools_->levelEditor.editingDocument();
    context.decorationPlacementReady =
        context.editorEditing &&
        tools_->levelEditor.tool() == LevelEditor::Tool::Decorations &&
        !tools_->levelEditor.selectedDecorationModel().empty();
    context.draftPlaying = tools_->levelEditor.playingDraft();
    context.draftExitConfirmationOpen = tools_->draftExitConfirmationOpen;
#endif
    return context;
}

std::filesystem::path Application::screenPath(
    int levelIndex,
    int screenIndex) const
{
    return assetRoot_ /
        "levels" /
        ("level" + std::to_string(levelIndex)) /
        ("screen" + std::to_string(screenIndex) + ".scr");
}

std::filesystem::path Application::overworldPath() const
{
    return assetRoot_ / "levels" / "overworld.scr";
}

std::filesystem::path Application::overworldRoot() const
{
    return assetRoot_ / "levels" / "overworld";
}

void Application::buildLevelCatalog()
{
    std::vector<int> screenCounts;
    levelMetadata_.clear();
    for (int level = 0;; ++level) {
        int screens = 0;
        while (std::filesystem::exists(screenPath(level, screens))) {
            ++screens;
        }
        if (screens == 0) {
            break;
        }
        screenCounts.push_back(screens);
        levelMetadata_.push_back(loadLevelMetadata(
            assetRoot_ / "levels" /
                ("level" + std::to_string(level)),
            static_cast<std::size_t>(screens)));
    }
    std::vector<LevelLocation> selectorTargets;
    const std::filesystem::path layoutPath = overworldRoot() / "layout.json";
    if (std::filesystem::exists(layoutPath)) {
        overworldMap_ = OverworldMap::load(overworldRoot());
        overworldMap_->validatePuzzleSelectors(
            screenCounts, OverworldValidationMode::Structural);
        std::vector<OverworldScreenId> screenIds;
        screenIds.reserve(overworldMap_->screens().size());
        for (const OverworldScreenRuntime& screen : overworldMap_->screens()) {
            screenIds.push_back(screen.id);
        }
        campaign_.setOverworldTopology(
            overworldMap_->fingerprint(),
            std::move(screenIds),
            overworldMap_->startScreen());
        for (const OverworldSelectorRuntime& selector :
             overworldMap_->selectors()) {
            if (selector.target) {
                selectorTargets.push_back(*selector.target);
            }
        }
    } else {
        overworldMap_.reset();
        campaign_.setOverworldTopology(0, { 1 }, 1);
        const Level overworld = Level::loadFromFile(overworldPath());
        for (const Level::ScreenSelector& selector : overworld.selectors()) {
            if (selector.target) {
                selectorTargets.push_back(*selector.target);
            }
        }
    }
    campaign_.setLevelScreenCounts(std::move(screenCounts));
    campaign_.setOverworldTargets(std::move(selectorTargets));
}

void Application::restoreProfileLocation()
{
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

RenderAssetRequirements Application::levelAssetRequirements(int levelIndex) const
{
    RenderAssetRequirements requirements;
    for (int screenIndex = 0;
         campaign_.screenExists(levelIndex, screenIndex);
         ++screenIndex) {
        try {
            requirements.merge(renderAssetRequirementsForLevel(
                Level::loadFromFile(screenPath(levelIndex, screenIndex)),
                assetManifest_,
                LevelLocation { .level = levelIndex, .screen = screenIndex },
                &animationCatalog_));
        } catch (const std::exception& error) {
            log::warning(log::Category::Assets)
                << "asset preload skipped "
                << screenPath(levelIndex, screenIndex).string()
                << ": " << error.what();
        }
    }
    return requirements;
}

void Application::preloadUpcomingAssets()
{
    renderer_.cancelQueuedAssetPrefetches();
    RenderAssetRequirements requirements;
    if (campaign_.inOverworld()) {
        for (int level = 0; level < campaign_.levelCount(); ++level) {
            requirements.merge(levelAssetRequirements(level));
        }
    } else {
        requirements = levelAssetRequirements(campaign_.currentLevel());
        requirements.merge(renderAssetRequirementsForLevel(
            overworldMap_ ? overworldMap_->level()
                          : Level::loadFromFile(overworldPath()),
            assetManifest_));
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

RenderFrameData Application::buildRenderFrame(
    const InputRouter::EditorInput& editorInput)
{
    (void)editorInput;
    const float beltScrollOffset =
        presentation_.conveyorBeltScrollOffset(
            gameplaySession_.stepDurationSeconds());
#if SOKOBAN_ENABLE_DEBUG_UI
    if (const std::optional<RenderFrameData> preview =
            tools_->animationPreviewDebugUi.previewFrame(
                assetManifest_, presentationSettings_)) {
        return *preview;
    }
    if (tools_->levelEditor.editingDocument()) {
        const std::vector<LevelEditor::LevelDirectory> editorLevels =
            tools_->levelEditor.collectLevelDirectories();
        const std::optional<LevelEditor::MoveObject>& pendingMove =
            tools_->levelEditor.pendingMove();
        const std::optional<TileType> movedTile =
            pendingMove &&
                pendingMove->kind == LevelEditor::MoveObject::Kind::Tile
            ? std::optional<TileType> { pendingMove->tile }
            : std::nullopt;
        std::vector<RenderFrameBuilder::EditorInput::OverworldNeighbor>
            overworldNeighbors;
        const std::optional<OverworldScreenId> editedOverworldScreen =
            tools_->levelEditor.overworldScreenId();
        const OverworldMapEditor& topology = tools_->overworldMapEditor;
        const bool matchingTopologyRoot =
            std::filesystem::absolute(topology.projectLevelRoot())
                .lexically_normal() ==
            std::filesystem::absolute(tools_->levelEditor.browserRoot())
                .lexically_normal();
        if (tools_->levelEditor.showOverworldNeighbors() &&
            editedOverworldScreen && topology.loaded() &&
            matchingTopologyRoot) {
            const OverworldScreenSpec* active =
                topology.screen(*editedOverworldScreen);
            if (active) {
                for (const OverworldMapEditor::ScreenSummary& candidate :
                     topology.screens()) {
                    const int deltaX = candidate.slot.x - active->slot.x;
                    const int deltaY = candidate.slot.y - active->slot.y;
                    if ((deltaX == 0 && deltaY == 0) ||
                        std::abs(deltaX) > 1 || std::abs(deltaY) > 1) {
                        continue;
                    }
                    if (const Level::Definition* definition =
                            topology.definition(candidate.id)) {
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
        FrameArena& arena = beginRenderFrameArena();
        return RenderFrameBuilder::buildEditor({
            .manifest = assetManifest_,
            .editor = tools_->levelEditor,
            .settings = presentationSettings_,
            .animations = &animationCatalog_,
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
        }, arena);
    }
#endif

    if (!campaign_.gameLoaded()) {
        // Title-only: nothing to draw behind the fullscreen menu.
        return RenderFrameData {};
    }

    // Held by reference for the duration of the call, so it has to outlive it.
    const GameState projectedState = gameplaySession_.projectedState();
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
            overworldOverviewProgress_);
    }
    std::function<bool(GridPosition3)> visibleOverworldCell;
    std::vector<RenderFrameBuilder::GameplayInput::GroundSplatRegion>
        groundSplatRegions;
    if (overworldView && renderedOverworld) {
        visibleOverworldCell = [
            renderedOverworld,
            visibleScreens = overworldView->visibleScreens
        ](GridPosition3 cell) {
            const std::optional<OverworldScreenId> owner =
                renderedOverworld->screenAt(cell);
            return owner && std::ranges::find(visibleScreens, *owner) !=
                visibleScreens.end();
        };
        groundSplatRegions.reserve(overworldView->visibleScreens.size());
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
            });
        }
    }
    FrameArena& arena = beginRenderFrameArena();
    RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = assetManifest_,
        .level = level_,
        .state = gameplaySession_.state(),
        .moving = gameplaySession_.moving(),
        .projectedState = projectedState,
        .presentation = presentation_,
        .settings = presentationSettings_,
        .animations = &animationCatalog_,
        .conveyorBeltScrollOffset = beltScrollOffset,
        .cameraPitchDegrees = presentation_.cameraPitchDegrees(),
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
    }, arena);
    particleSystem_.appendRenderData(frame);
    frame.levelTransitionAmount = levelTransition_.amount();
    return frame;
}

} // namespace sokoban
