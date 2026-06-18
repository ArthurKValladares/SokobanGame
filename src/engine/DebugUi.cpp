#include "engine/DebugUi.hpp"

#if SOKOBAN_ENABLE_DEBUG_UI

#include <imgui.h>

#include <utility>
#include <vector>

namespace sokoban {
namespace {

struct DebugWindow {
    std::string name;
    DebugUi::DrawCallback callback;
};

std::vector<DebugWindow>& debugWindows()
{
    static std::vector<DebugWindow> windows;
    return windows;
}

} // namespace

void DebugUi::addWindow(std::string name, DrawCallback callback)
{
    debugWindows().push_back({
        .name = std::move(name),
        .callback = std::move(callback),
    });
}

void DebugUi::clearWindows()
{
    debugWindows().clear();
}

void DebugUi::draw()
{
    for (DebugWindow& window : debugWindows()) {
        if (ImGui::Begin(window.name.c_str())) {
            window.callback();
        }
        ImGui::End();
    }
}

} // namespace sokoban

#endif
