#include "engine/Application.hpp"

#include "engine/DebugUi.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/Rules.hpp"
#include "engine/RuntimeContent.hpp"

#include <SDL3/SDL.h>

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#endif

#include <algorithm>
#include <iostream>
#include <utility>

namespace sokoban {
namespace {

constexpr double profileAutosaveIntervalSeconds = 2.0;

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
{
    std::cerr << saveSlots_.progressStatus() << '\n';
    currentLevel_ = playerProfile_.currentLevel;
    currentScreen_ = playerProfile_.currentScreen;
    if (!screenExists(currentLevel_, currentScreen_)) {
        currentLevel_ = 0;
        currentScreen_ = 0;
        playerProfile_.setCurrentLevel(0);
    }
    applyLoadedProfileSettings();
    presentationSettings_.applyTileScales(assetManifest_);
    presentationSettings_.normalize();
    presentation_.setPlayerClips(
        assetManifest_.playerMoveAnimation(),
        assetManifest_.playerPushAnimation());
    // The world stays unloaded until the title's Continue/New Game, but its
    // assets warm up in the background so that first load doesn't block.
    openTitleScreen();
    renderer_.preloadAssets(levelAssetRequirements(currentLevel_));

#if SOKOBAN_ENABLE_DEBUG_UI
    levelEditor_.initialize(
        SOKOBAN_SOURCE_LEVEL_DIR,
        assetRoot_ / "levels",
        currentLevel_,
        currentScreen_);
    levelEditorDebugUi_.initialize(levelEditor_);
    animationPreviewDebugUi_.initialize(SOKOBAN_SOURCE_ASSET_DIR);
    assetManifestEditor_.initialize(
        std::filesystem::path(SOKOBAN_SOURCE_ASSET_DIR) / "manifest.json");

    DebugUi::addTab("Engine", [this] {
        applicationDebugUi_.draw({
            .currentLevel = currentLevel_,
            .currentScreen = currentScreen_,
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
                applyAudioSettings(settings, persist);
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
    if (gameLoaded_ && !gameplaySession_.moving() && !levelEditor_.playingDraft()) {
        checkpointCurrentScreen(false);
    } else if (!playerProfile_.progressEmpty()) {
        persistProfile(true);
    }
    saveSlots_.flush();
    if (!saveSlots_.progressDiagnostics().lastWriteSucceeded) {
        std::cerr << saveSlots_.progressStatus() << '\n';
    }
    if (!saveSlots_.settingsDiagnostics().lastWriteSucceeded) {
        std::cerr << saveSlots_.settingsStatus() << '\n';
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

            // While the Controls page listens for a binding, raw key/pad
            // events become candidates instead of navigation; keys bound to
            // MenuBack still pass through so Escape can cancel the capture.
            const bool capturingBinding = optionsMenu_.capturingBinding();
            if (capturingBinding) {
                if (const std::optional<InputBinding> candidate =
                        InputState::bindingCandidate(event)) {
                    optionsMenu_.provideBindingCandidate(*candidate);
                }
            }

            const bool isKeyboardEvent =
                event.type == SDL_EVENT_KEY_DOWN ||
                event.type == SDL_EVENT_KEY_UP;
            const bool isMouseEvent =
                event.type == SDL_EVENT_MOUSE_MOTION ||
                event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                event.type == SDL_EVENT_MOUSE_BUTTON_UP;
            bool isEditorEditModifier = false;
#if SOKOBAN_ENABLE_DEBUG_UI
            isEditorEditModifier =
                isKeyboardEvent &&
                levelEditor_.editingDocument() &&
                (event.key.scancode == SDL_SCANCODE_R ||
                    event.key.scancode == SDL_SCANCODE_D);
#endif
            const bool allowKeyboardInput =
                !isKeyboardEvent ||
                shellMenuOpen() ||
                !renderer_.wantsKeyboardCapture() ||
                event.type == SDL_EVENT_KEY_UP ||
                isEditorEditModifier ||
                input_.keyBoundToAction(
                    event.key.scancode,
                    InputAction::MenuBack);
            const bool allowMouseInput =
                !isMouseEvent ||
                shellMenuOpen() ||
                !renderer_.wantsMouseCapture() ||
                event.type == SDL_EVENT_MOUSE_BUTTON_UP;
            const bool suppressForCapture = capturingBinding &&
                ((isKeyboardEvent &&
                     !input_.keyBoundToAction(
                         event.key.scancode, InputAction::MenuBack)) ||
                    event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                    event.type == SDL_EVENT_GAMEPAD_BUTTON_UP ||
                    event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION);
            if (!suppressForCapture && allowKeyboardInput && allowMouseInput) {
                input_.handleEvent(event);
            }

            if (event.type == SDL_EVENT_QUIT) {
                handleShellEvent(ShellCloseRequested {});
            }
        }

        if (input_.actionPressed(InputAction::MenuBack)) {
#if SOKOBAN_ENABLE_DEBUG_UI
            if (draftExitConfirmationOpen_) {
                draftExitConfirmationOpen_ = false;
            } else if (levelEditor_.playingDraft()) {
                draftExitConfirmationOpen_ = true;
            } else
#endif
            {
                handleShellEvent(ShellBackPressed {});
            }
        }

        const float dt = frameTimer_.tick();
        update(dt);
        animationPreviewDebugUi_.update(dt, renderer_);

        const Vec2 windowSize = window_.size();
        const Vec2 pixelSize = window_.sizeInPixels();
        const Vec2 mouse = input_.mousePosition();
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
            input_.mouseButtonDown(SDL_BUTTON_LEFT),
            input_.mouseButtonPressed(SDL_BUTTON_LEFT));

        renderer_.beginDebugUiFrame();
        if (!optionsMenu_.isOpen() && !titleScreen_.isOpen()) {
            DebugUi::draw();
        }
        drawDraftExitConfirmation();

        const bool optionsOnTop = optionsMenu_.isOpen();
        const bool navUp = input_.actionPressed(InputAction::MoveUp);
        const bool navDown = input_.actionPressed(InputAction::MoveDown);
        const bool navLeft = input_.actionPressed(InputAction::MoveLeft);
        const bool navRight = input_.actionPressed(InputAction::MoveRight);
        const bool navConfirm = input_.actionPressed(InputAction::MenuConfirm);

        if (const std::optional<TitleAction> titleAction = titleScreen_.draw(
                ui_, pixelSize,
                optionsOnTop
                    ? TitleScreenInput {}
                    : TitleScreenInput {
                        .up = navUp,
                        .down = navDown,
                        .left = navLeft,
                        .right = navRight,
                        .confirm = navConfirm,
                    })) {
            handleShellEvent(ShellTitleAction { *titleAction });
        }

        if (const std::optional<OverlayAction> overlayAction =
                levelCompleteOverlay_.draw(
                    ui_, pixelSize,
                    optionsOnTop
                        ? LevelCompleteInput {}
                        : LevelCompleteInput {
                            .up = navUp,
                            .down = navDown,
                            .confirm = navConfirm,
                        })) {
            handleShellEvent(ShellOverlayAction { *overlayAction });
        }

        if (const std::optional<OptionsAction> optionsAction =
                optionsMenu_.draw(ui_, pixelSize, {
                    .up = navUp,
                    .down = navDown,
                    .left = navLeft,
                    .right = navRight,
                    .confirm = navConfirm,
                })) {
            handleShellEvent(ShellOptionsAction { *optionsAction });
        }
        ui_.endFrame();
        renderer_.drawFrame(buildRenderFrame(), ui_.drawData());
    }
}

void Application::update(float dt)
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
        updateEditorPainting();
        return;
    }
#endif

    currentLevelElapsedSeconds_ += static_cast<double>(std::max(dt, 0.0f));
    queuePressedCommands();
    advancePlayerMovement(dt);
    updateDeferredCheckpoint(dt);

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

void Application::updateEditorPainting()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    editorHoverCell_.reset();
    if (input_.keyPressed(SDL_SCANCODE_Z)) {
        const bool undone = levelEditor_.tryUndoEdit();
        (void)undone;
        return;
    }
    if (renderer_.wantsMouseCapture()) {
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

    const Vec2 mouse = input_.mousePosition();
    const Vec2 mousePixels {
        mouse.x * pixelSize.x / windowSize.x,
        mouse.y * pixelSize.y / windowSize.y,
    };
    const RenderFrameData editorFrame = RenderFrameBuilder::buildEditor({
        .manifest = assetManifest_,
        .editor = levelEditor_,
        .settings = presentationSettings_,
        .hoverCell = editorHoverCell_,
        .deleting = input_.keyDown(SDL_SCANCODE_D),
        .worldAnimationTimeSeconds =
            presentation_.worldAnimationTimeSeconds(),
        .conveyorBeltScrollOffset =
            presentation_.conveyorBeltScrollOffset(
                gameplaySession_.stepDurationSeconds()),
    });
    if (const std::optional<GridPosition3> clicked =
            renderer_.pickIsoGridCell(editorFrame, mousePixels)) {
        GridPosition3 target = *clicked;
        const bool deleting = input_.keyDown(SDL_SCANCODE_D);
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
        } else if (!input_.keyDown(SDL_SCANCODE_R)) {
            const std::optional<int> occupied =
                topmostOccupiedLayer(target);
            target.z = occupied ? *occupied + 1 : 0;
        }

        editorHoverCell_ = target;
        if (input_.mouseButtonPressed(SDL_BUTTON_LEFT)) {
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
    const PlayerProfile::ActiveScreen* checkpoint = nullptr;
    if (playerProfile_.activeScreen &&
        playerProfile_.activeScreen->level == currentLevel_ &&
        playerProfile_.activeScreen->screen == currentScreen_) {
        checkpoint = &*playerProfile_.activeScreen;
        completedLevelMoveCount_ = checkpoint->completedLevelMoveCount;
        currentLevelElapsedSeconds_ = checkpoint->levelElapsedSeconds;
    }

    const bool restored = applyLevel(
        Level::loadFromFile(screenPath(currentLevel_, currentScreen_)),
        checkpoint ? &checkpoint->session : nullptr);
    if (checkpoint && !restored) {
        std::cerr << "Discarded invalid gameplay checkpoint for level "
                  << currentLevel_ << " screen " << currentScreen_ << '\n';
        playerProfile_.activeScreen.reset();
    }
    playerProfile_.setCurrentScreen(currentLevel_, currentScreen_);
    playerProfile_.recordReachedScreen(currentLevel_, currentScreen_);
    checkpointCurrentScreen(true);
    audioSystem_.playMusicForLevel(currentLevel_);
    preloadUpcomingAssets();
    levelEditor_.setPlayingDraft(false);
    levelEditor_.setEditingDocument(false);
    editorHoverCell_.reset();

    std::cerr << "player started level "
              << currentLevel_
              << " screen "
              << currentScreen_
              << '\n';
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
    gameLoaded_ = true;
    return restored;
}

void Application::advanceScreen()
{
    const int completedMoves =
        completedLevelMoveCount_ + gameplaySession_.playerMoveCount();
    if (screenExists(currentLevel_, currentScreen_ + 1)) {
        completedLevelMoveCount_ = completedMoves;
        ++currentScreen_;
        playerProfile_.setCurrentScreen(currentLevel_, currentScreen_);
        loadCurrentScreen();
        return;
    }

    // The level is complete: record it, then hold on the finished screen
    // behind the stats overlay until the player chooses where to go.
    const bool hasNextLevel = screenExists(currentLevel_ + 1, 0);
    const PlayerProfile::LevelProgress* progress =
        playerProfile_.progressForLevel(currentLevel_);
    const std::optional<int> previousBestMoves =
        progress ? progress->bestMoves : std::nullopt;
    const std::optional<double> previousBestTime =
        progress ? progress->bestTimeSeconds : std::nullopt;
    const LevelCompleteStats stats {
        .level = currentLevel_,
        .moves = completedMoves,
        .timeSeconds = currentLevelElapsedSeconds_,
        .previousBestMoves = previousBestMoves,
        .previousBestTimeSeconds = previousBestTime,
        .newBestMoves = levelRunFromStart_ &&
            (!previousBestMoves || completedMoves < *previousBestMoves),
        .newBestTime = levelRunFromStart_ &&
            (!previousBestTime || currentLevelElapsedSeconds_ < *previousBestTime),
        .hasNextLevel = hasNextLevel,
    };
    playerProfile_.recordLevelCompletion(
        currentLevel_,
        completedMoves,
        currentLevelElapsedSeconds_,
        hasNextLevel,
        levelRunFromStart_);
    persistProfile(true);
    pendingNextLevel_ = hasNextLevel ? currentLevel_ + 1 : 0;
    if (!hasNextLevel) {
        // The final level: congratulate with whole-game stats instead of the
        // per-level screen; Level Select is unlocked from here on.
        std::vector<GameCompleteLevelStats> levels;
        for (int level = 0; screenExists(level, 0); ++level) {
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
    currentLevel_ = pendingNextLevel_.value_or(0);
    currentScreen_ = 0;
    pendingNextLevel_.reset();
    completedLevelMoveCount_ = 0;
    currentLevelElapsedSeconds_ = 0.0;
    levelRunFromStart_ = true;
    playerProfile_.setCurrentScreen(currentLevel_, currentScreen_);
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

void Application::applyLoadedProfileSettings()
{
    playerProfile_.normalize();
    if (playerProfile_.video.fullscreen) {
        window_.setFullscreen(true);
    } else {
        window_.setWindowedSize(
            playerProfile_.video.windowWidth,
            playerProfile_.video.windowHeight);
    }
    audioSystem_.setMasterVolume(playerProfile_.audio.masterVolume);
    audioSystem_.setMusicVolume(playerProfile_.audio.musicVolume);
    audioSystem_.setSoundVolume(playerProfile_.audio.soundVolume);
    gameplaySession_.setStepDurationSeconds(
        playerProfile_.accessibility.reducedMotion
            ? 0.05f
            : config::stepDurationSeconds);
    input_.setBindings(playerProfile_.input);
    presentationSettings_.lighting.ambientOcclusionEnabled =
        playerProfile_.video.ambientOcclusion;
    presentationSettings_.normalize();
}

std::vector<SaveSlotInfo> Application::saveSlotInfos() const
{
    int levelCount = 0;
    while (screenExists(levelCount, 0)) {
        ++levelCount;
    }

    std::vector<SaveSlotInfo> slots;
    for (const SaveSlotManager::SlotSummary& summary :
        saveSlots_.slotSummaries(playerProfile_, levelCount)) {
        slots.push_back({
            .empty = summary.empty,
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
    if (gameLoaded_ && !gameplaySession_.moving() && !levelEditor_.playingDraft()) {
        checkpointCurrentScreen(true);
    } else {
        persistProfile(true);
    }

    std::optional<PlayerProfile> switched =
        saveSlots_.switchTo(slot, playerProfile_);
    if (!switched) {
        return;
    }
    playerProfile_ = std::move(*switched);
    std::cerr << saveSlots_.progressStatus() << '\n';

    // The new slot's world loads on Continue/New Game, like at boot.
    gameLoaded_ = false;
    pendingNextLevel_.reset();
    levelCompleteOverlay_.close();
    levelRunFromStart_ = true;
    completedLevelMoveCount_ = 0;
    currentLevelElapsedSeconds_ = 0.0;
    currentLevel_ = playerProfile_.currentLevel;
    currentScreen_ = playerProfile_.currentScreen;
    if (!screenExists(currentLevel_, currentScreen_)) {
        currentLevel_ = 0;
        currentScreen_ = 0;
        playerProfile_.setCurrentLevel(0);
    }
    renderer_.preloadAssets(levelAssetRequirements(currentLevel_));
    openTitleScreen();
}

void Application::deleteSaveSlot(int slot)
{
    if (slot < 0 || slot >= SaveSlotManager::slotCount) {
        return;
    }
    if (slot == saveSlots_.activeSlot()) {
        // The file stays absent until the player starts playing again.
        deferredCheckpointPending_ = false;
        playerProfile_.resetProgress();
        gameLoaded_ = false;
        pendingNextLevel_.reset();
        levelCompleteOverlay_.close();
        levelRunFromStart_ = true;
        completedLevelMoveCount_ = 0;
        currentLevelElapsedSeconds_ = 0.0;
        currentLevel_ = 0;
        currentScreen_ = 0;
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
        .gameLoaded = gameLoaded_,
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
            currentLevel_ = playerProfile_.currentLevel;
            currentScreen_ = playerProfile_.currentScreen;
            if (!screenExists(currentLevel_, currentScreen_)) {
                currentLevel_ = 0;
                currentScreen_ = 0;
                playerProfile_.setCurrentLevel(0);
            }
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
                optionsMenuSettings(), open.pauseContext, open.allowLevelSelect);
        },
        [&](const shell::CloseOptions&) { optionsMenu_.close(); },
        [&](const shell::OptionsBack&) { optionsMenu_.back(); },
        [&](const shell::ApplySettings&) {
            applyOptionsMenuSettings(optionsMenu_.settings());
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
    bool anyLevel = false;
    for (int level = 0; screenExists(level, 0); ++level) {
        anyLevel = true;
        const PlayerProfile::LevelProgress* progress =
            playerProfile_.progressForLevel(level);
        if (progress == nullptr || !progress->completed) {
            return false;
        }
    }
    return anyLevel;
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
    for (int level = 0; screenExists(level, 0); ++level) {
        int screens = 0;
        while (screenExists(level, screens)) {
            ++screens;
        }
        const PlayerProfile::LevelProgress* progress =
            playerProfile_.progressForLevel(level);
        int reached = progress ? progress->reachedScreens : 0;
        if (level == currentLevel_) {
            reached = std::max(reached, currentScreen_ + 1);
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
    playerProfile_.resetProgress();
    titleScreen_.close();
    levelCompleteOverlay_.close();
    pendingNextLevel_.reset();
    currentLevel_ = 0;
    currentScreen_ = 0;
    completedLevelMoveCount_ = 0;
    currentLevelElapsedSeconds_ = 0.0;
    levelRunFromStart_ = true;
    loadCurrentScreen();
    persistProfile(true);
}

void Application::startLevel(int level, int screen)
{
    if (!screenExists(level, screen)) {
        return;
    }
    if (level != currentLevel_ || screen != currentScreen_) {
        // Fresh start at the chosen screen instead of the saved checkpoint.
        playerProfile_.activeScreen.reset();
        completedLevelMoveCount_ = 0;
        currentLevelElapsedSeconds_ = 0.0;
    }
    levelRunFromStart_ = screen == 0;
    currentLevel_ = level;
    currentScreen_ = screen;
    playerProfile_.setCurrentScreen(level, screen);
    loadCurrentScreen();
}

void Application::checkpointCurrentScreen(bool immediateSave)
{
    playerProfile_.setCurrentScreen(currentLevel_, currentScreen_);
    playerProfile_.activeScreen = PlayerProfile::ActiveScreen {
        .level = currentLevel_,
        .screen = currentScreen_,
        .completedLevelMoveCount = completedLevelMoveCount_,
        .levelElapsedSeconds = currentLevelElapsedSeconds_,
        .session = gameplaySession_.snapshot(),
    };
    deferredCheckpointPending_ = false;
    deferredCheckpointAgeSeconds_ = 0.0;
    persistProfile(immediateSave);
}

void Application::deferCurrentScreenCheckpoint()
{
    if (!deferredCheckpointPending_) {
        deferredCheckpointPending_ = true;
        deferredCheckpointAgeSeconds_ = 0.0;
    }
    if (deferredCheckpointAgeSeconds_ >= profileAutosaveIntervalSeconds) {
        checkpointCurrentScreen(true);
    }
}

void Application::updateDeferredCheckpoint(float dt)
{
    if (!deferredCheckpointPending_ || levelEditor_.playingDraft()) {
        return;
    }
    deferredCheckpointAgeSeconds_ += static_cast<double>(std::max(dt, 0.0f));
    if (deferredCheckpointAgeSeconds_ >= profileAutosaveIntervalSeconds &&
        !gameplaySession_.moving()) {
        checkpointCurrentScreen(true);
    }
}

void Application::applyAudioSettings(
    const PlayerProfile::AudioSettings& settings,
    bool persist)
{
    playerProfile_.audio = settings;
    playerProfile_.normalize();
    audioSystem_.setMasterVolume(playerProfile_.audio.masterVolume);
    audioSystem_.setMusicVolume(playerProfile_.audio.musicVolume);
    audioSystem_.setSoundVolume(playerProfile_.audio.soundVolume);
    if (persist) {
        persistProfile(true);
        persistSettings(true);
    }
}

OptionsMenuSettings Application::optionsMenuSettings() const
{
    return {
        .antiAliasingSamples = playerProfile_.video.antiAliasingSamples,
        .renderScalePercent = playerProfile_.video.renderScalePercent,
        .customRenderScale = playerProfile_.video.customRenderScale,
        .customRenderScalePercent = playerProfile_.video.customRenderScalePercent,
        .ambientOcclusion = playerProfile_.video.ambientOcclusion,
        .fullscreen = playerProfile_.video.fullscreen,
        .windowWidth = playerProfile_.video.windowWidth,
        .windowHeight = playerProfile_.video.windowHeight,
        .masterVolume = playerProfile_.audio.masterVolume,
        .musicVolume = playerProfile_.audio.musicVolume,
        .input = playerProfile_.input,
    };
}

void Application::applyOptionsMenuSettings(const OptionsMenuSettings& settings)
{
    const PlayerProfile::VideoSettings oldVideo = playerProfile_.video;
    playerProfile_.video.antiAliasingSamples = settings.antiAliasingSamples;
    playerProfile_.video.renderScalePercent = settings.renderScalePercent;
    playerProfile_.video.customRenderScale = settings.customRenderScale;
    playerProfile_.video.customRenderScalePercent =
        settings.customRenderScalePercent;
    playerProfile_.video.ambientOcclusion = settings.ambientOcclusion;
    playerProfile_.video.fullscreen = settings.fullscreen;
    playerProfile_.video.windowWidth = settings.windowWidth;
    playerProfile_.video.windowHeight = settings.windowHeight;

    playerProfile_.audio.masterVolume = settings.masterVolume;
    playerProfile_.audio.musicVolume = settings.musicVolume;
    if (!(playerProfile_.input == settings.input)) {
        playerProfile_.input = settings.input;
        input_.setBindings(playerProfile_.input);
    }
    playerProfile_.normalize();

    if (oldVideo.antiAliasingSamples != playerProfile_.video.antiAliasingSamples) {
        renderer_.setAntiAliasingMode(
            antiAliasingModeForSamples(playerProfile_.video.antiAliasingSamples));
    }
    if (oldVideo.effectiveRenderScalePercent() !=
        playerProfile_.video.effectiveRenderScalePercent()) {
        renderer_.setRenderScalePercent(
            playerProfile_.video.effectiveRenderScalePercent());
    }
    presentationSettings_.lighting.ambientOcclusionEnabled =
        playerProfile_.video.ambientOcclusion;

    if (oldVideo.fullscreen != playerProfile_.video.fullscreen ||
        (!playerProfile_.video.fullscreen &&
            (oldVideo.windowWidth != playerProfile_.video.windowWidth ||
                oldVideo.windowHeight != playerProfile_.video.windowHeight))) {
        if (playerProfile_.video.fullscreen) {
            window_.setFullscreen(true);
        } else {
            window_.setWindowedSize(
                playerProfile_.video.windowWidth,
                playerProfile_.video.windowHeight);
        }
    }

    audioSystem_.setMasterVolume(playerProfile_.audio.masterVolume);
    audioSystem_.setMusicVolume(playerProfile_.audio.musicVolume);
    persistProfile(false);
    persistSettings(false);
}

void Application::persistProfile(bool immediate)
{
    // Slots with no progress stay off disk entirely (settings persist through
    // their own shared store), preserving the fresh-install empty slate.
    if (!gameLoaded_ && playerProfile_.progressEmpty()) {
        return;
    }
    saveSlots_.saveProgress(playerProfile_, immediate);
}

void Application::queuePressedCommands()
{
    if (input_.actionPressed(InputAction::Undo)) {
        gameplaySession_.queueUndo();
    }
    if (input_.actionPressed(InputAction::Restart)) {
        gameplaySession_.queueRestart();
    }

    const std::optional<MoveDirection> vertical =
        pressedVerticalDirection();
    const std::optional<MoveDirection> horizontal =
        pressedHorizontalDirection();
    if (vertical) {
        gameplaySession_.queueMove(*vertical);
    }
    if (horizontal) {
        gameplaySession_.queueMove(*horizontal);
    }
}

void Application::advancePlayerMovement(float dt)
{
    presentation_.advanceAnimations(dt);
    float remainingTime = dt;

    while (remainingTime > 0.0f) {
        if (!gameplaySession_.moving() && !tryStartNextMove()) {
            return;
        }

        const float duration =
            gameplaySession_.activeActionDuration();
        if (duration <= 0.0f) {
            if (completeActiveAction()) {
                return;
            }
            continue;
        }

        const float timeToFinish =
            gameplaySession_.activeActionRemainingSeconds();
        const float step = std::min(remainingTime, timeToFinish);
        remainingTime -= step;
        gameplaySession_.advanceActiveAction(step);

        if (gameplaySession_.activeActionComplete() &&
            completeActiveAction()) {
            return;
        }
    }
}

bool Application::completeActiveAction()
{
    gameplaySession_.completeActiveAction();
    presentation_.finishAction(gameplaySession_.state());

    if (rules::isAtUnlockedEnd(level_, gameplaySession_.state())) {
        if (levelEditor_.playingDraft()) {
            levelEditor_.markDraftSolved();
            return false;
        }
        advanceScreen();
        return true;
    }
    if (!levelEditor_.playingDraft()) {
        deferCurrentScreenCheckpoint();
    }
    return false;
}

bool Application::tryStartNextMove()
{
    const GameplaySession::Controls controls {
        .undoHeld = input_.actionDown(InputAction::Undo),
        .verticalMove = heldVerticalDirection(),
        .horizontalMove = heldHorizontalDirection(),
    };
    if (!gameplaySession_.tryStartNextAction(level_, controls)) {
        return false;
    }
    presentation_.beginAction(gameplaySession_.activeAction());
    return true;
}

std::optional<MoveDirection> Application::pressedVerticalDirection() const
{
    const bool upPressed = input_.actionPressed(InputAction::MoveUp);
    const bool downPressed = input_.actionPressed(InputAction::MoveDown);
    if (upPressed == downPressed) {
        return std::nullopt;
    }
    if ((upPressed && input_.actionDown(InputAction::MoveDown)) ||
        (downPressed && input_.actionDown(InputAction::MoveUp))) {
        return std::nullopt;
    }
    return upPressed ? MoveDirection::Up : MoveDirection::Down;
}

std::optional<MoveDirection> Application::pressedHorizontalDirection() const
{
    const bool leftPressed = input_.actionPressed(InputAction::MoveLeft);
    const bool rightPressed = input_.actionPressed(InputAction::MoveRight);
    if (leftPressed == rightPressed) {
        return std::nullopt;
    }
    if ((leftPressed && input_.actionDown(InputAction::MoveRight)) ||
        (rightPressed && input_.actionDown(InputAction::MoveLeft))) {
        return std::nullopt;
    }
    return leftPressed ? MoveDirection::Left : MoveDirection::Right;
}

std::optional<MoveDirection> Application::heldVerticalDirection() const
{
    const bool up = input_.actionDown(InputAction::MoveUp);
    const bool down = input_.actionDown(InputAction::MoveDown);
    if (up == down) {
        return std::nullopt;
    }
    return up ? MoveDirection::Up : MoveDirection::Down;
}

std::optional<MoveDirection> Application::heldHorizontalDirection() const
{
    const bool left = input_.actionDown(InputAction::MoveLeft);
    const bool right = input_.actionDown(InputAction::MoveRight);
    if (left == right) {
        return std::nullopt;
    }
    return left ? MoveDirection::Left : MoveDirection::Right;
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

bool Application::screenExists(int levelIndex, int screenIndex) const
{
    if (levelIndex < 0 || screenIndex < 0) {
        return false;
    }
    return std::filesystem::exists(
        screenPath(levelIndex, screenIndex));
}

RenderAssetRequirements Application::levelAssetRequirements(int levelIndex) const
{
    RenderAssetRequirements requirements;
    for (int screenIndex = 0; screenExists(levelIndex, screenIndex); ++screenIndex) {
        try {
            requirements.merge(renderAssetRequirementsForLevel(
                Level::loadFromFile(screenPath(levelIndex, screenIndex)),
                assetManifest_));
        } catch (const std::exception& error) {
            std::cerr << "asset preload skipped "
                      << screenPath(levelIndex, screenIndex).string()
                      << ": " << error.what() << '\n';
        }
    }
    return requirements;
}

void Application::preloadUpcomingAssets()
{
    RenderAssetRequirements requirements =
        levelAssetRequirements(currentLevel_);

    int nextLevel = currentLevel_ + 1;
    if (!screenExists(nextLevel, 0)) {
        nextLevel = 0;
    }
    if (nextLevel != currentLevel_ && screenExists(nextLevel, 0)) {
        requirements.merge(levelAssetRequirements(nextLevel));
    }
    renderer_.preloadAssets(requirements);
}

RenderFrameData Application::buildRenderFrame() const
{
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
            .deleting = input_.keyDown(SDL_SCANCODE_D),
            .worldAnimationTimeSeconds =
                presentation_.worldAnimationTimeSeconds(),
            .conveyorBeltScrollOffset = beltScrollOffset,
        });
    }
#endif

    if (!gameLoaded_) {
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
