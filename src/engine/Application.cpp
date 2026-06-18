#include "engine/Application.hpp"

#include "engine/Config.hpp"
#include "engine/DebugUi.hpp"

#include <SDL3/SDL.h>

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#endif

#include <algorithm>
#include <cmath>
#include <iostream>

namespace sokoban {
namespace {

Vec2 toVec2(GridPosition position)
{
    return { static_cast<float>(position.x), static_cast<float>(position.y) };
}

Vec2 lerp(Vec2 from, Vec2 to, float t)
{
    return {
        from.x + (to.x - from.x) * t,
        from.y + (to.y - from.y) * t,
    };
}

} // namespace

Application::Application()
    : window_("Sokoban 3D", 1280, 720)
    , renderer_(window_.nativeHandle(), SOKOBAN_ASSET_DIR)
    , assetRoot_(SOKOBAN_ASSET_DIR)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    DebugUi::addWindow("Engine", [this] {
        drawDebugUi();
    });
#endif

    loadCurrentScreen();
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

            if (!renderer_.wantsKeyboardCapture() || event.type == SDL_EVENT_KEY_UP) {
                input_.handleEvent(event);
            }

            if (event.type == SDL_EVENT_QUIT) {
                running_ = false;
            }

            if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) {
                running_ = false;
            }
        }

        const float dt = frameTimer_.tick();
        update(dt);

        renderer_.beginDebugUiFrame();
        DebugUi::draw();
        renderer_.drawFrame(buildRenderFrame());
    }
}

void Application::update(float dt)
{
    queuePressedCommands();
    advancePlayerMovement(dt);
}

void Application::drawDebugUi()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::Text("Level %d Screen %d", currentLevel_, currentScreen_);
    ImGui::Text("Player (%d, %d)", playerCell_.x, playerCell_.y);
    ImGui::Text("Rocks %zu", rocks_.size());
    ImGui::Text("History %zu", moveHistory_.size());
    ImGui::Text("End %s", isEndUnlocked() ? "unlocked" : "locked");
#endif
}

void Application::loadCurrentScreen()
{
    level_ = Level::loadFromFile(screenPath(currentLevel_, currentScreen_));
    playerCell_ = level_.playerStart();
    playerRenderPosition_ = toVec2(playerCell_);
    rocks_.clear();
    rocks_.reserve(level_.rocks().size());
    for (GridPosition rockPosition : level_.rocks()) {
        rocks_.push_back({
            .cell = rockPosition,
            .renderPosition = toVec2(rockPosition),
        });
    }
    pendingCommands_.clear();
    moveHistory_.clear();
    undoCursor_.reset();
    activeAction_ = {};
    moving_ = false;
    moveElapsed_ = 0.0f;

    std::cerr << "player started level " << currentLevel_ << " screen " << currentScreen_ << '\n';
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
        pendingCommands_.push_back({ .type = MoveCommandType::Undo });
    }
    if (input_.keyPressed(SDL_SCANCODE_R)) {
        pendingCommands_.push_back({ .type = MoveCommandType::Restart });
    }

    const std::optional<MoveDirection> vertical = pressedVerticalDirection();
    const std::optional<MoveDirection> horizontal = pressedHorizontalDirection();
    if (vertical) {
        pendingCommands_.push_back({ .type = MoveCommandType::Move, .direction = *vertical });
    }
    if (horizontal) {
        pendingCommands_.push_back({ .type = MoveCommandType::Move, .direction = *horizontal });
    }
}

void Application::advancePlayerMovement(float dt)
{
    float remainingTime = dt;

    while (remainingTime > 0.0f) {
        if (!moving_ && !tryStartNextMove()) {
            return;
        }

        if constexpr (config::playerMoveDurationSeconds <= 0.0f) {
            if (completeActiveAction()) {
                return;
            }
            continue;
        }

        constexpr float duration = config::playerMoveDurationSeconds;
        const float timeToFinish = duration - moveElapsed_;
        const float step = std::min(remainingTime, timeToFinish);
        remainingTime -= step;
        moveElapsed_ += step;

        const float t = std::clamp(moveElapsed_ / duration, 0.0f, 1.0f);
        playerRenderPosition_ = lerp(toVec2(activeAction_.before.player), toVec2(activeAction_.after.player), t);
        for (size_t i = 0; i < rocks_.size(); ++i) {
            rocks_[i].renderPosition = lerp(toVec2(activeAction_.before.rocks[i]), toVec2(activeAction_.after.rocks[i]), t);
        }

        if (moveElapsed_ >= duration) {
            if (completeActiveAction()) {
                return;
            }
        }
    }
}

