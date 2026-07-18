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

Application::Application()
    : window_("Sokoban 3D", 1280, 720)
    , assetRoot_(runtimeContentRoot())
    , assetManifest_(AssetManifest::loadFromFile(assetRoot_ / "manifest.json"))
    , renderer_(
          window_.nativeHandle(),
          assetRoot_,
          assetManifest_)
    , audioSystem_(assetRoot_, assetManifest_)
{
    presentationSettings_.applyTileScales(assetManifest_);
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

    DebugUi::addWindow("Engine", [this] {
        applicationDebugUi_.draw({
            .currentLevel = currentLevel_,
            .currentScreen = currentScreen_,
            .level = level_,
            .gameplaySession = gameplaySession_,
            .renderer = renderer_,
            .settings = presentationSettings_,
            .audio = audioSystem_,
        });
    });
    DebugUi::addWindow("Asset Manifest", [this] {
        assetManifestDebugUi_.draw(assetManifestEditor_);
    }, true);
    DebugUi::addWindow("Level Editor", [this] {
        levelEditorDebugUi_.draw(levelEditor_, {
            .playDraft = [this](Level level) {
                applyLevel(std::move(level));
            },
            .returnToCurrentScreen = [this] {
                loadCurrentScreen();
            },
        });
    }, true);
    DebugUi::addWindow("Animation Preview", [this] {
        animationPreviewDebugUi_.draw(renderer_);
    });
#endif
}

Application::~Application()
{
    DebugUi::clearWindows();
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
                !renderer_.wantsKeyboardCapture() ||
                event.type == SDL_EVENT_KEY_UP ||
                isEditorEditModifier;
            const bool allowMouseInput =
                !isMouseEvent ||
                !renderer_.wantsMouseCapture() ||
                event.type == SDL_EVENT_MOUSE_BUTTON_UP;
            if (allowKeyboardInput && allowMouseInput) {
                input_.handleEvent(event);
            }

            if (event.type == SDL_EVENT_QUIT) {
                running_ = false;
            }
            if (event.type == SDL_EVENT_KEY_DOWN &&
                event.key.scancode == SDL_SCANCODE_ESCAPE) {
#if SOKOBAN_ENABLE_DEBUG_UI
                if (levelEditor_.playingDraft()) {
                    draftExitConfirmationOpen_ = true;
                } else {
                    quitConfirmationOpen_ = true;
                }
#else
                quitConfirmationOpen_ = true;
#endif
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
        DebugUi::draw();
        drawQuitConfirmation();
        drawDraftExitConfirmation();
        drawEditorModeIndicator();
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

    if (quitConfirmationOpen_) {
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

    queuePressedCommands();
    advancePlayerMovement(dt);

    const GameplayPresentation::PlayerVisual& playerVisual = presentation_.player();
    const bool pushing =
        playerVisual.motion.moving &&
        playerVisual.movingClip == assetManifest_.playerPushAnimation();
    audioSystem_.update(dt, playerVisual.motion.moving, pushing);
}

void Application::drawEditorModeIndicator()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    const char* label = nullptr;
    ImVec4 color {};
    if (levelEditor_.editingDocument()) {
        label = "Editing Draft";
        color = ImVec4(0.24f, 0.58f, 0.95f, 1.0f);
    } else if (levelEditor_.playingDraft()) {
        label = "Testing Draft";
        color = ImVec4(0.20f, 0.72f, 0.38f, 1.0f);
    }
    if (!label) {
        return;
    }

    constexpr float margin = 12.0f;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + margin,
            viewport->WorkPos.y + viewport->WorkSize.y - margin),
        ImGuiCond_Always,
        ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.82f);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("EditorModeIndicator", nullptr, flags)) {
        ImGui::TextColored(color, "%s", label);
    }
    ImGui::End();
#endif
}

