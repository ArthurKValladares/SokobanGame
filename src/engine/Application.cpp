#include "engine/Application.hpp"

#include "engine/DebugUi.hpp"
#include "engine/Log.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/RuntimeContent.hpp"

#include <SDL3/SDL.h>

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#endif

#include <algorithm>
#include <exception>
#include <utility>

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
    : window_("Sokoban 3D", 1280, 720)
    , saveSlots_(SaveStore::preferencePath("Sokoban3D", "Sokoban3D"))
    , playerProfile_(saveSlots_.loadActiveProfile())
    , assetRoot_(runtimeContentRoot())
    , assetManifest_(AssetManifest::loadFromFile(assetRoot_ / "manifest.json"))
    , uiFont_(FontAtlas::load(assetRoot_ / config::uiFontPath))
    , renderer_(
          window_.nativeHandle(),
          assetRoot_,
          assetManifest_,
          uiFont_,
          antiAliasingModeForSamples(playerProfile_.video.antiAliasingSamples),
          playerProfile_.video.effectiveRenderScalePercent(),
          playerProfile_.video.vsync)
    , ui_(uiFont_)
    , audioSystem_(assetRoot_, assetManifest_)
    , settingsCoordinator_(playerProfile_, presentationSettings_)
{
    // Leave a diagnostic trail next to the profiles so shipped builds can be
    // debugged from the save directory; Debug builds also emit debug traces.
    log::addFileSink(saveSlots_.directory() / "log.txt");
#if SOKOBAN_ENABLE_DEBUG_UI
    log::setMinimumLevel(log::Level::Debug);
#endif
    log::info() << saveSlots_.progressStatus();
    buildLevelCatalog();
    restoreProfileLocation();
    applySettingsEffects(settingsCoordinator_.initialize());
    presentationSettings_.applyTileScales(assetManifest_);
    presentationSettings_.normalize();
    presentation_.setPlayerClips(
        assetManifest_.playerMoveAnimation(),
        assetManifest_.playerPushAnimation());
    // The world stays unloaded until the title's Continue/New Game, but its
    // assets warm up in the background so that first load doesn't block.
    openTitleScreen();
    renderer_.preloadAssets(
        levelAssetRequirements(campaign_.currentLevel()));

#if SOKOBAN_ENABLE_DEBUG_UI
    levelEditor_.initialize(
        SOKOBAN_SOURCE_LEVEL_DIR,
        assetRoot_ / "levels",
        campaign_.currentLevel(),
        campaign_.currentScreen());
    levelEditorDebugUi_.initialize(levelEditor_);
    animationPreviewDebugUi_.initialize(SOKOBAN_SOURCE_ASSET_DIR);
    assetManifestEditor_.initialize(
        std::filesystem::path(SOKOBAN_SOURCE_ASSET_DIR) / "manifest.json");

    DebugUi::addTab("Engine", [this] {
        applicationDebugUi_.draw({
            .currentLevel = campaign_.currentLevel(),
            .currentScreen = campaign_.currentScreen(),
            .level = level_,
            .gameplaySession = gameplaySession_,
            .input = input_,
            .renderer = renderer_,
            .settings = presentationSettings_,
            .audio = audioSystem_,
            .saveDiagnostics = saveSlots_.progressDiagnostics(),
            .audioSettings = playerProfile_.audio,
            .updateAudioSettings = [this](
                PlayerProfile::AudioSettings settings,
                bool persist) {
                applySettingsEffects(
                    settingsCoordinator_.applyAudioSettings(settings, persist));
            },
        });
    });
    DebugUi::addTab("Asset Manifest", [this] {
        assetManifestDebugUi_.draw(assetManifestEditor_);
    });
    DebugUi::addTab("Level Editor", [this] {
        levelEditorDebugUi_.draw(levelEditor_, {
            .playDraft = [this](Level level) {
                (void)applyLevel(std::move(level));
            },
            .returnToCurrentScreen = [this] {
                loadCurrentScreen();
            },
        });
    });
    DebugUi::addTab("Animation Preview", [this] {
        animationPreviewDebugUi_.draw(renderer_);
    });
#endif
}

