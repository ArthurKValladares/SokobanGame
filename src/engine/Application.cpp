#include "engine/Application.hpp"

#include "engine/Config.hpp"

#include <SDL3/SDL.h>

#include <cmath>

namespace sokoban {

Application::Application()
    : window_("Sokoban 3D", 1280, 720)
    , renderer_(window_.nativeHandle(), SOKOBAN_ASSET_DIR)
    , level_(Level::loadFromFile(std::filesystem::path(SOKOBAN_ASSET_DIR) / "test-level.lvl"))
    , playerPosition_(level_.playerStart())
{
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
    Vec2 movement;

    if (input_.keyDown(SDL_SCANCODE_A)) {
        movement.x -= 1.0f;
    }
    if (input_.keyDown(SDL_SCANCODE_D)) {
        movement.x += 1.0f;
    }
    if (input_.keyDown(SDL_SCANCODE_W)) {
        movement.y -= 1.0f;
    }
    if (input_.keyDown(SDL_SCANCODE_S)) {
        movement.y += 1.0f;
    }

    const float length = std::sqrt(movement.x * movement.x + movement.y * movement.y);
    if (length > 0.0f) {
        movement.x /= length;
        movement.y /= length;
    }

    playerPosition_.x += movement.x * config::playerMoveSpeedTilesPerSecond * dt;
    playerPosition_.y += movement.y * config::playerMoveSpeedTilesPerSecond * dt;
}

RenderFrameData Application::buildRenderFrame() const
{
    RenderFrameData frame;
    frame.levelWidth = level_.width();
    frame.levelHeight = level_.height();
    frame.playerPosition = playerPosition_;

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
        .position = playerPosition_,
        .color = { 0.0f, 1.0f, 0.15f, 1.0f },
    });

    return frame;
}

} // namespace sokoban