void Application::drawQuitConfirmation()
{
    if (!quitConfirmationOpen_) {
        return;
    }

    const Vec2 viewport = window_.sizeInPixels();
    const UiRect panel {
        .position = {
            (viewport.x - 360.0f) * 0.5f,
            (viewport.y - 180.0f) * 0.5f,
        },
        .size = { 360.0f, 180.0f },
    };

    ui_.panel(panel);
    ui_.text(
        { panel.position.x + 78.0f, panel.position.y + 38.0f },
        "QUIT GAME?",
        { 0.96f, 0.88f, 0.72f, 1.0f },
        4.0f);

    if (ui_.button(
            "quit_game_confirm",
            {
                { panel.position.x + 62.0f, panel.position.y + 108.0f },
                { 104.0f, 42.0f },
            },
            "QUIT")) {
        running_ = false;
        quitConfirmationOpen_ = false;
    }
    if (ui_.button(
            "quit_game_cancel",
            {
                { panel.position.x + 194.0f, panel.position.y + 108.0f },
                { 104.0f, 42.0f },
            },
            "CANCEL")) {
        quitConfirmationOpen_ = false;
    }
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
    applyLevel(
        Level::loadFromFile(screenPath(currentLevel_, currentScreen_)));
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

void Application::applyLevel(Level level)
{
    renderer_.ensureAssets(renderAssetRequirementsForLevel(level, assetManifest_));
    level_ = std::move(level);
    gameplaySession_.reset(level_);
    presentation_.resetEntities(gameplaySession_.state());
}

void Application::advanceScreen()
{
    if (screenExists(currentLevel_, currentScreen_ + 1)) {
        ++currentScreen_;
    } else if (screenExists(currentLevel_ + 1, 0)) {
        ++currentLevel_;
        currentScreen_ = 0;
    } else {
        currentLevel_ = 0;
        currentScreen_ = 0;
    }
    loadCurrentScreen();
}

void Application::queuePressedCommands()
{
    if (input_.keyPressed(SDL_SCANCODE_Z)) {
        gameplaySession_.queueUndo();
    }
    if (input_.keyPressed(SDL_SCANCODE_R)) {
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
    return false;
}

bool Application::tryStartNextMove()
{
    const GameplaySession::Controls controls {
        .undoHeld = input_.keyDown(SDL_SCANCODE_Z),
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
    const bool upPressed = input_.keyPressed(SDL_SCANCODE_W);
    const bool downPressed = input_.keyPressed(SDL_SCANCODE_S);
    if (upPressed == downPressed) {
        return std::nullopt;
    }
    if ((upPressed && input_.keyDown(SDL_SCANCODE_S)) ||
        (downPressed && input_.keyDown(SDL_SCANCODE_W))) {
        return std::nullopt;
    }
    return upPressed ? MoveDirection::Up : MoveDirection::Down;
}

std::optional<MoveDirection> Application::pressedHorizontalDirection() const
{
    const bool leftPressed = input_.keyPressed(SDL_SCANCODE_A);
    const bool rightPressed = input_.keyPressed(SDL_SCANCODE_D);
    if (leftPressed == rightPressed) {
        return std::nullopt;
    }
    if ((leftPressed && input_.keyDown(SDL_SCANCODE_D)) ||
        (rightPressed && input_.keyDown(SDL_SCANCODE_A))) {
        return std::nullopt;
    }
    return leftPressed ? MoveDirection::Left : MoveDirection::Right;
}

std::optional<MoveDirection> Application::heldVerticalDirection() const
{
    const bool up = input_.keyDown(SDL_SCANCODE_W);
    const bool down = input_.keyDown(SDL_SCANCODE_S);
    if (up == down) {
        return std::nullopt;
    }
    return up ? MoveDirection::Up : MoveDirection::Down;
}

std::optional<MoveDirection> Application::heldHorizontalDirection() const
{
    const bool left = input_.keyDown(SDL_SCANCODE_A);
    const bool right = input_.keyDown(SDL_SCANCODE_D);
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
