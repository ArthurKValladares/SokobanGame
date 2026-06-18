#pragma once

#include "engine/Input.hpp"
#include "engine/Math.hpp"
#include "engine/Time.hpp"
#include "engine/Window.hpp"
#include "engine/render/VulkanRenderer.hpp"

namespace sokoban {

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();

private:
    void update(float dt);

    Window window_;
    VulkanRenderer renderer_;
    InputState input_;
    FrameTimer frameTimer_;
    Vec2 triangleOffset_ {};
    bool running_ = true;
};

} // namespace sokoban
