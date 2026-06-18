#pragma once

#include "engine/Input.hpp"
#include "engine/Level.hpp"
#include "engine/Math.hpp"
#include "engine/Time.hpp"
#include "engine/Window.hpp"
#include "engine/render/VulkanRenderer.hpp"

#include <deque>

namespace sokoban {

enum class MoveDirection {
    Up,
    Down,
    Left,
    Right,
};

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();

private:
    void update(float dt);
    void queuePressedMovement();
    void advancePlayerMovement(float dt);
    [[nodiscard]] bool tryStartNextMove();
    [[nodiscard]] bool tryStartHeldMove();
    [[nodiscard]] bool tryStartMove(MoveDirection direction);
    [[nodiscard]] GridPosition movementTarget(MoveDirection direction) const;
    [[nodiscard]] RenderFrameData buildRenderFrame() const;

    Window window_;
    VulkanRenderer renderer_;
    Level level_;
    InputState input_;
    FrameTimer frameTimer_;
    GridPosition playerCell_ {};
    Vec2 playerRenderPosition_ {};
    std::deque<MoveDirection> pendingMoves_;
    GridPosition moveStart_ {};
    GridPosition moveTarget_ {};
    float moveElapsed_ = 0.0f;
    bool moving_ = false;
    bool running_ = true;
};

} // namespace sokoban
