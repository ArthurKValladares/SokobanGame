#pragma once

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
    Window window_;
    VulkanRenderer renderer_;
    bool running_ = true;
};

} // namespace sokoban
