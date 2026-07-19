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
    , saveStore_(SaveStore::preferencePath("Sokoban3D", "Sokoban3D"))
    , playerProfile_(saveStore_.load().profile)
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
    std::cerr << saveStore_.status() << '\n';
    currentLevel_ = playerProfile_.currentLevel;
    currentScreen_ = playerProfile_.currentScreen;
    if (!screenExists(currentLevel_, currentScreen_)) {
        currentLevel_ = 0;
        currentScreen_ = 0;
        playerProfile_.setCurrentLevel(0);
    }
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
    presentationSettings_.applyTileScales(assetManifest_);
    presentationSettings_.lighting.ambientOcclusionEnabled =
        playerProfile_.video.ambientOcclusion;
    presentationSettings_.normalize();
    presentation_.setPlayerClips(
        assetManifest_.playerMoveAnimation(),
        assetManifest_.playerPushAnimation());
    loadCurrentScreen();

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
            .saveDiagnostics = saveStore_.diagnostics(),
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
    if (!gameplaySession_.moving() && !levelEditor_.playingDraft()) {
        checkpointCurrentScreen(false);
    } else {
        persistProfile(true);
    }
    saveStore_.flush();
    if (!saveStore_.diagnostics().lastWriteSucceeded) {
        std::cerr << saveStore_.status() << '\n';
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
                optionsMenu_.isOpen() ||
                !renderer_.wantsKeyboardCapture() ||
                event.type == SDL_EVENT_KEY_UP ||
                isEditorEditModifier ||
                input_.keyBoundToAction(
                    event.key.scancode,
                    InputAction::MenuBack);
            const bool allowMouseInput =
                !isMouseEvent ||
                optionsMenu_.isOpen() ||
                !renderer_.wantsMouseCapture() ||
                event.type == SDL_EVENT_MOUSE_BUTTON_UP;
            if (allowKeyboardInput && allowMouseInput) {
                input_.handleEvent(event);
            }

            if (event.type == SDL_EVENT_QUIT) {
                optionsMenu_.requestQuitConfirmation();
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
            if (optionsMenu_.isOpen()) {
                optionsMenu_.back();
            } else {
                optionsMenu_.open(optionsMenuSettings());
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
        if (!optionsMenu_.isOpen()) {
            DebugUi::draw();
        }
        drawDraftExitConfirmation();
        const OptionsMenuResult menuResult = optionsMenu_.draw(ui_, pixelSize, {
            .up = input_.actionPressed(InputAction::MoveUp),
            .down = input_.actionPressed(InputAction::MoveDown),
            .left = input_.actionPressed(InputAction::MoveLeft),
            .right = input_.actionPressed(InputAction::MoveRight),
            .confirm = input_.actionPressed(InputAction::MenuConfirm),
        });
        if (menuResult.settingsChanged) {
            applyOptionsMenuSettings(optionsMenu_.settings());
        }
        if (menuResult.quitRequested) {
            running_ = false;
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

    if (optionsMenu_.isOpen()) {
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
    return restored;
}

void Application::advanceScreen()
{
    const int completedMoves =
        completedLevelMoveCount_ + gameplaySession_.playerMoveCount();
    if (screenExists(currentLevel_, currentScreen_ + 1)) {
        completedLevelMoveCount_ = completedMoves;
        ++currentScreen_;
    } else if (screenExists(currentLevel_ + 1, 0)) {
        playerProfile_.recordLevelCompletion(
            currentLevel_,
            completedMoves,
            currentLevelElapsedSeconds_,
            true);
        ++currentLevel_;
        currentScreen_ = 0;
        completedLevelMoveCount_ = 0;
        currentLevelElapsedSeconds_ = 0.0;
    } else {
        playerProfile_.recordLevelCompletion(
            currentLevel_,
            completedMoves,
            currentLevelElapsedSeconds_,
            false);
        currentLevel_ = 0;
        currentScreen_ = 0;
        completedLevelMoveCount_ = 0;
        currentLevelElapsedSeconds_ = 0.0;
    }
    playerProfile_.setCurrentScreen(currentLevel_, currentScreen_);
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
}

void Application::persistProfile(bool immediate)
{
    saveStore_.requestSave(
        playerProfile_,
        immediate
            ? AsyncSaveStore::Urgency::Immediate
            : AsyncSaveStore::Urgency::Deferred);
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