Application::~Application()
{
    if (campaign_.gameLoaded() &&
        !gameplaySession_.moving() &&
        !levelEditor_.playingDraft()) {
        checkpointCurrentScreen(false);
    } else if (!playerProfile_.progressEmpty()) {
        persistProfile(true);
    }
    saveSlots_.flush();
    if (!saveSlots_.progressDiagnostics().lastWriteSucceeded) {
        log::error() << saveSlots_.progressStatus();
    }
    if (!saveSlots_.settingsDiagnostics().lastWriteSucceeded) {
        log::error() << saveSlots_.settingsStatus();
    }
    DebugUi::clearTabs();
    renderer_.waitIdle();
}

void Application::run()
{
    while (running_) {
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
            eventContext.editorEditing = levelEditor_.editingDocument();
#endif
            const InputRouter::EventResult routedEvent =
                inputRouter_.routeEvent(event, input_, eventContext);
            if (routedEvent.bindingCandidate) {
                optionsMenu_.provideBindingCandidate(
                    *routedEvent.bindingCandidate);
            }
            if (routedEvent.closeRequested) {
                handleShellEvent(ShellCloseRequested {});
            }
        }

        switch (inputRouter_.backAction(input_, inputRoutingContext())) {
        case InputRouter::BackAction::CloseDraftConfirmation:
            draftExitConfirmationOpen_ = false;
            break;
        case InputRouter::BackAction::OpenDraftConfirmation:
            draftExitConfirmationOpen_ = true;
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
        update(dt, routedInput);
        animationPreviewDebugUi_.update(dt, renderer_);

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
        drawDraftExitConfirmation();

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
                optionsMenu_.draw(ui_, pixelSize, routedInput.options)) {
            handleShellEvent(ShellOptionsAction { *optionsAction });
        }
        ui_.endFrame();
        renderer_.drawFrame(
            buildRenderFrame(routedInput.editor), ui_.drawData());
    }
}

void Application::update(float dt, const InputRouter::Frame& input)
{
    const bool reversed =
        gameplaySession_.moving() &&
        gameplaySession_.activeAction().reversed;
    presentation_.advanceClocks(dt, reversed);

    if (shellMenuOpen()) {
        audioSystem_.update(dt, false, false);
        return;
    }

#if SOKOBAN_ENABLE_DEBUG_UI
    if (draftExitConfirmationOpen_) {
        audioSystem_.update(dt, false, false);
        return;
    }
    if (levelEditor_.editingDocument()) {
        audioSystem_.update(dt, false, false);
        updateEditorPainting(input.editor);
        return;
    }
#endif

    campaign_.addElapsedTime(dt);
    const GameplayLoop::UpdateResult gameplayResult = GameplayLoop::update(
        level_,
        gameplaySession_,
        presentation_,
        input.gameplay,
        dt,
        levelEditor_.playingDraft());
    if (gameplayResult.draftSolved) {
        levelEditor_.markDraftSolved();
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
            levelEditor_.playingDraft())) {
        checkpointCurrentScreen(true);
    }

    const GameplayPresentation::PlayerVisual& playerVisual = presentation_.player();
    const bool pushing =
        playerVisual.motion.moving &&
        playerVisual.movingClip == assetManifest_.playerPushAnimation();
    audioSystem_.update(dt, playerVisual.motion.moving, pushing);
}

