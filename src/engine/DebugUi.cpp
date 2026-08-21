#include "engine/DebugUi.hpp"

#if SOKOBAN_ENABLE_DEBUG_UI

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace sokoban {
namespace {

struct DebugTab {
    std::string name;
    DebugUi::DrawCallback callback;
    bool open = true;
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

struct DebugUiWorkspaceState {
    bool gameViewportOpen = true;
    bool resetLayout = false;
    std::array<char, 64> layoutName {};
    std::vector<std::string> layouts;
    std::string activeLayout;
    std::string status;
};

DebugUiWorkspaceState& workspaceState()
{
    static DebugUiWorkspaceState state;
    return state;
}

void applyDebugUiScale(float scale);

std::filesystem::path layoutDirectory()
{
    const char* iniFilename = ImGui::GetIO().IniFilename;
    const std::filesystem::path iniPath =
        iniFilename && *iniFilename
        ? std::filesystem::path(iniFilename)
        : std::filesystem::path("imgui.ini");
    return iniPath.parent_path() / "imgui-layouts";
}

std::string normalizedLayoutName(const char* input)
{
    std::string name = input ? input : "";
    const auto valid = [](unsigned char value) {
        return std::isalnum(value) || value == ' ' || value == '-' ||
            value == '_';
    };
    std::replace_if(name.begin(), name.end(), [&](char value) {
        return !valid(static_cast<unsigned char>(value));
    }, '_');
    const auto content = [](unsigned char value) {
        return !std::isspace(value);
    };
    const auto first = std::find_if(name.begin(), name.end(), content);
    const auto last = std::find_if(name.rbegin(), name.rend(), content).base();
    if (first >= last) {
        return {};
    }
    name = std::string(first, last);
    if (name.size() > 48) {
        name.resize(48);
    }
    return name;
}

std::filesystem::path layoutPath(std::string_view name)
{
    return layoutDirectory() / (std::string(name) + ".ini");
}

void refreshLayouts()
{
    DebugUiWorkspaceState& state = workspaceState();
    state.layouts.clear();
    std::error_code error;
    const std::filesystem::path directory = layoutDirectory();
    if (!std::filesystem::exists(directory, error)) {
        return;
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory, error)) {
        if (error) {
            break;
        }
        if (entry.is_regular_file(error) &&
            entry.path().extension() == ".ini") {
            state.layouts.push_back(entry.path().stem().string());
        }
    }
    std::sort(state.layouts.begin(), state.layouts.end());
}

void saveLayout(const char* requestedName)
{
    DebugUiWorkspaceState& state = workspaceState();
    const std::string name = normalizedLayoutName(requestedName);
    if (name.empty()) {
        state.status = "Enter a layout name first.";
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(layoutDirectory(), error);
    if (error) {
        state.status = "Could not create the layout directory.";
        return;
    }
    const std::filesystem::path path = layoutPath(name);
    ImGui::SaveIniSettingsToDisk(path.string().c_str());
    state.activeLayout = name;
    state.status = "Saved layout '" + name + "'.";
    refreshLayouts();
}

void loadLayout(std::string_view name)
{
    DebugUiWorkspaceState& state = workspaceState();
    const std::filesystem::path path = layoutPath(name);
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        state.status = "The selected layout no longer exists.";
        refreshLayouts();
        return;
    }
    ImGui::ClearIniSettings();
    ImGui::LoadIniSettingsFromDisk(path.string().c_str());
    state.activeLayout = std::string(name);
    state.status = "Loaded layout '" + std::string(name) + "'.";
}

void deleteLayout(std::string_view name)
{
    DebugUiWorkspaceState& state = workspaceState();
    std::error_code error;
    const bool removed = std::filesystem::remove(layoutPath(name), error);
    if (removed && !error) {
        if (state.activeLayout == name) {
            state.activeLayout.clear();
        }
        state.status = "Deleted layout '" + std::string(name) + "'.";
    } else {
        state.status = "Could not delete layout '" + std::string(name) + "'.";
    }
    refreshLayouts();
}

void buildDefaultLayout(ImGuiID dockspaceId)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(
        dockspaceId,
        ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    ImGuiID toolsNode = 0;
    ImGuiID gameNode = dockspaceId;
    ImGui::DockBuilderSplitNode(
        gameNode,
        ImGuiDir_Right,
        0.32f,
        &toolsNode,
        &gameNode);
    ImGui::DockBuilderDockWindow("Game Viewport", gameNode);
    for (const DebugTab& tab : debugTabs()) {
        ImGui::DockBuilderDockWindow(tab.name.c_str(), toolsNode);
    }
    ImGui::DockBuilderFinish(dockspaceId);
}

void drawScaleControl()
{
    DebugUiScaleState& scaleState = debugUiScaleState();
    float requestedScale = scaleState.scale;
    ImGui::SetNextItemWidth(140.0f * scaleState.scale);
    bool scaleChanged = ImGui::SliderFloat(
        "UI Scale",
        &requestedScale,
        1.0f,
        3.0f,
        "%.2fx",
        ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::MenuItem("Reset UI Scale")) {
        requestedScale = 1.0f;
        scaleChanged = true;
    }
    if (scaleChanged) {
        applyDebugUiScale(requestedScale);
        ImGui::MarkIniSettingsDirty();
    }
}

void drawWorkspaceMenu()
{
    DebugUiWorkspaceState& state = workspaceState();
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Layouts")) {
            ImGui::SetNextItemWidth(220.0f);
            const bool submitted = ImGui::InputText(
                "##LayoutName",
                state.layoutName.data(),
                state.layoutName.size(),
                ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Save") || submitted) {
                saveLayout(state.layoutName.data());
            }

            if (ImGui::BeginMenu("Load")) {
                if (state.layouts.empty()) {
                    ImGui::TextDisabled("No saved layouts");
                }
                for (const std::string& layout : state.layouts) {
                    if (ImGui::MenuItem(
                            layout.c_str(),
                            nullptr,
                            layout == state.activeLayout)) {
                        loadLayout(layout);
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Delete")) {
                std::string deleteRequest;
                if (state.layouts.empty()) {
                    ImGui::TextDisabled("No saved layouts");
                }
                for (const std::string& layout : state.layouts) {
                    if (ImGui::MenuItem(layout.c_str())) {
                        deleteRequest = layout;
                        break;
                    }
                }
                ImGui::EndMenu();
                if (!deleteRequest.empty()) {
                    deleteLayout(deleteRequest);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset to Default")) {
                state.resetLayout = true;
                state.activeLayout.clear();
                state.status = "Restored the default layout.";
            }
            if (!state.status.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("%s", state.status.c_str());
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Panels")) {
            ImGui::MenuItem("Game Viewport", nullptr, &state.gameViewportOpen);
            for (DebugTab& tab : debugTabs()) {
                ImGui::MenuItem(tab.name.c_str(), nullptr, &tab.open);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            drawScaleControl();
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

DebugUi::DrawResult drawGameViewport(DebugUi::GameViewport viewport)
{
    DebugUi::DrawResult result;
    DebugUiWorkspaceState& state = workspaceState();
    if (!state.gameViewportOpen) {
        return result;
    }
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("Game Viewport", &state.gameViewportOpen, flags)) {
        result.viewportFocused = ImGui::IsWindowFocused(
            ImGuiFocusedFlags_RootAndChildWindows);
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (viewport.texture == 0 || viewport.width == 0 ||
            viewport.height == 0) {
            ImGui::TextDisabled("The game render target is not available.");
        } else if (available.x > 0.0f && available.y > 0.0f) {
            const float aspect = static_cast<float>(viewport.width) /
                static_cast<float>(viewport.height);
            ImVec2 size { available.x, available.x / aspect };
            if (size.y > available.y) {
                size = { available.y * aspect, available.y };
            }
            const ImVec2 cursor = ImGui::GetCursorPos();
            ImGui::SetCursorPos({
                cursor.x + std::max(0.0f, (available.x - size.x) * 0.5f),
                cursor.y + std::max(0.0f, (available.y - size.y) * 0.5f),
            });
            ImGui::Image(viewport.texture, size);
            const ImVec2 minimum = ImGui::GetItemRectMin();
            const ImVec2 maximum = ImGui::GetItemRectMax();
            result.viewportX = minimum.x;
            result.viewportY = minimum.y;
            result.viewportWidth = maximum.x - minimum.x;
            result.viewportHeight = maximum.y - minimum.y;
            result.viewportHovered = ImGui::IsItemHovered();
        }
    }
    ImGui::End();
    return result;
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
    refreshLayouts();

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
        .open = true,
    });
}

void DebugUi::clearTabs()
{
    debugTabs().clear();
}

DebugUi::DrawResult DebugUi::draw(GameViewport gameViewport)
{
    DebugUiScaleState& scaleState = debugUiScaleState();
    if (!scaleState.baseStyleCaptured) {
        applyDebugUiScale(scaleState.scale);
    }

    drawWorkspaceMenu();
    const ImGuiID dockspaceId = ImGui::GetID("SokobanDockSpace");
    const bool dockspaceExisted =
        ImGui::DockBuilderGetNode(dockspaceId) != nullptr;
    ImGui::DockSpaceOverViewport(
        dockspaceId,
        ImGui::GetMainViewport(),
        ImGuiDockNodeFlags_None);
    DebugUiWorkspaceState& workspace = workspaceState();
    if (workspace.resetLayout || !dockspaceExisted) {
        buildDefaultLayout(dockspaceId);
        workspace.resetLayout = false;
    }

    const DrawResult result = drawGameViewport(gameViewport);
    for (DebugTab& tab : debugTabs()) {
        if (!tab.open) {
            continue;
        }
        if (ImGui::Begin(tab.name.c_str(), &tab.open)) {
            tab.callback();
        }
        ImGui::End();
    }
    return result;
}

} // namespace sokoban

#endif