bool Application::completeActiveAction()
{
    applyMoveRecord(activeAction_.after);

    moveHistory_.push_back(activeAction_);
    moving_ = false;
    moveElapsed_ = 0.0f;

    if (level_.isEnd(playerCell_) && isEndUnlocked()) {
        advanceScreen();
        return true;
    }

    return false;
}

bool Application::tryStartNextMove()
{
    while (!pendingCommands_.empty()) {
        const MoveCommand command = pendingCommands_.front();
        pendingCommands_.pop_front();

        if (command.type == MoveCommandType::Undo && tryStartUndoMove()) {
            return true;
        }

        if (command.type == MoveCommandType::Restart && tryStartRestart()) {
            return true;
        }

        if (command.type == MoveCommandType::Move && tryStartHeldDirection(command.direction, heldPerpendicularDirection(command.direction))) {
            return true;
        }
    }

    return tryStartHeldMove();
}

bool Application::tryStartHeldMove()
{
    if (input_.keyDown(SDL_SCANCODE_Z)) {
        return tryStartUndoMove();
    }

    const std::optional<MoveDirection> vertical = heldVerticalDirection();
    const std::optional<MoveDirection> horizontal = heldHorizontalDirection();
    if (vertical && tryStartHeldDirection(*vertical, horizontal)) {
        return true;
    }
    if (horizontal && tryStartHeldDirection(*horizontal, vertical)) {
        return true;
    }

    return false;
}

bool Application::tryStartMove(MoveDirection direction)
{
    const GridPosition target = movementTarget(direction);
    if (!level_.isWalkable(target)) {
        return false;
    }

    activeAction_ = {
        .before = captureMoveRecord(),
        .after = captureMoveRecord(),
    };
    activeAction_.after.player = target;

    if (Rock* rock = rockAt(target)) {
        if (!canMoveRock(target, direction)) {
            return false;
        }

        const size_t rockIndex = static_cast<size_t>(rock - rocks_.data());
        activeAction_.after.rocks[rockIndex] = movementTarget(rock->cell, direction);
    }

    undoCursor_.reset();
    moveElapsed_ = 0.0f;
    moving_ = true;
    return true;
}

bool Application::tryStartUndoMove()
{
    if (moveHistory_.empty()) {
        return false;
    }

    if (!undoCursor_) {
        // A contiguous undo run walks backward through the pre-existing history while
        // each completed undo still appends its inverse move for future branching.
        undoCursor_ = moveHistory_.size();
    }

    if (*undoCursor_ == 0) {
        return false;
    }

    --(*undoCursor_);
    activeAction_ = invertActionRecord(moveHistory_[*undoCursor_]);
    moveElapsed_ = 0.0f;
    moving_ = true;
    return true;
}

bool Application::tryStartRestart()
{
    MoveRecord restarted {
        .player = level_.playerStart(),
    };
    restarted.rocks.reserve(level_.rocks().size());
    for (GridPosition rockPosition : level_.rocks()) {
        restarted.rocks.push_back(rockPosition);
    }

    const MoveRecord current = captureMoveRecord();
    if (current.player == restarted.player && current.rocks == restarted.rocks) {
        return false;
    }

    activeAction_ = {
        .before = current,
        .after = std::move(restarted),
    };
    undoCursor_.reset();
    moveElapsed_ = 0.0f;
    moving_ = true;
    return true;
}