void Application::drawDraftExitConfirmation()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    constexpr const char* popupName = "Stop Testing Draft?";
    if (draftExitConfirmationOpen_) {
        ImGui::OpenPopup(popupName);
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
            viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
        ImGuiCond_Appearing,
        ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(
            popupName,
            &draftExitConfirmationOpen_,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(
            "Stop testing this draft and return to the editor?");
        ImGui::Separator();

        if (ImGui::Button("Stop Testing", ImVec2(120.0f, 0.0f))) {
            levelEditor_.setEditingDocument(true);
            editorHoverCell_.reset();
            draftExitConfirmationOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
            draftExitConfirmationOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#endif
}

void Application::updateEditorPainting(const InputRouter::EditorInput& input)
{
    (void)input;
#if SOKOBAN_ENABLE_DEBUG_UI
    editorHoverCell_.reset();
    if (input.undoPressed) {
        const bool undone = levelEditor_.tryUndoEdit();
        (void)undone;
        return;
    }
    if (input.pointerCaptured) {
        return;
    }

    const uint32_t documentWidth = levelEditor_.documentWidth();
    const uint32_t documentHeight = levelEditor_.documentHeight();
    if (documentWidth == 0 || documentHeight == 0) {
        return;
    }

    const Vec2 windowSize = window_.size();
    const Vec2 pixelSize = window_.sizeInPixels();
    if (windowSize.x <= 0.0f ||
        windowSize.y <= 0.0f ||
        pixelSize.x <= 0.0f ||
        pixelSize.y <= 0.0f) {
        return;
    }

    const Vec2 mouse = input.pointerPosition;
    const Vec2 mousePixels {
        mouse.x * pixelSize.x / windowSize.x,
        mouse.y * pixelSize.y / windowSize.y,
    };
    const RenderFrameData editorFrame = RenderFrameBuilder::buildEditor({
        .manifest = assetManifest_,
        .editor = levelEditor_,
        .settings = presentationSettings_,
        .hoverCell = editorHoverCell_,
        .deleting = input.deleting,
        .worldAnimationTimeSeconds =
            presentation_.worldAnimationTimeSeconds(),
        .conveyorBeltScrollOffset =
            presentation_.conveyorBeltScrollOffset(
                gameplaySession_.stepDurationSeconds()),
    });
    if (const std::optional<GridPosition3> clicked =
            renderer_.pickIsoGridCell(editorFrame, mousePixels)) {
        GridPosition3 target = *clicked;
        const bool deleting = input.deleting;
        auto topmostOccupiedLayer =
            [&](GridPosition3 position) -> std::optional<int> {
                const Level::LayerRows& layers =
                    levelEditor_.documentLayers();
                for (int z = static_cast<int>(layers.size()) - 1;
                     z >= 0;
                     --z) {
                    if (position.y < 0 ||
                        position.x < 0 ||
                        position.y >= static_cast<int>(
                            layers[static_cast<std::size_t>(z)].size()) ||
                        position.x >= static_cast<int>(
                            layers[static_cast<std::size_t>(z)]
                                [static_cast<std::size_t>(position.y)]
                                    .size())) {
                        continue;
                    }
                    if (charToTileType(
                            layers[static_cast<std::size_t>(z)]
                                [static_cast<std::size_t>(position.y)]
                                [static_cast<std::size_t>(position.x)])
                            .value_or(TileType::Air) != TileType::Air) {
                        return z;
                    }
                }
                return std::nullopt;
            };

        if (levelEditor_.layerLocked()) {
            target.z = static_cast<int>(levelEditor_.activeLayer());
        } else if (deleting) {
            target.z = topmostOccupiedLayer(target).value_or(target.z);
        } else if (!input.replaceLayer) {
            const std::optional<int> occupied =
                topmostOccupiedLayer(target);
            target.z = occupied ? *occupied + 1 : 0;
        }

        editorHoverCell_ = target;
        if (input.primaryPressed) {
            if (deleting) {
                levelEditor_.eraseCell(target);
            } else {
                levelEditor_.paintCell(target);
            }
        }
    }
#endif
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
        log::warning() << "Discarded invalid gameplay checkpoint for level "
            << campaign_.currentLevel() << " screen "
            << campaign_.currentScreen();
        playerProfile_.activeScreen.reset();
    }
    campaign_.finishScreenLoad(playerProfile_);
    checkpointCurrentScreen(true);
    audioSystem_.playMusicForLevel(campaign_.currentLevel());
    preloadUpcomingAssets();
    levelEditor_.setPlayingDraft(false);
    levelEditor_.setEditingDocument(false);
    editorHoverCell_.reset();

    log::debug() << "player started level " << campaign_.currentLevel()
        << " screen " << campaign_.currentScreen();
}

bool Application::applyLevel(
    Level level,
    const GameplaySession::Snapshot* snapshot)
{
    renderer_.ensureAssets(renderAssetRequirementsForLevel(level, assetManifest_));
    level_ = std::move(level);
    const bool restored = snapshot && gameplaySession_.restore(level_, *snapshot);
    if (!restored) {
        gameplaySession_.reset(level_);
    }
    presentation_.resetEntities(gameplaySession_.state());
    campaign_.markWorldLoaded();
    return restored;
}

void Application::advanceScreen()
{
    const CampaignSession::AdvanceResult result = campaign_.advanceScreen(
        playerProfile_, gameplaySession_.playerMoveCount());
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

    // Settle the outgoing slot on disk first.
    if (campaign_.gameLoaded() &&
        !gameplaySession_.moving() &&
        !levelEditor_.playingDraft()) {
        checkpointCurrentScreen(true);
    } else {
        persistProfile(true);
    }

    std::optional<PlayerProfile> switched;
    try {
        switched = saveSlots_.switchTo(slot, playerProfile_);
    } catch (const std::exception& error) {
        log::error() << "Could not switch to save slot " << (slot + 1) <<
            ": " << error.what();
        return;
    }
    if (!switched) {
        return;
    }
    playerProfile_ = std::move(*switched);
    log::info() << saveSlots_.progressStatus();

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
                settingsCoordinator_.userSettings(),
                open.pauseContext,
                open.allowLevelSelect);
        },
        [&](const shell::CloseOptions&) { optionsMenu_.close(); },
        [&](const shell::OptionsBack&) { optionsMenu_.back(); },
        [&](const shell::ApplySettings&) {
            applySettingsEffects(
                settingsCoordinator_.applyUserSettings(
                    optionsMenu_.settings()));
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
        .mouseCaptured = renderer_.wantsMouseCapture(),
    };
