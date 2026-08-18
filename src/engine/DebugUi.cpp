#include "engine/DebugUi.hpp"

#if SOKOBAN_ENABLE_DEBUG_UI

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
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

struct DebugUiScaleState {
    ImGuiStyle baseStyle;
    float scale = 1.0f;
    bool baseStyleCaptured = false;
};

DebugUiScaleState& debugUiScaleState()
{
    static DebugUiScaleState state;
    return state;
}

void applyDebugUiScale(float scale)
{
    DebugUiScaleState& state = debugUiScaleState();
    if (!state.baseStyleCaptured) {
        state.baseStyle = ImGui::GetStyle();
        state.baseStyleCaptured = true;
    }

    state.scale = std::clamp(scale, 1.0f, 3.0f);
    ImGuiStyle scaledStyle = state.baseStyle;
    scaledStyle.ScaleAllSizes(state.scale);
    scaledStyle.FontScaleMain =
        state.baseStyle.FontScaleMain * state.scale;
    ImGui::GetStyle() = scaledStyle;
}

void resetDebugUiScaleSettings(
    ImGuiContext*, ImGuiSettingsHandler*)
{
    debugUiScaleState().scale = 1.0f;
}

void* openDebugUiScaleSettings(
    ImGuiContext*, ImGuiSettingsHandler*, const char* name)
{
    return std::strcmp(name, "Settings") == 0
        ? &debugUiScaleState()
        : nullptr;
}

void readDebugUiScaleSetting(
    ImGuiContext*,
    ImGuiSettingsHandler*,
    void* entry,
    const char* line)
{
    auto& state = *static_cast<DebugUiScaleState*>(entry);
    constexpr char prefix[] = "Scale=";
    if (std::strncmp(line, prefix, sizeof(prefix) - 1) != 0) {
        return;
    }
    char* end = nullptr;
    const float scale = std::strtof(line + sizeof(prefix) - 1, &end);
    if (end != line + sizeof(prefix) - 1) {
        state.scale = std::clamp(scale, 1.0f, 3.0f);
    }
}

void applyDebugUiScaleSettings(
    ImGuiContext*, ImGuiSettingsHandler*)
{
    applyDebugUiScale(debugUiScaleState().scale);
}

void writeDebugUiScaleSettings(
    ImGuiContext*,
    ImGuiSettingsHandler*,
    ImGuiTextBuffer* output)
{
    output->appendf(
        "[DebugUi][Settings]\nScale=%.3f\n\n",
        debugUiScaleState().scale);
}

} // namespace

void DebugUi::initialize()
{
    DebugUiScaleState& state = debugUiScaleState();
    state.baseStyle = ImGui::GetStyle();
    state.baseStyleCaptured = true;
    state.scale = 1.0f;

    if (ImGui::FindSettingsHandler("DebugUi")) {
        return;
    }
    ImGuiSettingsHandler handler;
    handler.TypeName = "DebugUi";
    handler.TypeHash = ImHashStr("DebugUi");
    handler.ReadInitFn = resetDebugUiScaleSettings;
    handler.ReadOpenFn = openDebugUiScaleSettings;
    handler.ReadLineFn = readDebugUiScaleSetting;
    handler.ApplyAllFn = applyDebugUiScaleSettings;
    handler.WriteAllFn = writeDebugUiScaleSettings;
    ImGui::AddSettingsHandler(&handler);
}

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
    DebugUiScaleState& scaleState = debugUiScaleState();
    if (!scaleState.baseStyleCaptured) {
        applyDebugUiScale(scaleState.scale);
    }

    ImGui::SetNextWindowSize(
        ImVec2 { 560.0f * scaleState.scale, 620.0f * scaleState.scale },
        ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Developer Tools")) {
        float requestedScale = scaleState.scale;
        ImGui::SetNextItemWidth(180.0f * scaleState.scale);
        bool scaleChanged = ImGui::SliderFloat(
            "Debug UI Scale",
            &requestedScale,
            1.0f,
            3.0f,
            "%.2fx",
            ImGuiSliderFlags_AlwaysClamp);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset Scale")) {
            requestedScale = 1.0f;
            scaleChanged = true;
        }
        if (scaleChanged) {
            const float ratio = requestedScale / scaleState.scale;
            const ImVec2 windowSize = ImGui::GetWindowSize();
            applyDebugUiScale(requestedScale);
            ImGui::SetWindowSize(
                ImVec2 {
                    windowSize.x * ratio,
                    windowSize.y * ratio,
                });
            ImGui::MarkIniSettingsDirty();
        }

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
