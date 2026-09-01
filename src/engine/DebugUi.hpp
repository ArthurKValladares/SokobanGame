#pragma once

#include <functional>
#include <cstdint>
#include <string>

#ifndef SOKOBAN_ENABLE_DEBUG_UI
// Deliberately fatal rather than defaulting to 0. This flag decides whether
// Application and DebugUi declare some of their members, so a translation unit
// that quietly assumed a value would disagree with the rest of the program
// about those class layouts - and link anyway. CMake defines it PUBLIC on
// sokoban_core, so anything linking a Sokoban library already has it.
#error "SOKOBAN_ENABLE_DEBUG_UI must be defined by the build (see CMakeLists.txt)"
#endif

namespace sokoban {

class DebugUi {
public:
#if SOKOBAN_ENABLE_DEBUG_UI
    using DrawCallback = std::function<void()>;

    struct GameViewport {
        uint64_t texture = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct DrawResult {
        float viewportX = 0.0f;
        float viewportY = 0.0f;
        float viewportWidth = 0.0f;
        float viewportHeight = 0.0f;
        bool viewportHovered = false;
        bool viewportFocused = false;
    };

    static void initialize();
    static void addTab(std::string name, DrawCallback callback);
    static void clearTabs();
    [[nodiscard]] static DrawResult draw(GameViewport gameViewport);
#else
    static void initialize() {}

    template <typename Callback>
    static void addTab(std::string, Callback&&)
    {
    }

    static void clearTabs() {}
    template <typename Viewport>
    static void draw(Viewport&&) {}
#endif
};

} // namespace sokoban