#if SOKOBAN_ENABLE_DEBUG_UI
    context.editorEditing = levelEditor_.editingDocument();
    context.draftPlaying = levelEditor_.playingDraft();
    context.draftExitConfirmationOpen = draftExitConfirmationOpen_;
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
    for (int level = 0;; ++level) {
        int screens = 0;
        while (std::filesystem::exists(screenPath(level, screens))) {
            ++screens;
        }
        if (screens == 0) {
            break;
        }
        screenCounts.push_back(screens);
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
        log::warning() << "Saved level location " << saved.level << ':' <<
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
                assetManifest_));
        } catch (const std::exception& error) {
            log::warning() << "asset preload skipped "
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
    const InputRouter::EditorInput& editorInput) const
{
    (void)editorInput;
    const float beltScrollOffset =
        presentation_.conveyorBeltScrollOffset(
            gameplaySession_.stepDurationSeconds());
#if SOKOBAN_ENABLE_DEBUG_UI
    if (levelEditor_.editingDocument()) {
        return RenderFrameBuilder::buildEditor({
            .manifest = assetManifest_,
            .editor = levelEditor_,
            .settings = presentationSettings_,
            .hoverCell = editorHoverCell_,
            .deleting = editorInput.deleting,
            .worldAnimationTimeSeconds =
                presentation_.worldAnimationTimeSeconds(),
            .conveyorBeltScrollOffset = beltScrollOffset,
        });
    }
#endif

    if (!campaign_.gameLoaded()) {
        // Title-only: nothing to draw behind the fullscreen menu.
        return RenderFrameData {};
    }

    return RenderFrameBuilder::buildGameplay({
        .manifest = assetManifest_,
        .level = level_,
        .state = gameplaySession_.state(),
        .moving = gameplaySession_.moving(),
        .activeAction = gameplaySession_.activeAction(),
        .presentation = presentation_,
        .settings = presentationSettings_,
        .conveyorBeltScrollOffset = beltScrollOffset,
    });
}

} // namespace sokoban
