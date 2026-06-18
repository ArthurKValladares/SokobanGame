#include "engine/Application.hpp"

#include "engine/Config.hpp"

#include <SDL3/SDL.h>

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
    loadCurrentScreen();
}

Application::~Application()
{
    renderer_.waitIdle();
}

void Application::run()
{
    while (running_) {
        input_.beginFrame();

        SDL_Event event {};
        while (SDL_PollEvent(&event)) {
            input_.handleEvent(event);

            if (event.type == SDL_EVENT_QUIT) {
                running_ = false;
            }

            if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) {
                running_ = false;
            }
        }

        const float dt = frameTimer_.tick();
        update(dt);

        renderer_.drawFrame(buildRenderFrame());
    }
}

void Application::update(float dt)
{
    queuePressedMovement();
    advancePlayerMovement(dt);
}

void Application::loadCurrentScreen()
{
    level_ = Level::loadFromFile(screenPath(currentLevel_, currentScreen_));
    playerCell_ = level_.playerStart();
    playerRenderPosition_ = toVec2(playerCell_);
    pendingMoves_.clear();
    moving_ = false;
    moveElapsed_ = 0.0f;
    moveStart_ = playerCell_;
    moveTarget_ = playerCell_;

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

void Application::queuePressedMovement()
{
    if (input_.keyPressed(SDL_SCANCODE_W)) {
        pendingMoves_.push_back(MoveDirection::Up);
    }
    if (input_.keyPressed(SDL_SCANCODE_S)) {
        pendingMoves_.push_back(MoveDirection::Down);
    }
    if (input_.keyPressed(SDL_SCANCODE_A)) {
        pendingMoves_.push_back(MoveDirection::Left);
    }
    if (input_.keyPressed(SDL_SCANCODE_D)) {
        pendingMoves_.push_back(MoveDirection::Right);
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
            playerCell_ = moveTarget_;
            playerRenderPosition_ = toVec2(playerCell_);
            moving_ = false;
            continue;
        }

        constexpr float duration = config::playerMoveDurationSeconds;
        const float timeToFinish = duration - moveElapsed_;
        const float step = std::min(remainingTime, timeToFinish);
        remainingTime -= step;
        moveElapsed_ += step;

        const float t = std::clamp(moveElapsed_ / duration, 0.0f, 1.0f);
        playerRenderPosition_ = lerp(toVec2(moveStart_), toVec2(moveTarget_), t);

        if (moveElapsed_ >= duration) {
            playerCell_ = moveTarget_;
            playerRenderPosition_ = toVec2(playerCell_);
            moving_ = false;
            moveElapsed_ = 0.0f;

            if (level_.isEnd(playerCell_)) {
                advanceScreen();
                return;
            }
        }
    }
}

bool Application::tryStartNextMove()
{
    while (!pendingMoves_.empty()) {
        const MoveDirection direction = pendingMoves_.front();
        pendingMoves_.pop_front();

        if (tryStartMove(direction)) {
            return true;
        }
    }

    return tryStartHeldMove();
}

bool Application::tryStartHeldMove()
{
    if (input_.keyDown(SDL_SCANCODE_W)) {
        return tryStartMove(MoveDirection::Up);
    }
    if (input_.keyDown(SDL_SCANCODE_S)) {
        return tryStartMove(MoveDirection::Down);
    }
    if (input_.keyDown(SDL_SCANCODE_A)) {
        return tryStartMove(MoveDirection::Left);
    }
    if (input_.keyDown(SDL_SCANCODE_D)) {
        return tryStartMove(MoveDirection::Right);
    }

    return false;
}

bool Application::tryStartMove(MoveDirection direction)
{
    const GridPosition target = movementTarget(direction);
    if (!level_.isWalkable(target)) {
        return false;
    }

    moveStart_ = playerCell_;
    moveTarget_ = target;
    moveElapsed_ = 0.0f;
    moving_ = true;
    return true;
}

GridPosition Application::movementTarget(MoveDirection direction) const
{
    GridPosition target = playerCell_;

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
                color = { 1.0f, 0.05f, 0.04f, 1.0f };
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

    return frame;
}

} // namespace sokoban