std::optional<MoveDirection> Application::pressedVerticalDirection() const
{
    const bool upPressed = input_.keyPressed(SDL_SCANCODE_W);
    const bool downPressed = input_.keyPressed(SDL_SCANCODE_S);
    if (upPressed == downPressed) {
        return std::nullopt;
    }

    if ((upPressed && input_.keyDown(SDL_SCANCODE_S)) || (downPressed && input_.keyDown(SDL_SCANCODE_W))) {
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

    if ((leftPressed && input_.keyDown(SDL_SCANCODE_D)) || (rightPressed && input_.keyDown(SDL_SCANCODE_A))) {
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

std::optional<MoveDirection> Application::heldPerpendicularDirection(MoveDirection direction) const
{
    switch (direction) {
    case MoveDirection::Up:
    case MoveDirection::Down:
        return heldHorizontalDirection();
    case MoveDirection::Left:
    case MoveDirection::Right:
        return heldVerticalDirection();
    }

    return std::nullopt;
}

bool Application::hasPendingMove(MoveDirection direction) const
{
    return std::ranges::any_of(pendingCommands_, [direction](const MoveCommand& command) {
        return command.type == MoveCommandType::Move && command.direction == direction;
    });
}

bool Application::tryStartHeldDirection(MoveDirection direction, std::optional<MoveDirection> queuedDirection)
{
    if (!tryStartMove(direction)) {
        return false;
    }

    if (queuedDirection && !hasPendingMove(*queuedDirection)) {
        pendingCommands_.push_back({ .type = MoveCommandType::Move, .direction = *queuedDirection });
    }

    return true;
}

Application::MoveRecord Application::captureMoveRecord() const
{
    MoveRecord record {
        .player = playerCell_,
    };
    record.rocks.reserve(rocks_.size());
    for (const Rock& rock : rocks_) {
        record.rocks.push_back(rock.cell);
    }

    return record;
}

void Application::applyMoveRecord(const MoveRecord& record)
{
    playerCell_ = record.player;
    playerRenderPosition_ = toVec2(playerCell_);

    for (size_t i = 0; i < rocks_.size(); ++i) {
        rocks_[i].cell = record.rocks[i];
        rocks_[i].renderPosition = toVec2(rocks_[i].cell);
    }
}

Application::ActionRecord Application::invertActionRecord(const ActionRecord& record) const
{
    return {
        .before = record.after,
        .after = record.before,
    };
}

GridPosition Application::movementTarget(MoveDirection direction) const
{
    return movementTarget(playerCell_, direction);
}

GridPosition Application::movementTarget(GridPosition origin, MoveDirection direction) const
{
    GridPosition target = origin;

    switch (direction) {
    case MoveDirection::Up:
        target.y -= 1;
        break;
    case MoveDirection::Down:
        target.y += 1;
        break;
    case MoveDirection::Left:
        target.x -= 1;
        break;
    case MoveDirection::Right:
        target.x += 1;
        break;
    }

    return target;
}

Application::Rock* Application::rockAt(GridPosition position)
{
    const auto rock = std::ranges::find_if(rocks_, [position](const Rock& candidate) {
        return candidate.cell == position;
    });

    return rock != rocks_.end() ? &*rock : nullptr;
}

const Application::Rock* Application::rockAt(GridPosition position) const
{
    const auto rock = std::ranges::find_if(rocks_, [position](const Rock& candidate) {
        return candidate.cell == position;
    });

    return rock != rocks_.end() ? &*rock : nullptr;
}

bool Application::canMoveRock(GridPosition position, MoveDirection direction) const
{
    const GridPosition target = movementTarget(position, direction);
    return level_.isWalkable(target) && rockAt(target) == nullptr;
}

bool Application::allPressurePlatesActive() const
{
    return std::ranges::all_of(level_.pressurePlates(), [this](GridPosition plate) {
        return playerCell_ == plate || rockAt(plate) != nullptr;
    });
}

bool Application::isEndUnlocked() const
{
    return allPressurePlatesActive();
}

std::filesystem::path Application::screenPath(int levelIndex, int screenIndex) const
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

    return std::filesystem::exists(screenPath(levelIndex, screenIndex));
}

RenderFrameData Application::buildRenderFrame() const
{
    RenderFrameData frame;
    frame.levelWidth = level_.width();
    frame.levelHeight = level_.height();
    frame.playerPosition = playerRenderPosition_;
    const bool endUnlocked = isEndUnlocked();

    frame.tiles.reserve(static_cast<size_t>(level_.width()) * level_.height());
    for (uint32_t y = 0; y < level_.height(); ++y) {
        for (uint32_t x = 0; x < level_.width(); ++x) {
            const TileType tile = level_.tileAt(x, y);
            Vec4 color;

            switch (tile) {
            case TileType::Wall:
                color = { 0.62f, 0.32f, 0.09f, 1.0f };
                break;
            case TileType::End:
                color = endUnlocked ? Vec4 { 1.0f, 0.05f, 0.04f, 1.0f } : Vec4 { 0.38f, 0.04f, 0.04f, 1.0f };
                break;
            case TileType::PressurePlate:
                color = { 0.18f, 0.18f, 0.18f, 1.0f };
                break;
            case TileType::Empty:
                color = { 0.82f, 0.82f, 0.84f, 1.0f };
                break;
            }

            frame.tiles.push_back({
                .position = { static_cast<float>(x), static_cast<float>(y) },
                .color = color,
            });
        }
    }

    frame.tiles.push_back({
        .position = playerRenderPosition_,
        .color = { 0.0f, 1.0f, 0.15f, 1.0f },
    });

    for (const Rock& rock : rocks_) {
        frame.tiles.push_back({
            .position = rock.renderPosition,
            .color = { 0.20f, 0.10f, 0.04f, 1.0f },
        });
    }

    return frame;
}

} // namespace sokoban
