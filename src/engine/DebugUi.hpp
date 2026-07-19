#pragma once

#include <functional>
#include <string>

#ifndef SOKOBAN_ENABLE_DEBUG_UI
#define SOKOBAN_ENABLE_DEBUG_UI 0
#endif

namespace sokoban {

class DebugUi {
public:
#if SOKOBAN_ENABLE_DEBUG_UI
    using DrawCallback = std::function<void()>;

    static void addTab(std::string name, DrawCallback callback);
    static void clearTabs();
    static void draw();
#else
    template <typename Callback>
    static void addTab(std::string, Callback&&)
    {
    }

    static void clearTabs() {}
    static void draw() {}
#endif
};

} // namespace sokoban
