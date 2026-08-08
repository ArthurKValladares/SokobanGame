#include "engine/Application.hpp"
#include "engine/ApplicationTools.hpp"

#include "engine/ParticleConfig.hpp"
#include "engine/render/CameraConfig.hpp"

#include "engine/DebugUi.hpp"
#include "engine/Log.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/RuntimeContent.hpp"
#include "engine/UserSettingsConfig.hpp"
#include "engine/ui/UiConfig.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <exception>
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

} // namespace

Application::Application()
    : window_(
          "Sokoban 3D",
          config::windowWidth,
          config::windowHeight)
    , saveSlots_(SaveStore::preferencePath("Sokoban3D", "Sokoban3D"))
    , playerProfile_(saveSlots_.loadActiveProfile())
    , assetRoot_(runtimeContentRoot())
    , assetManifest_(AssetManifest::loadFromFile(assetRoot_ / "manifest.json"))
    , animationCatalog_(AnimationCatalog::loadFromFile(
          assetRoot_ / "animation_catalog.json", assetManifest_))
    , uiFont_(FontAtlas::load(
          assetRoot_ / config::uiFontPath,
          config::uiFontPixelHeight,
          config::uiFontAtlasSize))
    , renderer_(
          window_.nativeHandle(),
          assetRoot_,
          assetManifest_,
          uiFont_,
          antiAliasingModeForSamples(
              playerProfile_.settings.video.antiAliasingSamples),
          playerProfile_.settings.video.effectiveRenderScalePercent(),
          playerProfile_.settings.video.vsync)
    , ui_(uiFont_)
    , audioSystem_(assetRoot_, assetManifest_)
    , mirrorSwapParticleEffect_(
          makeMirrorSwapParticleEffect(assetManifest_))
    , settingsCoordinator_(playerProfile_, presentationSettings_)
    , tools_(std::make_unique<ApplicationTools>())
    , renderFrameArena_("render frame", renderFrameArenaBytes())
{
    // Leave a diagnostic trail next to the profiles so shipped builds can be
    // debugged from the save directory; Debug builds also emit debug traces.
    log::addFileSink(saveSlots_.directory() / "log.txt");
#if SOKOBAN_ENABLE_DEBUG_UI
    log::setMinimumLevel(log::Level::Debug);
#endif
    log::info(log::Category::Persistence)
        << saveSlots_.progressStatus();
    buildLevelCatalog();
    restoreProfileLocation();
    applySettingsEffects(settingsCoordinator_.initialize());
    presentationSettings_.applyTileScales(assetManifest_);
    presentationSettings_.normalize();
    presentation_.setAnimationCatalog(&animationCatalog_);
    // The world stays unloaded until the title's Continue/New Game, but its
    // assets warm up in the background so that first load doesn't block.
    openTitleScreen();
    renderer_.preloadAssets(
        levelAssetRequirements(campaign_.currentLevel()));

#if SOKOBAN_ENABLE_DEBUG_UI
    tools_->initialize(
        SOKOBAN_SOURCE_LEVEL_DIR,
        SOKOBAN_SOURCE_ASSET_DIR,
        assetRoot_,
        campaign_.currentLevel(),
        campaign_.currentScreen(),
        assetManifest_,
        animationCatalog_);

    DebugUi::addTab("Engine", [this] {
        const ApplicationDebugUi::Result result = tools_->applicationDebugUi.draw({
            .currentLevel = campaign_.currentLevel(),
            .currentScreen = campaign_.currentScreen(),
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
        tools_->levelEditorDebugUi.draw(tools_->levelEditor, tools_->splatPainter, {
            .playDraft = [this](Level level) {
                // Playing a draft leaves the document view; a half-finished
                // paint session would otherwise keep painting on the level
                // being played.
                tools_->splatPainter.close();
                (void)applyLevel(std::move(level));
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
}

Application::~Application()
{
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
    if (campaign_.gameLoaded() && !tools_->levelEditor.playingDraft()) {
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
    DebugUi::clearTabs();
    renderer_.waitIdle();
}

bool Application::bakeTileThumbnails()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    return tools_->bakeTileThumbnails(
        renderer_,
        ui_,
        assetManifest_,
        presentationSettings_,
        animationCatalog_,
        SOKOBAN_SOURCE_ASSET_DIR,
        assetRoot_,
        window_.sizeInPixels());
#else
    return false;
#endif
}

void Application::run()
{
    while (running_) {
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

        switch (inputRouter_.backAction(input_, inputRoutingContext())) {
        case InputRouter::BackAction::CloseDraftConfirmation:
            tools_->draftExitConfirmationOpen = false;
            break;
        case InputRouter::BackAction::OpenDraftConfirmation:
            tools_->draftExitConfirmationOpen = true;
            break;
        case InputRouter::BackAction::ShellBack:
            handleShellEvent(ShellBackPressed {});
            break;
        case InputRouter::BackAction::None:
            break;
        }

        const InputRouter::Frame routedInput =
            inputRouter_.routeFrame(input_, inputRoutingContext());
        const float dt = frameTimer_.tick();
        update(
            dt,
            routedInput,
            preparedRenderFrame_ ? &*preparedRenderFrame_ : nullptr);
        const Vec2 windowSize = window_.size();
        const Vec2 pixelSize = window_.sizeInPixels();
        const Vec2 mouse = routedInput.pointer.position;
        const Vec2 mousePixels {
            windowSize.x > 0.0f
                ? mouse.x * pixelSize.x / windowSize.x
                : mouse.x,
            windowSize.y > 0.0f
                ? mouse.y * pixelSize.y / windowSize.y
                : mouse.y,
        };

        ui_.beginFrame(
            pixelSize,
            mousePixels,
            routedInput.pointer.primaryDown,
            routedInput.pointer.primaryPressed);

        renderer_.beginDebugUiFrame();
        if (!optionsMenu_.isOpen() && !titleScreen_.isOpen()) {
            DebugUi::draw();
        }
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
        if (const std::optional<OptionsMenuIntent> intent =
                optionsMenuView_.draw(
                    ui_,
                    pixelSize,
                    optionsMenu_.state(),
                    settingsCoordinator_.userSettings())) {
            if (const std::optional<OptionsAction> optionsAction =
                    optionsMenu_.dispatch(
                        settingsCoordinator_.userSettings(),
                        *intent)) {
                handleShellEvent(ShellOptionsAction { *optionsAction });
            }
        }
        tools_->animationPreviewDebugUi.update(dt, renderer_);
        ui_.endFrame();
        preparedRenderFrame_ = renderer_.prepareFrame(
            buildRenderFrame(routedInput.editor));
        renderer_.drawFrame(*preparedRenderFrame_, ui_.drawData());
    }
}

void Application::update(
    float dt,
    const InputRouter::Frame& input,
    const VulkanRenderer::PreparedFrame* previousRenderFrame)
{
#if !SOKOBAN_ENABLE_DEBUG_UI
    (void)previousRenderFrame;
#endif

    const bool reversed =
        gameplaySession_.moving() &&
        gameplaySession_.activeAction().reversed;
    presentation_.advanceClocks(dt, reversed);
    presentation_.updateCameraPitch(
        input.showTopDownView ? 0.0f : config::cameraPitchDegrees,
        dt,
        config::cameraPitchTransitionSeconds);

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

    campaign_.addElapsedTime(dt);
    particleSystem_.update(dt);
    const GameplayLoop::UpdateResult gameplayResult = GameplayLoop::update(
        level_,
        gameplaySession_,
        presentation_,
        input.gameplay,
        dt,
        tools_->levelEditor.playingDraft());
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
        tools_->levelEditor.markDraftSolved();
    }
    if (gameplayResult.screenSolved) {
        advanceScreen();
    } else if (gameplayResult.stateCommitted &&
        campaign_.deferCheckpoint()) {
        checkpointCurrentScreen(true);
    }
    if (campaign_.updateDeferredCheckpoint(
            dt,
            gameplaySession_.moving(),
            tools_->levelEditor.playingDraft())) {
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
    const CampaignSession::ScreenRestore restore =
        campaign_.prepareScreenLoad(playerProfile_);

    const bool restored = applyLevel(
        Level::loadFromFile(screenPath(
            campaign_.currentLevel(), campaign_.currentScreen())),
        restore.snapshot ? &*restore.snapshot : nullptr);
    if (restore.checkpointMatched && !restored) {
        log::warning(log::Category::Persistence)
            << "Discarded invalid gameplay checkpoint for level "
            << campaign_.currentLevel() << " screen "
            << campaign_.currentScreen();
        playerProfile_.activeScreen.reset();
    }
    campaign_.finishScreenLoad(playerProfile_);
    checkpointCurrentScreen(true);
    audioSystem_.playMusicForLevel(campaign_.currentLevel());
    preloadUpcomingAssets();
    tools_->levelEditor.setPlayingDraft(false);
    tools_->levelEditor.setEditingDocument(false);
    tools_->hoverCell.reset();

    log::debug(log::Category::Gameplay)
        << "player started level " << campaign_.currentLevel()
        << " screen " << campaign_.currentScreen();
}

bool Application::applyLevel(
    Level level,
    const GameplaySession::Snapshot* snapshot)
{
    // Same location the render frame will use, so the splat map this screen
    // draws with is the one guaranteed resident here.
    renderer_.ensureAssets(renderAssetRequirementsForLevel(
        level,
        assetManifest_,
        LevelLocation {
            .level = campaign_.currentLevel(),
            .screen = campaign_.currentScreen(),
        },
        &animationCatalog_));
    level_ = std::move(level);
    const bool restored = snapshot && gameplaySession_.restore(level_, *snapshot);
    if (!restored) {
        gameplaySession_.reset(level_);
    }
    presentation_.resetEntities(gameplaySession_.state());
    particleSystem_.reset();
    campaign_.markWorldLoaded();
    return restored;
}

void Application::advanceScreen()
{
    const CampaignSession::AdvanceResult result = campaign_.advanceScreen(
        playerProfile_, gameplaySession_.playerMoveCount());
    handleCampaignAdvance(result);
}

void Application::solveCurrentScreenForDebug()
{
    if (!campaign_.gameLoaded() || tools_->levelEditor.playingDraft() ||
        levelCompleteOverlay_.isOpen()) {
        return;
    }
    const CampaignSession::AdvanceResult result =
        campaign_.completeCurrentScreenForDebug(
        playerProfile_, gameplaySession_.playerMoveCount());
    handleCampaignAdvance(result);
}

void Application::handleCampaignAdvance(
    const CampaignSession::AdvanceResult& result)
{
    if (std::holds_alternative<CampaignSession::ScreenAdvanced>(result)) {
        loadCurrentScreen();
        return;
    }

    const bool gameCompleted =
        std::holds_alternative<CampaignSession::GameCompleted>(result);
    const CampaignSession::LevelCompleted& completed = gameCompleted
        ? std::get<CampaignSession::GameCompleted>(result).finalLevel
        : std::get<CampaignSession::LevelCompleted>(result);
    const LevelCompleteStats stats {
        .level = completed.level,
        .moves = completed.moves,
        .timeSeconds = completed.timeSeconds,
        .previousBestMoves = completed.previousBestMoves,
        .previousBestTimeSeconds = completed.previousBestTimeSeconds,
        .newBestMoves = completed.newBestMoves,
        .newBestTime = completed.newBestTime,
        .hasNextLevel = completed.hasNextLevel,
    };
    persistProfile(true);
    if (gameCompleted) {
        // The final level: congratulate with whole-game stats instead of the
        // per-level screen; Level Select is unlocked from here on.
        std::vector<GameCompleteLevelStats> levels;
        for (int level = 0; level < campaign_.levelCount(); ++level) {
            const PlayerProfile::LevelProgress* levelProgress =
                playerProfile_.progressForLevel(level);
            levels.push_back({
                .bestMoves = levelProgress ? levelProgress->bestMoves : std::nullopt,
                .bestTimeSeconds =
                    levelProgress ? levelProgress->bestTimeSeconds : std::nullopt,
            });
        }
        levelCompleteOverlay_.openGameComplete(std::move(levels));
        return;
    }
    levelCompleteOverlay_.open(stats);
}

void Application::resolveLevelComplete(bool toTitle)
{
    levelCompleteOverlay_.close();
    campaign_.resolveLevelComplete(playerProfile_);
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
        saveSlots_.slotSummaries(playerProfile_, campaign_.levelCount())) {
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
    if (campaign_.gameLoaded() && !tools_->levelEditor.playingDraft()) {
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
    if (slot == saveSlots_.activeSlot()) {
        // The file stays absent until the player starts playing again.
        playerProfile_.resetProgress();
        levelCompleteOverlay_.close();
        campaign_.resetForProfile(playerProfile_);
    }
    saveSlots_.deleteSlot(slot);
    titleScreen_.setSaveSlots(saveSlotInfos(), saveSlots_.activeSlot());
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
    return campaign_.allLevelsCompleted(playerProfile_);
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
    if (campaign_.startLevel(playerProfile_, level, screen)) {
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
    if (effects.audio) {
        audioSystem_.setMasterVolume(effects.audio->masterVolume);
        audioSystem_.setMusicVolume(effects.audio->musicVolume);
        audioSystem_.setSoundVolume(effects.audio->soundVolume);
    }
    if (effects.input) {
        input_.setBindings(*effects.input);
    }
    if (effects.stepDurationSeconds) {
        gameplaySession_.setStepDurationSeconds(
            *effects.stepDurationSeconds);
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
    campaign_.setLevelScreenCounts(std::move(screenCounts));
}

void Application::restoreProfileLocation()
{
    const LevelLocation saved {
        .level = playerProfile_.currentLevel,
        .screen = playerProfile_.currentScreen,
    };
    if (!campaign_.restoreProfileLocation(playerProfile_)) {
        log::warning(log::Category::Persistence)
            << "Saved level location " << saved.level << ':' <<
            saved.screen << " does not exist; falling back to 0:0";
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
    RenderAssetRequirements requirements =
        levelAssetRequirements(campaign_.currentLevel());

    int nextLevel = campaign_.currentLevel() + 1;
    if (!campaign_.screenExists(nextLevel, 0)) {
        nextLevel = 0;
    }
    if (nextLevel != campaign_.currentLevel() &&
        campaign_.screenExists(nextLevel, 0)) {
        requirements.merge(levelAssetRequirements(nextLevel));
    }
    renderer_.preloadAssets(requirements);
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
        renderFrameArena_.reset();
        return RenderFrameBuilder::buildEditor({
            .manifest = assetManifest_,
            .editor = tools_->levelEditor,
            .settings = presentationSettings_,
            .animations = &animationCatalog_,
            .hoverCell = tools_->hoverCell,
            .hoverDecoration = tools_->hoverDecoration,
            .deleting = editorInput.deleting &&
                tools_->levelEditor.tool() == LevelEditor::Tool::Tiles,
            .worldAnimationTimeSeconds =
                presentation_.worldAnimationTimeSeconds(),
            .conveyorBeltScrollOffset = beltScrollOffset,
            .levelLocation =
                levelLocationFromScreenPath(tools_->levelEditor.loadedDocumentPath()),
        }, renderFrameArena_);
    }
#endif

    if (!campaign_.gameLoaded()) {
        // Title-only: nothing to draw behind the fullscreen menu.
        return RenderFrameData {};
    }

    // Held by reference for the duration of the call, so it has to outlive it.
    const GameState projectedState = gameplaySession_.projectedState();
    renderFrameArena_.reset();
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
        .levelLocation = LevelLocation {
            .level = campaign_.currentLevel(),
            .screen = campaign_.currentScreen(),
        },
    }, renderFrameArena_);
    particleSystem_.appendRenderData(frame);
    return frame;
}

} // namespace sokoban
