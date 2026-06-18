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
    bool startCollapsed = false;
    bool appliedInitialState = false;
};

std::vector<DebugWindow>& debugWindows()
{
    static std::vector<DebugWindow> windows;
    return windows;
}

} // namespace

void DebugUi::addWindow(std::string name, DrawCallback callback, bool startCollapsed)
{
    debugWindows().push_back({
        .name = std::move(name),
        .callback = std::move(callback),
        .startCollapsed = startCollapsed,
    });
}

void DebugUi::clearWindows()
{
    debugWindows().clear();
}

void DebugUi::draw()
{
    for (DebugWindow& window : debugWindows()) {
        if (!window.appliedInitialState) {
            ImGui::SetNextWindowCollapsed(window.startCollapsed, ImGuiCond_Once);
            window.appliedInitialState = true;
        }
        if (ImGui::Begin(window.name.c_str())) {
            window.callback();
        }
        ImGui::End();
    }
}

} // namespace sokoban

#endif
