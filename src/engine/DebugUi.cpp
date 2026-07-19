#include "engine/DebugUi.hpp"

#if SOKOBAN_ENABLE_DEBUG_UI

#include <imgui.h>

#include <utility>
#include <vector>

namespace sokoban {
namespace {

struct DebugTab {
    std::string name;
    DebugUi::DrawCallback callback;
};

std::vector<DebugTab>& debugTabs()
{
    static std::vector<DebugTab> tabs;
    return tabs;
}

} // namespace

void DebugUi::addTab(std::string name, DrawCallback callback)
{
    debugTabs().push_back({
        .name = std::move(name),
        .callback = std::move(callback),
    });
}

void DebugUi::clearTabs()
{
    debugTabs().clear();
}

void DebugUi::draw()
{
    ImGui::SetNextWindowSize(ImVec2 { 560.0f, 620.0f }, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Developer Tools")) {
        constexpr ImGuiTabBarFlags tabFlags =
            ImGuiTabBarFlags_Reorderable |
            ImGuiTabBarFlags_FittingPolicyScroll;
        if (ImGui::BeginTabBar("DeveloperToolsTabs", tabFlags)) {
            for (DebugTab& tab : debugTabs()) {
                if (ImGui::BeginTabItem(tab.name.c_str())) {
                    tab.callback();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

} // namespace sokoban

#endif
