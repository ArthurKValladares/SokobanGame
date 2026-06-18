#include "engine/Application.hpp"

#include "engine/Config.hpp"

#include <SDL3/SDL.h>

#include <cmath>

namespace sokoban {

Application::Application()
    : window_("Sokoban 3D", 1280, 720)
    , renderer_(window_.nativeHandle(), SOKOBAN_ASSET_DIR)
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

        renderer_.drawFrame({ .triangleOffset = triangleOffset_ });
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

    triangleOffset_.x += movement.x * config::triangleMoveSpeed * dt;
    triangleOffset_.y += movement.y * config::triangleMoveSpeed * dt;
}

} // namespace sokoban
